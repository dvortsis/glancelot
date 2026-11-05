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
    void release() { { std::lock_guard lock(mutex); open = true; } wake.notify_all(); }
    bool readDirectory(const fs::path&, const std::function<bool(const assets::ListingInput&)>&) override {
        ++entered;
        std::unique_lock lock(mutex);
        wake.wait(lock, [this] { return open; });
        return false;
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
    void cleanup() { qunsetenv("GLANCELOT_CONFIG_DIR"); config.reset(); assets::detail::setLookupBackendForTests(nullptr); }

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
