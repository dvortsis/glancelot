#include "glancelot/viewer_controller.hpp"
#include "glancelot/window_slot.hpp"
#include "glancelot/settings_recovery.hpp"
#include "file_lock.hpp"
#include "settings_transaction.hpp"
#include <QScopeGuard>
#include <QPointer>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class RecoveryControllerTests final:public QObject {
    Q_OBJECT
    static QJsonObject settings(const QString& directory) {
        QFile file(QDir(directory).filePath("settings.json"));
        return file.open(QIODevice::ReadOnly)?QJsonDocument::fromJson(file.readAll()).object():QJsonObject{};
    }
    static QJsonObject record(const QString& directory) {return settings(directory).value("recovery").toObject().value("windows").toObject().value("1").toObject();}
    static QString folder(const QString& directory) {return QDir::fromNativeSeparators(settings(directory).value("windows").toObject().value("1").toObject().value("folder").toString());}
    static bool writePosition(const QString& directory,const QString& folder,const QString& image={}) {
        QFile file(QDir(directory).filePath("settings.json"));
        const QJsonObject root{{"schema",1},{"windows",QJsonObject{{"1",QJsonObject{{"folder",folder},{"image",image},{"saved",1},{"future",true}}}}}};
        const auto bytes=QJsonDocument(root).toJson();return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
    }
private slots:
    void successfulEmptyDirectoryIsCheckpointedAndCleaned() {
        QTemporaryDir config,media;QVERIFY(config.isValid()&&media.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        WindowSlot slot(config.path(),0);
        {
            ViewerController viewer;viewer.setWindowClaim(slot.claimIdentity());viewer.navigateDirectory(media.path());
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.directoryPending(),3000);QVERIFY(viewer.directoryError().isEmpty());
            viewer.flushWindowPosition();QTRY_COMPARE_WITH_TIMEOUT(folder(config.path()),media.path(),3000);
            QCOMPARE(record(config.path()).value("state"),QJsonValue("dirty"));QCOMPARE(record(config.path()).value("origin"),QJsonValue("navigation"));
            QVERIFY(viewer.filePath().isEmpty());
        }
        QCOMPARE(record(config.path()).value("state"),QJsonValue("clean"));
    }
    void failedExplicitVisitBeforeLateClaimPreventsResume() {
        QTemporaryDir config,oldMedia;QVERIFY(config.isValid()&&oldMedia.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(writePosition(config.path(),oldMedia.path()));WindowSlot slot(config.path(),0);
        {
            ViewerController viewer;viewer.navigateDirectory(oldMedia.filePath("does-not-exist"));
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.directoryPending(),3000);QVERIFY(!viewer.directoryError().isEmpty());
            viewer.setWindowClaim(slot.claimIdentity());viewer.restoreWindow();
            QTRY_COMPARE_WITH_TIMEOUT(record(config.path()).value("state"),QJsonValue("dirty"),3000);
            QTest::qWait(150);QVERIFY(viewer.filePath().isEmpty());QVERIFY(viewer.directory().isEmpty());QVERIFY(!viewer.directoryError().isEmpty());
            QCOMPARE(folder(config.path()),oldMedia.path());
        }
        QCOMPARE(record(config.path()).value("state"),QJsonValue("clean"));
    }
    void explicitVisitWinsWhileBeginWaitsAndUiKeepsResponding() {
        QTemporaryDir config,oldMedia,newMedia;QVERIFY(config.isValid()&&oldMedia.isValid()&&newMedia.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(writePosition(config.path(),oldMedia.path()));WindowSlot slot(config.path(),0);
        {
            ViewerController viewer;FileLock held(config.filePath("settings.json.lock"));QVERIFY(held.acquire(0));
            viewer.setWindowClaim(slot.claimIdentity());viewer.restoreWindow();
            viewer.navigateDirectory(newMedia.path());QTRY_VERIFY_WITH_TIMEOUT(!viewer.directoryPending(),3000);QVERIFY(viewer.directoryError().isEmpty());
            QCOMPARE(QDir::fromNativeSeparators(viewer.directory()),newMedia.path());QCOMPARE(folder(config.path()),oldMedia.path());
            held.release();viewer.flushWindowPosition();QTRY_COMPARE_WITH_TIMEOUT(folder(config.path()),newMedia.path(),3000);
            QCOMPARE(settings(config.path()).value("windows").toObject().value("1").toObject().value("future"),QJsonValue(true));
        }
        QCOMPARE(record(config.path()).value("state"),QJsonValue("clean"));
    }
    void decodedSuccessPersistsAndFailedDecodeKeepsCheckpoint() {
        QTemporaryDir config,media;QVERIFY(config.isValid()&&media.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        const auto imagePath=media.filePath("generated.png");QImage image(24,24,QImage::Format_RGB32);image.fill(Qt::red);QVERIFY(image.save(imagePath));
        WindowSlot slot(config.path(),0);
        {
            ViewerController viewer;viewer.setWindowClaim(slot.claimIdentity());viewer.open(imagePath);
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading(),5000);QVERIFY2(viewer.imageReady(),qPrintable(viewer.status()));viewer.flushWindowPosition();
            QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("windows").toObject().value("1").toObject().value("image"),QJsonValue(imagePath),3000);
            const auto checkpoint=record(config.path()).value("checkpointRevision");
            viewer.open(media.filePath("missing.png"));QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading(),5000);QVERIFY(!viewer.imageReady());viewer.flushWindowPosition();
            QCOMPARE(record(config.path()).value("checkpointRevision"),checkpoint);
        }
        QCOMPARE(record(config.path()).value("state"),QJsonValue("clean"));
    }
    void reentrantExplicitVisitWinsWhenResumeAnnouncesStatus() {
        QTemporaryDir config,oldMedia,newMedia;QVERIFY(config.isValid()&&oldMedia.isValid()&&newMedia.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(writePosition(config.path(),oldMedia.path()));WindowSlot slot(config.path(),0);
        ViewerController viewer;viewer.setWindowClaim(slot.claimIdentity());bool replaced=false;
        connect(&viewer,&ViewerController::stateChanged,&viewer,[&]{
            if(!replaced&&viewer.status().startsWith("Restoring")){replaced=true;viewer.navigateDirectory(newMedia.path());}
        });
        viewer.restoreWindow();QTRY_VERIFY_WITH_TIMEOUT(replaced,3000);QTRY_VERIFY_WITH_TIMEOUT(!viewer.directoryPending(),3000);
        QCOMPARE(QDir::fromNativeSeparators(viewer.directory()),newMedia.path());viewer.flushWindowPosition();QTRY_COMPARE_WITH_TIMEOUT(folder(config.path()),newMedia.path(),3000);
    }
    void claimReplacementWaitsForOutgoingWorkerWithoutBlockingUi() {
        struct Gate {std::mutex mutex;std::condition_variable changed;bool released{};std::atomic_bool entered{};std::atomic_int replacements{};};
        const auto gate=std::make_shared<Gate>();glancelot::settings_detail::TransactionHooks hooks;
        hooks.observe=[gate](glancelot::settings_detail::Stage stage){
            if(stage!=glancelot::settings_detail::Stage::BeforeReplace||++gate->replacements!=1)return;
            std::unique_lock lock(gate->mutex);gate->entered=true;gate->changed.wait(lock,[&]{return gate->released;});
        };
        const auto* previous=glancelot::settings_detail::transactionHooks;glancelot::settings_detail::transactionHooks=&hooks;
        const auto restore=qScopeGuard([previous]{glancelot::settings_detail::transactionHooks=previous;});
        QTemporaryDir config;QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        WindowSlot first(config.path(),0),second(config.path(),0);QCOMPARE(first.number(),1);QCOMPARE(second.number(),2);
        ViewerController viewer;
        const auto release=qScopeGuard([gate]{std::lock_guard lock(gate->mutex);gate->released=true;gate->changed.notify_all();});
        viewer.setWindowClaim(first.claimIdentity());QTRY_VERIFY_WITH_TIMEOUT(gate->entered.load(),3000);
        QElapsedTimer elapsed;elapsed.start();viewer.setWindowClaim(second.claimIdentity());
        qInfo()<<"claim reassignment while outgoing transaction blocked:"<<elapsed.elapsed()<<"ms";
        QVERIFY(elapsed.elapsed()<750);QCOMPARE(viewer.windowSlot(),0);
        {std::lock_guard lock(gate->mutex);gate->released=true;gate->changed.notify_all();}
        QTRY_COMPARE_WITH_TIMEOUT(viewer.windowSlot(),2,3000);
        QTRY_COMPARE_WITH_TIMEOUT(settings(config.path()).value("recovery").toObject().value("windows").toObject().value("2").toObject().value("state"),QJsonValue("dirty"),3000);
        QCOMPARE(record(config.path()).value("state"),QJsonValue("clean"));
    }
    void ownerCanBeDestroyedWhenResumeAnnouncesStatus() {
        QTemporaryDir config,media;QVERIFY(config.isValid()&&media.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());QVERIFY(writePosition(config.path(),media.path()));
        WindowSlot slot(config.path(),0);auto viewer=std::make_unique<ViewerController>();QPointer<ViewerController> alive(viewer.get());
        viewer->setWindowClaim(slot.claimIdentity());
        connect(viewer.get(),&ViewerController::stateChanged,viewer.get(),[&]{if(viewer&&viewer->status().startsWith("Restoring"))viewer.reset();});
        viewer->restoreWindow();QTRY_VERIFY_WITH_TIMEOUT(!alive,3000);
        QCOMPARE(record(config.path()).value("state"),QJsonValue("clean"));
    }
    void safeFeedbackUsesExistingStatusAndSuppressesResume() {
        QTemporaryDir config,media;QVERIFY(config.isValid()&&media.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());QVERIFY(writePosition(config.path(),media.path()));
        ViewerController viewer;viewer.setWindowSlot(1);viewer.setSafeMode(true);viewer.restoreWindow();
        QVERIFY(viewer.status().startsWith("Safe mode"));QVERIFY(viewer.directory().isEmpty());QVERIFY(viewer.filePath().isEmpty());
        viewer.setSafeMode(false);QVERIFY(!viewer.status().startsWith("Safe mode"));
    }
};
QTEST_MAIN(RecoveryControllerTests)
#include "recovery_controller_tests.moc"
