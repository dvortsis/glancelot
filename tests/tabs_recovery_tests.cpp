// STACK-F015-RECOVERY: tabs persist through the production recovery session. The tab snapshot is written in the same
// transaction as the slot's folder/image and checkpoint revision, read back on the next claim, and restored only when
// SAFE mode, a reset or an explicit file do not suppress resume.
#include "glancelot/recovery_checkpoint.hpp"
#include "glancelot/settings_file.hpp"
#include "glancelot/viewer_controller.hpp"
#include "glancelot/window_slot.hpp"
#include "file_lock.hpp"
#include "recovery_session.hpp"
#include "recovery_tabs.hpp"
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
#include <optional>
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
// Empty when slot 1's stored entry holds exactly these tab images (each in its own folder) with `active` as the active
// tab and as the slot's folder/image; one image means one location without tabs. Otherwise a description of the mismatch.
QString storedTabsDiffer(const QString& config, const QStringList& images, int active) {
    const auto entry = position(config);
    const auto where = [](const QJsonValue& row) { return row.toObject().value("image").toString() + " in " + row.toObject().value("folder").toString(); };
    const auto actual = [&] {
        QStringList text; for (const auto& row : entry.value("tabs").toArray()) text.append(where(row));
        return QString("stored tabs [%1] active %2, position %3").arg(text.join(", ")).arg(entry.value("activeTab").toVariant().toString(), where(entry));
    };
    if (!same(entry.value("image").toString(), images.value(active)) || !same(entry.value("folder").toString(), QFileInfo(images.value(active)).absolutePath()))
        return "wrong position: " + actual();
    if (images.size() < 2) return entry.contains("tabs") || entry.contains("activeTab") ? "unexpected tabs: " + actual() : QString{};
    const auto stored = entry.value("tabs").toArray();
    if (stored.size() != images.size() || entry.value("activeTab") != QJsonValue(active)) return "wrong tab set: " + actual();
    for (int i = 0; i < images.size(); ++i)
        if (!same(stored.at(i).toObject().value("image").toString(), images.at(i)) ||
            !same(stored.at(i).toObject().value("folder").toString(), QFileInfo(images.at(i)).absolutePath()))
            return QString("tab %1 holds the wrong location: ").arg(i) + actual();
    return {};
}
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
    static QStringList images(const Media& media) { return {media.file("alpha/a.png"), media.file("beta/b.png"), media.file("gamma/c.png")}; }
    static bool shows(const ViewerController& viewer, const QString& image) { return viewer.imageReady() && same(viewer.filePath(), image); }
    // Opens alpha, beta and gamma in three tabs and settles on `active` (claimed or not; nothing waits for storage).
    static bool showThreeTabs(ViewerController& viewer, const Media& media, int active) {
        const auto files = images(media);
        viewer.open(files[0]);
        if (!QTest::qWaitFor([&] { return shows(viewer, files[0]); }, 10000)) return false;
        for (int i = 1; i < 3; ++i) {
            if (!viewer.newTab()) return false;
            viewer.open(files[i]);
            if (!QTest::qWaitFor([&] { return shows(viewer, files[i]); }, 10000)) return false;
        }
        return active == 2 || (viewer.activateTab(active) && QTest::qWaitFor([&] { return shows(viewer, files[active]); }, 10000));
    }
    // Opens alpha, beta and gamma in three tabs of a claimed viewer, settles on `active` and waits until that is saved.
    static bool openThreeTabs(const QString& config, ViewerController& viewer, const Media& media, int active) {
        return showThreeTabs(viewer, media, active) &&
               QTest::qWaitFor([&] { return storedTabsDiffer(config, images(media), active).isEmpty(); }, 5000);
    }
    // Empty when the live tab strip holds exactly these images (each in its own folder) with `active` selected.
    static QString liveTabsDiffer(const ViewerController& viewer, const QStringList& images, int active) {
        const auto rows = viewer.tabs();
        QStringList text; for (const auto& row : rows) text.append(row.toMap().value("image").toString());
        const auto actual = QString("live tabs [%1] active %2").arg(text.join(", ")).arg(viewer.activeTab());
        if (images.size() < 2) return rows.isEmpty() ? QString{} : "unexpected live tabs: " + actual;
        if (rows.size() != images.size() || viewer.activeTab() != active) return "wrong live tab set: " + actual;
        for (int i = 0; i < images.size(); ++i)
            if (!same(rows.at(i).toMap().value("image").toString(), images.at(i)) ||
                !same(rows.at(i).toMap().value("folder").toString(), QFileInfo(images.at(i)).absolutePath()))
                return QString("live tab %1 holds the wrong location: ").arg(i) + actual;
        return {};
    }
    static QStringList pick(const QStringList& files, const QList<int>& indexes) {
        QStringList result; for (const int i : indexes) result.append(files.at(i)); return result;
    }
    // Round 3: the active tab's folder is unavailable. Its row says so and nothing of the previous tab is shown.
    static QString unavailableDiffers(const ViewerController& viewer) {
        const auto row = viewer.tabs().value(viewer.activeTab()).toMap();
        if (!row.value("unavailable").toBool()) return "the active tab is not marked unavailable";
        if (viewer.imageReady() || viewer.loading() || !viewer.filePath().isEmpty() || !viewer.directory().isEmpty())
            return QString("previous content still shown: file \"%1\", folder \"%2\", ready %3").arg(viewer.filePath(), viewer.directory()).arg(viewer.imageReady());
        return {};
    }
    // A first run with a claimed slot and three tabs (active: beta), closed normally.
    static bool firstRun(const QString& config, const Media& media) {
        WindowSlot slot(config, 0); if (slot.number() != 1) return false;
        ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity());
        return openThreeTabs(config, viewer, media, 1);
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

    // Tab state, folder/image and the recovery record's revision and sequence are one replace of settings.json: a failure
    // before the replace changes none of them, and the replace that lands carries all of them.
    void tabsAndPositionLandInOneTransaction() {
        using glancelot::settings_detail::Stage;
        QTemporaryDir dir; QVERIFY(dir.isValid()); WindowSlot slot(dir.path(), 0);
        QCOMPARE(beginWindow(slot.claimIdentity()).status.code, Code::Committed);
        const QList<TabLocation> first{tab("a", "a.png"), tab("b")}, second{tab("c"), tab("d", "d.png"), tab("e", "e.png")};
        const auto earlier = checkpoint(slot.claimIdentity(), withTabs(first, 0, 1)); QCOMPARE(earlier.status.code, Code::Committed);
        const auto before = readBytes(dir.path());
        {
            glancelot::settings_detail::TransactionHooks hooks;
            hooks.fail = [](Stage stage) { return stage == Stage::BeforeReplace; };
            Hooks installed(hooks);
            QCOMPARE(checkpoint(slot.claimIdentity(), withTabs(second, 2, 2)).status.code, Code::WriteFailed);
        }
        QCOMPARE(readBytes(dir.path()), before);   // no torn state: tabs, position and revision all unchanged
        int replaces = 0; QJsonObject beforeReplace, afterReplace;
        glancelot::settings_detail::TransactionHooks hooks;
        hooks.observe = [&](Stage stage) {
            if (stage == Stage::BeforeReplace) beforeReplace = readRoot(dir.path());
            if (stage == Stage::AfterReplace) { ++replaces; afterReplace = readRoot(dir.path()); }
        };
        CheckpointResult saved;
        { Hooks installed(hooks); saved = checkpoint(slot.claimIdentity(), withTabs(second, 2, 2)); }
        QCOMPARE(saved.status.code, Code::Committed); QCOMPARE(replaces, 1);
        const auto slotOf = [](const QJsonObject& root) { return root.value("windows").toObject().value("1").toObject(); };
        const auto recordOf = [](const QJsonObject& root) { return root.value("recovery").toObject().value("windows").toObject().value("1").toObject(); };
        // Just before the one replace: still entirely the earlier checkpoint.
        QCOMPARE(slotOf(beforeReplace).value("tabs"), QJsonValue(rows(first)));
        QCOMPARE(slotOf(beforeReplace).value("activeTab"), QJsonValue(0));
        QCOMPARE(slotOf(beforeReplace).value("image"), QJsonValue(first[0].image));
        QCOMPARE(recordOf(beforeReplace).value("checkpointRevision"), QJsonValue(earlier.checkpointRevision));
        QCOMPARE(recordOf(beforeReplace).value("sequence"), QJsonValue("1"));
        // Right after it: entirely the new one, with the revision the checkpoint returned.
        QCOMPARE(slotOf(afterReplace).value("tabs"), QJsonValue(rows(second)));
        QCOMPARE(slotOf(afterReplace).value("activeTab"), QJsonValue(2));
        QCOMPARE(slotOf(afterReplace).value("folder"), QJsonValue(second[2].folder));
        QCOMPARE(slotOf(afterReplace).value("image"), QJsonValue(second[2].image));
        QCOMPARE(recordOf(afterReplace).value("checkpointRevision"), QJsonValue(saved.checkpointRevision));
        QCOMPARE(recordOf(afterReplace).value("sequence"), QJsonValue("2"));
        QCOMPARE(readRoot(dir.path()), afterReplace);
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
        // Every settings replace the session's worker makes, as the recovery sequence it left on disk.
        struct Written { std::mutex mutex; QStringList sequences; };
        const auto written = std::make_shared<Written>();
        glancelot::settings_detail::TransactionHooks hooks;
        hooks.observe = [written, path = dir.path()](glancelot::settings_detail::Stage stage) {
            if (stage != glancelot::settings_detail::Stage::AfterReplace) return;
            const auto sequence = record(path).value("sequence").toString();
            std::lock_guard lock(written->mutex); written->sequences.append(sequence.isEmpty() ? "none" : sequence);
        };
        Hooks installed(hooks);   // copied into the session's worker at construction
        Session session(slot.claimIdentity()); QVERIFY(session.waitForIdle(3000)); QVERIFY(session.snapshot().begun);
        std::mutex mutex; std::condition_variable changed; bool ready = false; std::atomic_bool acquired = false;
        std::jthread contender([&] {
            FileLock held(dir.filePath("settings.json.lock")); acquired = held.acquire(0);
            { std::lock_guard lock(mutex); ready = true; changed.notify_all(); }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        });
        { std::unique_lock lock(mutex); QVERIFY(changed.wait_for(lock, std::chrono::seconds(3), [&] { return ready; })); }
        QVERIFY(acquired.load());
        // While the writer is busy, newer tab snapshots replace the pending one; only the latest is written. The pauses let
        // the worker start retrying a snapshot before the next one supersedes it, well inside the 250 ms the lock is held.
        QVERIFY(session.accept(withTabs({tab("a"), tab("b")}, 0, 1)));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        QVERIFY(session.accept(withTabs({tab("a"), tab("b"), tab("c")}, 2, 2)));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const QList<TabLocation> latest{tab("a"), tab("b"), tab("c"), tab("d", "d.png")};
        QVERIFY(session.accept(withTabs(latest, 3, 3)));
        QVERIFY(!session.accept(withTabs({tab("z"), tab("y")}, 0, 3)));   // stale sequence refused at the session
        QVERIFY(session.close(3000));
        const auto state = session.snapshot(); QCOMPARE(state.finalStatus.code, Code::Committed);
        QCOMPARE(state.persistedSequence, quint64(3));
        QCOMPARE(position(dir.path()).value("tabs"), QJsonValue(rows(latest)));
        QCOMPARE(position(dir.path()).value("activeTab"), QJsonValue(3));
        QCOMPARE(record(dir.path()).value("state"), QJsonValue("clean"));
        // The two intermediate snapshots were superseded while the writer was busy and never reached disk.
        std::lock_guard lock(written->mutex);
        QVERIFY2(!written->sequences.contains("1") && !written->sequences.contains("2"), qPrintable(written->sequences.join(", ")));
        QVERIFY2(written->sequences.contains("3"), qPrintable(written->sequences.join(", ")));
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
        // A checkpoint offered after the failed begin never authorizes a write: the worker only retries the begin.
        session.accept(withTabs(tabs, 1, 1)); QVERIFY(session.waitForIdle(5000));
        const auto after = session.snapshot();
        QVERIFY(!after.begun); QCOMPARE(after.persistedSequence, quint64(0));
        QVERIFY(after.lastAttempt.status.code != Code::Committed); QVERIFY(after.lastAttempt.checkpointRevision.isEmpty());
        QCOMPARE(readBytes(dir.path()), before);
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

    // Review round 1 (High): a checkpoint taken while a tab switch is still decoding records every tab's own location and
    // the selected tab, so the final close never marks a wrong record clean.
    void closingDuringATabSwitchKeepsEveryTabsLocation() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const auto files = images(media);
        {
            WindowSlot slot(config.path(), 0); QCOMPARE(slot.number(), 1);
            ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity());
            QVERIFY(openThreeTabs(config.path(), viewer, media, 2));
            QVERIFY(viewer.activateTab(0));
            QVERIFY(!viewer.imageReady());   // destroyed below while alpha is still decoding
        }
        const auto differ = storedTabsDiffer(config.path(), files, 0); QVERIFY2(differ.isEmpty(), qPrintable(differ));
        QCOMPARE(record(config.path()).value("state"), QJsonValue("clean"));
    }

    void closingTheActiveTabBeforeItsNeighbourDecodesKeepsEveryTabsLocation_data() {
        QTest::addColumn<int>("active");
        QTest::addColumn<QList<int>>("remaining");   // indexes into alpha, beta, gamma
        QTest::addColumn<int>("selected");
        QTest::newRow("middle of three") << 1 << QList<int>{0, 2} << 1;
        QTest::newRow("last of three") << 2 << QList<int>{0, 1} << 1;
        QTest::newRow("first of three") << 0 << QList<int>{1, 2} << 0;
    }
    void closingTheActiveTabBeforeItsNeighbourDecodesKeepsEveryTabsLocation() {
        QFETCH(int, active); QFETCH(QList<int>, remaining); QFETCH(int, selected);
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const auto files = images(media);
        {
            WindowSlot slot(config.path(), 0); ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity());
            QVERIFY(openThreeTabs(config.path(), viewer, media, active));
            QVERIFY(viewer.closeTab(active)); QCOMPARE(viewer.activeTab(), selected);
            QVERIFY(!viewer.imageReady());
        }
        QStringList expected; for (const int i : remaining) expected.append(files[i]);
        const auto differ = storedTabsDiffer(config.path(), expected, selected); QVERIFY2(differ.isEmpty(), qPrintable(differ));
        QCOMPARE(record(config.path()).value("state"), QJsonValue("clean"));
    }

    // Closing the active one of two tabs leaves one location: the remaining tab's, never the closed one's.
    void closingTheActiveOfTwoTabsBeforeTheOtherDecodesKeepsTheOtherLocation() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const auto files = images(media);
        {
            WindowSlot slot(config.path(), 0); ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity());
            QVERIFY(openThreeTabs(config.path(), viewer, media, 2));
            QVERIFY(viewer.closeTab(0));   // inactive: no switch
            QTRY_VERIFY_WITH_TIMEOUT(storedTabsDiffer(config.path(), {files[1], files[2]}, 1).isEmpty(), 5000);
            QVERIFY(viewer.closeTab(1)); QVERIFY(viewer.tabs().isEmpty());
            QVERIFY(!viewer.imageReady());
        }
        const auto differ = storedTabsDiffer(config.path(), {files[1]}, 0); QVERIFY2(differ.isEmpty(), qPrintable(differ));
        QCOMPARE(record(config.path()).value("state"), QJsonValue("clean"));
    }

    // The target tab's image cannot be decoded: the switch never settles, and the close still saves each tab's location.
    void switchingToAnUndecodableTabKeepsEveryTabsLocation() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const QString broken = media.file("broken/x.png");
        QVERIFY(QDir(media.dir.path()).mkpath("broken"));
        { QFile file(broken); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("generated: not a PNG"); }
        const QStringList files{media.file("alpha/a.png"), broken, media.file("gamma/c.png")};
        {
            WindowSlot slot(config.path(), 0); ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity());
            viewer.open(files[0]); QTRY_VERIFY_WITH_TIMEOUT(shows(viewer, files[0]), 10000);
            QVERIFY(viewer.newTab()); viewer.open(broken);
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && same(viewer.filePath(), broken), 10000); QVERIFY(!viewer.imageReady());
            QVERIFY(viewer.newTab()); viewer.open(files[2]); QTRY_VERIFY_WITH_TIMEOUT(shows(viewer, files[2]), 10000);
            QTRY_VERIFY_WITH_TIMEOUT(storedTabsDiffer(config.path(), files, 2).isEmpty(), 5000);
            QVERIFY(viewer.activateTab(1));
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && same(viewer.filePath(), broken), 10000);
            QVERIFY(!viewer.imageReady());   // decode failed: notePosition never ran for the target
            QCOMPARE(viewer.activeTab(), 1);
        }
        const auto differ = storedTabsDiffer(config.path(), files, 1); QVERIFY2(differ.isEmpty(), qPrintable(differ));
        QCOMPARE(record(config.path()).value("state"), QJsonValue("clean"));
    }

    // Review round 2 (B1): a decode of the previous tab's image, still in flight when a switch or close falls back to the
    // target's folder (its image is gone), lands after the switch. It must not settle the switch: the target tab keeps its
    // own location in memory and on disk, and the record marked clean is the correct one. The target folder is gone too,
    // so nothing later corrects a wrong settle.
    void staleDecodeCannotSettleADirectorySwitch_data() {
        QTest::addColumn<bool>("close");
        QTest::addColumn<QList<int>>("expected");   // indexes into alpha, beta, gamma
        QTest::addColumn<int>("active");
        QTest::newRow("switch") << false << QList<int>{0, 1, 0} << 1;
        QTest::newRow("close the active tab") << true << QList<int>{0, 1} << 1;
    }
    void staleDecodeCannotSettleADirectorySwitch() {
        QFETCH(bool, close); QFETCH(QList<int>, expected); QFETCH(int, active);
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const auto files = images(media);
        {
            WindowSlot slot(config.path(), 0); QCOMPARE(slot.number(), 1);
            ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity());
            QVERIFY(openThreeTabs(config.path(), viewer, media, 2));
            QVERIFY(QDir(media.dir.filePath("beta")).removeRecursively());
            viewer.open(files[0]); QVERIFY(viewer.loading());   // tab 2 moves to alpha; its decode is in flight
            QVERIFY(close ? viewer.closeTab(2) : viewer.activateTab(1));   // beta's image is gone: the folder route
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !viewer.directoryPending(), 10000);
            // Round 3 (lead decision b): the failed folder route retires the stale decode and shows the tab as unavailable
            // instead of the previous tab's photo (round 2 asserted that photo was on screen).
            const auto state = unavailableDiffers(viewer); QVERIFY2(state.isEmpty(), qPrintable(state));
            const auto live = liveTabsDiffer(viewer, pick(files, expected), active); QVERIFY2(live.isEmpty(), qPrintable(live));
        }
        const auto differ = storedTabsDiffer(config.path(), pick(files, expected), active); QVERIFY2(differ.isEmpty(), qPrintable(differ));
        QCOMPARE(record(config.path()).value("state"), QJsonValue("clean"));
    }

    // Review round 2: a second tab action before the target settles uses the target's stored location for the active
    // tab, not live state that may still be the previous tab's (the folder route keeps the old image until it lands).
    void secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation_data() {
        QTest::addColumn<bool>("stale");     // tab 2 moves to alpha and is still decoding when the switch starts
        QTest::addColumn<QString>("action");
        QTest::addColumn<QList<int>>("expected");   // indexes into alpha, beta, gamma
        QTest::addColumn<int>("active");
        QTest::newRow("new tab") << false << "new" << QList<int>{0, 1, 1, 2} << 2;
        QTest::newRow("switch again") << false << "switch" << QList<int>{0, 1, 2} << 0;
        QTest::newRow("close another tab") << false << "close" << QList<int>{1, 2} << 0;
        QTest::newRow("stale decode, new tab") << true << "new" << QList<int>{0, 1, 1, 0} << 2;
    }
    void secondTabActionBeforeTheTargetSettlesKeepsEveryTabsLocation() {
        QFETCH(bool, stale); QFETCH(QString, action); QFETCH(QList<int>, expected); QFETCH(int, active);
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const auto files = images(media);
        {
            WindowSlot slot(config.path(), 0); QCOMPARE(slot.number(), 1);
            ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity());
            QVERIFY(openThreeTabs(config.path(), viewer, media, 2));
            QVERIFY(QDir(media.dir.filePath("beta")).removeRecursively());
            if (stale) { viewer.open(files[0]); QVERIFY(viewer.loading()); }
            QVERIFY(viewer.activateTab(1));   // folder route; the second action follows before anything lands
            QVERIFY(viewer.directoryPending());
            if (action == "new") QVERIFY(viewer.newTab());
            else if (action == "switch") QVERIFY(viewer.activateTab(0));
            else QVERIFY(viewer.closeTab(0));
            const auto early = liveTabsDiffer(viewer, pick(files, expected), active); QVERIFY2(early.isEmpty(), qPrintable(early));
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !viewer.directoryPending(), 10000);
            if (action == "switch") QVERIFY2(shows(viewer, files[0]), qPrintable(viewer.status()));
            else { const auto state = unavailableDiffers(viewer); QVERIFY2(state.isEmpty(), qPrintable(state)); }   // round 3 (b)
            const auto live = liveTabsDiffer(viewer, pick(files, expected), active); QVERIFY2(live.isEmpty(), qPrintable(live));
        }
        const auto differ = storedTabsDiffer(config.path(), pick(files, expected), active); QVERIFY2(differ.isEmpty(), qPrintable(differ));
        QCOMPARE(record(config.path()).value("state"), QJsonValue("clean"));
    }

    // Review round 2 (N1): app/main.cpp retries a busy startup claim every 4 s and then calls setWindowClaim() and
    // restoreWindow() on a window that may already have tabs. That claim replays the last decoded image as the position;
    // in the middle of a switch it must keep the target's stored location instead.
    void claimRetryDuringATabSwitchKeepsEveryTabsLocation() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const auto files = images(media);
        {
            std::optional<WindowSlot> slot;   // outlives the viewer, as in app/main.cpp
            ViewerController viewer;          // unclaimed: the startup claim was busy
            QVERIFY(showThreeTabs(viewer, media, 2));
            QVERIFY(viewer.activateTab(0)); QVERIFY(!viewer.imageReady());   // alpha opened, not decoded yet
            slot.emplace(config.path(), 0); QCOMPARE(slot->number(), 1);
            viewer.setWindowClaim(slot->claimIdentity()); viewer.restoreWindow();   // the retry at app/main.cpp:211
            QVERIFY(!viewer.imageReady());
            const auto live = liveTabsDiffer(viewer, files, 0); QVERIFY2(live.isEmpty(), qPrintable(live));
        }
        const auto differ = storedTabsDiffer(config.path(), files, 0); QVERIFY2(differ.isEmpty(), qPrintable(differ));
        QCOMPARE(record(config.path()).value("state"), QJsonValue("clean"));
    }

    // Review round 3 (B2): after a switch whose folder is unavailable, nothing that reopens, refreshes or reconciles the
    // previous tab's content may settle the target tab. Lead decision: a tab's location changes only from an open in its
    // own folder (or explicit navigation in that tab); while the folder is unavailable the view shows that state, not the
    // previous tab's photo, and F5 / Retry retries the target.
    void reopenAfterAFailedFolderSwitchKeepsTheTarget_data() {
        QTest::addColumn<QString>("trigger");
        QTest::addColumn<bool>("stale");
        QTest::newRow("F5 refresh") << "refresh" << false;
        QTest::newRow("external edit") << "edit" << false;
        QTest::newRow("external delete") << "delete" << false;
        QTest::newRow("F5 after stale decode") << "refresh" << true;
    }
    void reopenAfterAFailedFolderSwitchKeepsTheTarget() {
        QFETCH(QString, trigger); QFETCH(bool, stale);
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const auto files = images(media);
        const auto expected = pick(files, stale ? QList<int>{0, 1, 0} : QList<int>{0, 1, 2});
        {
            WindowSlot slot(config.path(), 0); QCOMPARE(slot.number(), 1);
            ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity());
            QVERIFY(openThreeTabs(config.path(), viewer, media, 2));
            QVERIFY(QDir(media.dir.filePath("beta")).removeRecursively());
            if (stale) { viewer.open(files[0]); QVERIFY(viewer.loading()); }
            QVERIFY(viewer.activateTab(1));   // folder route; beta's folder is gone
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !viewer.directoryPending(), 10000);
            // Precondition: the switch failed and every tab still holds its own location, live and stored.
            const auto before = liveTabsDiffer(viewer, expected, 1); QVERIFY2(before.isEmpty(), qPrintable(before));
            QTRY_VERIFY_WITH_TIMEOUT(storedTabsDiffer(config.path(), expected, 1).isEmpty(), 5000);
            if (trigger == "edit") {
                QImage changed(48, 32, QImage::Format_RGB32); changed.fill(Qt::darkRed); QVERIFY(changed.save(files[2]));
                QCOMPARE(QImage(files[2]).width(), 48);   // gamma's image really changed on disk
            } else if (trigger == "delete") {
                QVERIFY(QFile::remove(files[2])); QVERIFY(!QFileInfo::exists(files[2]));
            } else viewer.trigger("refresh");
            QTest::qWait(4000);   // the folder watcher's settle time, as the reviewer's probe waited
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !viewer.directoryPending(), 10000);
            const auto live = liveTabsDiffer(viewer, expected, 1); QVERIFY2(live.isEmpty(), qPrintable(live));
            const auto state = unavailableDiffers(viewer); QVERIFY2(state.isEmpty(), qPrintable(state));
        }
        const auto differ = storedTabsDiffer(config.path(), expected, 1); QVERIFY2(differ.isEmpty(), qPrintable(differ));
        QCOMPARE(record(config.path()).value("state"), QJsonValue("clean"));
    }

    // An automatic move while the switch's folder route is still pending (following a rename of the previous tab's photo,
    // or its removal by the delete flow) replaces that route. It never settles the target, and the route runs again, so
    // the tab still ends unavailable rather than showing the previous tab's content.
    void automaticMoveDuringAPendingSwitchNeverSettlesIt_data() {
        QTest::addColumn<QString>("move");
        QTest::newRow("rename followed") << "rename";
        QTest::newRow("removal followed") << "remove";
    }
    void automaticMoveDuringAPendingSwitchNeverSettlesIt() {
        QFETCH(QString, move);
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const auto files = images(media);
        {
            WindowSlot slot(config.path(), 0); QCOMPARE(slot.number(), 1);
            ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity());
            QVERIFY(openThreeTabs(config.path(), viewer, media, 2));
            QVERIFY(QDir(media.dir.filePath("beta")).removeRecursively());
            QVERIFY(viewer.activateTab(1)); QVERIFY(viewer.directoryPending());   // folder route still pending
            if (move == "rename") {
                const QString renamed = media.file("gamma/c2.png");
                QVERIFY(QFile::rename(files[2], renamed));
                viewer.reconcileRenamedPath(files[2], renamed);
            } else {
                QVERIFY(QFile::remove(files[2]));
                viewer.reconcileRemovedPaths({files[2]});
            }
            QTest::qWait(1000);
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !viewer.directoryPending(), 10000);
            const auto live = liveTabsDiffer(viewer, files, 1); QVERIFY2(live.isEmpty(), qPrintable(live));
            const auto state = unavailableDiffers(viewer); QVERIFY2(state.isEmpty(), qPrintable(state));
        }
        const auto differ = storedTabsDiffer(config.path(), files, 1); QVERIFY2(differ.isEmpty(), qPrintable(differ));
        QCOMPARE(record(config.path()).value("state"), QJsonValue("clean"));
    }

    // Retry (F5, or the placeholder's Retry button, which runs the same command) reopens the target once its folder is back.
    void retryReopensTheTargetWhenItsFolderReturns() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const auto files = images(media);
        {
            WindowSlot slot(config.path(), 0); QCOMPARE(slot.number(), 1);
            ViewerController viewer; viewer.setWindowClaim(slot.claimIdentity());
            QVERIFY(openThreeTabs(config.path(), viewer, media, 2));
            const QString away = media.dir.filePath("beta-offline");
            QVERIFY(QDir(media.dir.path()).rename("beta", "beta-offline"));   // the share goes offline
            QVERIFY(viewer.activateTab(1));
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !viewer.directoryPending(), 10000);
            const auto before = liveTabsDiffer(viewer, files, 1); QVERIFY2(before.isEmpty(), qPrintable(before));
            QVERIFY(QDir(media.dir.path()).rename("beta-offline", "beta"));   // and comes back
            QVERIFY(!QFileInfo::exists(away) && QFileInfo::exists(files[1]));
            viewer.trigger("refresh");   // Retry
            QTRY_VERIFY2_WITH_TIMEOUT(shows(viewer, files[1]), qPrintable(viewer.status() + " showing " + viewer.filePath()), 10000);
            const auto live = liveTabsDiffer(viewer, files, 1); QVERIFY2(live.isEmpty(), qPrintable(live));
            QVERIFY(!viewer.tabs().value(1).toMap().value("unavailable").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(storedTabsDiffer(config.path(), files, 1).isEmpty(), 5000);
        }
        const auto differ = storedTabsDiffer(config.path(), files, 1); QVERIFY2(differ.isEmpty(), qPrintable(differ));
        QCOMPARE(record(config.path()).value("state"), QJsonValue("clean"));
    }

    // Retry while the folder is still unavailable keeps the unavailable state and every location. The window starts
    // unclaimed, so the busy-claim retry at app/main.cpp:211 (setWindowClaim, then restoreWindow) runs while the tab is
    // unavailable: it must neither restore the stored set over the live one nor save the previous tab's location.
    void retryWhileStillUnavailableKeepsEveryTabsLocation() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const auto files = images(media);
        {
            std::optional<WindowSlot> slot;   // outlives the viewer, as in app/main.cpp
            ViewerController viewer;
            QVERIFY(showThreeTabs(viewer, media, 2));
            QVERIFY(QDir(media.dir.filePath("beta")).removeRecursively());
            QVERIFY(viewer.activateTab(1));
            QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !viewer.directoryPending(), 10000);
            slot.emplace(config.path(), 0); QCOMPARE(slot->number(), 1);
            viewer.setWindowClaim(slot->claimIdentity()); viewer.restoreWindow();   // the retry at app/main.cpp:211
            for (int attempt = 0; attempt < 2; ++attempt) {
                viewer.trigger("refresh");   // Retry, folder still gone
                QTest::qWait(300);
                QTRY_VERIFY_WITH_TIMEOUT(!viewer.loading() && !viewer.directoryPending(), 10000);
                const auto live = liveTabsDiffer(viewer, files, 1); QVERIFY2(live.isEmpty(), qPrintable(live));
                const auto state = unavailableDiffers(viewer); QVERIFY2(state.isEmpty(), qPrintable(state));
            }
            QTRY_VERIFY_WITH_TIMEOUT(storedTabsDiffer(config.path(), files, 1).isEmpty(), 5000);
        }
        const auto differ = storedTabsDiffer(config.path(), files, 1); QVERIFY2(differ.isEmpty(), qPrintable(differ));
        QCOMPARE(record(config.path()).value("state"), QJsonValue("clean"));
    }

    // Lead decision (round 2, N3): production always claims a slot, so tab state is written only by the recovery
    // checkpoint. The no-claim position writer (setWindowSlot without a claim, kept for window_slots) writes no tabs and
    // drops a stale stored set rather than leaving it next to a position it did not write.
    void noClaimWriterNeverWritesTabs() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const QList<TabLocation> old{tab("a"), tab("b", "b.png")};
        QVERIFY(writeRoot(config.path(), {{"schema", 1}, {"windows", QJsonObject{{"1", QJsonObject{{"folder", at("b")}, {"image", at("b/b.png")},
            {"saved", 1}, {"tabs", rows(old)}, {"activeTab", 1}}}}}}));
        const auto files = images(media);
        {
            ViewerController viewer; viewer.setWindowSlot(1);
            viewer.open(files[0]); QTRY_VERIFY_WITH_TIMEOUT(shows(viewer, files[0]), 10000);
            QVERIFY(viewer.newTab()); viewer.open(files[1]); QTRY_VERIFY_WITH_TIMEOUT(shows(viewer, files[1]), 10000);
            QCOMPARE(viewer.tabs().size(), 2);
            viewer.flushWindowPosition();
            QVERIFY(same(position(config.path()).value("image").toString(), files[1]));   // the position itself is saved
            QVERIFY2(!position(config.path()).contains("tabs") && !position(config.path()).contains("activeTab"),
                     qPrintable(QJsonDocument(position(config.path())).toJson(QJsonDocument::Compact)));
        }
        QVERIFY(!position(config.path()).contains("tabs")); QVERIFY(!position(config.path()).contains("activeTab"));
        QVERIFY(!readRoot(config.path()).contains("recovery"));   // and no recovery record without a claim
    }

    // Lead decision: the legacy no-claim restore stays for legacy reads, but validates exactly as the claimed path does.
    void legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath_data() { malformedOrOversizedStoredTabsReadAsOneLocation_data(); }
    void legacyNoClaimRestoreValidatesTabsLikeTheClaimedPath() {
        QFETCH(QJsonValue, tabs); QFETCH(QJsonValue, active);
        QTemporaryDir config; QVERIFY(config.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QJsonObject entry{{"folder", at("b")}, {"image", at("b/b.png")}, {"saved", 1}, {"tabs", tabs}};
        if (!active.isUndefined()) entry["activeTab"] = active;
        QVERIFY(writeRoot(config.path(), {{"schema", 1}, {"windows", QJsonObject{{"1", entry}}}}));
        ViewerController viewer; viewer.setWindowSlot(1); viewer.restoreWindow();
        QVERIFY(viewer.tabs().isEmpty()); QCOMPARE(viewer.activeTab(), 0);   // one location, as the claimed path reads it
    }
    void legacyNoClaimRestoreKeepsValidTabs() {
        QTemporaryDir config; QVERIFY(config.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const QList<TabLocation> stored{tab("a"), tab("b", "b.png"), tab("c")};
        QVERIFY(writeRoot(config.path(), {{"schema", 1}, {"windows", QJsonObject{{"1", QJsonObject{{"folder", at("b")}, {"image", at("b/b.png")},
            {"saved", 1}, {"tabs", rows(stored)}, {"activeTab", 1}}}}}}));
        ViewerController viewer; viewer.setWindowSlot(1); viewer.restoreWindow();
        QCOMPARE(viewer.tabs().size(), 3); QCOMPARE(viewer.activeTab(), 1);
        QCOMPARE(viewer.tabs().at(0).toMap().value("folder").toString(), at("a"));
        QCOMPARE(viewer.tabs().at(2).toMap().value("folder").toString(), at("c"));
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
        // Tabs are stored, so a reset that kept them, or a restore that ignored the reset, would bring them back.
        QCOMPARE(position(config.path()).value("tabs").toArray().size(), qsizetype(3));
        QString moved; QVERIFY(glancelot::resetSettings(&moved)); QVERIFY(!moved.isEmpty());
        {
            QFile aside(moved); QVERIFY(aside.open(QIODevice::ReadOnly));   // set aside with the tabs, never deleted
            const auto old = QJsonDocument::fromJson(aside.readAll()).object().value("windows").toObject().value("1").toObject();
            QCOMPARE(old.value("tabs").toArray().size(), qsizetype(3));
        }
        WindowSlot slot(config.path(), 0); QCOMPARE(slot.number(), 1); ViewerController viewer;
        viewer.setWindowClaim(slot.claimIdentity()); viewer.restoreWindow();
        // Wait for the claim's begin (its dirty marker) so the restore has really run before checking it restored nothing.
        QTRY_COMPARE_WITH_TIMEOUT(record(config.path()).value("state"), QJsonValue("dirty"), 5000);
        QTest::qWait(300);
        QVERIFY(viewer.tabs().isEmpty()); QVERIFY(viewer.filePath().isEmpty());
        QVERIFY(!position(config.path()).contains("tabs"));
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
