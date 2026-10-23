// STACK-F015-RECOVERY: tabs persist through the production recovery session. The tab snapshot is written in the same
// transaction as the slot's folder/image and checkpoint revision, read back on the next claim, and restored only when
// SAFE mode, a reset or an explicit file do not suppress resume.
#include "glancelot/recovery_checkpoint.hpp"
#include "glancelot/settings_file.hpp"
#include "glancelot/viewer_controller.hpp"
#include "glancelot/window_slot.hpp"
#include "file_lock.hpp"
#include "recovery_session.hpp"
#include "settings_transaction.hpp"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace glancelot::recovery;

namespace {
#ifdef Q_OS_WIN
const QString root = "C:/generated";
#else
const QString root = "/generated";
#endif
QString at(const QString& name) { return root + "/" + name; }
TabLocation tab(const QString& folder, const QString& image = {}) { return {at(folder), image.isEmpty() ? QString{} : at(folder + "/" + image)}; }
QJsonObject readRoot(const QString& directory) {
    QFile file(QDir(directory).filePath("settings.json"));
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
}
QByteArray readBytes(const QString& directory) {
    QFile file(QDir(directory).filePath("settings.json"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool writeRoot(const QString& directory, const QJsonObject& value) {
    QFile file(QDir(directory).filePath("settings.json"));
    const auto bytes = QJsonDocument(value).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QJsonObject position(const QString& directory, int slot = 1) { return readRoot(directory).value("windows").toObject().value(QString::number(slot)).toObject(); }
QJsonObject record(const QString& directory, int slot = 1) {
    return readRoot(directory).value("recovery").toObject().value("windows").toObject().value(QString::number(slot)).toObject();
}
QJsonArray rows(const QList<TabLocation>& tabs) {
    QJsonArray result; for (const auto& t : tabs) result.append(QJsonObject{{"folder", t.folder}, {"image", t.image}}); return result;
}
CheckpointInput withTabs(const QList<TabLocation>& tabs, int active, quint64 sequence) {
    CheckpointInput input{tabs.value(active).folder, tabs.value(active).image, sequence};
    input.tabs = tabs; input.activeTab = active; return input;
}
struct Hooks {
    const glancelot::settings_detail::TransactionHooks* old = glancelot::settings_detail::transactionHooks;
    explicit Hooks(const glancelot::settings_detail::TransactionHooks& hooks) { glancelot::settings_detail::transactionHooks = &hooks; }
    ~Hooks() { glancelot::settings_detail::transactionHooks = old; }
};
bool same(const QString& a, const QString& b) { return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0; }
} // namespace

class TabsRecoveryTests final : public QObject {
    Q_OBJECT
    // Generated media: alpha/a.png, beta/b.png, gamma/c.png.
    struct Media {
        QTemporaryDir dir;
        Media() {
            for (const auto* name : {"alpha/a.png", "beta/b.png", "gamma/c.png"}) {
                QDir(dir.path()).mkpath(QFileInfo(dir.filePath(name)).path());
                QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::darkCyan); image.save(dir.filePath(name));
            }
        }
        QString file(const char* name) const { return dir.filePath(name); }
    };
    // A first run with a claimed slot and three tabs (active: beta), closed normally.
    static bool firstRun(const QString& config, const Media& media) {
        WindowSlot slot(config, 0); if (slot.number() != 1) return false;
        ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity());
        viewer.open(media.file("alpha/a.png"));
        if (!QTest::qWaitFor([&] { return viewer.imageReady(); }, 10000)) return false;
        if (!viewer.newTab()) return false;
        viewer.open(media.file("beta/b.png"));
        if (!QTest::qWaitFor([&] { return viewer.imageReady() && same(viewer.filePath(), media.file("beta/b.png")); }, 10000)) return false;
        if (!viewer.newTab()) return false;
        viewer.open(media.file("gamma/c.png"));
        if (!QTest::qWaitFor([&] { return viewer.imageReady() && same(viewer.filePath(), media.file("gamma/c.png")); }, 10000)) return false;
        if (!viewer.activateTab(1)) return false;
        if (!QTest::qWaitFor([&] { return viewer.imageReady() && same(viewer.filePath(), media.file("beta/b.png")); }, 10000)) return false;
        return QTest::qWaitFor([&] { return position(config).value("tabs").toArray().size() == 3 && position(config).value("activeTab") == QJsonValue(1); }, 5000);
    }

private slots:
    void init() { qunsetenv("GLANCELOT_CONFIG_DIR"); }

    // ---- checkpoint and session level

    void tabsShareTheCheckpointRevisionAndReturnOnTheNextClaim() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QList<TabLocation> tabs{tab("one", "a.png"), tab("two"), tab("three", "c.png")};
        QString revision;
        {
            WindowSlot slot(dir.path(), 0); QCOMPARE(beginWindow(slot.claimIdentity()).status.code, Code::Committed);
            const auto saved = checkpoint(slot.claimIdentity(), withTabs(tabs, 1, 4)); QCOMPARE(saved.status.code, Code::Committed);
            revision = saved.checkpointRevision;
            QCOMPARE(position(dir.path()).value("tabs"), QJsonValue(rows(tabs)));
            QCOMPARE(position(dir.path()).value("activeTab"), QJsonValue(1));
            QCOMPARE(position(dir.path()).value("folder"), QJsonValue(at("two")));
            QCOMPARE(record(dir.path()).value("checkpointRevision"), QJsonValue(revision));
            QCOMPARE(record(dir.path()).value("sequence"), QJsonValue("4"));
        } // dirty exit: no finishWindow
        WindowSlot next(dir.path(), 0); QCOMPARE(next.number(), 1);
        const auto begun = beginWindow(next.claimIdentity()); QCOMPARE(begun.status.code, Code::Committed);
        QVERIFY(begun.previous.priorDirty); QCOMPARE(begun.previous.checkpointRevision, revision);
        QCOMPARE(begun.previous.tabs, tabs); QCOMPARE(begun.previous.activeTab, 1);
        QCOMPARE(begun.previous.folder, at("two"));
        // One location again: the stored tab set is removed in the same write.
        const auto single = checkpoint(next.claimIdentity(), {at("two"), {}, 1}); QCOMPARE(single.status.code, Code::Committed);
        QVERIFY(!position(dir.path()).contains("tabs")); QVERIFY(!position(dir.path()).contains("activeTab"));
        QCOMPARE(finishWindow(next.claimIdentity(), single.checkpointRevision, 1).code, Code::Committed);
    }

    void legacyEntryWithoutTabsIsOneLocation() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(writeRoot(dir.path(), {{"schema", 1}, {"windows", QJsonObject{{"1", QJsonObject{{"folder", at("old")}, {"image", at("old/x.png")}, {"saved", 1}, {"future", true}}}}}}));
        WindowSlot slot(dir.path(), 0); const auto begun = beginWindow(slot.claimIdentity());
        QCOMPARE(begun.status.code, Code::Committed); QVERIFY(begun.previous.hasPosition);
        QCOMPARE(begun.previous.image, at("old/x.png")); QVERIFY(begun.previous.tabs.isEmpty()); QCOMPARE(begun.previous.activeTab, 0);
        // Existing call sites keep their meaning: a three-field input saves one location and keeps unknown fields.
        QCOMPARE(checkpoint(slot.claimIdentity(), {at("old"), at("old/x.png"), 1}).status.code, Code::Committed);
        QVERIFY(!position(dir.path()).contains("tabs")); QCOMPARE(position(dir.path()).value("future"), QJsonValue(true));
    }

    void malformedOrOversizedStoredTabsReadAsOneLocation_data() {
        QTest::addColumn<QJsonValue>("tabs");
        QTest::addColumn<QJsonValue>("active");
        const QJsonArray two = rows({tab("a"), tab("b", "b.png")});
        QJsonArray nine; for (int i = 0; i < 9; ++i) nine.append(QJsonObject{{"folder", at(QString::number(i))}, {"image", ""}});
        nine[1] = QJsonObject{{"folder", at("b")}, {"image", at("b/b.png")}};
        QJsonArray relative = two; relative[0] = QJsonObject{{"folder", "relative"}, {"image", ""}};
        QJsonArray nul = two; nul[0] = QJsonObject{{"folder", at("a") + QChar(0)}, {"image", ""}};
        QJsonArray huge = two; huge[0] = QJsonObject{{"folder", at(QString(32768, 'x'))}, {"image", ""}};
        QJsonArray notObject = two; notObject[0] = 7;
        QJsonArray missing = two; missing[0] = QJsonObject{{"folder", at("a")}};
        QTest::newRow("not an array") << QJsonValue(QJsonObject{}) << QJsonValue(1);
        QTest::newRow("one record") << QJsonValue(QJsonArray{two[1]}) << QJsonValue(0);
        QTest::newRow("oversized") << QJsonValue(nine) << QJsonValue(1);
        QTest::newRow("active out of range") << QJsonValue(two) << QJsonValue(2);
        QTest::newRow("active negative") << QJsonValue(two) << QJsonValue(-1);
        QTest::newRow("active fraction") << QJsonValue(two) << QJsonValue(1.5);
        QTest::newRow("active text") << QJsonValue(two) << QJsonValue("1");
        QTest::newRow("active missing") << QJsonValue(two) << QJsonValue(QJsonValue::Undefined);
        QTest::newRow("active disagrees with position") << QJsonValue(two) << QJsonValue(0);
        QTest::newRow("relative path") << QJsonValue(relative) << QJsonValue(1);
        QTest::newRow("NUL in path") << QJsonValue(nul) << QJsonValue(1);
        QTest::newRow("path too long") << QJsonValue(huge) << QJsonValue(1);
        QTest::newRow("record not an object") << QJsonValue(notObject) << QJsonValue(1);
        QTest::newRow("record missing image") << QJsonValue(missing) << QJsonValue(1);
    }
    void malformedOrOversizedStoredTabsReadAsOneLocation() {
        QFETCH(QJsonValue, tabs); QFETCH(QJsonValue, active);
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QJsonObject entry{{"folder", at("b")}, {"image", at("b/b.png")}, {"saved", 1}, {"tabs", tabs}};
        if (!active.isUndefined()) entry["activeTab"] = active;
        QVERIFY(writeRoot(dir.path(), {{"schema", 1}, {"windows", QJsonObject{{"1", entry}}}}));
        WindowSlot slot(dir.path(), 0);
        const auto begun = beginWindow(slot.claimIdentity());
        QCOMPARE(begun.status.code, Code::Committed);   // bad tab state never blocks recovery of the position
        QVERIFY(begun.previous.hasPosition); QCOMPARE(begun.previous.image, at("b/b.png"));
        QVERIFY(begun.previous.tabs.isEmpty()); QCOMPARE(begun.previous.activeTab, 0);
        // The read-only fallback validates the same way.
        ResumeState fallback{true, false, at("b"), at("b/b.png"), {}};
        readTabs(entry, fallback); QVERIFY(fallback.tabs.isEmpty());
        // The next checkpoint replaces the bad state.
        const QList<TabLocation> fresh{tab("b", "b.png"), tab("c")};
        QCOMPARE(checkpoint(slot.claimIdentity(), withTabs(fresh, 0, 1)).status.code, Code::Committed);
        QCOMPARE(position(dir.path()).value("tabs"), QJsonValue(rows(fresh)));
    }

    void invalidTabInputIsRefusedWithoutWriting() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); WindowSlot slot(dir.path(), 0);
        QCOMPARE(beginWindow(slot.claimIdentity()).status.code, Code::Committed);
        const auto before = readBytes(dir.path());
        QList<TabLocation> nine; for (int i = 0; i < 9; ++i) nine.append(tab(QString::number(i)));
        QCOMPARE(checkpoint(slot.claimIdentity(), withTabs(nine, 0, 1)).status.code, Code::Corrupt);
        const QList<TabLocation> two{tab("a"), tab("b")};
        auto outOfRange = withTabs(two, 0, 1); outOfRange.activeTab = 2;
        QCOMPARE(checkpoint(slot.claimIdentity(), outOfRange).status.code, Code::Corrupt);
        auto negative = withTabs(two, 0, 1); negative.activeTab = -1;
        QCOMPARE(checkpoint(slot.claimIdentity(), negative).status.code, Code::Corrupt);
        auto disagrees = withTabs(two, 0, 1); disagrees.folder = at("elsewhere");
        QCOMPARE(checkpoint(slot.claimIdentity(), disagrees).status.code, Code::Corrupt);
        QCOMPARE(checkpoint(slot.claimIdentity(), withTabs({tab("a"), TabLocation{"relative", {}}}, 0, 1)).status.code, Code::Corrupt);
        QCOMPARE(checkpoint(slot.claimIdentity(), withTabs({tab("a"), TabLocation{at(QString(32768, 'x')), {}}}, 0, 1)).status.code, Code::Corrupt);
        QCOMPARE(readBytes(dir.path()), before);
        // Eight is the bound, not an error.
        QList<TabLocation> eight = nine.mid(0, MaxTabs);
        QCOMPARE(checkpoint(slot.claimIdentity(), withTabs(eight, 7, 1)).status.code, Code::Committed);
        QCOMPARE(position(dir.path()).value("tabs").toArray().size(), qsizetype(MaxTabs));
    }

    void staleSequenceCannotReplaceNewerTabs() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); WindowSlot slot(dir.path(), 0);
        QCOMPARE(beginWindow(slot.claimIdentity()).status.code, Code::Committed);
        const QList<TabLocation> newer{tab("a"), tab("b"), tab("c")}, older{tab("x"), tab("y")};
        const auto saved = checkpoint(slot.claimIdentity(), withTabs(newer, 2, 5)); QCOMPARE(saved.status.code, Code::Committed);
        const auto bytes = readBytes(dir.path());
        QCOMPARE(checkpoint(slot.claimIdentity(), withTabs(older, 0, 4)).status.code, Code::Conflict);
        QCOMPARE(checkpoint(slot.claimIdentity(), withTabs(older, 0, 5)).status.code, Code::Conflict);
        QCOMPARE(readBytes(dir.path()), bytes);
        QCOMPARE(finishWindow(slot.claimIdentity(), saved.checkpointRevision, 4).code, Code::Conflict);
        QCOMPARE(finishWindow(slot.claimIdentity(), saved.checkpointRevision, 5).code, Code::Committed);
        QCOMPARE(position(dir.path()).value("tabs"), QJsonValue(rows(newer)));
    }

    void otherSlotsAndUnknownFieldsArePreserved() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QJsonObject other{{"folder", at("two")}, {"image", ""}, {"saved", 3}, {"tabs", rows({tab("two"), tab("other")})}, {"activeTab", 0}, {"future", 1}};
        QVERIFY(writeRoot(dir.path(), {{"schema", 1}, {"future", 9}, {"windows", QJsonObject{{"1", QJsonObject{{"future", true}}}, {"2", other}}}}));
        WindowSlot slot(dir.path(), 0); QCOMPARE(slot.number(), 1);
        QCOMPARE(beginWindow(slot.claimIdentity()).status.code, Code::Committed);
        const QList<TabLocation> tabs{tab("one"), tab("three", "c.png")};
        const auto saved = checkpoint(slot.claimIdentity(), withTabs(tabs, 1, 1)); QCOMPARE(saved.status.code, Code::Committed);
        QCOMPARE(finishWindow(slot.claimIdentity(), saved.checkpointRevision, 1).code, Code::Committed);
        const auto stored = readRoot(dir.path());
        QCOMPARE(stored.value("future"), QJsonValue(9));
        QCOMPARE(stored.value("windows").toObject().value("2"), QJsonValue(other));
        QCOMPARE(position(dir.path()).value("future"), QJsonValue(true));
        QCOMPARE(position(dir.path()).value("tabs"), QJsonValue(rows(tabs)));
    }

    void failedTabCheckpointNeverCleansAndRetrySucceeds() {
        const auto failing = std::make_shared<std::atomic_bool>(false);
        glancelot::settings_detail::TransactionHooks hooks;
        hooks.fail = [failing](glancelot::settings_detail::Stage stage) { return failing->load() && stage == glancelot::settings_detail::Stage::BeforeReplace; };
        QTemporaryDir dir; QVERIFY(dir.isValid()); WindowSlot slot(dir.path(), 0); Hooks installed(hooks);
        const QList<TabLocation> tabs{tab("a"), tab("b")};
        {
            Session session(slot.claimIdentity()); QVERIFY(session.waitForIdle(3000)); QVERIFY(session.snapshot().begun);
            const auto original = readBytes(dir.path());
            *failing = true; QVERIFY(session.accept(withTabs(tabs, 1, 1))); QVERIFY(session.waitForIdle(3000));
            QCOMPARE(session.snapshot().lastAttempt.status.code, Code::WriteFailed);
            QVERIFY(!session.close(3000));   // the newest tab snapshot never persisted: not clean
            QCOMPARE(session.snapshot().finalStatus.code, Code::WriteFailed);
            QCOMPARE(readBytes(dir.path()), original);
            QCOMPARE(record(dir.path()).value("state"), QJsonValue("dirty"));
        }
        // Retry under the same claim with a newer sequence: the tab snapshot persists and the session cleans.
        *failing = false;
        Session retry(slot.claimIdentity()); QVERIFY(retry.waitForIdle(3000)); QVERIFY(retry.snapshot().begun);
        QVERIFY(retry.accept(withTabs(tabs, 1, 2))); QVERIFY(retry.waitForIdle(3000));
        QCOMPARE(retry.snapshot().lastAttempt.status.code, Code::Committed);
        QVERIFY(retry.close(3000));
        QCOMPARE(position(dir.path()).value("tabs"), QJsonValue(rows(tabs)));
        QCOMPARE(record(dir.path()).value("state"), QJsonValue("clean"));
    }

    void busyWriterIsRetriedAndLatestPendingTabsWin() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); WindowSlot slot(dir.path(), 0);
        Session session(slot.claimIdentity()); QVERIFY(session.waitForIdle(3000)); QVERIFY(session.snapshot().begun);
        std::mutex mutex; std::condition_variable changed; bool ready = false; std::atomic_bool acquired = false;
        std::jthread contender([&] {
            FileLock held(dir.filePath("settings.json.lock")); acquired = held.acquire(0);
            { std::lock_guard lock(mutex); ready = true; changed.notify_all(); }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        });
        { std::unique_lock lock(mutex); QVERIFY(changed.wait_for(lock, std::chrono::seconds(3), [&] { return ready; })); }
        QVERIFY(acquired.load());
        // While the writer is busy, newer tab snapshots replace the pending one; only the latest is written.
        QVERIFY(session.accept(withTabs({tab("a"), tab("b")}, 0, 1)));
        QVERIFY(session.accept(withTabs({tab("a"), tab("b"), tab("c")}, 2, 2)));
        const QList<TabLocation> latest{tab("a"), tab("b"), tab("c"), tab("d", "d.png")};
        QVERIFY(session.accept(withTabs(latest, 3, 3)));
        QVERIFY(!session.accept(withTabs({tab("z"), tab("y")}, 0, 3)));   // stale sequence refused at the session
        QVERIFY(session.close(3000));
        const auto state = session.snapshot(); QCOMPARE(state.finalStatus.code, Code::Committed);
        QCOMPARE(state.persistedSequence, quint64(3));
        QCOMPARE(position(dir.path()).value("tabs"), QJsonValue(rows(latest)));
        QCOMPARE(position(dir.path()).value("activeTab"), QJsonValue(3));
        QCOMPARE(record(dir.path()).value("state"), QJsonValue("clean"));
    }

    void failedBeginOffersReadOnlyTabsWithoutWriting() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QList<TabLocation> tabs{tab("a"), tab("b", "b.png")};
        QVERIFY(writeRoot(dir.path(), {{"schema", 1}, {"windows", QJsonObject{{"1", QJsonObject{{"folder", at("b")}, {"image", at("b/b.png")},
            {"saved", 1}, {"tabs", rows(tabs)}, {"activeTab", 1}}}}}}));
        const auto before = readBytes(dir.path());
        glancelot::settings_detail::TransactionHooks hooks;
        hooks.fail = [](glancelot::settings_detail::Stage stage) { return stage == glancelot::settings_detail::Stage::BeforeReplace; };
        WindowSlot slot(dir.path(), 0); Hooks installed(hooks);
        Session session(slot.claimIdentity()); QVERIFY(session.waitForIdle(5000));
        const auto state = session.snapshot();
        QVERIFY(state.beginFinished); QVERIFY(!state.begun); QCOMPARE(state.beginning.status.code, Code::WriteFailed);
        QVERIFY(state.readOnlyResume.has_value());
        QCOMPARE(state.readOnlyResume->tabs, tabs); QCOMPARE(state.readOnlyResume->activeTab, 1);
        QVERIFY(!session.accept(withTabs(tabs, 1, 1)) || !session.snapshot().begun);   // never authorizes writes
        QVERIFY(!session.close(3000));
        QCOMPARE(readBytes(dir.path()), before);
    }

    // ---- controller level: the production recovery session

    void restartRestoresTabsThroughTheRecoverySession() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(firstRun(config.path(), media));
        QCOMPARE(record(config.path()).value("state"), QJsonValue("clean"));
        const auto stored = position(config.path()).value("tabs").toArray();
        QVERIFY(same(stored.at(1).toObject().value("image").toString(), position(config.path()).value("image").toString()));
        WindowSlot slot(config.path(), 0); QCOMPARE(slot.number(), 1);
        ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity()); viewer.restoreWindow();
        QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady() && same(viewer.filePath(), media.file("beta/b.png")), 10000);
        QCOMPARE(viewer.tabs().size(), 3); QCOMPARE(viewer.activeTab(), 1);
        QVERIFY(same(viewer.tabs().at(0).toMap().value("image").toString(), media.file("alpha/a.png")));
        QVERIFY(same(viewer.tabs().at(2).toMap().value("image").toString(), media.file("gamma/c.png")));
        // Switching restores the other tab's location.
        QVERIFY(viewer.activateTab(2));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady() && same(viewer.filePath(), media.file("gamma/c.png")), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(position(config.path()).value("activeTab"), QJsonValue(2), 5000);
    }

    void tabChangeWithoutNavigationIsSavedBeforeTheSessionCleans() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        {
            WindowSlot slot(config.path(), 0); ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity());
            viewer.open(media.file("alpha/a.png")); QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady(), 10000);
            QVERIFY(viewer.newTab());   // no navigation; the close below must still save it before cleaning
        }
        QCOMPARE(record(config.path()).value("state"), QJsonValue("clean"));
        QCOMPARE(position(config.path()).value("tabs").toArray().size(), qsizetype(2));
        QCOMPARE(position(config.path()).value("activeTab"), QJsonValue(1));
    }

    void safeModeDoesNotRestoreTabs() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(firstRun(config.path(), media));
        const auto stored = position(config.path());
        {
            WindowSlot slot(config.path(), 0); ViewerController viewer; viewer.setSafeMode(true);
            viewer.setWindowClaim(slot.claimIdentity()); viewer.restoreWindow();
            QTest::qWait(300);
            QVERIFY(viewer.tabs().isEmpty()); QVERIFY(viewer.filePath().isEmpty()); QVERIFY(!viewer.imageReady());
        }
        QCOMPARE(position(config.path()).value("tabs"), stored.value("tabs"));   // left for a normal start
        QCOMPARE(position(config.path()).value("activeTab"), stored.value("activeTab"));
    }

    void resetSettingsDoesNotRestoreTabs() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(firstRun(config.path(), media));
        QString moved; QVERIFY(glancelot::resetSettings(&moved)); QVERIFY(!moved.isEmpty());
        WindowSlot slot(config.path(), 0); ViewerController viewer;
        viewer.setWindowClaim(slot.claimIdentity()); viewer.restoreWindow();
        QTest::qWait(300);
        QVERIFY(viewer.tabs().isEmpty()); QVERIFY(viewer.filePath().isEmpty());
    }

    void explicitFileWinsOverSavedTabs() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(firstRun(config.path(), media));
        WindowSlot slot(config.path(), 0); ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity());
        viewer.open(media.file("gamma/c.png")); viewer.restoreWindow();
        QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady() && same(viewer.filePath(), media.file("gamma/c.png")), 10000);
        QTest::qWait(200);
        QVERIFY(viewer.tabs().isEmpty());
    }
};

QTEST_MAIN(TabsRecoveryTests)
#include "tabs_recovery_tests.moc"
