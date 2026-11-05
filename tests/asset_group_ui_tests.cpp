// ASSET-GROUP (F022) through the shipped ui/Main.qml: companions shown in every delete, copy, move and rename confirmation,
// and the explicit choice between the named files and the whole asset. Generated files only; the trash is a fake.
// GLANCELOT_B1_EVIDENCE=<folder> also saves captures of each confirmation at 560 and 1180 px.
#include "asset_group_internal.hpp"
#include "glancelot/copy_move_service.hpp"
#include "glancelot/mutation_service.hpp"
#include "ui_test_support.hpp"
#include <QSignalSpy>
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace fs = std::filesystem;
using namespace glancelot;
namespace {
constexpr int kWait = 10000;
fs::path nativeOf(const QString& path) { return assets::detail::nativePath(path); }
struct FakeTrash final : TrashBackend {
    fs::path bin;
    std::shared_ptr<QStringList> asked = std::make_shared<QStringList>();
    explicit FakeTrash(fs::path folder) : bin(std::move(folder)) {}
    DeleteResult moveToTrash(const MutationTarget& target) override {
        *asked << assets::detail::displayPath(target.path.filename());
        std::error_code error;
        fs::rename(target.path, bin / target.path.filename(), error);
        return {target.path, error ? DeleteOutcome::Failed : DeleteOutcome::Trashed, error ? error.message() : std::string{}};
    }
};
// Holds a lookup open so a state between two plans can be looked at.
struct GatedLookup final : assets::detail::LookupBackend {
    std::mutex mutex;
    std::condition_variable wake;
    bool open{false};
    std::atomic_int entered{0};
    bool readable{false}; // once released: an empty folder, or one that can't be read
    void release() { { std::lock_guard lock(mutex); open = true; } wake.notify_all(); }
    bool readDirectory(const fs::path&, const std::function<bool(const assets::ListingInput&)>&) override {
        ++entered;
        std::unique_lock lock(mutex);
        wake.wait(lock, [this] { return open; });
        return readable;
    }
    std::optional<MutationTarget> snapshot(const fs::path&) override { return std::nullopt; }
    bool caseSensitive(const fs::path&, const fs::path&) override { return false; }
    std::chrono::steady_clock::time_point now() override { return std::chrono::steady_clock::now(); }
};
// A volume so slow that the budget has gone before the first read can start.
struct SpentBudgetLookup final : assets::detail::LookupBackend {
    std::chrono::steady_clock::time_point clock{};
    bool readDirectory(const fs::path&, const std::function<bool(const assets::ListingInput&)>&) override { return false; }
    std::optional<MutationTarget> snapshot(const fs::path&) override { return std::nullopt; }
    bool caseSensitive(const fs::path&, const fs::path&) override { return false; }
    std::chrono::steady_clock::time_point now() override { return clock += std::chrono::milliseconds(assets::kExpandBudgetMs); }
};
struct Scene {
    QTemporaryDir media, bin, target;
    QString touch(const QString& name, const QByteArray& bytes = "generated") {
        QFile file(media.filePath(name));
        if (file.open(QIODevice::WriteOnly)) file.write(bytes);
        return media.filePath(name);
    }
    QString touchTarget(const QString& name) {
        QFile file(target.filePath(name));
        if (file.open(QIODevice::WriteOnly)) file.write("already there");
        return target.filePath(name);
    }
};
QStringList rowsOf(QQuickItem* list) {
    QStringList rows;
    QList<QQuickItem*> pending{list};
    while (!pending.isEmpty()) {
        QQuickItem* item = pending.takeFirst();
        if (item->objectName() == "assetCompanionState") rows << item->property("text").toString();
        pending << item->childItems();
    }
    return rows;
}
QString textIn(QQuickItem* root) {
    QStringList texts;
    QList<QQuickItem*> pending{root};
    while (!pending.isEmpty()) {
        QQuickItem* item = pending.takeFirst();
        const QVariant text = item->property("text");
        if (text.isValid() && item->isVisible()) texts << text.toString();
        pending << item->childItems();
    }
    return texts.join(" | ");
}
}

class AssetGroupUiTests final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> config;
    // Each flow holds its own copy of the shared component: the one on screen, or else any.
    QQuickItem* item(Harness& h, const QString& name) {
        QQuickItem* any = nullptr;
        for (QQuickItem* candidate : namedAll(h.window, name)) {
            if (candidate->isVisible()) return candidate;
            any = candidate;
        }
        return any;
    }
    bool dialogOpen(Harness& h, const char* name) { QObject* d = h.window->findChild<QObject*>(name); return d && d->property("visible").toBool(); }
    QString flowPhase(Harness& h, const char* flow) { return h.object(flow)->property("phase").toString(); }
    bool requestDelete(Harness& h, const QStringList& paths, const QString& folder) {
        return QMetaObject::invokeMethod(h.object("deleteFlow"), "request", Q_ARG(QVariant, paths), Q_ARG(QVariant, QString("grid")), Q_ARG(QVariant, folder));
    }
    // A new summary relays the dialog on the next polish. grabWindow runs it, so the click lands where the control is now.
    void click(Harness& h, QQuickItem* target) {
        (void)h.window->grabWindow();
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, sceneCenter(target));
    }
    void waitForPhase(Harness& h, const char* flow) { QTRY_COMPARE_WITH_TIMEOUT(flowPhase(h, flow), QString("confirm"), kWait); }
    void capture(Harness& h, const QString& name) {
        const QString folder = qEnvironmentVariable("GLANCELOT_B1_EVIDENCE");
        if (folder.isEmpty()) return;
        const QSize sizes[] = {{560, 640}, {1180, 780}};
        for (const QSize& size : sizes) {
            h.window->resize(size);
            QTRY_COMPARE_WITH_TIMEOUT(h.window->size(), size, kWait);
            QCoreApplication::processEvents();
            QVERIFY(h.window->grabWindow().save(QDir(folder).filePath(QString("%1-%2.png").arg(name).arg(size.width()))));
        }
    }
    // Paste with the slow-plan progress out of the way, so "no dialog" can't depend on how busy the machine is.
    void startPaste(Harness& h, const QStringList& paths, const QString& destination, const char* mark = "markCopy") {
        QObject* flow = h.object("transferFlow");
        flow->setProperty("progressDelay", 3600000);
        QVERIFY(QMetaObject::invokeMethod(flow, mark, Q_ARG(QVariant, paths)));
        QVERIFY(QMetaObject::invokeMethod(flow, "paste", Q_ARG(QVariant, destination)));
    }
    // The Tab key until `name` holds the focus. Each option group is one stop.
    void tabTo(Harness& h, const QString& name) {
        for (int i = 0; i < 16 && !item(h, name)->hasActiveFocus(); ++i) QTest::keyClick(h.window, Qt::Key_Tab);
        QVERIFY2(item(h, name)->hasActiveFocus(), qPrintable(name));
    }
    void paste(Harness& h, const QStringList& paths, const QString& destination, const char* mark = "markCopy") {
        QObject* flow = h.object("transferFlow");
        QVERIFY(QMetaObject::invokeMethod(flow, mark, Q_ARG(QVariant, paths)));
        QVERIFY(QMetaObject::invokeMethod(flow, "paste", Q_ARG(QVariant, destination)));
        QTRY_VERIFY_WITH_TIMEOUT(dialogOpen(h, "transferConfirmation"), kWait);
        waitForPhase(h, "transferFlow");
    }
private slots:
    void initTestCase() {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        for (const char* face : {"Geist-Light", "Geist-Regular", "Geist-Medium", "Geist-SemiBold", "GeistMono-Regular"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/geist/%1.ttf").arg(QLatin1String(face)));
        qmlRegisterType<ImageCanvas>("Glancelot", 1, 0, "ImageCanvas");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")), "Glancelot", 1, 0, "Theme");
    }
    void init() { config = std::make_unique<QTemporaryDir>(); qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8()); }
    void cleanup() {
        qunsetenv("GLANCELOT_CONFIG_DIR");
        config.reset();
        assets::detail::setLookupBackendForTests(nullptr);
        QGuiApplication::setLayoutDirection(Qt::LeftToRight);
    }

    void deleteShowsCompanionsAndIsFileOnlyByDefault() {
        Scene scene;
        const QString cr2 = scene.touch("IMG.CR2");
        scene.touch("IMG.jpg");
        scene.touch("IMG.xmp");
        auto backend = std::make_unique<FakeTrash>(nativeOf(scene.bin.path()));
        auto asked = backend->asked;
        Harness h(std::move(backend));
        QVERIFY(h.window);
        QVERIFY(requestDelete(h, {cr2}, scene.media.path()));
        waitForPhase(h, "deleteFlow");
        QQuickItem* companions = item(h, "assetCompanions");
        QVERIFY(companions && companions->isVisible());
        QVERIFY(item(h, "assetScopeFile")->property("checked").toBool());
        QVERIFY(!item(h, "assetScopeAsset")->property("checked").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(rowsOf(item(h, "assetCompanionList")), (QStringList{"Not included", "Not included"}), kWait);
        const QString shown = textIn(companions);
        QVERIFY2(shown.contains("IMG.jpg") && shown.contains("IMG.xmp"), qPrintable(shown));
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "deleteCancel")->hasActiveFocus(), kWait); // the choice never takes default focus
        capture(h, "delete-file-only");
        click(h, item(h, "deleteConfirm"));
        QTRY_COMPARE_WITH_TIMEOUT(*asked, QStringList{"IMG.CR2"}, kWait);
        QVERIFY(QFile::exists(scene.media.filePath("IMG.jpg")) && QFile::exists(scene.media.filePath("IMG.xmp")));
    }
    void choosingAssetTrashesWhatItShowsAndIsNotRemembered() {
        Scene scene;
        const QString cr2 = scene.touch("IMG.CR2");
        const QString jpg = scene.touch("IMG.jpg");
        const QString xmp = scene.touch("IMG.xmp");
        const QString tif = scene.touch("IMG.tif");
        auto backend = std::make_unique<FakeTrash>(nativeOf(scene.bin.path()));
        auto asked = backend->asked;
        Harness h(std::move(backend));
        QVERIFY(h.window);
        QVERIFY(requestDelete(h, {cr2}, scene.media.path()));
        waitForPhase(h, "deleteFlow");
        QObject* flow = h.object("deleteFlow");
        // There and back and there again, so both choices have been clicked before the next operation resets them.
        for (const char* choice : {"assetScopeAsset", "assetScopeFile", "assetScopeAsset"}) {
            click(h, item(h, choice));
            const QString expected = QString(choice) == "assetScopeAsset" ? "asset" : "file";
            QTRY_VERIFY_WITH_TIMEOUT(flow->property("summary").toMap()["scope"].toString() == expected, kWait);
        }
        QVERIFY(!flow->property("replanning").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(rowsOf(item(h, "assetCompanionList")).count("Included"), 2, kWait);
        QVERIFY(textIn(item(h, "assetCompanions")).contains("edited copy"));
        QCOMPARE(item(h, "deleteScope")->property("text").toString(), QString("3 files"));
        capture(h, "delete-asset");
        click(h, item(h, "deleteConfirm"));
        QTRY_COMPARE_WITH_TIMEOUT(asked->size(), 3, kWait);
        QVERIFY(!QFile::exists(cr2) && !QFile::exists(jpg) && !QFile::exists(xmp) && QFile::exists(tif));
        // The next delete starts file-only again.
        const QString other = scene.touch("next.jpg");
        scene.touch("next.xmp");
        QTRY_VERIFY_WITH_TIMEOUT(!h.fileOperations.busy(), kWait);
        QVERIFY(requestDelete(h, {other}, scene.media.path()));
        QTRY_COMPARE_WITH_TIMEOUT(flow->property("summary").toMap()["scope"].toString(), QString("file"), kWait);
        QVERIFY(item(h, "assetScopeFile")->property("checked").toBool());
        QVERIFY(!item(h, "assetScopeAsset")->property("checked").toBool());
    }
    void confirmWaitsForTheNewPlan() {
        Scene scene;
        const QString cr2 = scene.touch("IMG.CR2");
        scene.touch("IMG.xmp");
        auto backend = std::make_unique<FakeTrash>(nativeOf(scene.bin.path()));
        auto asked = backend->asked;
        Harness h(std::move(backend));
        QVERIFY(h.window);
        QVERIFY(requestDelete(h, {cr2}, scene.media.path()));
        waitForPhase(h, "deleteFlow");
        GatedLookup gate;
        assets::detail::setLookupBackendForTests(&gate);
        click(h, item(h, "assetScopeAsset"));
        QTRY_VERIFY_WITH_TIMEOUT(gate.entered.load() == 1, kWait);
        QObject* flow = h.object("deleteFlow");
        QVERIFY(flow->property("replanning").toBool());
        QVERIFY(!item(h, "deleteConfirm")->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(flow, "confirmPlan")); // even a direct call is refused
        QVERIFY(asked->isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(dialogOpen(h, "deleteConfirmation"), kWait);
        gate.release();
        QTRY_VERIFY_WITH_TIMEOUT(!flow->property("replanning").toBool(), kWait);
        assets::detail::setLookupBackendForTests(nullptr);
        QVERIFY(item(h, "deleteConfirm")->isEnabled());
    }
    void noCompanionsLeaveTheConfirmationAsItWas() {
        Scene scene;
        const QString jpg = scene.touch("IMG.jpg");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        QVERIFY(requestDelete(h, {jpg}, scene.media.path()));
        waitForPhase(h, "deleteFlow");
        QVERIFY(!item(h, "assetCompanions")->isVisible());
        QVERIFY(item(h, "deleteNote")->property("text").toString().contains("sidecars"));
    }
    void aCollisionIsExplainedInTheConfirmation() {
        Scene scene;
        const QString cr2 = scene.touch("IMG.CR2");
        const QString nef = scene.touch("IMG.NEF");
        scene.touch("IMG.xmp");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        QVERIFY(requestDelete(h, {cr2}, scene.media.path()));
        waitForPhase(h, "deleteFlow");
        click(h, item(h, "assetScopeAsset"));
        QTRY_VERIFY_WITH_TIMEOUT(h.object("deleteFlow")->property("summary").toMap()["scope"].toString() == "asset", kWait);
        QTRY_VERIFY_WITH_TIMEOUT(textIn(item(h, "assetCompanions")).contains("shares its name"), kWait);
        QVERIFY(textIn(item(h, "assetCompanions")).contains("more than one photo"));
        capture(h, "delete-collision");
        QCOMPARE(h.object("deleteFlow")->property("summary").toMap()["targets"].toList().size(), 1);
        QVERIFY(QFile::exists(nef));
    }
    void companionsThatCouldNotBeCheckedAreSaid() {
        Scene scene;
        const QString jpg = scene.touch("IMG.jpg");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        SpentBudgetLookup slow;
        assets::detail::setLookupBackendForTests(&slow);
        QVERIFY(requestDelete(h, {jpg}, scene.media.path()));
        waitForPhase(h, "deleteFlow");
        QQuickItem* notice = item(h, "assetCompanionsIncomplete");
        QVERIFY(notice && notice->isVisible());
        QVERIFY(textIn(notice).contains("Companions couldn't all be checked."));
        QVERIFY(!item(h, "assetScopeFile")->isVisible());
    }

    void pasteAsksFirstAndRunsNothingUntilConfirmed() {
        Scene scene;
        const QString cr2 = scene.touch("IMG.CR2");
        scene.touch("IMG.xmp");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        auto* service = h.viewer.copyMoveService();
        QSignalSpy batch(service, &CopyMoveService::batchFinished);
        paste(h, {cr2}, scene.target.path());
        QVERIFY(QDir(scene.target.path()).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "transferConfirm")->hasActiveFocus(), kWait);
        QVERIFY(textIn(item(h, "assetCompanions")).contains("IMG.xmp"));
        QVERIFY(item(h, "assetScopeFile")->property("checked").toBool());
        capture(h, "copy-file-only");
        click(h, item(h, "transferCancel"));
        QTRY_VERIFY_WITH_TIMEOUT(!dialogOpen(h, "transferConfirmation"), kWait);
        QVERIFY(QDir(scene.target.path()).isEmpty());
        QVERIFY(batch.isEmpty());
        QVERIFY(service->hasClipboardTargets()); // Cancel keeps what was copied
    }
    void pasteOfAWholeAsset() {
        Scene scene;
        const QString cr2 = scene.touch("IMG.CR2");
        const QString xmp = scene.touch("IMG.xmp");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        auto* service = h.viewer.copyMoveService();
        QSignalSpy batch(service, &CopyMoveService::batchFinished);
        paste(h, {cr2}, scene.target.path(), "markCut");
        click(h, item(h, "assetScopeAsset"));
        QTRY_VERIFY_WITH_TIMEOUT(h.object("transferFlow")->property("summary").toMap()["scope"].toString() == "asset", kWait);
        QTRY_COMPARE_WITH_TIMEOUT(rowsOf(item(h, "assetCompanionList")).count("Included"), 1, kWait);
        QCOMPARE(item(h, "transferConfirm")->property("text").toString(), QString("Move here"));
        capture(h, "move-asset");
        click(h, item(h, "transferConfirm"));
        QTRY_COMPARE_WITH_TIMEOUT(batch.count(), 1, kWait);
        QVERIFY(!QFile::exists(cr2) && !QFile::exists(xmp));
        QVERIFY(QFile::exists(QDir(scene.target.path()).filePath("IMG.CR2")) && QFile::exists(QDir(scene.target.path()).filePath("IMG.xmp")));
        QVERIFY(!service->hasClipboardTargets()); // a finished cut is spent
    }
    void aCollisionTakesOneDecisionAndShowsTheNewNames() {
        Scene scene;
        const QString cr2 = scene.touch("IMG.CR2");
        scene.touch("IMG.xmp");
        scene.touchTarget("IMG.xmp");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        auto* service = h.viewer.copyMoveService();
        QSignalSpy batch(service, &CopyMoveService::batchFinished);
        paste(h, {cr2}, scene.target.path());
        click(h, item(h, "assetScopeAsset"));
        QTRY_VERIFY_WITH_TIMEOUT(h.object("transferFlow")->property("summary").toMap()["scope"].toString() == "asset", kWait);
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "transferCollisions")->isVisible(), kWait);
        QVERIFY(item(h, "transferKeepBoth")->property("checked").toBool()); // never overwrites by default
        QVERIFY(!item(h, "transferOverwrite"));
        QTRY_VERIFY_WITH_TIMEOUT(textIn(item(h, "assetCompanions")).contains("IMG (2).xmp"), kWait);
        QVERIFY(item(h, "transferRenamedTo")->property("text").toString().contains("IMG (2).CR2"));
        capture(h, "copy-collision");
        click(h, item(h, "transferSkip"));
        QTRY_VERIFY_WITH_TIMEOUT(!item(h, "transferRenamedTo") || !item(h, "transferRenamedTo")->isVisible(), kWait);
        click(h, item(h, "transferConfirm"));
        QTRY_COMPARE_WITH_TIMEOUT(batch.count(), 1, kWait);
        QCOMPARE(QDir(scene.target.path()).entryList(QDir::Files), QStringList{"IMG.xmp"}); // the whole asset stayed behind, under any name
        QVERIFY(QFile::exists(cr2));
    }
    void aMemberChangedAfterConfirmationIsReported() {
        Scene scene;
        const QString cr2 = scene.touch("IMG.CR2");
        scene.touch("IMG.xmp"); // something to show, so the confirmation waits
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        auto* service = h.viewer.copyMoveService();
        QSignalSpy batch(service, &CopyMoveService::batchFinished);
        paste(h, {cr2}, scene.target.path());
        QVERIFY(QFile::remove(cr2));
        click(h, item(h, "transferConfirm"));
        QTRY_VERIFY_WITH_TIMEOUT(dialogOpen(h, "transferResults"), kWait);
        QCOMPARE(batch[0][2].toInt(), 1);
        QVERIFY(textIn(h.window->contentItem()).contains("It no longer exists."));
        click(h, item(h, "transferResultsClose"));
        QTRY_VERIFY_WITH_TIMEOUT(!dialogOpen(h, "transferResults"), kWait);
    }
    void thePasteCommandGoesThroughTheConfirmation() {
        Scene scene;
        const QString file = scene.touch("p.png");
        QImage image(64, 64, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        QVERIFY(image.save(file));
        scene.touch("p.xmp");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        h.viewer.open(file);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QVERIFY(QMetaObject::invokeMethod(h.window, "invoke", Q_ARG(QVariant, QString("copy-selection"))));
        QVERIFY(QMetaObject::invokeMethod(h.window, "invoke", Q_ARG(QVariant, QString("paste"))));
        QTRY_VERIFY_WITH_TIMEOUT(dialogOpen(h, "transferConfirmation"), kWait);
        QVERIFY(h.window->property("modalOpen").toBool());
        waitForPhase(h, "transferFlow");
        QCOMPARE(h.object("transferFlow")->property("summary").toMap()["targets"].toList().at(0).toMap()["name"].toString(), QString("p.png"));
    }

    void aPlainPasteRunsWithNoDialog_data() {
        QTest::addColumn<QString>("mark");
        QTest::newRow("copy") << QString("markCopy");
        QTest::newRow("move") << QString("markCut");
    }
    void aPlainPasteRunsWithNoDialog() {
        QFETCH(QString, mark);
        Scene scene;
        const QString jpg = scene.touch("IMG.jpg");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        auto* service = h.viewer.copyMoveService();
        QSignalSpy batch(service, &CopyMoveService::batchFinished);
        QObject* dialog = h.window->findChild<QObject*>("transferConfirmation");
        QSignalSpy shown(dialog, SIGNAL(visibleChanged()));
        startPaste(h, {jpg}, scene.target.path(), mark.toUtf8().constData());
        QTRY_COMPARE_WITH_TIMEOUT(batch.count(), 1, kWait);
        QCOMPARE(shown.count(), 0); // not even a flash
        QVERIFY(QFile::exists(QDir(scene.target.path()).filePath("IMG.jpg")));
        QCOMPARE(QFile::exists(jpg), mark == "markCopy");
        QCOMPARE(service->hasClipboardTargets(), mark == "markCopy"); // a finished cut is spent, a copy can be pasted again
        QVERIFY(!h.window->property("modalOpen").toBool());
    }
    void thePasteCommandRunsPlainFilesStraightThrough() {
        Scene scene;
        const QString viewed = scene.touch("p.png");
        QImage image(64, 64, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        QVERIFY(image.save(viewed));
        QFile other(scene.target.filePath("other.jpg"));
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.close();
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        h.viewer.open(viewed);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QObject* flow = h.object("transferFlow");
        flow->setProperty("progressDelay", 3600000);
        QSignalSpy batch(h.viewer.copyMoveService(), &CopyMoveService::batchFinished);
        QSignalSpy shown(h.window->findChild<QObject*>("transferConfirmation"), SIGNAL(visibleChanged()));
        QVERIFY(QMetaObject::invokeMethod(flow, "markCopy", Q_ARG(QVariant, QStringList{other.fileName()})));
        QVERIFY(QMetaObject::invokeMethod(h.window, "invoke", Q_ARG(QVariant, QString("paste"))));
        QTRY_COMPARE_WITH_TIMEOUT(batch.count(), 1, kWait);
        QCOMPARE(shown.count(), 0);
        QVERIFY(QFile::exists(scene.media.filePath("other.jpg")));
    }
    void everythingToDecideIsSeen_data() {
        QTest::addColumn<QVariantMap>("change");
        QTest::addColumn<bool>("runs");
        const QVariantList one{QVariantMap{{"path", "x"}}};
        QTest::newRow("plain") << QVariantMap{} << true;
        QTest::newRow("listed companion") << QVariantMap{{"companions", one}} << false;
        QTest::newRow("companion past the list") << QVariantMap{{"companionsNotListed", 3}} << false;
        QTest::newRow("collision") << QVariantMap{{"collisions", one}} << false;
        QTest::newRow("exclusion") << QVariantMap{{"rejected", one}} << false;
        QTest::newRow("couldn't all be checked") << QVariantMap{{"companionsIncomplete", true}} << false;
        QTest::newRow("refused") << QVariantMap{{"error", "destinationMissing"}} << false;
        QTest::newRow("nothing to run") << QVariantMap{{"targets", QVariantList{}}} << false;
    }
    void everythingToDecideIsSeen() {
        QFETCH(QVariantMap, change);
        QFETCH(bool, runs);
        Scene scene;
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        QVariantMap plan{{"targets", QVariantList{QVariantMap{{"path", "a"}}}}, {"rejected", QVariantList{}}, {"collisions", QVariantList{}},
                         {"companions", QVariantList{}}, {"companionsNotListed", 0}, {"companionsIncomplete", false}};
        plan.insert(change);
        QVariant result;
        QVERIFY(QMetaObject::invokeMethod(h.object("transferFlow"), "nothingToDecide", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, plan)));
        QCOMPARE(result.toBool(), runs);
    }
    void aRealPlanWithSomethingToDecideShowsTheDialog_data() {
        QTest::addColumn<QString>("condition");
        for (const char* row : {"companion", "collision", "couldn't check", "missing source", "missing destination"}) QTest::newRow(row) << QString(row);
    }
    void aRealPlanWithSomethingToDecideShowsTheDialog() {
        QFETCH(QString, condition);
        Scene scene;
        const QString jpg = scene.touch("IMG.jpg");
        QStringList paths{jpg};
        QString destination = scene.target.path();
        if (condition == "companion") scene.touch("IMG.xmp");
        if (condition == "collision") scene.touchTarget("IMG.jpg");
        if (condition == "missing source") paths << scene.media.filePath("gone.jpg");
        if (condition == "missing destination") destination = scene.target.filePath("gone");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        SpentBudgetLookup slow;
        if (condition == "couldn't check") assets::detail::setLookupBackendForTests(&slow);
        auto* service = h.viewer.copyMoveService();
        QSignalSpy started(service, &CopyMoveService::itemStarted);
        startPaste(h, paths, destination);
        QTRY_VERIFY_WITH_TIMEOUT(dialogOpen(h, "transferConfirmation"), kWait);
        QCOMPARE(flowPhase(h, "transferFlow"), QString("confirm"));
        QVERIFY(started.isEmpty());
        QVERIFY(!QFile::exists(QDir(scene.target.path()).filePath("IMG.jpg")) || condition == "collision");
    }
    void aSlowPlanShowsProgressThenRunsIfThereIsNothingToAsk() {
        Scene scene;
        const QString jpg = scene.touch("IMG.jpg");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        GatedLookup gate;
        gate.readable = true;
        assets::detail::setLookupBackendForTests(&gate);
        QSignalSpy batch(h.viewer.copyMoveService(), &CopyMoveService::batchFinished);
        QObject* flow = h.object("transferFlow");
        flow->setProperty("progressDelay", 50);
        QVERIFY(QMetaObject::invokeMethod(flow, "markCopy", Q_ARG(QVariant, QStringList{jpg})));
        QVERIFY(QMetaObject::invokeMethod(flow, "paste", Q_ARG(QVariant, scene.target.path())));
        QVERIFY(!dialogOpen(h, "transferConfirmation")); // nothing yet
        QTRY_VERIFY_WITH_TIMEOUT(dialogOpen(h, "transferConfirmation"), kWait);
        QVERIFY(item(h, "transferPlanning")->isVisible());
        QVERIFY(item(h, "transferCancel")->isVisible());
        gate.release();
        QTRY_VERIFY_WITH_TIMEOUT(!dialogOpen(h, "transferConfirmation"), kWait);
        QTRY_COMPARE_WITH_TIMEOUT(batch.count(), 1, kWait);
        QVERIFY(QFile::exists(QDir(scene.target.path()).filePath("IMG.jpg")));
        assets::detail::setLookupBackendForTests(nullptr);
    }
    void aSlowPlanCanBeCancelled() {
        Scene scene;
        const QString jpg = scene.touch("IMG.jpg");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        GatedLookup gate;
        gate.readable = true;
        assets::detail::setLookupBackendForTests(&gate);
        QSignalSpy started(h.viewer.copyMoveService(), &CopyMoveService::itemStarted);
        QSignalSpy planned(h.viewer.copyMoveService(), &CopyMoveService::transferPlanReady);
        QObject* flow = h.object("transferFlow");
        flow->setProperty("progressDelay", 50);
        QVERIFY(QMetaObject::invokeMethod(flow, "markCopy", Q_ARG(QVariant, QStringList{jpg})));
        QVERIFY(QMetaObject::invokeMethod(flow, "paste", Q_ARG(QVariant, scene.target.path())));
        QTRY_VERIFY_WITH_TIMEOUT(dialogOpen(h, "transferConfirmation"), kWait);
        click(h, item(h, "transferCancel"));
        QTRY_VERIFY_WITH_TIMEOUT(!dialogOpen(h, "transferConfirmation"), kWait);
        gate.release();
        // A later plan's result is the barrier that proves the cancelled one ran nothing.
        assets::detail::setLookupBackendForTests(nullptr);
        const QString xmp = scene.touch("IMG.xmp");
        QVERIFY(QMetaObject::invokeMethod(flow, "paste", Q_ARG(QVariant, scene.target.path())));
        QTRY_COMPARE_WITH_TIMEOUT(planned.count(), 1, kWait);
        QTRY_VERIFY_WITH_TIMEOUT(dialogOpen(h, "transferConfirmation"), kWait);
        QVERIFY(started.isEmpty());
        QVERIFY(QDir(scene.target.path()).isEmpty());
        QVERIFY(QFile::exists(xmp));
    }
    void movingWithinOneFolderIsANoticeNotADialog() {
        Scene scene;
        const QString jpg = scene.touch("IMG.jpg");
        scene.touch("IMG.xmp"); // a companion doesn't turn "nothing would change" into a question
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        auto* service = h.viewer.copyMoveService();
        QSignalSpy started(service, &CopyMoveService::itemStarted);
        QSignalSpy planned(service, &CopyMoveService::transferPlanReady);
        QSignalSpy shown(h.window->findChild<QObject*>("transferConfirmation"), SIGNAL(visibleChanged()));
        startPaste(h, {jpg}, scene.media.path(), "markCut");
        QTRY_COMPARE_WITH_TIMEOUT(planned.count(), 1, kWait);
        QTRY_COMPARE_WITH_TIMEOUT(item(h, "deleteNotice")->property("text").toString(), QString("It's already in this folder."), kWait);
        QCOMPARE(shown.count(), 0);
        QVERIFY(started.isEmpty());
        QVERIFY(QFile::exists(jpg));
        QVERIFY(service->hasClipboardTargets()); // still cut: it can be pasted somewhere else
    }
    void aMoveWithOnlySomeItemsAlreadyHereStillAsks() {
        Scene scene;
        const QString here = scene.touch("here.jpg");
        QFile away(scene.target.filePath("away.jpg"));
        QVERIFY(away.open(QIODevice::WriteOnly));
        away.close();
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        startPaste(h, {here, away.fileName()}, scene.media.path(), "markCut");
        QTRY_VERIFY_WITH_TIMEOUT(dialogOpen(h, "transferConfirmation"), kWait);
        QVERIFY(textIn(item(h, "transferRejected")).contains("already in this folder"));
    }

    // Tab reaches a group once, on its checked option, with a visible ring, and the arrows move and select within it.
    void theKeyboardWalksTheChoices_data() {
        QTest::addColumn<QString>("kind");
        for (const char* row : {"delete", "copy", "move", "rename"}) QTest::newRow(row) << QString(row);
    }
    void theKeyboardWalksTheChoices() {
        QFETCH(QString, kind);
        Scene scene;
        const QString cr2 = scene.touch("IMG.CR2");
        scene.touch("IMG.xmp");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        h.window->resize(1180, 780);
        QVERIFY(activate(h));
        QObject* flow = nullptr;
        const char* scopeProperty = "scope";
        if (kind == "delete") {
            flow = h.object("deleteFlow");
            QVERIFY(requestDelete(h, {cr2}, scene.media.path()));
            waitForPhase(h, "deleteFlow");
        } else if (kind == "rename") {
            flow = h.object("renameFlow");
            scopeProperty = "renameScope";
            QVERIFY(QMetaObject::invokeMethod(flow, "openRename", Q_ARG(QVariant, cr2)));
            QTRY_VERIFY_WITH_TIMEOUT(textIn(item(h, "assetCompanions")).contains("Keeps its name"), kWait);
        } else {
            flow = h.object("transferFlow");
            paste(h, {cr2}, scene.target.path(), kind == "move" ? "markCut" : "markCopy");
        }
        const auto scope = [&] { return flow->property(scopeProperty).toString(); };
        tabTo(h, "assetScopeFile");
        QVERIFY(item(h, "assetScopeFile")->property("visualFocus").toBool()); // the ring
        QVERIFY(!item(h, "assetScopeAsset")->activeFocusOnTab()); // one stop per group
        QTest::keyClick(h.window, Qt::Key_Down);
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "assetScopeAsset")->hasActiveFocus(), kWait);
        QVERIFY(item(h, "assetScopeAsset")->property("checked").toBool());
        QVERIFY(item(h, "assetScopeAsset")->property("visualFocus").toBool());
        QCOMPARE(scope(), QString("asset"));
        if (kind == "rename") QTRY_VERIFY_WITH_TIMEOUT(item(h, "renameRefusal")->isVisible(), kWait);
        else QTRY_VERIFY_WITH_TIMEOUT(!flow->property("replanning").toBool(), kWait);
        capture(h, kind + "-keyboard");
        QVERIFY(item(h, "assetScopeAsset")->hasActiveFocus()); // a new plan leaves the focus on the choice
        // The other arrows, wrapping at both ends.
        QTest::keyClick(h.window, Qt::Key_Down);
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "assetScopeFile")->hasActiveFocus(), kWait);
        QCOMPARE(scope(), QString("file"));
        QTest::keyClick(h.window, Qt::Key_Up);
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "assetScopeAsset")->hasActiveFocus(), kWait);
        QCOMPARE(scope(), QString("asset"));
        QTest::keyClick(h.window, Qt::Key_Left);
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "assetScopeFile")->hasActiveFocus(), kWait);
        QCOMPARE(scope(), QString("file"));
        QTest::keyClick(h.window, Qt::Key_Right);
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "assetScopeAsset")->hasActiveFocus(), kWait);
        QCOMPARE(scope(), QString("asset"));
        // Tab leaves the group and Shift+Tab comes back to the option now checked.
        QTest::keyClick(h.window, Qt::Key_Tab);
        QVERIFY(!item(h, "assetScopeAsset")->hasActiveFocus() && !item(h, "assetScopeFile")->hasActiveFocus());
        QTest::keyClick(h.window, Qt::Key_Backtab, Qt::ShiftModifier);
        QVERIFY(item(h, "assetScopeAsset")->hasActiveFocus());
        // A held arrow moves once.
        QTest::keyPress(h.window, Qt::Key_Down, Qt::NoModifier);
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier, QString(), true);
        QCoreApplication::sendEvent(h.window, &repeat);
        QTest::keyRelease(h.window, Qt::Key_Down, Qt::NoModifier);
        QVERIFY(item(h, "assetScopeFile")->hasActiveFocus());
        QCOMPARE(scope(), QString("file"));
    }
    void theKeyboardMakesTheCollisionChoice() {
        Scene scene;
        const QString jpg = scene.touch("IMG.jpg");
        scene.touchTarget("IMG.jpg");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        h.window->resize(1180, 780);
        QVERIFY(activate(h));
        paste(h, {jpg}, scene.target.path());
        QObject* flow = h.object("transferFlow");
        tabTo(h, "transferKeepBoth");
        QVERIFY(item(h, "transferKeepBoth")->property("visualFocus").toBool());
        QTest::keyClick(h.window, Qt::Key_Down);
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "transferSkip")->hasActiveFocus(), kWait);
        QVERIFY(!flow->property("keepBoth").toBool());
        QVERIFY(item(h, "transferSkip")->property("checked").toBool());
    }
    void leftAndRightFollowTheLayoutDirection() {
        QGuiApplication::setLayoutDirection(Qt::RightToLeft);
        Scene scene;
        const QString cr2 = scene.touch("IMG.CR2");
        scene.touch("IMG.xmp");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        h.window->resize(1180, 780);
        QVERIFY(activate(h));
        QVERIFY(requestDelete(h, {cr2}, scene.media.path()));
        waitForPhase(h, "deleteFlow");
        tabTo(h, "assetScopeFile");
        QTest::keyClick(h.window, Qt::Key_Left); // forward, when the layout runs right to left
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "assetScopeAsset")->hasActiveFocus(), kWait);
        QTest::keyClick(h.window, Qt::Key_Right);
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "assetScopeFile")->hasActiveFocus(), kWait);
    }
    void aFocusedChoiceStaysInViewInAShortWindow() {
        Scene scene;
        QStringList paths;
        for (int i = 0; i < 5; ++i) paths << scene.touch(QString("IMG_%1.CR2").arg(i));
        scene.touch("IMG_0.xmp");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        h.window->resize(560, 320);
        QTRY_COMPARE_WITH_TIMEOUT(h.window->height(), 320, kWait);
        QVERIFY(activate(h));
        QVERIFY(requestDelete(h, paths, scene.media.path()));
        waitForPhase(h, "deleteFlow");
        (void)h.window->grabWindow();
        tabTo(h, "assetScopeFile");
        QQuickItem* choice = item(h, "assetScopeFile");
        QQuickItem* view = choice->parentItem();
        while (view && !view->inherits("QQuickFlickable")) view = view->parentItem();
        QVERIFY(view);
        QVERIFY2(view->property("contentHeight").toReal() > view->height(), "the body must overflow for this to mean anything");
        const QPointF top = choice->mapToItem(view, QPointF(0, 0));
        QVERIFY(top.y() >= 0 && top.y() + choice->height() <= view->height());
    }
    void arrowsScrollALongCompanionList() {
        Scene scene;
        const QString cr2 = scene.touch("IMG.CR2");
        for (const char* name : {"IMG.jpg", "IMG.mov", "IMG.mp4", "IMG.xmp", "IMG.tif", "IMG.png", "IMG.webp"}) scene.touch(name);
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        h.window->resize(1180, 780);
        QVERIFY(activate(h));
        QVERIFY(requestDelete(h, {cr2}, scene.media.path()));
        waitForPhase(h, "deleteFlow");
        QTRY_VERIFY_WITH_TIMEOUT(rowsOf(item(h, "assetCompanionList")).size() >= 5, kWait);
        (void)h.window->grabWindow();
        QQuickItem* list = item(h, "assetCompanionList");
        QVERIFY(list->property("contentHeight").toReal() > list->height());
        tabTo(h, "assetCompanionList");
        for (int i = 0; i < 6; ++i) QTest::keyClick(h.window, Qt::Key_Down);
        QTRY_VERIFY_WITH_TIMEOUT(list->property("contentY").toReal() > 0, kWait);
    }

    void renameShowsWhatKeepsItsNameAndRefusesTheSetWithAReason() {
        Scene scene;
        const QString cr2 = scene.touch("IMG.CR2");
        scene.touch("IMG.CR2.xmp");
        scene.touch("IMG.jpg");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        QObject* flow = h.object("renameFlow");
        QVERIFY(QMetaObject::invokeMethod(flow, "openRename", Q_ARG(QVariant, cr2)));
        QTRY_VERIFY_WITH_TIMEOUT(flow->property("modal").toBool(), kWait);
        QTRY_VERIFY_WITH_TIMEOUT(textIn(item(h, "assetCompanions")).contains("Keeps its name"), kWait);
        QVERIFY(!item(h, "renameConfirm")->isEnabled()); // nothing typed yet
        capture(h, "rename-file-only");
        click(h, item(h, "assetScopeAsset"));
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "renameRefusal")->isVisible(), kWait);
        QVERIFY(textIn(item(h, "renameRefusal")).contains("isn't available yet"));
        QVERIFY(!item(h, "renameConfirm")->isEnabled());
        QQuickItem* field = nullptr;
        QList<QQuickItem*> pending{h.window->contentItem()};
        while (!pending.isEmpty()) {
            QQuickItem* candidate = pending.takeFirst();
            if (candidate->inherits("QQuickTextInput") && candidate->isVisible() && candidate->property("text").toString() == "IMG.CR2") field = candidate;
            pending << candidate->childItems();
        }
        QVERIFY(field);
        // A new name previews every member, and is still refused: neither the button nor a direct call runs it.
        field->setProperty("text", "Holiday.CR2");
        QTRY_VERIFY_WITH_TIMEOUT(textIn(item(h, "assetCompanions")).contains("Holiday.jpg"), kWait);
        QVERIFY(textIn(item(h, "assetCompanions")).contains("Holiday.CR2.xmp"));
        QVERIFY(!item(h, "renameConfirm")->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(flow, "executeRename"));
        QVERIFY(QFile::exists(cr2) && flow->property("modal").toBool());
        capture(h, "rename-asset-refused");
        // Back to the file: the new name renames exactly that file, and nothing else.
        click(h, item(h, "assetScopeFile"));
        QTRY_VERIFY_WITH_TIMEOUT(!item(h, "renameRefusal")->isVisible(), kWait);
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "renameConfirm")->isEnabled(), kWait);
        click(h, item(h, "renameConfirm"));
        QTRY_VERIFY_WITH_TIMEOUT(!flow->property("modal").toBool(), kWait);
        QVERIFY(QFile::exists(scene.media.filePath("Holiday.CR2")));
        QVERIFY(QFile::exists(scene.media.filePath("IMG.CR2.xmp")) && QFile::exists(scene.media.filePath("IMG.jpg")));
        // Not remembered, and a second open of the same name, whose text doesn't change, still gets its preview.
        const QString other = scene.touch("next.jpg");
        scene.touch("next.xmp");
        QSignalSpy ready(h.viewer.mutationService(), &MutationService::renamePlanReady);
        QVERIFY(QMetaObject::invokeMethod(flow, "openRename", Q_ARG(QVariant, other)));
        QTRY_VERIFY_WITH_TIMEOUT(textIn(item(h, "assetCompanions")).contains("next.xmp"), kWait);
        click(h, item(h, "assetScopeAsset"));
        QTRY_VERIFY_WITH_TIMEOUT(item(h, "renameRefusal")->isVisible(), kWait);
        click(h, item(h, "renameCancel"));
        QTRY_VERIFY_WITH_TIMEOUT(!flow->property("modal").toBool(), kWait);
        QVERIFY(QMetaObject::invokeMethod(flow, "openRename", Q_ARG(QVariant, other)));
        QTRY_VERIFY_WITH_TIMEOUT(textIn(item(h, "assetCompanions")).contains("Keeps its name"), kWait);
        QCOMPARE(flow->property("renameScope").toString(), QString("file"));
        QVERIFY(item(h, "assetScopeFile")->property("checked").toBool());
        QVERIFY(!item(h, "renameRefusal")->isVisible());
    }
    void renameWithNoCompanionsLooksAsItDid() {
        Scene scene;
        const QString jpg = scene.touch("IMG.jpg");
        Harness h(std::make_unique<FakeTrash>(nativeOf(scene.bin.path())));
        QVERIFY(h.window);
        QObject* flow = h.object("renameFlow");
        QSignalSpy ready(h.viewer.mutationService(), &MutationService::renamePlanReady);
        QVERIFY(QMetaObject::invokeMethod(flow, "openRename", Q_ARG(QVariant, jpg)));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, kWait);
        for (auto* candidate : namedAll(h.window, "assetCompanions")) QVERIFY(!candidate->isVisible());
    }
};

QTEST_MAIN(AssetGroupUiTests)
#include "asset_group_ui_tests.moc"
