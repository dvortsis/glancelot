// B9 (F057): the in-app restore of a workspace recovered after an unexpected exit, through the production recovery
// engine and controller. A crash is a claim whose last checkpoint was never finished (no finishWindow), exactly as the
// engine leaves a killed process. Generated media only: <temp>/alpha/a.png, beta/b.png, gamma/c.png, delta/d.png.
#include "glancelot/recovery_checkpoint.hpp"
#include "glancelot/settings_file.hpp"
#include "glancelot/viewer_controller.hpp"
#include "glancelot/window_slot.hpp"
#include "restore_offer.hpp"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QUuid>
#include <QtTest>
#include <memory>

using namespace glancelot::recovery;
using glancelot::RestoreOffer;

namespace {
bool same(const QString& a, const QString& b) { return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0; }
QJsonObject readRoot(const QString& config) {
    QFile file(QDir(config).filePath("settings.json"));
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
}
bool writeRoot(const QString& config, const QJsonObject& root) {
    QFile file(QDir(config).filePath("settings.json"));
    const auto bytes = QJsonDocument(root).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readBytes(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool writeBytes(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QJsonObject position(const QString& config) { return readRoot(config).value("windows").toObject().value("1").toObject(); }
QJsonObject record(const QString& config) { return readRoot(config).value("recovery").toObject().value("windows").toObject().value("1").toObject(); }

struct Media {
    QTemporaryDir dir;
    Media() {
        for (const auto* name : {"alpha/a.png", "beta/b.png", "gamma/c.png", "delta/d.png"}) {
            QDir(dir.path()).mkpath(QFileInfo(dir.filePath(name)).path());
            QImage image(48, 32, QImage::Format_RGB32); image.fill(Qt::darkCyan); image.save(dir.filePath(name));
        }
    }
    QString file(const char* name) const { return QDir::cleanPath(dir.filePath(name)); }
    QString folder(const char* name) const { return QDir::cleanPath(dir.filePath(name)); }
    TabLocation at(const char* folderName, const char* image = nullptr) const {
        return {folder(folderName), image ? file((QString(folderName) + "/" + image).toUtf8().constData()) : QString{}};
    }
};

// The previous window ended without a clean close: its claim began, checkpointed `tabs` (one location when a single
// entry) and was never finished. Returns the checkpoint revision, the recovery point.
QString crash(const QString& config, const QList<TabLocation>& tabs, int active) {
    WindowSlot slot(config, 0);
    if (beginWindow(slot.claimIdentity()).status.code != Code::Committed) return {};
    CheckpointInput input{tabs.at(active).folder, tabs.at(active).image, 1};
    if (tabs.size() > 1) { input.tabs = tabs; input.activeTab = active; }
    const auto saved = checkpoint(slot.claimIdentity(), input);
    return saved.status.code == Code::Committed ? saved.checkpointRevision : QString{};
}
// The same, followed by a clean close.
QString cleanExit(const QString& config, const TabLocation& location) {
    WindowSlot slot(config, 0);
    if (beginWindow(slot.claimIdentity()).status.code != Code::Committed) return {};
    const auto saved = checkpoint(slot.claimIdentity(), {location.folder, location.image, 1});
    if (saved.status.code != Code::Committed) return {};
    return finishWindow(slot.claimIdentity(), saved.checkpointRevision, 1).code == Code::Committed ? saved.checkpointRevision : QString{};
}

// One window start: claims the next slot, like app/main.cpp, and exposes the controller's offer.
struct Start {
    WindowSlot slot;
    ViewerController viewer;
    RestoreOffer* offer;
    explicit Start(const QString& config, bool safe = false) : slot(config, 0), offer(qobject_cast<RestoreOffer*>(viewer.workspaceRestore())) {
        viewer.setSafeMode(safe);
        viewer.setWindowClaim(slot.claimIdentity());
    }
    // The claim's begin has reached the offer and its probe has answered.
    bool settled() const { return offer->generation() > 0 && !offer->checking(); }
    bool shows(const QString& image) const { return viewer.imageReady() && same(viewer.filePath(), image); }
    bool idle() const { return !viewer.navigating() && !viewer.loading() && !viewer.directoryPending(); }
};
QStringList tabFolders(const ViewerController& viewer) {
    QStringList folders; for (const auto& row : viewer.tabs()) folders.append(QDir::cleanPath(row.toMap().value("folder").toString())); return folders;
}
ResumeState dirty(const QList<TabLocation>& tabs, int active, const QString& revision) {
    ResumeState state; state.priorDirty = true; state.hasPosition = true; state.checkpointRevision = revision;
    state.folder = tabs.at(active).folder; state.image = tabs.at(active).image;
    if (tabs.size() > 1) { state.tabs = tabs; state.activeTab = active; }
    return state;
}
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
} // namespace

class RestoreOfferTests final : public QObject {
    Q_OBJECT
private slots:
    void init() { qunsetenv("GLANCELOT_CONFIG_DIR"); }

    // An explicit open wins: nothing replaces it, and the recovered tabs are offered instead of lost.
    void explicitOpenAfterCrashIsOfferedAndRestoreAddsTabsBesideIt() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const QList<TabLocation> tabs{media.at("alpha", "a.png"), media.at("beta"), media.at("gamma", "c.png")};
        QVERIFY(!crash(config.path(), tabs, 1).isEmpty());
        Start start(config.path());
        start.viewer.open(media.file("delta/d.png")); // the command-line file
        QTRY_VERIFY_WITH_TIMEOUT(start.shows(media.file("delta/d.png")), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(start.offer->state(), QString("offer"), 10000);
        QVERIFY(start.shows(media.file("delta/d.png"))); // not replaced while offered
        QCOMPARE(start.offer->locationCount(), 3);
        QCOMPARE(start.offer->unavailableCount(), 0);
        QCOMPARE(start.offer->folderName(), QString("beta"));

        QVERIFY(start.offer->accept());
        QCOMPARE(start.offer->state(), QString("restoring"));
        QCOMPARE(tabFolders(start.viewer), (QStringList{media.folder("delta"), media.folder("alpha"), media.folder("beta"), media.folder("gamma")}));
        QCOMPARE(start.viewer.activeTab(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(start.offer->state(), QString("hidden"), 10000);
        QVERIFY(start.offer->feedback().contains("3 tabs"));
        QVERIFY(same(start.viewer.directory(), media.folder("beta")));
        // The explicit tab keeps its photo; the restored tabs keep their saved locations until they are switched to.
        const auto rows = start.viewer.tabs();
        QVERIFY(same(rows.at(0).toMap().value("image").toString(), media.file("delta/d.png")));
        QVERIFY(same(rows.at(1).toMap().value("image").toString(), media.file("alpha/a.png")));
        QVERIFY(same(rows.at(3).toMap().value("image").toString(), media.file("gamma/c.png")));
        start.viewer.flushWindowPosition();
        QTRY_COMPARE_WITH_TIMEOUT(position(config.path()).value("tabs").toArray().size(), qsizetype(4), 10000);
        QVERIFY(!start.offer->accept()); // one restore per recovery point
    }

    void cleanExitIsNeverOffered() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(!cleanExit(config.path(), media.at("alpha", "a.png")).isEmpty());
        Start start(config.path());
        start.viewer.open(media.file("delta/d.png"));
        QTRY_VERIFY_WITH_TIMEOUT(start.settled() && start.shows(media.file("delta/d.png")), 10000);
        QCOMPARE(start.offer->state(), QString("hidden"));
    }

    // Without an explicit open the engine's lazy resume runs (SAFE-MODE-01, verify-recovery.py), so nothing is offered.
    void crashWithoutExplicitOpenResumesAndIsNotOffered() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(!crash(config.path(), {media.at("alpha", "a.png"), media.at("beta", "b.png")}, 0).isEmpty());
        Start start(config.path());
        start.viewer.restoreWindow();
        QTRY_VERIFY_WITH_TIMEOUT(start.shows(media.file("alpha/a.png")) && start.settled(), 10000);
        QCOMPARE(start.viewer.tabs().size(), qsizetype(2));
        QCOMPARE(start.offer->state(), QString("hidden"));
    }

    void explicitOpenOfTheRecoveredPhotoIsNotOffered() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(!crash(config.path(), {media.at("alpha", "a.png")}, 0).isEmpty());
        Start start(config.path());
        start.viewer.open(media.file("alpha/a.png"));
        QTRY_VERIFY_WITH_TIMEOUT(start.settled() && start.shows(media.file("alpha/a.png")), 10000);
        QCOMPARE(start.offer->state(), QString("hidden"));
    }

    // Dismissing is remembered for that recovery point across starts, and only for it.
    void dismissalIsRememberedForThatRecoveryPoint() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        const auto revision = crash(config.path(), {media.at("alpha", "a.png"), media.at("beta")}, 0);
        QVERIFY(!revision.isEmpty());
        const auto crashed = readBytes(config.filePath("settings.json"));
        const auto memory = RestoreOffer::dismissalPath(config.path(), 1);
        {
            Start start(config.path());
            start.viewer.open(media.file("delta/missing.png")); // a failed explicit open: it never checkpoints
            QTRY_COMPARE_WITH_TIMEOUT(start.offer->state(), QString("offer"), 10000);
            QVERIFY(start.offer->dismiss());
            QCOMPARE(start.offer->state(), QString("hidden"));
            QVERIFY(start.viewer.tabs().isEmpty());
            QTRY_COMPARE_WITH_TIMEOUT(RestoreOffer::readDismissedRevision(memory), revision, 10000);
        }
        // The window crashed again before it navigated anywhere: the same recovery point, already dismissed.
        QVERIFY(writeBytes(config.filePath("settings.json"), crashed));
        {
            Start start(config.path());
            start.viewer.open(media.file("delta/missing.png"));
            QTRY_VERIFY_WITH_TIMEOUT(start.settled(), 10000);
            QCOMPARE(start.offer->state(), QString("hidden"));
        }
        // A newer recovery point is offered again.
        QVERIFY(!crash(config.path(), {media.at("gamma", "c.png")}, 0).isEmpty());
        Start start(config.path());
        start.viewer.open(media.file("delta/d.png"));
        QTRY_COMPARE_WITH_TIMEOUT(start.offer->state(), QString("offer"), 10000);
        QCOMPARE(start.offer->folderName(), QString("gamma"));
    }

    void damagedDismissalRecordStillOffers_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("not json") << QByteArray("{ not json");
        QTest::newRow("future version") << QByteArray("{\"version\":2,\"checkpointRevision\":\"") + uuid().toUtf8() + "\"}";
        QTest::newRow("not a uuid") << QByteArray("{\"version\":1,\"checkpointRevision\":\"yesterday\"}");
        QTest::newRow("oversized") << QByteArray(8192, ' ');
    }
    void damagedDismissalRecordStillOffers() {
        QFETCH(QByteArray, bytes);
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(!crash(config.path(), {media.at("alpha", "a.png")}, 0).isEmpty());
        QVERIFY(writeBytes(RestoreOffer::dismissalPath(config.path(), 1), bytes));
        Start start(config.path());
        start.viewer.open(media.file("delta/d.png"));
        QTRY_COMPARE_WITH_TIMEOUT(start.offer->state(), QString("offer"), 10000);
        QVERIFY(start.offer->dismiss()); // and a later dismissal repairs it
        QTRY_VERIFY_WITH_TIMEOUT(!RestoreOffer::readDismissedRevision(RestoreOffer::dismissalPath(config.path(), 1)).isEmpty(), 10000);
    }

    // Corrupt or partial recovery bookkeeping cannot prove a crash: no offer, the read-only resume and bytes stay.
    void corruptRecoveryRecordIsNeverOffered() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(!crash(config.path(), {media.at("alpha", "a.png")}, 0).isEmpty());
        auto root = readRoot(config.path());
        auto recovery = root.value("recovery").toObject(); auto windows = recovery.value("windows").toObject();
        auto row = windows.value("1").toObject(); row.remove("sequence"); windows["1"] = row; recovery["windows"] = windows;
        root["recovery"] = recovery; QVERIFY(writeRoot(config.path(), root));
        Start start(config.path());
        QStringList statuses;
        connect(&start.viewer, &ViewerController::stateChanged, this, [&] { statuses.append(start.viewer.status()); });
        start.viewer.open(media.file("delta/d.png"));
        QTRY_VERIFY_WITH_TIMEOUT(statuses.join('\n').contains("Browsing recovery is unavailable") && start.shows(media.file("delta/d.png")), 10000);
        QCOMPARE(start.offer->generation(), quint64(0));
        QCOMPARE(start.offer->state(), QString("hidden"));
        QCOMPARE(record(config.path()), row); // never rewritten
    }

    // Partial tab state (one bad row) falls back to the one validated location, as every engine path does.
    void partialTabRecordOffersTheSingleLocation() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(!crash(config.path(), {media.at("alpha", "a.png"), media.at("beta")}, 0).isEmpty());
        auto root = readRoot(config.path());
        auto windows = root.value("windows").toObject(); auto entry = windows.value("1").toObject();
        auto rows = entry.value("tabs").toArray(); rows[1] = QJsonObject{{"folder", 7}}; entry["tabs"] = rows;
        windows["1"] = entry; root["windows"] = windows; QVERIFY(writeRoot(config.path(), root));
        Start start(config.path());
        start.viewer.open(media.file("delta/d.png"));
        QTRY_COMPARE_WITH_TIMEOUT(start.offer->state(), QString("offer"), 10000);
        QCOMPARE(start.offer->locationCount(), 1);
        QCOMPARE(start.offer->folderName(), QString("alpha"));
    }

    void missingFoldersAreReportedAndSkipped() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(!crash(config.path(), {media.at("alpha", "a.png"), media.at("beta")}, 0).isEmpty());
        QVERIFY(QDir(media.folder("alpha")).removeRecursively());
        {
            Start start(config.path());
            start.viewer.open(media.file("delta/d.png"));
            QTRY_COMPARE_WITH_TIMEOUT(start.offer->state(), QString("offer"), 10000);
            QCOMPARE(start.offer->unavailableCount(), 1);
            QVERIFY(start.offer->accept()); // the recovered active folder is gone, so the one still there opens
            QTRY_COMPARE_WITH_TIMEOUT(start.offer->state(), QString("hidden"), 10000);
            QVERIFY(same(start.viewer.directory(), media.folder("beta")));
            QCOMPARE(tabFolders(start.viewer), (QStringList{media.folder("delta"), media.folder("alpha"), media.folder("beta")}));
        }
        QVERIFY(!crash(config.path(), {media.at("alpha", "a.png"), media.at("gamma")}, 0).isEmpty());
        QVERIFY(QDir(media.folder("gamma")).removeRecursively());
        Start start(config.path());
        start.viewer.open(media.file("delta/d.png"));
        QTRY_VERIFY_WITH_TIMEOUT(start.offer->state() == "unavailable" && start.shows(media.file("delta/d.png")), 10000);
        QCOMPARE(start.offer->unavailableCount(), 2);
        QVERIFY(!start.offer->accept());
        QVERIFY(start.viewer.tabs().isEmpty());
        QVERIFY(start.offer->dismiss());
        QCOMPARE(start.offer->state(), QString("hidden"));
    }

    // Cancelled midway: the explicit tab comes back, the added tabs go, a late result of the restore replaces nothing,
    // and the offer stands again.
    void cancelMidwayReturnsToTheExplicitOpen() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(!crash(config.path(), {media.at("alpha", "a.png"), media.at("beta")}, 1).isEmpty());
        Start start(config.path());
        start.viewer.open(media.file("delta/d.png"));
        QTRY_VERIFY_WITH_TIMEOUT(start.offer->state() == "offer" && start.shows(media.file("delta/d.png")) && start.idle(), 10000);
        QVERIFY(start.offer->accept());
        QVERIFY(start.offer->cancellable());
        QVERIFY(start.offer->cancel()); // before the recovered folder's listing has returned
        QVERIFY(start.viewer.tabs().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(start.idle() && start.shows(media.file("delta/d.png")), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(start.offer->state(), QString("offer"), 10000);
        QVERIFY(same(start.viewer.directory(), media.folder("delta")));
        start.viewer.flushWindowPosition();
        QTRY_VERIFY_WITH_TIMEOUT(same(position(config.path()).value("image").toString(), media.file("delta/d.png")), 10000);
        QVERIFY(!position(config.path()).contains("tabs"));
        QVERIFY(!start.offer->cancel());
    }

    // Nothing is shown (the explicit folder was missing): the recovered set takes the empty window; cancel stops its
    // listing and the tab keeps its saved location.
    void cancelWhileTheRecoveredFolderListsInAnEmptyWindow() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(!crash(config.path(), {media.at("alpha", "a.png"), media.at("beta")}, 1).isEmpty());
        Start start(config.path());
        start.viewer.navigateDirectory(media.folder("nowhere"));
        QTRY_VERIFY_WITH_TIMEOUT(start.offer->state() == "offer" && start.idle(), 10000);
        QVERIFY(start.viewer.filePath().isEmpty() && start.viewer.directory().isEmpty());
        QVERIFY(start.offer->accept());
        QCOMPARE(tabFolders(start.viewer), (QStringList{media.folder("alpha"), media.folder("beta")}));
        QVERIFY(start.viewer.directoryPending());
        QVERIFY(start.offer->cancel());
        QVERIFY(start.offer->feedback().contains("stopped"));
        QTRY_VERIFY_WITH_TIMEOUT(start.idle(), 10000);
        QCOMPARE(start.offer->state(), QString("hidden"));
        QVERIFY(start.viewer.tabs().at(1).toMap().value("unavailable").toBool()); // F015: the cancelled tab keeps its location
    }

    void safeModeIsShownAndNeverRestores() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(!crash(config.path(), {media.at("alpha", "a.png")}, 0).isEmpty());
        Start start(config.path(), true);
        start.viewer.restoreWindow();
        start.viewer.open(media.file("delta/d.png"));
        QTRY_VERIFY_WITH_TIMEOUT(start.settled() && start.shows(media.file("delta/d.png")), 10000);
        QCOMPARE(start.offer->state(), QString("safe"));
        QVERIFY(!start.offer->accept());
        QVERIFY(start.viewer.tabs().isEmpty());
        QVERIFY(start.offer->dismiss());
        QCOMPARE(start.offer->state(), QString("hidden"));
        QVERIFY(!QFile::exists(RestoreOffer::dismissalPath(config.path(), 1))); // SAFE writes nothing to the saved root
    }

    void resetIsShownAndNeverRestores() {
        QTemporaryDir config; Media media; QVERIFY(config.isValid() && media.dir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config.path().toUtf8());
        QVERIFY(!crash(config.path(), {media.at("alpha", "a.png")}, 0).isEmpty());
        QString moved; QVERIFY(glancelot::resetSettings(&moved)); QVERIFY(!moved.isEmpty());
        Start start(config.path());
        start.offer->setStartupArguments({"glancelot", "--reset-settings"});
        start.viewer.restoreWindow();
        QTRY_COMPARE_WITH_TIMEOUT(start.offer->state(), QString("reset"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(start.offer->resetFile(), QFileInfo(moved).fileName(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(record(config.path()).value("state"), QJsonValue("dirty"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(start.offer->generation() > 0 && start.idle(), 10000);
        QVERIFY(start.viewer.filePath().isEmpty());
        QVERIFY(!start.offer->accept());
        QVERIFY(start.offer->dismiss());
        QCOMPARE(start.offer->state(), QString("hidden"));
    }

    void resetIntentFollowsTheStartupParser_data() {
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<bool>("reset");
        QTest::newRow("double dash") << QStringList{"glancelot", "--reset-settings"} << true;
        QTest::newRow("single dash word") << QStringList{"glancelot", "-reset-settings", "photo.png"} << true;
        QTest::newRow("absent") << QStringList{"glancelot", "photo.png"} << false;
        QTest::newRow("a file after --") << QStringList{"glancelot", "--", "--reset-settings"} << false;
        QTest::newRow("program name only") << QStringList{"--reset-settings"} << false;
    }
    void resetIntentFollowsTheStartupParser() {
        QFETCH(QStringList, arguments); QFETCH(bool, reset);
        QCOMPARE(RestoreOffer::resetRequested(arguments), reset);
    }

    // Generation fencing: a newer recovery point is never replaced by the probe of an older one, whatever order the
    // probes finish in. Repeated, because the pool decides that order.
    void staleProbeNeverReplacesANewerRecoveryPoint() {
        Media media; QTemporaryDir config; QVERIFY(media.dir.isValid() && config.isValid());
        ViewerController viewer; auto* offer = qobject_cast<RestoreOffer*>(viewer.workspaceRestore());
        offer->setSettingsDirectory(config.path());
        viewer.open(media.file("delta/d.png"));
        QTRY_VERIFY_WITH_TIMEOUT(viewer.imageReady(), 10000);
        const auto missing = TabLocation{media.folder("nowhere"), {}};
        for (int round = 0; round < 20; ++round) {
            offer->recovered(dirty({missing, media.at("gone")}, 0, uuid()));
            offer->recovered(dirty({media.at("alpha", "a.png")}, 0, uuid()));
            QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
            QTRY_VERIFY_WITH_TIMEOUT(!offer->checking(), 10000);
            QCoreApplication::processEvents(); // deliver any finished probe still queued
            QCOMPARE(offer->state(), QString("offer"));
            QCOMPARE(offer->folderName(), QString("alpha"));
            QCOMPARE(offer->unavailableCount(), 0);
        }
        offer->recovered(ResumeState{}); // a clean begin withdraws the offer
        QCOMPARE(offer->state(), QString("hidden"));
    }

    // The offer may be destroyed with its controller while its probe still runs.
    void controllerMayCloseWhileTheProbeRuns() {
        Media media; QTemporaryDir config; QVERIFY(media.dir.isValid() && config.isValid());
        {
            auto viewer = std::make_unique<ViewerController>();
            auto* offer = qobject_cast<RestoreOffer*>(viewer->workspaceRestore());
            offer->setSettingsDirectory(config.path());
            offer->recovered(dirty({media.at("alpha", "a.png")}, 0, uuid()));
            QVERIFY(offer->checking());
        }
        QVERIFY(QThreadPool::globalInstance()->waitForDone(10000));
        QCoreApplication::processEvents();
    }
};

QTEST_MAIN(RestoreOfferTests)
#include "restore_offer_tests.moc"
