// EN-11: a late first file-watcher scan (baseline) is a background refresh. It must
// never drop, cancel or overwrite a newer user request. Every interleaving below is
// forced with explicit gates on the monitor's snapshot reader and on the single
// navigation thread, never with sleeps.
#include <QtTest>
#include <QImage>
#include <QImageReader>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <cstdio>
#include <memory>
#include "fs_monitor.hpp"
#include "fs_revision.hpp"
#include "fs_source_token.hpp"
#include "shared_file_read.hpp"
#include "glancelot/viewer_controller.hpp"

class LateBaselineTests : public QObject {
    Q_OBJECT
    struct HookScope {
        const fs_watch::ControllerTestHooks* previous{fs_watch::controllerTestHooks};
        explicit HookScope(const fs_watch::ControllerTestHooks& hooks){fs_watch::controllerTestHooks=&hooks;}
        ~HookScope(){fs_watch::controllerTestHooks=previous;}
    };
    struct Gate {std::atomic_bool entered{},release{};};
    static QString executable(const QString& name) {
        return QCoreApplication::applicationDirPath()+"/"+name+
#ifdef Q_OS_WIN
            ".exe";
#else
            "";
#endif
    }
    static bool png(const QString& path,Qt::GlobalColor color) {
        QSaveFile target(path);if(!target.open(QIODevice::WriteOnly))return false;
        QImage image(QSize(32,24),QImage::Format_RGB32);image.fill(color);
        return image.save(&target,"PNG")&&target.commit();
    }
    static bool write(const QString& path,const QByteArray& bytes) {
        QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
    }
    static bool same(const std::filesystem::path& actual,const QString& expected) {
        return fs_watch::key(QString::fromStdWString(actual.wstring()))==fs_watch::key(QFileInfo(expected).absoluteFilePath());
    }
    // Holds the monitor's first read of folder until gate->release: the baseline
    // cannot be published, so decode starts on the production 100 ms fallback.
    static void holdFirstScan(fs_watch::ControllerTestHooks& hooks,std::shared_ptr<Gate> gate,const QString& folder) {
        hooks.options.read=[gate,folder](const QString& path,std::stop_token stop){
            if(path==folder) {
                gate->entered.store(true);
                while(!gate->release.load()&&!stop.stop_requested())QThread::msleep(2);
            }
            return fs_watch::snapshot(path,stop);
        };
    }
    static int traceCount(const QString& file,const QByteArray& event) {
        QFile trace(file);return trace.open(QIODevice::ReadOnly)?int(trace.readAll().count(event+'\n')):-1;
    }
    // QTest output does not reach CTest's log on the Windows machines; stderr does.
    static void note(const QString& text) {
        std::fprintf(stderr,"late_baseline: %s\n",qPrintable(text));std::fflush(stderr);
    }
    static QString commitPng(const QString& path,Qt::GlobalColor color) {
        QSaveFile target(path);if(!target.open(QIODevice::WriteOnly))return "open: "+target.errorString();
        QImage image(QSize(32,24),QImage::Format_RGB32);image.fill(color);
        if(!image.save(&target,"PNG"))return "encode: "+target.errorString();
        return target.commit()?QString{}:QString("commit %1: %2").arg(int(target.error())).arg(target.errorString());
    }
    QTemporaryDir config;
private slots:
    void initTestCase(){QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());}
    void cleanup(){if(QTest::currentTestFailed())note(QString("FAILED %1").arg(QTest::currentTestFunction()));}

    // folder_tree openFolderWithoutImagesReportsAndKeepsSelection (:427/:430) and
    // folder_grid auditReadyCurrentPhotoDoesNotReopen (:947) under load: the user's
    // folder open is in flight on the navigation thread when the late baseline lands.
    void lateBaselineKeepsInFlightFolderOpen() {
        QTemporaryDir media;QVERIFY(media.isValid());
        const auto start=media.filePath("p1.png"),traceFile=media.filePath("trace.txt");
        QVERIFY(png(start,Qt::red));QVERIFY(QDir(media.path()).mkpath("bare/also-bare"));
        const bool hadTrace=qEnvironmentVariableIsSet("GLANCELOT_TRACE_FILE");const auto oldTrace=qgetenv("GLANCELOT_TRACE_FILE");
        qputenv("GLANCELOT_TRACE_FILE",traceFile.toUtf8());
        const auto restoreTrace=qScopeGuard([&]{if(hadTrace)qputenv("GLANCELOT_TRACE_FILE",oldTrace);else qunsetenv("GLANCELOT_TRACE_FILE");});
        auto scan=std::make_shared<Gate>(),navigation=std::make_shared<Gate>();
        fs_watch::ControllerTestHooks hooks;holdFirstScan(hooks,scan,media.path());
        // Lookahead discovery shares the single navigation thread: holding it queues
        // the user's folder open behind it, deterministically in flight.
        hooks.discover=[navigation](int,std::stop_token) -> std::optional<std::filesystem::path> {
            navigation->entered.store(true);
            while(!navigation->release.load())QThread::msleep(2);
            return std::nullopt;
        };
        bool baseline=false;hooks.pending=[&](const fs_watch::Observation& value){baseline=baseline||value.baseline;};
        HookScope override(hooks);ViewerController viewer;
        const auto unblock=qScopeGuard([scan,navigation]{scan->release.store(true);navigation->release.store(true);});
        viewer.setBoundaryMode("continue");viewer.open(start);
        QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady()&&!viewer.loading()&&same(viewer.snapshot().current,start),15000);
        QTRY_VERIFY_WITH_TIMEOUT(scan->entered.load(),15000);QVERIFY(!baseline);
        QTRY_VERIFY_WITH_TIMEOUT(navigation->entered.load(),15000);
        QSignalSpy images(&viewer,&ViewerController::imageChanged);
        viewer.openFolder(media.filePath("bare"));
        QVERIFY(viewer.status().contains("Opening folder"));
        scan->release.store(true);
        QTRY_VERIFY_WITH_TIMEOUT(baseline,15000);
        navigation->release.store(true);
        QTRY_VERIFY2_WITH_TIMEOUT(viewer.status().contains("No supported images"),qPrintable(viewer.status()),15000);
        QVERIFY(same(viewer.snapshot().current,start));QVERIFY(viewer.imageReady());QVERIFY(!viewer.loading());
        QCOMPARE(viewer.image().pixelColor(0,0),QColor(Qt::red));
        // A registered revision equal to the decoded one proves the pixels current:
        // no second decode and no image change. Without file identity (no birth
        // time or file index) the token is empty and one fresh decode validates.
        const bool provable=!fs_watch::sourceToken(fs_watch::probe(fs_watch::native(start))).isEmpty();
        qInfo()<<"source revision provable"<<provable;
        note(QString("in-flight open: provable %1 decodes %2 imageChanged %3 status \"%4\"")
            .arg(provable).arg(traceCount(traceFile,"decode:begin")).arg(images.count()).arg(viewer.status()));
        QCOMPARE(traceCount(traceFile,"decode:begin"),provable?1:2);
        if(provable)QCOMPARE(images.count(),0);
        // The user's next request is unaffected.
        viewer.openFolder(media.filePath("does-not-exist"));
        QTRY_VERIFY2_WITH_TIMEOUT(viewer.status().contains("cannot be read"),qPrintable(viewer.status()),15000);
        QVERIFY(same(viewer.snapshot().current,start));
    }

    // The baseline lands after the user's request finished, and the registered
    // revision does not prove the shown pixels (fixture tokens never match): the
    // refresh decodes fresh pixels in the background but keeps the user's message.
    void lateBaselineRefreshKeepsNewerMessage() {
        QTemporaryDir media,control;QVERIFY(media.isValid()&&control.isValid());
        const auto start=media.filePath("p1.png"),worker=control.filePath("worker.json");
        QVERIFY(png(start,Qt::red));QVERIFY(QDir(media.path()).mkdir("bare"));
        QVERIFY(write(worker,R"({"color":"red"})"));
        auto scan=std::make_shared<Gate>();
        fs_watch::ControllerTestHooks hooks;holdFirstScan(hooks,scan,media.path());
        hooks.decoderProgram=executable("fs_watch_decoder_fixture");hooks.decoderArguments={"--fixture-config",worker};
        int decodes=0;hooks.workerStarted=[&](QProcess*,const QString& purpose,const QString&){if(purpose=="decode")++decodes;};
        bool baseline=false;hooks.pending=[&](const fs_watch::Observation& value){baseline=baseline||value.baseline;};
        HookScope override(hooks);ViewerController viewer;
        const auto unblock=qScopeGuard([scan]{scan->release.store(true);});
        viewer.setBoundaryMode("stop");viewer.open(start);
        QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady()&&!viewer.loading()&&same(viewer.snapshot().current,start),15000);
        QCOMPARE(viewer.image().pixelColor(0,0),QColor(Qt::red));QCOMPARE(decodes,1);
        viewer.openFolder(media.filePath("bare"));
        QTRY_VERIFY2_WITH_TIMEOUT(viewer.status().contains("No supported images"),qPrintable(viewer.status()),15000);
        QVERIFY(!baseline);
        QVERIFY(write(worker,R"({"color":"blue"})"));  // the next decode sees changed pixels
        scan->release.store(true);
        QTRY_VERIFY_WITH_TIMEOUT(baseline,15000);
        QTRY_COMPARE_WITH_TIMEOUT(viewer.image().pixelColor(0,0),QColor(Qt::blue),15000);
        QCOMPARE(decodes,2);
        QVERIFY2(viewer.status().contains("No supported images"),qPrintable(viewer.status()));
        QVERIFY(same(viewer.snapshot().current,start));QVERIFY(viewer.imageReady());QVERIFY(!viewer.loading());
    }

    // The baseline lands while the fallback decode is still running: nothing was
    // shown yet, so the decode restarts once and reports normally.
    void lateBaselineDuringDecodeRestartsOnce() {
        QTemporaryDir media,control;QVERIFY(media.isValid()&&control.isValid());
        const auto start=media.filePath("p1.png"),worker=control.filePath("worker.json");
        QVERIFY(png(start,Qt::red));
        QVERIFY(write(worker,R"({"color":"red","decodeDelayMs":60000})"));
        auto scan=std::make_shared<Gate>();
        fs_watch::ControllerTestHooks hooks;holdFirstScan(hooks,scan,media.path());
        hooks.decoderProgram=executable("fs_watch_decoder_fixture");hooks.decoderArguments={"--fixture-config",worker};
        QPointer<QProcess> first;int decodes=0;
        hooks.workerStarted=[&](QProcess* process,const QString& purpose,const QString&){if(purpose=="decode"&&++decodes==1)first=process;};
        HookScope override(hooks);ViewerController viewer;
        const auto unblock=qScopeGuard([scan]{scan->release.store(true);});
        viewer.setBoundaryMode("stop");viewer.open(start);
        QTRY_COMPARE_WITH_TIMEOUT(decodes,1,15000);QVERIFY(viewer.loading());QVERIFY(!viewer.imageReady());
        QVERIFY(write(worker,R"({"color":"blue"})"));
        scan->release.store(true);
        QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady()&&!viewer.loading(),15000);
        QCOMPARE(decodes,2);QCOMPARE(viewer.image().pixelColor(0,0),QColor(Qt::blue));
        QVERIFY2(viewer.status().contains("display profile"),qPrintable(viewer.status()));
        QTRY_VERIFY_WITH_TIMEOUT(!first||first->state()==QProcess::NotRunning,5000);
    }

    // A reader holding the image through openSharedRead must not block another
    // program renaming the image away or deleting it (save-via-backup editors,
    // Explorer). Meaningful on Windows; POSIX never blocks either.
    // Hosted windows-2022 evidence (run 39894499556): a replace (QSaveFile commit,
    // MoveFileEx REPLACE_EXISTING) onto the held name is refused while any handle
    // is open, shared-delete or not, so this cannot fix fs_watch :298/:1023; the
    // bounded retry in fs_watch png() does. Replace results are recorded, not asserted.
    void sharedReaderAllowsRenameAndDelete() {
        QTemporaryDir media;QVERIFY(media.isValid());
        const auto image=media.filePath("held.png"),moved=media.filePath("moved.png");QVERIFY(png(image,Qt::red));
        QFile held;QVERIFY(glancelot::detail::openSharedRead(held,image));
        QImageReader reader(&held);QCOMPARE(reader.size(),QSize(32,24));
        const auto replace=commitPng(image,Qt::blue);
        note("replace over shared-delete reader: "+(replace.isEmpty()?QString("ok"):replace));
        QVERIFY2(QFile::rename(image,moved),"A shared-delete reader must not block renaming the image away");
        QVERIFY2(QFile::remove(moved),"A shared-delete reader must not block deleting the image");
        held.close();
        QVERIFY(!QFileInfo::exists(image)&&!QFileInfo::exists(moved));
#ifdef Q_OS_WIN
        // Record (not assert) what a plain QFile reader blocks on this Windows.
        QVERIFY(png(image,Qt::red));
        {
            QFile plain(image);QVERIFY(plain.open(QIODevice::ReadOnly));
            const auto blocked=commitPng(image,Qt::green);
            note("replace over plain QFile reader: "+(blocked.isEmpty()?QString("ok"):blocked));
            note(QString("rename away under plain QFile reader: %1").arg(QFile::rename(image,moved)?"ok":"refused"));
        }
#endif
    }
};

QTEST_MAIN(LateBaselineTests)
#include "late_baseline_tests.moc"
