#include "glancelot/settings_file.hpp"
#include "glancelot/viewer_controller.hpp"
#include "glancelot/window_slot.hpp"
#include "../src/app/file_lock.hpp"
#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QTranslator>
#include <QtTest>
#include <QScopeGuard>
#include <array>
#include <iostream>
#include <future>

namespace {
// ---- helper processes (this executable re-launched with a mode) ----------------------------------------------
int helperMain(const QStringList& args) {
    const QString mode = args.value(1), dir = args.value(2);
    qputenv("GLANCELOT_CONFIG_DIR", dir.toUtf8());
    if (mode == "--slot-helper" || mode == "--crash-helper") {
        WindowSlot slot;
        std::cout << slot.number() << std::endl;
        if (mode == "--slot-helper") { while (std::cin.get() != EOF) {} return 0; } // closing stdin = closing the window
        for (;;) QThread::sleep(1);                                                // killed by the parent, never releases
    }
    if (mode == "--settings-helper") {
        const int slot = args.value(3).toInt(), count = args.value(4).toInt();
        const QString tag = args.value(5);
        for (int i = 0; i < count; ++i) {
            const auto status = glancelot::updateSettings([&](QJsonObject& root) {
                auto windows = root.value("windows").toObject();
                windows[QString::number(slot)] = QJsonObject{{"folder", "C:/f" + QString::number(slot)}, {"image", tag + "-" + QString::number(i)}};
                root["windows"] = windows;
            });
            // Under a saturated machine a writer can legitimately wait out the 4 s lock bound; a test helper retries it.
            auto result = status;
            for (int retry = 0; retry < 3 && result.code == glancelot::SettingsStatus::Busy; ++retry)
                result = glancelot::updateSettings([&](QJsonObject& root) {
                    auto windows = root.value("windows").toObject();
                    windows[QString::number(slot)] = QJsonObject{{"folder", "C:/f" + QString::number(slot)}, {"image", tag + "-" + QString::number(i)}};
                    root["windows"] = windows;
                });
            if (!result.ok()) return 10 + int(result.code);
        }
        return 0;
    }
    if (mode == "--lock-holder") {          // holds sessions.json.lock for N ms (a stuck or slow peer)
        FileLock lock(QDir(dir).filePath("sessions.json.lock"));
        if (!lock.acquire(5000)) return 3;
        std::cout << "held" << std::endl;
        QThread::msleep(quint32(args.value(3).toInt()));
        return 0;
    }
    if (mode == "--reader-spin") {          // windows starting up: the shared reader, far more often than real
        std::cout << "ready" << std::endl;
        for (;;) { QJsonObject snapshot; glancelot::readSettings(snapshot); QThread::msleep(25); }
    }
    if (mode == "--foreign-reader") {       // software that is not Glancelot (indexer, scanner) opening the file now and then
        std::cout << "ready" << std::endl;
        const QString path = QDir(dir).filePath("settings.json");
        for (;;) { { QFile file(path); if (file.open(QIODevice::ReadOnly)) file.readAll(); } QThread::msleep(20); }
    }
    if (mode == "--settings-spin") {
        std::cout << "ready" << std::endl;
        for (qint64 i = 0;; ++i) glancelot::updateSettings([&](QJsonObject& root) { root["spin"] = i; });
    }
    return 2;
}

struct Helper {
    QProcess process;
    int slot{-1};
    bool start(const QString& mode, const QString& dir, const QStringList& extra = {}) {
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start(QCoreApplication::applicationFilePath(), QStringList{mode, dir} + extra);
        return process.waitForStarted(10000);
    }
    bool readSlot() {
        if (!process.canReadLine() && !process.waitForReadyRead(15000)) return false;
        while (!process.canReadLine()) if (!process.waitForReadyRead(15000)) return false;
        slot = process.readLine().trimmed().toInt();
        return true;
    }
    void closeWindow() { process.closeWriteChannel(); process.waitForFinished(15000); }
    void crash() { process.kill(); process.waitForFinished(15000); }
};

QString writePng(const QString& directory, const QString& name, bool valid = true) {
    const auto path = directory + "/" + name;
    if (!valid) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write("not a png") > 0 ? path : QString{}; }
    QImage image(16, 10, QImage::Format_RGBA8888);
    image.fill(QColor(30, 90, 200));
    image.setColorSpace(QColorSpace::SRgb);
    return image.save(path, "PNG") ? path : QString{};
}
QString make(const QTemporaryDir& dir, const QString& relative) { const auto path = dir.filePath(relative); QDir().mkpath(path); return path; }
bool writeFile(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray readFile(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
QJsonObject settingsOf(const QString& dir) { return QJsonDocument::fromJson(readFile(QDir(dir).filePath("settings.json"))).object(); }
bool same(const QString& a, const QString& b) { return !a.isEmpty() && QFileInfo(a) == QFileInfo(b); }
QString pathOf(const ViewerController& viewer) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(viewer.snapshot().current.native());
#else
    return QFile::decodeName(viewer.snapshot().current.native().c_str());
#endif
}
void waitOpen(ViewerController& viewer, const QString& expected = {}) {
    QTRY_VERIFY_WITH_TIMEOUT(!viewer.snapshot().current.empty() && !viewer.loading() && (expected.isEmpty() || same(pathOf(viewer), expected)), 20000);
}
}

class WindowSlotTests final : public QObject {
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(QRegularExpression("QProcess: Destroyed while process.*still running.*")); }
    void cleanup() { qunsetenv("GLANCELOT_CONFIG_DIR"); }
    void staleWindowsMergeIndependentSortFields();
    void contendedSortReportsSessionOnly();
    void failedBackupAndOversizedEditsPreservePrimary();
    void staleSessionCounterCannotReuseLiveSlot();
    void unreadableSessionStateCannotClaimSlot();
    void lateSlotClaimRemembersOnlySuccessfulImages();
    void conflictingEditsFromDifferentWindowsAreRejected();
    void contendedPositionSaveRetriesWithoutBlocking();
    void counterLimitDoesNotReuseLiveSlots();
    void slotsFollowOpeningOrderAndAreNeverRenumbered();
    void reverseCloseOrderStartsANewSessionAtOne();
    void simultaneousLaunchesGetDistinctSlots();
    void crashedWindowSlotsAreReclaimed();
    void inProcessClaimsAndUnavailableCoordination();
    void settingsUpdatesFromManyProcessesLoseNothing();
    void killedWriterNeverCorruptsSettings();
    void futureOrCorruptSettingsAreNeverWritten();
    void settingsWritesSurviveReadersAndLockContention();
    void slotClaimPollsTheLockAndRetriesAfterTimeout();
    void sessionStateOfAnotherSchemaIsNeverRewritten();
    void damagedSettingsRecoverFromLastGoodCopyOrResetExplicitly();
    void windowsEditingDifferentPreferencesDoNotRevertEachOther();
    void restoreFromJunctionFolderIsReportedAsUnreadable();
    void positionIsSavedOnlyAfterSuccessfulDecode();
    void windowsKeepIndependentPositionsAndPreserveOtherSettings();
    void restoreOpensSavedImageOrFirstImageOfSavedFolder();
    void missingFolderIsReportedAndSavedLocationKept();
    void explicitOpenOverridesRestore();
    void positionFlushesOnDestructionAndDisabledSlotWritesNothing();
    // INPUT-02
    void scopeAwareConflictsAndChordPrefixes();
    void scopedBindingsPersistAndReload();
    void wheelModifiersPersistFallBackIndividuallyAndMerge();
    void bindingProblemExplainsEveryRefusal();
    void accessibilityOptionsPersistAndReset();
    void tabsKeepLocationsAndRestoreWithTheWindow();
    void commandNamesAreTranslatable();
};

void WindowSlotTests::staleWindowsMergeIndependentSortFields() {
    QTemporaryDir config; QVERIFY(config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
    QVERIFY(glancelot::updateSettings([](QJsonObject& root){
        root["sort"]=QJsonObject{{"key","name"},{"descending",false},{"seed",7},{"future-sort-option","preserve me"}};
    }).ok());
    ViewerController first, second;
    first.setSortKey("size");
    second.setSortDescending(true);
    const auto sort=settingsOf(config.path()).value("sort").toObject();
    QCOMPARE(sort.value("key").toString(),QString("size"));
    QVERIFY(sort.value("descending").toBool());
    QCOMPARE(sort.value("seed").toInt(),7);
    QCOMPARE(sort.value("future-sort-option").toString(),QString("preserve me"));
}
void WindowSlotTests::contendedSortReportsSessionOnly() {
    QTemporaryDir config; QVERIFY(config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
    ViewerController viewer;
    QVERIFY(QDir().mkpath(config.path()));
    FileLock lock(config.filePath("settings.json.lock")); QVERIFY(lock.acquire(0));
    viewer.setSortKey("size");
    QCOMPARE(viewer.sortKey(),QString("size"));
    QVERIFY(viewer.status().contains("for this session"));
    QVERIFY(!QFile::exists(config.filePath("settings.json")));
}

void WindowSlotTests::failedBackupAndOversizedEditsPreservePrimary() {
    QTemporaryDir config; QVERIFY(config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
    const auto path=config.filePath("settings.json");
    QVERIFY(glancelot::updateSettings([](QJsonObject& root){root["keep"]="original";}).ok());
    const auto before=readFile(path);
    // A directory at the backup path deterministically prevents replacing the backup.
    QVERIFY(QDir().mkpath(path+".bak"));
    QCOMPARE(glancelot::updateSettings([](QJsonObject& root){root["keep"]="lost";}).code,glancelot::SettingsStatus::WriteFailed);
    QCOMPARE(readFile(path),before);
    QVERIFY(QDir().rmdir(path+".bak"));
    QCOMPARE(glancelot::updateSettings([](QJsonObject& root){root["oversized"]=QString(1024*1024,'x');}).code,glancelot::SettingsStatus::WriteFailed);
    QCOMPARE(readFile(path),before);
    QVERIFY(glancelot::updateSettings([](QJsonObject& root){root["after"]=1;}).ok());
    QCOMPARE(readFile(path+".bak"),before);
    const QByteArray damaged="{ damaged";
    QVERIFY(writeFile(path,damaged));
    QVERIFY(QDir().mkpath(path+".damaged"));
    QCOMPARE(glancelot::updateSettings([](QJsonObject& root){root["recovered"]=1;}).code,glancelot::SettingsStatus::WriteFailed);
    QCOMPARE(readFile(path),damaged);QCOMPARE(readFile(path+".bak"),before);
    QVERIFY(QDir().rmdir(path+".damaged"));
    const auto recovered=glancelot::updateSettings([](QJsonObject& root){root["recovered"]=1;});
    QVERIFY(recovered.ok()&&recovered.recovered);QCOMPARE(readFile(path+".damaged"),damaged);
}

void WindowSlotTests::staleSessionCounterCannotReuseLiveSlot() {
    QTemporaryDir config; QVERIFY(config.isValid());
    WindowSlot first(config.path()); QCOMPARE(first.number(),1);
    const auto path=config.filePath("sessions.json");
    auto state=QJsonDocument::fromJson(readFile(path)).object();
    state["counter"]=0;
    QVERIFY(writeFile(path,QJsonDocument(state).toJson()));
    WindowSlot second(config.path()); QCOMPARE(second.number(),2);
}

void WindowSlotTests::unreadableSessionStateCannotClaimSlot() {
#ifdef Q_OS_WIN
    QTemporaryDir config; QVERIFY(config.isValid());
    WindowSlot first(config.path()); QCOMPARE(first.number(),1);
    const auto path=config.filePath("sessions.json");
    const auto before=readFile(path);
    // Deny other readers while allowing atomic replacement: this exposes destructive read-failure fallback.
    HANDLE handle=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,
        FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    QVERIFY(handle!=INVALID_HANDLE_VALUE);
    const auto close=qScopeGuard([&]{CloseHandle(handle);});
    WindowSlot refused(config.path(),0); QCOMPARE(refused.number(),0);
#else
    QSKIP("Windows read-sharing fixture");
#endif
}

void WindowSlotTests::lateSlotClaimRemembersOnlySuccessfulImages() {
    QTemporaryDir files, config; QVERIFY(files.isValid() && config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    const auto good=writePng(files.path(),"good.png"), bad=writePng(files.path(),"bad.png",false);
    ViewerController viewer;
    viewer.restoreWindow(); // no claim yet
    viewer.open(good); waitOpen(viewer,good);
    QVERIFY(!viewer.image().isNull());
    viewer.open(bad); QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading(),20000);
    viewer.setWindowSlot(1); viewer.restoreWindow(); viewer.flushWindowPosition();
    QVERIFY(same(settingsOf(config.path()).value("windows").toObject().value("1").toObject().value("image").toString(),good));
    ViewerController resumed;
    resumed.restoreWindow(); // unavailable at startup, then coordination recovers
    resumed.setWindowSlot(1); resumed.restoreWindow(); waitOpen(resumed,good);
    QVERIFY(!resumed.image().isNull());
}

void WindowSlotTests::conflictingEditsFromDifferentWindowsAreRejected() {
    QTemporaryDir config; QVERIFY(config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
    ViewerController first, second;
    QVERIFY(first.setBinding("rotate","X"));
    QVERIFY(!second.setBinding("fit","X"));
    QCOMPARE(second.bindingFor("fit"),QString("F"));
    QVERIFY(second.status().contains("another window"));
    ViewerController reopened;
    QCOMPARE(reopened.bindingFor("rotate"),QString("X"));
    QCOMPARE(reopened.bindingFor("fit"),QString("F"));
    QVERIFY(second.setBinding("fit","Y"));
    QCOMPARE(second.bindingFor("rotate"),QString("X"));
}

void WindowSlotTests::contendedPositionSaveRetriesWithoutBlocking() {
    QTemporaryDir files, config; QVERIFY(files.isValid() && config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
    const auto good=writePng(files.path(),"good.png");
    ViewerController viewer; viewer.setWindowSlot(1); viewer.open(good); waitOpen(viewer,good);
    FileLock lock(config.filePath("settings.json.lock")); QVERIFY(lock.acquire(0));
    QElapsedTimer elapsed; elapsed.start(); viewer.flushWindowPosition();
    QVERIFY2(elapsed.elapsed()<250,"position flush blocked on the GUI thread");
    lock.release();
    QTRY_VERIFY_WITH_TIMEOUT(same(settingsOf(config.path()).value("windows").toObject().value("1").toObject().value("image").toString(),good),5000);
    FileLock sessionLock(config.filePath("sessions.json.lock")); QVERIFY(sessionLock.acquire(0));
    elapsed.restart(); WindowSlot slot(config.path(),0);
    QCOMPARE(slot.number(),0); QVERIFY(elapsed.elapsed()<250);
    sessionLock.release(); QVERIFY(slot.claim(0));
}

void WindowSlotTests::counterLimitDoesNotReuseLiveSlots() {
    QTemporaryDir config; QVERIFY(config.isValid());
    WindowSlot live(config.path()); QCOMPARE(live.number(),1);
    const auto path=config.filePath("sessions.json");
    auto state=QJsonDocument::fromJson(readFile(path)).object();
    state["counter"]=1000000;
    QVERIFY(writeFile(path,QJsonDocument(state).toJson()));
    WindowSlot refused(config.path()); QCOMPARE(refused.number(),0);
    QCOMPARE(QJsonDocument::fromJson(readFile(path)).object().value("counter").toInt(),1000000);
    live.release(); QVERIFY(refused.claim()); QCOMPARE(refused.number(),1);
}

void WindowSlotTests::slotsFollowOpeningOrderAndAreNeverRenumbered() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Helper a, b, c, d, e, f;
    QVERIFY(a.start("--slot-helper", dir.path()) && a.readSlot()); QCOMPARE(a.slot, 1);
    QVERIFY(b.start("--slot-helper", dir.path()) && b.readSlot()); QCOMPARE(b.slot, 2);
    QVERIFY(c.start("--slot-helper", dir.path()) && c.readSlot()); QCOMPARE(c.slot, 3);
    b.closeWindow();                                  // out-of-order close must not renumber a or c
    QVERIFY(d.start("--slot-helper", dir.path()) && d.readSlot());
    QCOMPARE(d.slot, 4);                              // the next opening-order number, not the freed 2
    a.closeWindow(); c.closeWindow();
    QVERIFY(e.start("--slot-helper", dir.path()) && e.readSlot());
    QCOMPARE(e.slot, 5);                              // d is still alive, so the session continues
    d.closeWindow(); e.closeWindow();
    QVERIFY(f.start("--slot-helper", dir.path()) && f.readSlot());
    QCOMPARE(f.slot, 1);                              // every window closed: a new application session starts at 1
    f.closeWindow();
}

void WindowSlotTests::reverseCloseOrderStartsANewSessionAtOne() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Helper a, b, c, d;
    QVERIFY(a.start("--slot-helper", dir.path()) && a.readSlot());
    QVERIFY(b.start("--slot-helper", dir.path()) && b.readSlot());
    QVERIFY(c.start("--slot-helper", dir.path()) && c.readSlot());
    c.closeWindow(); b.closeWindow(); a.closeWindow(); // reversed
    QVERIFY(d.start("--slot-helper", dir.path()) && d.readSlot());
    QCOMPARE(d.slot, 1);
    d.closeWindow();
}

void WindowSlotTests::simultaneousLaunchesGetDistinctSlots() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    constexpr int count = 8;
    std::array<Helper, count> helpers;
    for (auto& helper : helpers) QVERIFY(helper.start("--slot-helper", dir.path())); // all launched before any is read
    QSet<int> seen;
    for (auto& helper : helpers) { QVERIFY(helper.readSlot()); seen.insert(helper.slot); }
    QCOMPARE(seen.size(), count);
    for (int slot = 1; slot <= count; ++slot) QVERIFY2(seen.contains(slot), qPrintable(QString("slot %1 missing").arg(slot)));
    for (auto& helper : helpers) helper.closeWindow();
}

void WindowSlotTests::crashedWindowSlotsAreReclaimed() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Helper a, b, c, d;
    QVERIFY(a.start("--slot-helper", dir.path()) && a.readSlot());
    QVERIFY(b.start("--crash-helper", dir.path()) && b.readSlot());
    QCOMPARE(a.slot, 1); QCOMPARE(b.slot, 2);
    b.crash();                                        // killed without releasing
    QVERIFY(c.start("--crash-helper", dir.path()) && c.readSlot());
    QCOMPARE(c.slot, 3);                              // the session continues while a is alive; b is pruned
    const auto live = QJsonDocument::fromJson(readFile(QDir(dir.path()).filePath("sessions.json"))).object().value("live").toArray();
    QCOMPARE(live.size(), 2);
    c.crash(); a.closeWindow();
    QVERIFY(d.start("--slot-helper", dir.path()) && d.readSlot());
    QCOMPARE(d.slot, 1);                              // nothing alive: the stale entries do not hold the counter up
    d.closeWindow();
}

void WindowSlotTests::inProcessClaimsAndUnavailableCoordination() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    {
        WindowSlot first(dir.path()), second(dir.path());
        QCOMPARE(first.number(), 1); QCOMPARE(second.number(), 2);
        first.release();
        QCOMPARE(first.number(), 0);
        WindowSlot third(dir.path());
        QCOMPARE(third.number(), 3);
        second.release(); third.release();
    }
    WindowSlot fresh(dir.path());
    QCOMPARE(fresh.number(), 1);
    // Coordination that cannot be established means "no position memory", never a guessed slot.
    QTemporaryDir other; QVERIFY(other.isValid());
    const auto blocker = other.filePath("file-not-directory");
    QVERIFY(writeFile(blocker, "x"));
    WindowSlot unavailable(blocker + "/nested");
    QCOMPARE(unavailable.number(), 0);
}

void WindowSlotTests::settingsUpdatesFromManyProcessesLoseNothing() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QByteArray seed = R"({"schema":1,"bindings":{"fit":"X"},"appearance":{"family":"slate-ice","mode":"dark"},"futureKey":{"x":[1,2,{"y":null}]}})";
    QVERIFY(writeFile(QDir(dir.path()).filePath("settings.json"), seed));
    const auto before = QJsonDocument::fromJson(seed).object();
    constexpr int count = 6, iterations = 25;
    std::array<Helper, count> helpers;
    for (int i = 0; i < count; ++i) QVERIFY(helpers[std::size_t(i)].start("--settings-helper", dir.path(), {QString::number(i + 1), QString::number(iterations), "w" + QString::number(i + 1)}));
    for (auto& helper : helpers) { QVERIFY(helper.process.waitForFinished(120000)); QCOMPARE(helper.process.exitCode(), 0); }
    const auto root = settingsOf(dir.path());
    QCOMPARE(root.value("schema").toInt(), 1);
    for (const char* key : {"bindings", "appearance", "futureKey"}) QCOMPARE(root.value(key), before.value(key));
    const auto windows = root.value("windows").toObject();
    QCOMPARE(windows.size(), count);
    for (int i = 1; i <= count; ++i)
        QCOMPARE(windows.value(QString::number(i)).toObject().value("image").toString(), QString("w%1-%2").arg(i).arg(iterations - 1));
}

void WindowSlotTests::killedWriterNeverCorruptsSettings() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    QVERIFY(writeFile(QDir(dir.path()).filePath("settings.json"), R"({"schema":1,"keep":"me"})"));
    qputenv("GLANCELOT_CONFIG_DIR", dir.path().toUtf8());
    Helper spinner;
    const auto stopSpinner = qScopeGuard([&] { if (spinner.process.state() != QProcess::NotRunning) spinner.crash(); });
    QVERIFY(spinner.start("--settings-spin", dir.path()));
    QVERIFY(spinner.process.waitForReadyRead(15000));
    QElapsedTimer timer; timer.start();
    int reads = 0;
    while (timer.elapsed() < 1500) {
        QJsonObject snapshot;
        const auto status = glancelot::readSettings(snapshot);
        QVERIFY2(status.ok(), "a reader saw a torn or unreadable settings file");
        QCOMPARE(snapshot.value("keep").toString(), QString("me"));
        ++reads; QThread::msleep(2);
    }
    spinner.crash();                                  // killed mid-write, possibly holding the lock
    QJsonObject after;
    QVERIFY(glancelot::readSettings(after).ok());
    QCOMPARE(after.value("keep").toString(), QString("me"));
    QVERIFY(after.contains("spin"));
    QElapsedTimer relock; relock.start();
    const auto status = glancelot::updateSettings([](QJsonObject& root) { root["after"] = true; });
    QVERIFY2(status.ok(), "a dead writer's lock was not reclaimed");
    QVERIFY(relock.elapsed() < 10000);
    QVERIFY(settingsOf(dir.path()).value("after").toBool());
    QVERIFY(reads > 50);
}

void WindowSlotTests::futureOrCorruptSettingsAreNeverWritten() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", dir.path().toUtf8());
    const auto path = QDir(dir.path()).filePath("settings.json");
    const QByteArray future = R"({"schema":2,"windows":{"1":{"image":"keep"}}})";
    QVERIFY(writeFile(path, future));
    auto status = glancelot::updateSettings([](QJsonObject& root) { root["x"] = 1; });
    QCOMPARE(status.code, glancelot::SettingsStatus::Unsupported);
    QCOMPARE(readFile(path), future);
    const QByteArray corrupt = "{ not json";
    QVERIFY(writeFile(path, corrupt));
    status = glancelot::updateSettings([](QJsonObject& root) { root["x"] = 1; });
    QCOMPARE(status.code, glancelot::SettingsStatus::Corrupt);
    QCOMPARE(readFile(path), corrupt);
    QJsonObject out;
    QCOMPARE(glancelot::readSettings(out).code, glancelot::SettingsStatus::Corrupt);
    QFile::remove(path);
    QVERIFY(glancelot::readSettings(out).ok() && out.isEmpty());
    QVERIFY(glancelot::updateSettings([](QJsonObject& root) { root["x"] = 1; }).ok());
    QVERIFY(settingsOf(dir.path()).value("x").toInt() == 1);
    // A window with a future-schema file keeps running and never writes a position.
    QVERIFY(writeFile(path, future));
    QTemporaryDir files; QVERIFY(files.isValid());
    ViewerController viewer;
    viewer.setWindowSlot(1);
    viewer.open(writePng(files.path(), "a.png"));
    waitOpen(viewer);
    viewer.flushWindowPosition();
    QCOMPARE(readFile(path), future);
}

void WindowSlotTests::settingsWritesSurviveReadersAndLockContention() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", dir.path().toUtf8());
    QVERIFY(writeFile(QDir(dir.path()).filePath("settings.json"), R"({"schema":1})"));
    Helper readerA, readerB, foreign;
    const auto stop = qScopeGuard([&] { for (Helper* h : {&readerA, &readerB, &foreign}) if (h->process.state() != QProcess::NotRunning) h->crash(); });
    QVERIFY(readerA.start("--reader-spin", dir.path()) && readerB.start("--reader-spin", dir.path()) && foreign.start("--foreign-reader", dir.path()));
    QVERIFY(readerA.process.waitForReadyRead(15000) && readerB.process.waitForReadyRead(15000) && foreign.process.waitForReadyRead(15000));
    int failures = 0, lastCode = 0;
    QElapsedTimer writes; writes.start();
    // Exercise the bounded-retry background writer; interactive writes intentionally return
    // immediately under contention and are covered by contendedPositionSaveRetriesWithoutBlocking.
    std::async(std::launch::async,[&] {
    for (int i = 0; i < 150; ++i) {
        const auto status = glancelot::updateSettings([&](QJsonObject& root) { root["n"] = i; });
        if (!status.ok()) { ++failures; lastCode = int(status.code); }
    }
    }).get();
    QVERIFY2(failures == 0, qPrintable(QString("%1 of 150 writes failed (last code %2) in %3 ms").arg(failures).arg(lastCode).arg(writes.elapsed())));
                               // a reader holding the file open at the rename must not lose a write
    QCOMPARE(settingsOf(dir.path()).value("n").toInt(), 149);
    // Contention among writers is a short wait, not a multi-second backoff.
    std::array<Helper, 6> writers;
    QElapsedTimer timer; timer.start();
    for (int i = 0; i < 6; ++i) QVERIFY(writers[std::size_t(i)].start("--settings-helper", dir.path(), {QString::number(i + 1), "30", "c" + QString::number(i)}));
    for (auto& writer : writers) { QVERIFY(writer.process.waitForFinished(120000)); QCOMPARE(writer.process.exitCode(), 0); }
    QVERIFY2(timer.elapsed() < 20000, qPrintable(QString("contended writes took %1 ms").arg(timer.elapsed())));
}

void WindowSlotTests::slotClaimPollsTheLockAndRetriesAfterTimeout() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    {
        Helper holder;
        QVERIFY(holder.start("--lock-holder", dir.path(), {"600"}));
        QVERIFY(holder.process.waitForReadyRead(15000));
        QElapsedTimer timer; timer.start();
        WindowSlot slot(dir.path());                   // the lock frees after ~600 ms; polling must notice quickly
        QCOMPARE(slot.number(), 1);
        QVERIFY2(timer.elapsed() < 2500, qPrintable(QString("claim waited %1 ms").arg(timer.elapsed())));
        holder.process.waitForFinished(15000);
    }
    Helper longHolder;
    QVERIFY(longHolder.start("--lock-holder", dir.path(), {"6000"}));
    QVERIFY(longHolder.process.waitForReadyRead(15000));
    QElapsedTimer timer; timer.start();
    WindowSlot slot(dir.path());
    QCOMPARE(slot.number(), 0);                        // bounded wait, then "no position memory" rather than a hang
    QVERIFY2(timer.elapsed() < 5500, qPrintable(QString("timeout took %1 ms").arg(timer.elapsed())));
    QVERIFY(longHolder.process.waitForFinished(15000));
    QVERIFY(slot.claim());                              // a later retry succeeds
    QVERIFY(slot.number() >= 1);
}

void WindowSlotTests::sessionStateOfAnotherSchemaIsNeverRewritten() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = QDir(dir.path()).filePath("sessions.json");
    const QByteArray future = R"({"schema":2,"counter":9,"live":[],"newField":{"keep":"me"}})";
    QVERIFY(writeFile(path, future));
    {
        WindowSlot slot(dir.path());
        QCOMPARE(slot.number(), 0);
    }
    QCOMPARE(readFile(path), future);
    // Damaged bookkeeping cannot prove that an existing slot is free: refuse and retain it.
    QVERIFY(writeFile(path, "{ nope"));
    { WindowSlot slot(dir.path()); QCOMPARE(slot.number(), 0); }
    QCOMPARE(readFile(path),QByteArray("{ nope"));
    const auto oversized=QByteArray(70*1024,'{');
    QVERIFY(writeFile(path,oversized));
    { WindowSlot slot(dir.path()); QCOMPARE(slot.number(), 0); }
    QCOMPARE(readFile(path),oversized);
    // Unknown keys of a valid schema-1 file travel through unchanged.
    QVERIFY(writeFile(path, R"({"schema":1,"counter":0,"live":[],"extra":{"k":[1,2]}})"));
    { WindowSlot slot(dir.path()); QCOMPARE(slot.number(), 1); QJsonObject root = QJsonDocument::fromJson(readFile(path)).object(); QCOMPARE(root.value("extra").toObject().value("k").toArray().size(), 2); }
}

void WindowSlotTests::damagedSettingsRecoverFromLastGoodCopyOrResetExplicitly() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", dir.path().toUtf8());
    const auto path = QDir(dir.path()).filePath("settings.json");
    QVERIFY(glancelot::updateSettings([](QJsonObject& root) { root["first"] = 1; root["keep"] = "a"; }).ok());
    QVERIFY(glancelot::updateSettings([](QJsonObject& root) { root["second"] = 2; }).ok()); // .bak now holds the 'first' state
    const QByteArray damaged = "{\"schema\":1,\"truncat";
    for (const QByteArray& bytes : {damaged, QByteArray()}) {
        QVERIFY(writeFile(path, bytes));
        QJsonObject read;
        const auto status = glancelot::readSettings(read);
        QVERIFY(status.ok() && status.recovered);
        QCOMPARE(read.value("keep").toString(), QString("a"));
        const auto update = glancelot::updateSettings([](QJsonObject& root) { root["third"] = 3; });
        QVERIFY(update.ok() && update.recovered);
        QCOMPARE(readFile(path + ".damaged"), bytes);                      // the damaged bytes are kept, not discarded
        const auto after = settingsOf(dir.path());
        QCOMPARE(after.value("keep").toString(), QString("a"));
        QCOMPARE(after.value("third").toInt(), 3);
        QVERIFY(!after.contains("second"));                                // last-known-good, as documented
        QVERIFY(glancelot::updateSettings([](QJsonObject& root) { root["fourth"] = 4; }).ok());
        QVERIFY(!glancelot::updateSettings([](QJsonObject&) {}).recovered);
    }
    // No valid copy: nothing is written until the user explicitly resets.
    QFile::remove(path + ".bak");
    QVERIFY(writeFile(path, damaged));
    QCOMPARE(glancelot::updateSettings([](QJsonObject& root) { root["x"] = 1; }).code, glancelot::SettingsStatus::Corrupt);
    QCOMPARE(readFile(path), damaged);
    QString moved;
    QVERIFY(glancelot::resetSettings(&moved));
    QVERIFY(!QFileInfo::exists(path));
    QCOMPARE(readFile(moved), damaged);                                    // set aside, never deleted
    QVERIFY(glancelot::updateSettings([](QJsonObject& root) { root["fresh"] = true; }).ok());
    QVERIFY(settingsOf(dir.path()).value("fresh").toBool());
    // A future schema is not damage: it is never recovered over, even when a backup exists.
    const QByteArray future = R"({"schema":2,"x":1})";
    QVERIFY(writeFile(path, future));
    QCOMPARE(glancelot::updateSettings([](QJsonObject&) {}).code, glancelot::SettingsStatus::Unsupported);
    QCOMPARE(readFile(path), future);
}

void WindowSlotTests::windowsEditingDifferentPreferencesDoNotRevertEachOther() {
    QTemporaryDir config; QVERIFY(config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    ViewerController first, second;                    // both started with identical in-memory preferences
    first.setWheelMode("zoom");
    second.setBoundaryMode("stop");
    QVERIFY(first.setBinding("rotate", "Y"));
    QVERIFY(second.setBinding("fit", "X"));
    first.setFilmstripVisible(false);
    second.setTreePinned(true);
    const auto root = settingsOf(config.path());
    QCOMPARE(root.value("wheel").toString(), QString("zoom"));
    QCOMPARE(root.value("boundary").toString(), QString("stop"));
    QCOMPARE(root.value("bindings").toObject().value("rotate").toString(), QString("Y"));
    QCOMPARE(root.value("bindings").toObject().value("fit").toString(), QString("X"));
    QVERIFY(!root.value("filmstrip").toBool(true));
    QVERIFY(root.value("treePinned").toBool());
}

void WindowSlotTests::restoreFromJunctionFolderIsReportedAsUnreadable() {
#ifdef Q_OS_WIN
    QTemporaryDir files, config; QVERIFY(files.isValid() && config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    const auto real = make(files, "real");
    const auto image = writePng(real, "r.png");
    const auto junction = files.filePath("junction");
    QProcess link;
    link.start("cmd.exe", {"/c", "mklink", "/J", QDir::toNativeSeparators(junction), QDir::toNativeSeparators(real)});
    QVERIFY(link.waitForFinished(15000));
    if (link.exitCode() != 0 || !QFileInfo::exists(junction)) QSKIP("Directory junctions cannot be created here");
    const QJsonObject entry{{"folder", junction}, {"image", junction + "/r.png"}};
    QVERIFY(glancelot::updateSettings([&](QJsonObject& root) { root["windows"] = QJsonObject{{"1", entry}}; }).ok());
    ViewerController viewer;
    viewer.setWindowSlot(1);
    viewer.restoreWindow();
    QTRY_VERIFY_WITH_TIMEOUT(viewer.status().contains("cannot be read"), 20000);   // links are never followed, even for restore
    QVERIFY(viewer.snapshot().current.empty());
    Q_UNUSED(image);
    QDir().rmdir(junction);                            // removes the junction itself, not its target
#else
    QSKIP("Windows junction fixture");
#endif
}

void WindowSlotTests::positionIsSavedOnlyAfterSuccessfulDecode() {
    QTemporaryDir files, config; QVERIFY(files.isValid() && config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    const auto good = writePng(files.path(), "good.png");
    const auto bad = writePng(files.path(), "bad.png", false);
    ViewerController viewer;
    viewer.setWindowSlot(1);
    viewer.open(good);
    waitOpen(viewer, good);
    viewer.flushWindowPosition();
    auto entry = settingsOf(config.path()).value("windows").toObject().value("1").toObject();
    QVERIFY(same(entry.value("image").toString(), good));
    QVERIFY(same(entry.value("folder").toString(), files.path()));
    viewer.open(bad);                                 // fails to decode: the saved location must not move
    QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading(), 20000);
    viewer.flushWindowPosition();
    entry = settingsOf(config.path()).value("windows").toObject().value("1").toObject();
    QVERIFY(same(entry.value("image").toString(), good));
}

void WindowSlotTests::windowsKeepIndependentPositionsAndPreserveOtherSettings() {
    QTemporaryDir files, config; QVERIFY(files.isValid() && config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    const QByteArray seed = R"({"schema":1,"bindings":{"fit":"X"},"appearance":{"family":"slate-ice","mode":"dark"},"futureKey":[1,2,3],"windows":{"7":{"folder":"C:/kept","image":"C:/kept/a.png"}}})";
    QVERIFY(writeFile(QDir(config.path()).filePath("settings.json"), seed));
    const auto before = QJsonDocument::fromJson(seed).object();
    const auto one = writePng(make(files, "one"), "1.png"), two = writePng(make(files, "two"), "2.png");
    {
        ViewerController first, second;
        first.setWindowSlot(1); second.setWindowSlot(2);
        first.open(one); second.open(two);
        waitOpen(first, one); waitOpen(second, two);
        first.flushWindowPosition(); second.flushWindowPosition();
    }
    const auto root = settingsOf(config.path());
    const auto windows = root.value("windows").toObject();
    QVERIFY(same(windows.value("1").toObject().value("image").toString(), one));
    QVERIFY(same(windows.value("2").toObject().value("image").toString(), two));
    QCOMPARE(windows.value("7"), before.value("windows").toObject().value("7")); // an unopened slot is retained
    for (const char* key : {"bindings", "appearance", "futureKey"}) QCOMPARE(root.value(key), before.value(key));
    // A preference change from one window does not drop another window's saved position.
    ViewerController third;
    third.setWindowSlot(3);
    third.setWheelMode("zoom");
    const auto after = settingsOf(config.path());
    QCOMPARE(after.value("wheel").toString(), QString("zoom"));
    QCOMPARE(after.value("windows"), root.value("windows"));
    QCOMPARE(after.value("futureKey"), before.value("futureKey"));
}

void WindowSlotTests::restoreOpensSavedImageOrFirstImageOfSavedFolder() {
    QTemporaryDir files, config; QVERIFY(files.isValid() && config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    const auto dir = make(files, "trip");
    writePng(dir, "b.png"); writePng(dir, "c.png");
    const auto saved = writePng(dir, "c.png");
    {
        ViewerController viewer;
        viewer.setWindowSlot(1);
        viewer.open(saved);
        waitOpen(viewer, saved);
        viewer.flushWindowPosition();
    }
    // The position is the image's path, not its index: new files sorting earlier must not shift the restore.
    writePng(dir, "0-new.png"); writePng(dir, "a.png");
    {
        ViewerController viewer;
        viewer.setWindowSlot(1);
        viewer.restoreWindow();
        waitOpen(viewer, saved);
        QCOMPARE(viewer.windowSlot(), 1);
    }
    // Saved image deleted: fall back to the first eligible image of the saved folder.
    bool removed = false;
    for (int attempt = 0; attempt < 50 && !removed; ++attempt) { removed = QFile::remove(saved); if (!removed) QTest::qWait(100); } // the decoder may still hold it under load
    QVERIFY(removed);
    {
        ViewerController viewer;
        viewer.setWindowSlot(1);
        viewer.restoreWindow();
        waitOpen(viewer, dir + "/0-new.png");
        viewer.flushWindowPosition();
    }
    const auto entry = settingsOf(config.path()).value("windows").toObject().value("1").toObject();
    QVERIFY(same(entry.value("image").toString(), dir + "/0-new.png"));
    // A slot without a saved position restores nothing and says nothing.
    ViewerController empty;
    empty.setWindowSlot(9);
    empty.restoreWindow();
    QTest::qWait(150);
    QVERIFY(empty.snapshot().current.empty());
    QVERIFY(!empty.status().contains("last folder"));
}

void WindowSlotTests::missingFolderIsReportedAndSavedLocationKept() {
    QTemporaryDir files, config; QVERIFY(files.isValid() && config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    const auto dir = make(files, "gone");
    const auto saved = writePng(dir, "x.png");
    {
        ViewerController viewer;
        viewer.setWindowSlot(2);
        viewer.open(saved);
        waitOpen(viewer, saved);
    }                                                 // destruction flushes the pending position
    const auto before = settingsOf(config.path()).value("windows").toObject().value("2");
    QVERIFY(!before.toObject().isEmpty());
    QVERIFY(QDir(dir).removeRecursively());
    {
        ViewerController viewer;
        viewer.setWindowSlot(2);
        viewer.restoreWindow();
        QTRY_VERIFY_WITH_TIMEOUT(viewer.status().contains("unavailable"), 20000);
        QVERIFY(viewer.snapshot().current.empty());
        viewer.flushWindowPosition();
    }
    QCOMPARE(settingsOf(config.path()).value("windows").toObject().value("2"), before); // never clobbered by a failed restore
    // An empty folder is reported differently and also keeps the slot.
    make(files, "gone");
    {
        ViewerController viewer;
        viewer.setWindowSlot(2);
        viewer.restoreWindow();
        QTRY_VERIFY_WITH_TIMEOUT(viewer.status().contains("no supported images"), 20000);
    }
    QCOMPARE(settingsOf(config.path()).value("windows").toObject().value("2"), before);
}

void WindowSlotTests::explicitOpenOverridesRestore() {
    QTemporaryDir files, config; QVERIFY(files.isValid() && config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    const auto remembered = writePng(make(files, "remembered"), "r.png");
    const auto explicitFile = writePng(make(files, "explicit"), "e.png");
    {
        ViewerController viewer;
        viewer.setWindowSlot(1);
        viewer.open(remembered);
        waitOpen(viewer, remembered);
    }
    ViewerController viewer;
    viewer.setWindowSlot(1);
    viewer.open(explicitFile);                        // an explicit file wins
    viewer.restoreWindow();                           // and a later restore must not replace it
    waitOpen(viewer, explicitFile);
    QTest::qWait(300);
    QVERIFY(same(pathOf(viewer), explicitFile));
    viewer.flushWindowPosition();
    QVERIFY(same(settingsOf(config.path()).value("windows").toObject().value("1").toObject().value("image").toString(), explicitFile));
    // A restore requested first and overtaken by an explicit open also loses.
    ViewerController racing;
    racing.setWindowSlot(1);
    racing.restoreWindow();
    racing.open(remembered);
    waitOpen(racing, remembered);
    QTest::qWait(300);
    QVERIFY(same(pathOf(racing), remembered));
}

void WindowSlotTests::positionFlushesOnDestructionAndDisabledSlotWritesNothing() {
    QTemporaryDir files, config; QVERIFY(files.isValid() && config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    const auto dir = make(files, "rapid");
    QStringList images;
    for (int i = 0; i < 4; ++i) images << writePng(dir, QString("p%1.png").arg(i));
    {
        ViewerController viewer;
        viewer.setWindowSlot(4);
        for (const auto& image : images) { viewer.open(image); waitOpen(viewer, image); }
    }                                                 // debounce still pending: the destructor writes the latest
    QVERIFY(same(settingsOf(config.path()).value("windows").toObject().value("4").toObject().value("image").toString(), images.last()));
    QTemporaryDir other; QVERIFY(other.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", other.path().toUtf8());
    {
        ViewerController viewer;                      // slot 0: position memory off
        viewer.open(images.first());
        waitOpen(viewer, images.first());
        viewer.flushWindowPosition();
    }
    QVERIFY(!settingsOf(other.path()).contains("windows"));
}

int main(int argc, char** argv) {
    if (argc >= 3 && QByteArray(argv[1]).startsWith("--") && QByteArray(argv[1]) != "--help") {
        QStringList args;
        for (int i = 0; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
        const QString mode = args.value(1);
        if (mode == "--slot-helper" || mode == "--crash-helper" || mode == "--settings-helper" || mode == "--settings-spin" || mode == "--lock-holder" || mode == "--reader-spin" || mode == "--foreign-reader") return helperMain(args);
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    WindowSlotTests tests;
    return QTest::qExec(&tests, argc, argv);
}
// ---- INPUT-02: scoped bindings, chords and the wheel matrix.
void WindowSlotTests::scopeAwareConflictsAndChordPrefixes() {
    QTemporaryDir config; QVERIFY(config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    ViewerController viewer;
    // Every registered command reports its scope, hold flag and key steps.
    QMap<QString, QVariantMap> byId;
    for (const auto& row : viewer.commands()) byId.insert(row.toMap().value("id").toString(), row.toMap());
    QCOMPARE(byId.value("next").value("scope").toString(), QString("canvas"));
    QCOMPARE(byId.value("open").value("scope").toString(), QString("global"));
    QCOMPARE(byId.value("grid-up").value("scope").toString(), QString("grid"));
    QCOMPARE(byId.value("tree-open-folder").value("scope").toString(), QString("tree"));
    QVERIFY(byId.value("hold-actual").value("hold").toBool());
    QCOMPARE(byId.value("hold-actual").value("binding").toString(), QString("Z"));
    QCOMPARE(byId.value("next").value("steps").toStringList(), QStringList{"Right"});
    QVERIFY(byId.value("grid-up").value("steps").toStringList().isEmpty());
    // Scopes that overlap conflict: same scope, and "global" with every scope.
    QCOMPARE(viewer.bindingConflict("rotate", "Right"), QString("next"));
    QCOMPARE(viewer.bindingConflict("hold-actual", "F"), QString("fit"));
    QCOMPARE(viewer.bindingConflict("grid-up", "Ctrl+O"), QString("open"));
    QCOMPARE(viewer.bindingConflict("tree-open-folder", "G"), QString("grid"));
    QCOMPARE(viewer.bindingConflict("rotate", "R"), QString()); // a command never conflicts with itself
    QCOMPARE(viewer.bindingConflict("rotate", ""), QString());
    // Scopes that are never active together may share a key.
    QCOMPARE(viewer.bindingConflict("grid-up", "Right"), QString());
    QCOMPARE(viewer.bindingConflict("tree-open-folder", "Right"), QString());
    QCOMPARE(viewer.bindingConflict("grid-select-all", "Left"), QString());
    QVERIFY(viewer.setBinding("grid-up", "Right"));
    QCOMPARE(viewer.bindingFor("grid-up"), QString("Right"));
    QCOMPARE(viewer.bindingFor("next"), QString("Right"));
    QCOMPARE(viewer.bindingConflict("grid-select-all", "Right"), QString("grid-up")); // same scope again
    QVERIFY(!viewer.setBinding("grid-select-all", "Right"));
    QCOMPARE(viewer.bindingConflict("zoom-in", "Right"), QString("next"));
    // Chords: a prefix collides only inside an overlapping scope.
    QCOMPARE(viewer.bindingConflict("rotate", "Right, D"), QString("next"));   // would swallow the single key
    QCOMPARE(viewer.bindingConflict("grid-open", "G, D"), QString("grid"));    // "G" is global
    QCOMPARE(viewer.bindingConflict("grid-open", "Left, D"), QString());       // "Left" is a canvas key; the grid never shares a pane
    QVERIFY(viewer.setBinding("grid-open", "Left, D"));
    QCOMPARE(viewer.bindingFor("grid-open"), QString("Left, D"));
    QCOMPARE(viewer.bindingConflict("rotate", "Ctrl+K, Ctrl+C"), QString());
    QVERIFY(viewer.setBinding("rotate", "Ctrl+K, Ctrl+C"));
    QCOMPARE(viewer.bindingConflict("fit", "Ctrl+K"), QString("rotate"));      // a single key inside an existing chord's prefix
    QCOMPARE(viewer.bindingConflict("fit", "Ctrl+K, Ctrl+C"), QString("rotate"));
    QVERIFY(!viewer.setBinding("fit", "Ctrl+K"));
    QStringList steps;
    for (const auto& row : viewer.commands()) if (row.toMap().value("id").toString() == "rotate") steps = row.toMap().value("steps").toStringList();
    QCOMPARE(steps, (QStringList{"Ctrl+K", "Ctrl+C"}));
    // A chord is at most two steps; Escape stays reserved; a hold needs one key to release.
    QVERIFY(!viewer.setBinding("background", "Ctrl+K, Ctrl+C, Ctrl+V"));
    QVERIFY(!viewer.setBinding("background", "Ctrl+J, Escape"));
    QVERIFY(!viewer.setBinding("hold-actual", "Ctrl+J, Ctrl+Z"));
    QVERIFY(viewer.setBinding("hold-actual", "Ctrl+Alt+Z"));
    // A rejected edit leaves the map as it was.
    QCOMPARE(viewer.bindingFor("background"), QString("B"));
    QCOMPARE(viewer.bindingFor("fit"), QString("F"));
}

void WindowSlotTests::scopedBindingsPersistAndReload() {
    QTemporaryDir config; QVERIFY(config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    { ViewerController viewer; QVERIFY(viewer.setBinding("grid-up", "Right")); QVERIFY(viewer.setBinding("rotate", "Ctrl+K, Ctrl+C")); }
    {   // A saved map that shares a key between scopes that never overlap is kept, not reset to defaults.
        ViewerController again;
        QCOMPARE(again.bindingFor("grid-up"), QString("Right"));
        QCOMPARE(again.bindingFor("next"), QString("Right"));
        QCOMPARE(again.bindingFor("rotate"), QString("Ctrl+K, Ctrl+C"));
        QVERIFY(!again.status().contains("conflict"));
    }
    // Two canvas commands on one key is a conflict: the whole map falls back to the defaults.
    QVERIFY(glancelot::updateSettings([](QJsonObject& root) { root["bindings"] = QJsonObject{{"fit", "Q"}, {"rotate", "Q"}}; }).ok());
    ViewerController broken;
    QCOMPARE(broken.bindingFor("fit"), QString("F"));
    QVERIFY(broken.status().contains("conflict"));
    // A default added later (hold-actual's Z) yields to a saved key that already uses it.
    QVERIFY(glancelot::updateSettings([](QJsonObject& root) { root["bindings"] = QJsonObject{{"rotate", "Z"}}; }).ok());
    ViewerController legacy;
    QCOMPARE(legacy.bindingFor("rotate"), QString("Z"));
    QCOMPARE(legacy.bindingFor("hold-actual"), QString());
}

void WindowSlotTests::wheelModifiersPersistFallBackIndividuallyAndMerge() {
    QTemporaryDir config; QVERIFY(config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    {
        ViewerController viewer;
        const auto defaults = viewer.wheelModifiers();
        QCOMPARE(defaults.value("none").toString(), QString("navigate"));   // today's behaviour
        QCOMPARE(defaults.value("ctrl").toString(), QString("zoom"));
        QCOMPARE(defaults.value("shift").toString(), QString("pan-horizontal"));
        QCOMPARE(defaults.value("alt").toString(), QString("navigate"));    // unset Alt follows the plain wheel
        QVERIFY(!viewer.setWheelModifier("ctrl", "spin"));
        QVERIFY(!viewer.setWheelModifier("meta", "zoom"));
        QVERIFY(!viewer.setWheelModifier("none", "pan-horizontal"));        // the plain wheel keeps its three values
        QCOMPARE(viewer.wheelModifiers().value("ctrl").toString(), QString("zoom"));
        QVERIFY(viewer.setWheelModifier("none", "zoom"));
        QCOMPARE(viewer.wheelMode(), QString("zoom"));
        QCOMPARE(viewer.wheelModifiers().value("alt").toString(), QString("zoom"));
        QVERIFY(viewer.setWheelModifier("alt", "pan"));
        QCOMPARE(viewer.wheelModifiers().value("alt").toString(), QString("pan"));
    }
    {   ViewerController reloaded;
        QCOMPARE(reloaded.wheelModifiers().value("none").toString(), QString("zoom"));
        QCOMPARE(reloaded.wheelModifiers().value("alt").toString(), QString("pan"));
        QCOMPARE(reloaded.wheelModifiers().value("ctrl").toString(), QString("zoom"));
    }
    // Invalid entries fall back one at a time; unknown keys survive an edit.
    QVERIFY(glancelot::updateSettings([](QJsonObject& root) {
        root["wheelModifiers"] = QJsonObject{{"ctrl", "bogus"}, {"shift", "navigate"}, {"alt", 5}, {"future", "kept"}};
    }).ok());
    ViewerController partial;
    QCOMPARE(partial.wheelModifiers().value("ctrl").toString(), QString("zoom"));
    QCOMPARE(partial.wheelModifiers().value("shift").toString(), QString("navigate"));
    QCOMPARE(partial.wheelModifiers().value("alt").toString(), partial.wheelMode());
    QVERIFY(partial.setWheelModifier("shift", "pan-horizontal"));
    auto stored = settingsOf(config.path()).value("wheelModifiers").toObject();
    QCOMPARE(stored.value("future").toString(), QString("kept"));
    QCOMPARE(stored.value("shift").toString(), QString("pan-horizontal"));
    // Two windows editing different modifiers do not revert each other.
    ViewerController first, second;
    QVERIFY(first.setWheelModifier("ctrl", "pan"));
    QVERIFY(second.setWheelModifier("alt", "navigate"));
    stored = settingsOf(config.path()).value("wheelModifiers").toObject();
    QCOMPARE(stored.value("ctrl").toString(), QString("pan"));
    QCOMPARE(stored.value("alt").toString(), QString("navigate"));
    QCOMPARE(stored.value("future").toString(), QString("kept"));
}
// F051: every refusal comes with a sentence; a shortcut that can be saved has none.
void WindowSlotTests::bindingProblemExplainsEveryRefusal() {
    QTemporaryDir config; QVERIFY(config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    ViewerController viewer;
    QCOMPARE(viewer.bindingProblem("rotate", ""), QString());                 // clearing is always fine
    QCOMPARE(viewer.bindingProblem("rotate", "Ctrl+Alt+J"), QString());
    QCOMPARE(viewer.bindingProblem("rotate", "Ctrl+K, Ctrl+C"), QString());
    QVERIFY(viewer.bindingProblem("rotate", "Escape").contains("Escape"));
    QVERIFY(viewer.bindingProblem("rotate", "Ctrl+K, Ctrl+C, Ctrl+V").contains("two"));
    QVERIFY(viewer.bindingProblem("hold-actual", "Ctrl+J, Ctrl+Z").contains("one key"));
    const QString taken = viewer.bindingProblem("rotate", "Right");           // belongs to "Next image"
    QVERIFY2(taken.contains("Already used by") && taken.contains("Next image"), qPrintable(taken));
    QVERIFY(viewer.bindingProblem("rotate", "G, D").contains("Toggle folder grid")); // chord prefix on a global key
    QCOMPARE(viewer.bindingProblem("no-such-command", "F9"), QString());
    QCOMPARE(viewer.keyStep(Qt::Key_J, Qt::ControlModifier), QString("Ctrl+J"));
    QCOMPARE(viewer.keyStep(Qt::Key_Right, Qt::NoModifier), QString("Right"));
}
// F053: both options default off, persist, and "Reset appearance" clears them.
void WindowSlotTests::accessibilityOptionsPersistAndReset() {
    QTemporaryDir config; QVERIFY(config.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    { ViewerController v; QVERIFY(!v.highContrast() && !v.largeTargets()); v.setHighContrast(true); v.setLargeTargets(true); }
    { ViewerController v; QVERIFY(v.highContrast() && v.largeTargets());
      const auto root = settingsOf(config.path());
      QVERIFY(root.value("highContrast").toBool() && root.value("largeTargets").toBool()); }
    QString error;
    QVERIFY(glancelot::resetSection("appearance", &error));
    ViewerController after; QVERIFY(!after.highContrast() && !after.largeTargets());
}
// F015 (shape A): a tab is a saved location; switching navigates; the set persists with the window and restores.
void WindowSlotTests::tabsKeepLocationsAndRestoreWithTheWindow() {
    QTemporaryDir config, media; QVERIFY(config.isValid() && media.isValid());
    qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    QVERIFY(QDir(media.path()).mkpath("alpha") && QDir(media.path()).mkpath("beta"));
    QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::darkGreen);
    const QString a = QDir(media.path()).filePath("alpha/a.png"), b = QDir(media.path()).filePath("beta/b.png");
    QVERIFY(image.save(a) && image.save(b));
    auto same = [](const QString& x, const QString& y) { return QDir::cleanPath(x).compare(QDir::cleanPath(y), Qt::CaseInsensitive) == 0; };
    {
        ViewerController v; v.setWindowSlot(6);
        QVERIFY(v.tabs().isEmpty()); QVERIFY(!v.closeTab(0)); QVERIFY(!v.activateTab(1)); // one implicit tab
        v.open(a); QTRY_VERIFY(v.imageReady());
        QVERIFY(v.newTab()); QCOMPARE(v.tabs().size(), 2); QCOMPARE(v.activeTab(), 1);
        v.open(b); QTRY_VERIFY(v.imageReady() && same(v.filePath(), b));
        QCOMPARE(v.tabs().at(1).toMap().value("title").toString(), QString("beta"));
        QCOMPARE(v.tabs().at(0).toMap().value("title").toString(), QString("alpha"));
        QVERIFY(v.activateTab(0)); QTRY_VERIFY(same(v.filePath(), a)); QCOMPARE(v.activeTab(), 0);
        QVERIFY(v.activateTab(1)); QTRY_VERIFY(same(v.filePath(), b));
        for (int i = 0; i < 6; ++i) QVERIFY(v.newTab()); QVERIFY(!v.newTab()); QCOMPARE(v.tabs().size(), 8); // at most eight
        while (v.tabs().size() > 2) QVERIFY(v.closeTab(v.tabs().size() - 1));
        v.flushWindowPosition();
    }
    {   ViewerController w; w.setWindowSlot(6); w.restoreWindow();
        QTRY_VERIFY(w.imageReady());
        QCOMPARE(w.tabs().size(), 2);
        QVERIFY(same(w.tabs().at(0).toMap().value("folder").toString(), QDir(media.path()).filePath("alpha")));
        QCOMPARE(w.activeTab(), w.tabs().at(1).toMap().value("active").toBool() ? 1 : 0);
        QVERIFY(w.closeTab(w.activeTab())); QVERIFY(w.tabs().isEmpty()); // closing down to one hides the strip
        QTRY_VERIFY(w.imageReady() || !w.directory().isEmpty());
    }
}
// F054: command names go through the Commands translation context, with the English text as the key.
void WindowSlotTests::commandNamesAreTranslatable() {
    struct Spanish : QTranslator {
        QString translate(const char* context, const char* source, const char*, int) const override {
            return QByteArray(context) == "Commands" && QByteArray(source) == "Next image" ? QStringLiteral("Imagen siguiente") : QString();
        }
        bool isEmpty() const override { return false; }
    } spanish;
    QTemporaryDir config; QVERIFY(config.isValid()); qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
    auto label = [](ViewerController& v, const QString& id) { for (const auto& r : v.commands()) if (r.toMap().value("id").toString() == id) return r.toMap().value("label").toString(); return QString(); };
    ViewerController v;
    QCOMPARE(label(v, "next"), QString("Next image"));
    QVERIFY(QCoreApplication::installTranslator(&spanish));
    QCOMPARE(label(v, "next"), QString("Imagen siguiente"));
    QCOMPARE(label(v, "previous"), QString("Previous image")); // an untranslated name falls back to English
    QCoreApplication::removeTranslator(&spanish);
    QCOMPARE(label(v, "next"), QString("Next image"));
}
#include "window_slot_tests.moc"

