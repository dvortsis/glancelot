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
#include <QtTest>
#include <QScopeGuard>
#include <array>
#include <iostream>

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
            if (!status.ok()) return 10 + int(status.code);
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
};

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
    for (int i = 0; i < 150; ++i) {
        const auto status = glancelot::updateSettings([&](QJsonObject& root) { root["n"] = i; });
        if (!status.ok()) { ++failures; lastCode = int(status.code); }
    }
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
    // Oversized or unparseable bookkeeping is disposable and restarts empty.
    QVERIFY(writeFile(path, "{ nope"));
    { WindowSlot slot(dir.path()); QCOMPARE(slot.number(), 1); }
    QVERIFY(writeFile(path, QByteArray(70 * 1024, '{')));
    { WindowSlot slot(dir.path()); QCOMPARE(slot.number(), 1); }
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
    QVERIFY(QFile::remove(saved));
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
#include "window_slot_tests.moc"
