#include "glancelot/file_operation_service.hpp"
#include "glancelot/file_operations.hpp"
#include "platform_trash.hpp"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <atomic>
#include <condition_variable>
#include <mutex>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#endif

// DELETE-01 service tests. Every file is generated under a QTemporaryDir. The injected backend renames into a folder
// inside the fixture; the platform backend is only exercised on generated files (see realTrash*).
namespace fs = std::filesystem;
namespace {
fs::path native(const QString& path) {
#ifdef Q_OS_WIN
    return fs::path(QDir::toNativeSeparators(path).toStdWString());
#else
    return fs::path(QFile::encodeName(path).constData());
#endif
}
QString key(const QString& path) { return QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded(); }
struct Gate {
    std::mutex mutex; std::condition_variable wake; bool open{true}; std::atomic_int waiting{0};
    void close() { std::lock_guard lock(mutex); open = false; }
    void release() { { std::lock_guard lock(mutex); open = true; } wake.notify_all(); }
    void pass() { std::unique_lock lock(mutex); ++waiting; wake.wait(lock, [this] { return open; }); --waiting; }
};
struct FakeTrash final : glancelot::TrashBackend {
    fs::path bin;
    std::shared_ptr<Gate> gate = std::make_shared<Gate>();
    std::atomic_int calls{0};
    explicit FakeTrash(fs::path folder) : bin(std::move(folder)) {}
    glancelot::DeleteResult moveToTrash(const glancelot::MutationTarget& target) override {
        gate->pass();
        const int n = ++calls;
        std::error_code ec;
        fs::rename(target.path, bin / (std::to_string(n) + "-" + target.path.filename().string()), ec);
        return {target.path, ec ? glancelot::DeleteOutcome::Failed : glancelot::DeleteOutcome::Trashed, ec ? ec.message() : std::string{}};
    }
};
QString touch(const QTemporaryDir& dir, const QString& name) {
    QFile file(dir.filePath(name)); if (file.open(QIODevice::WriteOnly)) file.write("generated"); return dir.filePath(name);
}
}

class FileOperationServiceTests final : public QObject {
    Q_OBJECT
    QTemporaryDir media, bin;
    FakeTrash* fake{};
    std::unique_ptr<FileOperationService> make() {
        auto backend = std::make_unique<FakeTrash>(native(bin.path()));
        fake = backend.get();
        return std::make_unique<FileOperationService>(std::move(backend));
    }
private slots:
    void init() { QVERIFY(media.isValid() && bin.isValid()); }
    void signalsAndResultMaps() {
        auto service = make();
        QSignalSpy ready(service.get(), &FileOperationService::planReady), done(service.get(), &FileOperationService::finished),
            removed(service.get(), &FileOperationService::pathsRemoved), progress(service.get(), &FileOperationService::progress),
            busy(service.get(), &FileOperationService::busyChanged);
        const QString a = touch(media, "a.jpg"), b = touch(media, "b.png");
        QDir(media.path()).mkpath("album/inner"); touch(media, "album/inner/c.jpg");
        const quint64 id = service->plan({a, b, media.filePath("album"), media.filePath("gone.jpg")}, "grid");
        QVERIFY(id > 0);
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready[0][0].toULongLong(), id);
        const auto summary = ready[0][1].toMap();
        QCOMPARE(summary["origin"].toString(), QString("grid"));
        QCOMPARE(summary["fileCount"].toInt(), 2); QCOMPARE(summary["folderCount"].toInt(), 1);
        QCOMPARE(summary["contentFiles"].toInt(), 1); QCOMPARE(summary["contentFolders"].toInt(), 1);
        QVERIFY(summary["contentCountComplete"].toBool());
        QCOMPARE(summary["trashName"].toString(), QString::fromStdString(platform_trash::trashName()));
        const auto targets = summary["targets"].toList();
        QCOMPARE(targets.size(), 3);
        QCOMPARE(targets[0].toMap()["name"].toString(), QString("a.jpg"));
        QCOMPARE(key(targets[0].toMap()["path"].toString()), key(a));
        QCOMPARE(targets[2].toMap()["kind"].toString(), QString("folder"));
        const auto rejected = summary["rejected"].toList();
        QCOMPARE(rejected.size(), 1); QCOMPARE(rejected[0].toMap()["reason"].toString(), QString("missing"));
        QVERIFY(service->confirm(id));
        QVERIFY(service->busy());
        QVERIFY(!service->confirm(id)); // one confirmation per plan
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(!service->busy()); QCOMPARE(busy.count(), 2);
        const auto results = done[0][1].toList();
        QCOMPARE(results.size(), 3);
        for (const auto& r : results) QCOMPARE(r.toMap()["outcome"].toString(), QString("trashed"));
        QCOMPARE(removed.count(), 1); QCOMPARE(removed[0][0].toStringList().size(), 3);
        QVERIFY(!progress.isEmpty()); QCOMPARE(progress.last()[1].toInt(), 3); QCOMPARE(progress.last()[2].toInt(), 3);
        QVERIFY(!QFile::exists(a) && !QFile::exists(b) && !QFileInfo::exists(media.filePath("album")));
    }
    void supersededPlanCannotBeConfirmed() {
        auto service = make();
        QSignalSpy ready(service.get(), &FileOperationService::planReady);
        const QString a = touch(media, "s1.jpg"), b = touch(media, "s2.jpg");
        const quint64 first = service->plan({a}, "grid");
        const quint64 second = service->plan({b}, "tree"); // supersedes while the first may still be planning
        QVERIFY(second > first);
        QTRY_VERIFY(!ready.isEmpty());
        QTest::qWait(100);
        QCOMPARE(ready.count(), 1); QCOMPARE(ready[0][0].toULongLong(), second);
        QVERIFY(!service->confirm(first));
        // A ready plan is also superseded by a newer one.
        const quint64 third = service->plan({a}, "grid");
        QVERIFY(!service->confirm(second));
        QTRY_COMPARE(ready.count(), 2);
        QVERIFY(!service->confirm(second));
        QVERIFY(service->confirm(third));
        QTRY_VERIFY(!service->busy());
        QVERIFY(!QFile::exists(a) && QFile::exists(b));
    }
    void confirmAfterCancelReturnsFalse() {
        auto service = make();
        QSignalSpy ready(service.get(), &FileOperationService::planReady), failed(service.get(), &FileOperationService::planFailed);
        const QString a = touch(media, "c1.jpg");
        quint64 id = service->plan({a}, "grid");
        QTRY_COMPARE(ready.count(), 1);
        service->cancel(id);
        QVERIFY(!service->confirm(id));
        id = service->plan({a}, "grid");
        service->cancel(id); // while planning: the result is discarded
        QTest::qWait(150);
        QCOMPARE(ready.count(), 1); QVERIFY(!service->confirm(id));
        QCOMPARE(fake->calls.load(), 0); QVERIFY(QFile::exists(a));
        QVERIFY(!service->confirm(0)); QVERIFY(!service->confirm(9999));
        const quint64 empty = service->plan({}, "grid");
        QTRY_COMPARE(failed.count(), 1); QCOMPARE(failed[0][0].toULongLong(), empty); QVERIFY(!service->confirm(empty));
        // A plan whose every item is rejected offers nothing to confirm.
        const quint64 none = service->plan({media.filePath("nothing-here.jpg")}, "grid");
        QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(ready[1][1].toMap()["targets"].toList().size(), 0);
        QVERIFY(!service->confirm(none));
    }
    void busyRejectsNewWorkAndCancelStopsBeforeNextItem() {
        auto service = make();
        QSignalSpy ready(service.get(), &FileOperationService::planReady), done(service.get(), &FileOperationService::finished);
        QStringList files; for (int i = 0; i < 3; ++i) files << touch(media, QString("b%1.jpg").arg(i));
        const quint64 id = service->plan(files, "grid");
        QTRY_COMPARE(ready.count(), 1);
        auto gate = fake->gate; gate->close();
        QVERIFY(service->confirm(id));
        QTRY_COMPARE(gate->waiting.load(), 1); // the first item is in the backend
        QVERIFY(service->busy());
        QCOMPARE(service->plan({files[2]}, "grid"), quint64(0)); // busy
        service->cancel(id);
        gate->release();
        QTRY_COMPARE(done.count(), 1);
        const auto results = done[0][1].toList();
        QCOMPARE(results[0].toMap()["outcome"].toString(), QString("trashed"));
        QCOMPARE(results[1].toMap()["outcome"].toString(), QString("cancelled"));
        QCOMPARE(results[2].toMap()["outcome"].toString(), QString("cancelled"));
        QVERIFY(!QFile::exists(files[0]) && QFile::exists(files[1]) && QFile::exists(files[2]));
        QVERIFY(!service->busy());
        QVERIFY(service->plan({files[1]}, "grid") > 0);
    }
    void changedBetweenPlanAndConfirmIsReported() {
        auto service = make();
        QSignalSpy ready(service.get(), &FileOperationService::planReady), done(service.get(), &FileOperationService::finished),
            removed(service.get(), &FileOperationService::pathsRemoved);
        const QString a = touch(media, "edit.jpg");
        const quint64 id = service->plan({a}, "grid");
        QTRY_COMPARE(ready.count(), 1);
        { QFile file(a); QVERIFY(file.open(QIODevice::Append)); file.write(" and more"); }
        QVERIFY(service->confirm(id));
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(done[0][1].toList()[0].toMap()["outcome"].toString(), QString("changed"));
        QVERIFY(QFile::exists(a)); QCOMPARE(removed.count(), 0); QCOMPARE(fake->calls.load(), 0);
    }
#ifdef Q_OS_WIN
    void windowsRefusesPermanentDeletion() {
        // The sink's PreDeleteItem decision: only an item the shell will recycle may proceed.
        QCOMPARE(platform_trash::preDeleteDecision(TSF_DELETE_RECYCLE_IF_POSSIBLE), long(S_OK));
        QCOMPARE(platform_trash::preDeleteDecision(TSF_DELETE_RECYCLE_IF_POSSIBLE | TSF_OVERWRITE_EXIST), long(S_OK));
        QCOMPARE(platform_trash::preDeleteDecision(0), long(E_FAIL));
        QCOMPARE(platform_trash::preDeleteDecision(TSF_OVERWRITE_EXIST | TSF_NORMAL), long(E_FAIL));
    }
#endif
    // The platform backend on generated files. Windows: real Recycle Bin only when GLANCELOT_TRASH_SMOKE is set
    // (items are named with a GUID and printed so they can be found and restored or emptied by hand). POSIX: always,
    // with XDG_DATA_HOME pointed into the fixture so the user's Trash is never touched.
    void realTrashSmoke() {
#ifdef Q_OS_WIN
        if (!qEnvironmentVariableIsSet("GLANCELOT_TRASH_SMOKE")) QSKIP("Set GLANCELOT_TRASH_SMOKE=1 to exercise the real Recycle Bin on generated files");
#else
        QTemporaryDir data; QVERIFY(data.isValid());
        const QByteArray previous = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_HOME", data.path().toUtf8());
        const auto restore = qScopeGuard([&] { if (previous.isEmpty()) qunsetenv("XDG_DATA_HOME"); else qputenv("XDG_DATA_HOME", previous); });
#endif
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString name = "glancelot-smoke-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".txt";
        const QString file = touch(dir, name);
        FileOperationService service; // the default (platform) backend
        QSignalSpy readySpy(&service, &FileOperationService::planReady), doneSpy(&service, &FileOperationService::finished);
        const quint64 id = service.plan({file}, "command");
        QTRY_COMPARE(readySpy.count(), 1);
        QVERIFY(service.confirm(id));
        QTRY_COMPARE_WITH_TIMEOUT(doneSpy.count(), 1, 30000);
        const auto result = doneSpy[0][1].toList()[0].toMap();
        qInfo() << "real trash result" << result["outcome"].toString() << result["message"].toString() << "item" << name;
        QCOMPARE(result["outcome"].toString(), QString("trashed"));
        QVERIFY(!QFile::exists(file));
#ifndef Q_OS_WIN
        QVERIFY(QFile::exists(data.filePath("Trash/files/" + name)));
        QVERIFY(QFile::exists(data.filePath("Trash/info/" + name + ".trashinfo")));
#else
        qInfo() << "RECORD: generated item left in the Recycle Bin:" << name;
#endif
    }
    // A locked file is reported InUse and stays in place (Windows; a failed recycle leaves nothing in the bin).
    void realTrashLockedFileStays() {
#ifndef Q_OS_WIN
        QSKIP("POSIX has no mandatory file locks; rename succeeds on open files");
#else
        if (!qEnvironmentVariableIsSet("GLANCELOT_TRASH_SMOKE")) QSKIP("Set GLANCELOT_TRASH_SMOKE=1 to exercise the real Recycle Bin on generated files");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString file = touch(dir, "locked-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".txt");
        HANDLE lock = CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(file).utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        QVERIFY(lock != INVALID_HANDLE_VALUE);
        const auto unlock = qScopeGuard([&] { CloseHandle(lock); });
        std::error_code ec;
        const auto target = glancelot::snapshotTarget(native(file), ec);
        QVERIFY(target);
        const auto backend = platform_trash::make();
        const auto result = backend->moveToTrash(*target);
        qInfo() << "locked file outcome" << int(result.outcome) << QString::fromStdString(result.message);
        QCOMPARE(int(result.outcome), int(glancelot::DeleteOutcome::InUse));
        QVERIFY(QFile::exists(file));
#endif
    }
    // A volume without a Recycle Bin (a subst drive over a generated folder) must refuse, never delete.
    void substDriveRefusesWithoutDeleting() {
#ifndef Q_OS_WIN
        QSKIP("Windows only");
#else
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString file = touch(dir, "subst-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".txt");
        QString letter;
        const DWORD used = GetLogicalDrives();
        for (char c = 'Z'; c >= 'M' && letter.isEmpty(); --c) if (!(used & (1u << (c - 'A')))) letter = QString(QChar(c)) + ":";
        if (letter.isEmpty()) QSKIP("No free drive letter for subst");
        if (QProcess::execute("subst", {letter, QDir::toNativeSeparators(dir.path())}) != 0) QSKIP("subst unavailable");
        const auto unsubst = qScopeGuard([&] { QProcess::execute("subst", {letter, "/d"}); });
        const QString viaSubst = letter + "/" + QFileInfo(file).fileName();
        QVERIFY(QFile::exists(viaSubst));
        std::error_code ec;
        const auto target = glancelot::snapshotTarget(native(viaSubst), ec);
        QVERIFY(target);
        const auto result = platform_trash::make()->moveToTrash(*target);
        qInfo() << "subst outcome" << int(result.outcome) << QString::fromStdString(result.message);
        QVERIFY2(QFile::exists(file), "the shell deleted the file instead of refusing");
        if (result.outcome == glancelot::DeleteOutcome::Trashed) QFAIL("subst drive unexpectedly recycled");
        QCOMPARE(int(result.outcome), int(glancelot::DeleteOutcome::TrashUnavailable));
        // Evidence for the backend choice: Qt 6.8.3's QFile::moveToTrash also refuses in PreDeleteItem, but does not
        // record the refusal, so its return value cannot be trusted to mean "recycled". Not used by the application.
        const QString second = touch(dir, "qt-subst-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".txt");
        const bool qtReported = QFile::moveToTrash(letter + "/" + QFileInfo(second).fileName());
        qInfo() << "QFile::moveToTrash on the subst drive returned" << qtReported << "; file still present:" << QFile::exists(second);
        QVERIFY2(QFile::exists(second), "Qt deleted the file permanently");
#endif
    }
};

QTEST_GUILESS_MAIN(FileOperationServiceTests)
#include "file_operation_service_tests.moc"
