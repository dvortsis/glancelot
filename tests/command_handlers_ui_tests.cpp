// EN-07: every view command a feature component handles still works when it reaches that component through the shipped
// Main.qml. Each case fires the command on the controller, past Main.qml's scope routing (invoke could refuse it, and the
// case would then prove nothing), checks that the controller really emitted it, and asserts one effect exactly once.
// The cleanup step fails on any QML warning, which catches a stale reference in a moved branch.
#include "glancelot/copy_move_service.hpp"
#include "ui_test_support.hpp"
#include <QFile>
#include <QMimeData>
#include <QMutex>
#include <QSignalSpy>
#include <utility>

namespace {
QMutex warningLock;
QStringList qmlWarnings;
QtMessageHandler previousHandler = nullptr;
void captureQmlWarnings(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    const QByteArray category(context.category ? context.category : "");
    const bool fromQml = (context.file && QByteArray(context.file).endsWith(".qml")) || message.contains(QLatin1String(".qml:"))
        || category.startsWith("qml") || category.startsWith("js");
    if (type != QtDebugMsg && type != QtInfoMsg && fromQml) { QMutexLocker lock(&warningLock); qmlWarnings << message; }
    if (previousHandler) previousHandler(type, context, message);
}
QStringList takeQmlWarnings() { QMutexLocker lock(&warningLock); return std::exchange(qmlWarnings, {}); }

// <temp>/album holds three photos and a folder with one photo, with an empty <temp>/other beside it. Nothing is ever confirmed
// for deletion here, and a paste only adds a copy to these disposable folders.
struct Album {
    QTemporaryDir dir;
    Album() {
        QDir root(dir.path());
        root.mkpath("album/sub"); root.mkpath("other");
        for (int i = 1; i <= 3; ++i) {
            QImage image(320, 240, QImage::Format_RGB32);
            image.fill(QColor::fromHsv(i * 70, 110, 180));
            image.save(photo(i));
            if (i == 1) image.save(album() + "/sub/inner.png");
        }
    }
    QString rootPath() const { return QDir::cleanPath(dir.path()); }
    QString album() const { return rootPath() + "/album"; }
    QString other() const { return rootPath() + "/other"; }
    QString photo(int number) const { return album() + QString("/p%1.png").arg(number); }
    static constexpr int rows = 4; // the three photos and "sub"
};

bool fire(Harness& h, const QString& id) {
    QSignalSpy emitted(&h.viewer, &ViewerController::viewCommand);
    h.viewer.trigger(id);
    for (const auto& arguments : emitted) if (arguments.first().toString() == id) return true;
    return false;
}
#define FIRE(h, id) QVERIFY2(fire(h, id), id)

bool openAlbum(Harness& h, const Album& album) {
    h.viewer.setFilmstripVisible(false);
    h.viewer.open(album.photo(1));
    return QTest::qWaitFor([&] { return h.viewer.imageReady() && h.viewer.fileCount() == 3; }, 15000);
}
bool openGrid(Harness& h, const Album& album) {
    if (!openAlbum(h, album)) return false;
    h.viewer.trigger("grid");
    return QTest::qWaitFor([&] { return flag(h, "gridMode") && h.fileGrid.count() == Album::rows && !h.fileGrid.loading(); }, 15000);
}
int rowOf(Harness& h, const QString& path) { return h.fileGrid.rowOf(path); }
QString clipboardText() { return QGuiApplication::clipboard()->text(); }
void setClipboardText(const QString& text) { QGuiApplication::clipboard()->setText(text); }
// The tree open on the album (its root row), with keys on the row of the album's "sub" folder.
bool focusSubRow(Harness& h, const Album& album) {
    h.viewer.setTreeVisible(true);
    auto* list = named(h.window, "folderList");
    if (!list || !activate(h)) return false;
    int row = -1;
    if (!QTest::qWaitFor([&] {
            if (h.folders.count() == 1) h.folders.expand(0);
            for (row = 0; row < h.folders.count(); ++row)
                if (QDir::cleanPath(h.folders.rowInfo(row).value("path").toString()) == album.album() + "/sub") return true;
            return false;
        }, 15000)) return false;
    list->forceActiveFocus(Qt::TabFocusReason);
    list->setProperty("currentIndex", row);
    return QTest::qWaitFor([&] { return list->hasActiveFocus(); }, 10000);
}
QString normal(const QVariant& path) { return QDir::cleanPath(path.toString()); }
} // namespace

// Reads the loupe at the moment a watched signal first fires.
class LoupeProbe final : public QObject {
    Q_OBJECT
public:
    explicit LoupeProbe(ImageCanvas* watched) : canvas(watched) {}
    int hits{};
    bool loupeAtFirstHit{};
public slots:
    void hit() { if (hits++ == 0) loupeAtFirstHit = canvas->loupeActive(); }
private:
    ImageCanvas* canvas;
};

class CommandHandlersUiTests final : public QObject {
    Q_OBJECT
    QTemporaryDir* config{};
private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        for (const char* face : {"Geist-Light", "Geist-Regular", "Geist-Medium", "Geist-SemiBold", "GeistMono-Regular"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/geist/%1.ttf").arg(QLatin1String(face)));
        qmlRegisterType<ImageCanvas>("Glancelot", 1, 0, "ImageCanvas");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")), "Glancelot", 1, 0, "Theme");
        previousHandler = qInstallMessageHandler(captureQmlWarnings);
    }
    void cleanupTestCase() { qInstallMessageHandler(previousHandler); }
    void init() { config = new QTemporaryDir; qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8()); takeQmlWarnings(); }
    void cleanup() {
        qunsetenv("GLANCELOT_CONFIG_DIR"); delete config; config = nullptr;
        const QStringList warnings = takeQmlWarnings();
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    // ---- PhotoGrid
    void gridViewCommandsStepOnce() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openGrid(h, album));
        auto* grid = h.object("photoGrid"); QVERIFY(grid);
        const bool details = grid->property("details").toBool();
        FIRE(h, "view-details"); QCOMPARE(grid->property("details").toBool(), !details);
        FIRE(h, "view-details"); QCOMPARE(grid->property("details").toBool(), details);
        QCOMPARE(grid->property("bucketIndex").toInt(), 2);
        FIRE(h, "grid-zoom-in"); QCOMPARE(grid->property("bucketIndex").toInt(), 3);
        FIRE(h, "grid-zoom-reset"); QCOMPARE(grid->property("bucketIndex").toInt(), 2);
        FIRE(h, "grid-zoom-out"); QCOMPARE(grid->property("bucketIndex").toInt(), 1);
    }
    void gridSelectionCommandsApplyOnce() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openGrid(h, album));
        FIRE(h, "select-all"); QCOMPARE(h.fileGrid.selectedCount(), Album::rows);
        FIRE(h, "clear-selection"); QCOMPARE(h.fileGrid.selectedCount(), 0);
        h.fileGrid.select(rowOf(h, album.photo(2)));
        FIRE(h, "invert-selection"); QCOMPARE(h.fileGrid.selectedCount(), Album::rows - 1); // twice would restore the one
        h.fileGrid.clearSelection();
        FIRE(h, "grid-select-all"); QCOMPARE(h.fileGrid.selectedCount(), Album::rows);
    }
    // Outside the grid (preview only, grid pane hidden) the model releases its rows but keeps its selection keys: the zoom
    // and clear commands keep their guard, so nothing changes until the grid comes back.
    void gridCommandsKeepTheirPaneGuard() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openGrid(h, album));
        auto* grid = h.object("photoGrid"); QVERIFY(grid);
        h.fileGrid.select(rowOf(h, album.photo(2)));
        h.fileGrid.select(rowOf(h, album.photo(3)), DirectoryEntryModel::Toggle);
        grid->setProperty("bucketIndex", 3);
        h.viewer.trigger("grid"); QTRY_VERIFY_WITH_TIMEOUT(!flag(h, "gridMode"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!flag(h, "gridScope") && !grid->property("visible").toBool(), 10000);
        FIRE(h, "clear-selection");
        FIRE(h, "grid-zoom-in"); QCOMPARE(grid->property("bucketIndex").toInt(), 3);
        FIRE(h, "grid-zoom-out"); QCOMPARE(grid->property("bucketIndex").toInt(), 3);
        FIRE(h, "grid-zoom-reset"); QCOMPARE(grid->property("bucketIndex").toInt(), 3);
        h.viewer.trigger("grid");
        QTRY_VERIFY_WITH_TIMEOUT(flag(h, "gridMode") && h.fileGrid.count() == Album::rows && !h.fileGrid.loading(), 10000);
        // Two, not one: entering the grid with nothing selected selects the viewed photo, which would hide a cleared selection.
        QTRY_COMPARE_WITH_TIMEOUT(h.fileGrid.selectedCount(), 2, 10000);
    }
    void gridOpenAndUpNavigate() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openGrid(h, album));
        h.fileGrid.select(rowOf(h, album.photo(2)));
        FIRE(h, "grid-open");
        QTRY_COMPARE_WITH_TIMEOUT(normal(h.viewer.filePath()), album.photo(2), 10000);
        h.viewer.trigger("view-grid"); QTRY_VERIFY_WITH_TIMEOUT(h.object("photoGrid")->property("visible").toBool(), 10000);
        FIRE(h, "grid-up");
        QTRY_COMPARE_WITH_TIMEOUT(normal(h.viewer.directory()), album.rootPath(), 10000);
    }
    void copyPathAndFileUseTheGridSelection() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openGrid(h, album));
        h.fileGrid.select(rowOf(h, album.photo(2)));
        h.fileGrid.select(rowOf(h, album.photo(3)), DirectoryEntryModel::Toggle);
        QCOMPARE(h.fileGrid.selectedCount(), 2);
        setClipboardText("sentinel");
        FIRE(h, "copy-path");
        QTRY_VERIFY_WITH_TIMEOUT(clipboardText().contains("p2.png") && clipboardText().contains("p3.png"), 10000);
        QVERIFY(!clipboardText().contains("p1.png"));
        setClipboardText("sentinel");
        FIRE(h, "copy-file");
        QTRY_COMPARE_WITH_TIMEOUT(QGuiApplication::clipboard()->mimeData()->urls().size(), 2, 10000);
    }
    void copyPathFallsBackToTheOpenPhoto() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        setClipboardText("sentinel");
        FIRE(h, "copy-path");
        QTRY_VERIFY_WITH_TIMEOUT(clipboardText().contains("p1.png"), 10000);
    }
    // With nothing open these report instead of starting a real file manager or terminal.
    void revealAndTerminalReachTheController() {
        Harness h; QVERIFY(h.window);
        FIRE(h, "reveal-in-file-manager");
        QCOMPARE(h.viewer.status(), QString("No photo or folder is open to reveal."));
        FIRE(h, "open-terminal");
        QCOMPARE(h.viewer.status(), QString("No folder is open to open a terminal."));
    }

    // ---- FolderTree
    void treeOpenFolderOpensTheCursorRow() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        QVERIFY(focusSubRow(h, album));
        FIRE(h, "tree-open-folder");
        QTRY_COMPARE_WITH_TIMEOUT(normal(h.viewer.directory()), album.album() + "/sub", 10000);
    }
    void focusPathShowsTheTreeAndFocusesTheAddress() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(false);
        FIRE(h, "focus-path");
        QVERIFY(h.viewer.treeVisible());
        auto* address = named(h.window, "folderAddress"); QVERIFY(address);
        QTRY_VERIFY_WITH_TIMEOUT(address->hasActiveFocus(), 10000);
    }
    void copyFolderPathCopiesTheDirectory() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        setClipboardText("sentinel");
        FIRE(h, "copy-folder-path"); // nothing open: nothing copied
        QCOMPARE(clipboardText(), QString("sentinel"));
        QVERIFY(openAlbum(h, album));
        FIRE(h, "copy-folder-path");
        QTRY_COMPARE_WITH_TIMEOUT(normal(clipboardText()), album.album(), 10000);
    }
    void pastePathNavigatesToTheClipboardFolder() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(false);
        setClipboardText(album.other());
        FIRE(h, "paste-path");
        QVERIFY(h.viewer.treeVisible());
        QTRY_COMPARE_WITH_TIMEOUT(normal(h.viewer.directory()), album.other(), 10000);
    }

    // ---- DeleteFlow. Requests stop at the confirmation; nothing here is confirmed.
    void deleteCommandsExplainWhenNothingIsOpen() {
        Harness h; QVERIFY(h.window);
        auto* flow = h.object("deleteFlow"); QVERIFY(flow);
        const auto notice = [&] { return flow->property("noticeText").toString(); };
        FIRE(h, "delete-current-file"); QVERIFY2(notice().startsWith("No photo is currently open"), qPrintable(notice()));
        FIRE(h, "delete-current-folder"); QVERIFY2(notice().startsWith("No folder is currently open"), qPrintable(notice()));
        FIRE(h, "delete-selection"); QVERIFY2(notice().startsWith("Select items in the grid"), qPrintable(notice()));
    }
    void deleteCommandsRequestConfirmation() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        auto* flow = h.object("deleteFlow"); QVERIFY(flow);
        const auto closeRequest = [&] {
            QMetaObject::invokeMethod(flow, "closeConfirm");
            return QTest::qWaitFor([&] { return !flag(h, "modalOpen") && flow->property("phase").toString().isEmpty(); }, 10000);
        };
        FIRE(h, "delete-current-file");
        QCOMPARE(flow->property("requestedCount").toInt(), 1);
        QCOMPARE(flow->property("requestFolder").toString(), h.viewer.directory());
        QVERIFY(flow->property("modal").toBool());
        QVERIFY(closeRequest());
        FIRE(h, "delete-current-folder");
        QCOMPARE(flow->property("requestedCount").toInt(), 1);
        QCOMPARE(flow->property("requestFolder").toString(), QString());
        QVERIFY(flow->property("modal").toBool());
        QVERIFY(closeRequest());
        FIRE(h, "delete-selection"); // no grid: the open photo
        QCOMPARE(flow->property("requestedCount").toInt(), 1);
        QCOMPARE(flow->property("requestFolder").toString(), h.viewer.directory());
        QVERIFY(closeRequest());
        QVERIFY(QFile::exists(album.photo(1)));
    }
    void deleteSelectionUsesTheGridSelection() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openGrid(h, album));
        h.fileGrid.select(rowOf(h, album.photo(2)));
        h.fileGrid.select(rowOf(h, album.photo(3)), DirectoryEntryModel::Toggle);
        auto* flow = h.object("deleteFlow"); QVERIFY(flow);
        FIRE(h, "delete-selection");
        QCOMPARE(flow->property("requestedCount").toInt(), 2);
        QCOMPARE(flow->property("requestFolder").toString(), h.fileGrid.directory());
        QMetaObject::invokeMethod(flow, "closeConfirm");
        QTRY_VERIFY_WITH_TIMEOUT(!flag(h, "modalOpen"), 10000);
    }
    void deleteSelectionUsesTheFocusedTreeFolder() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        QVERIFY(focusSubRow(h, album));
        auto* flow = h.object("deleteFlow"); QVERIFY(flow);
        FIRE(h, "delete-selection");
        QCOMPARE(flow->property("requestedCount").toInt(), 1);
        QCOMPARE(flow->property("requestFolder").toString(), QString()); // the tree's request names no grid folder
        QMetaObject::invokeMethod(flow, "closeConfirm");
        QTRY_VERIFY_WITH_TIMEOUT(!flag(h, "modalOpen"), 10000);
    }
    void deleteSelectionShowsTheBlockedReason() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        h.viewer.setTreeVisible(true);
        auto* list = named(h.window, "folderList"); QVERIFY(list);
        QTRY_VERIFY_WITH_TIMEOUT(h.folders.count() > 0, 10000);
        list->setProperty("currentIndex", 0); // a drive or filesystem root
        h.window->setProperty("focusRegion", "tree");
        auto* flow = h.object("deleteFlow"); QVERIFY(flow);
        FIRE(h, "delete-selection");
        QVERIFY2(flow->property("noticeText").toString().contains("can't be moved"), qPrintable(flow->property("noticeText").toString()));
        QVERIFY(!flow->property("modal").toBool());
    }

    // ---- RenameFlow. The dialogs open on the target; nothing is renamed or created.
    void renameTargetsTheOpenPhoto() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        auto* flow = h.object("renameFlow"); QVERIFY(flow);
        FIRE(h, "rename");
        QCOMPARE(flow->property("renameTargetPath").toString(), album.photo(1));
        QVERIFY(flow->property("modal").toBool());
    }
    void renameTargetsTheGridCursor() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openGrid(h, album));
        h.fileGrid.select(rowOf(h, album.photo(3)));
        auto* flow = h.object("renameFlow"); QVERIFY(flow);
        FIRE(h, "rename");
        QCOMPARE(flow->property("renameTargetPath").toString(), album.photo(3));
    }
    void renameTargetsTheFocusedTreeFolder() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        QVERIFY(focusSubRow(h, album));
        auto* flow = h.object("renameFlow"); QVERIFY(flow);
        h.window->setProperty("focusRegion", "tree");
        FIRE(h, "rename");
        QCOMPARE(normal(flow->property("renameTargetPath")), album.album() + "/sub");
    }
    void newFolderTargetsTheCurrentDirectory() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        auto* flow = h.object("renameFlow"); QVERIFY(flow);
        FIRE(h, "new-folder");
        QCOMPARE(flow->property("newFolderParentDir").toString(), album.album());
        QVERIFY(flow->property("modal").toBool());
    }
    void newFolderTargetsTheFocusedTreeFolder() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        QVERIFY(focusSubRow(h, album));
        auto* flow = h.object("renameFlow"); QVERIFY(flow);
        FIRE(h, "new-folder");
        QCOMPARE(normal(flow->property("newFolderParentDir")), album.album() + "/sub");
    }

    // ---- TransferHud
    void cutAndCopyMarkTheSelection() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openGrid(h, album));
        auto* service = h.viewer.copyMoveService(); QVERIFY(service);
        h.fileGrid.select(rowOf(h, album.photo(2)));
        FIRE(h, "cut-selection");
        QVERIFY(service->hasClipboardTargets());
        QCOMPARE(service->clipboardMode(), QString("cut"));
        FIRE(h, "copy-selection");
        QVERIFY(service->hasClipboardTargets());
        QCOMPARE(service->clipboardMode(), QString("copy"));
        service->clearClipboard();
    }
    void copyMarksTheOpenPhotoWithoutASelection() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        auto* service = h.viewer.copyMoveService(); QVERIFY(service);
        FIRE(h, "copy-selection");
        QVERIFY(service->hasClipboardTargets());
        QCOMPARE(service->clipboardMode(), QString("copy"));
        service->clearClipboard();
    }
    void pasteCopiesIntoTheCurrentFolder() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        auto* service = h.viewer.copyMoveService(); QVERIFY(service);
        FIRE(h, "copy-selection");
        h.viewer.navigateDirectory(album.other());
        QTRY_COMPARE_WITH_TIMEOUT(normal(h.viewer.directory()), album.other(), 10000);
        QSignalSpy finished(service, &glancelot::CopyMoveService::batchFinished);
        FIRE(h, "paste");
        QTRY_VERIFY_WITH_TIMEOUT(finished.size() == 1, 10000);
        QVERIFY(QFile::exists(album.other() + "/p1.png"));
        QVERIFY(QFile::exists(album.photo(1)));
        QTRY_VERIFY_WITH_TIMEOUT(!service->active(), 10000);
    }
    void pasteCopiesIntoTheFocusedTreeFolder() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        auto* service = h.viewer.copyMoveService(); QVERIFY(service);
        FIRE(h, "copy-selection");
        QVERIFY(focusSubRow(h, album));
        QSignalSpy finished(service, &glancelot::CopyMoveService::batchFinished);
        FIRE(h, "paste");
        QTRY_VERIFY_WITH_TIMEOUT(finished.size() == 1, 10000);
        QVERIFY(QFile::exists(album.album() + "/sub/p1.png"));
        QTRY_VERIFY_WITH_TIMEOUT(!service->active(), 10000);
    }

    // ---- Main.qml ends the loupe before a component's handler acts, as it did when they were one chain. This depends on
    // Main's viewCommand connection being declared last in Main.qml (QML connects in reverse declaration order). Each row
    // watches a signal its handler's first effect emits synchronously, and reads the canvas at that moment.
    void loupeEndsBeforeAMovedHandlerActs_data() {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QByteArray>("source"); // an objectName, or "viewer" or "service"
        QTest::addColumn<QByteArray>("signal");
        QTest::newRow("PhotoGrid") << QString("view-details") << QByteArray("photoGrid") << QByteArray(SIGNAL(detailsChanged()));
        QTest::newRow("FolderTree") << QString("focus-path") << QByteArray("viewer") << QByteArray(SIGNAL(stateChanged()));
        QTest::newRow("TransferHud") << QString("copy-selection") << QByteArray("service") << QByteArray(SIGNAL(clipboardChanged()));
        QTest::newRow("DeleteFlow") << QString("delete-current-file") << QByteArray("deleteFlow") << QByteArray(SIGNAL(requestedCountChanged()));
        QTest::newRow("RenameFlow") << QString("rename") << QByteArray("renameFlow") << QByteArray(SIGNAL(renameTargetPathChanged()));
        QTest::newRow("exportFlow") << QString("export-selection") << QByteArray("exportFlow") << QByteArray(SIGNAL(activeChanged()));
    }
    void loupeEndsBeforeAMovedHandlerActs() {
        QFETCH(QString, id); QFETCH(QByteArray, source); QFETCH(QByteArray, signal);
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        h.viewer.setTreeVisible(false);
        QObject* sender = source == "viewer" ? static_cast<QObject*>(&h.viewer)
            : source == "service" ? static_cast<QObject*>(h.viewer.copyMoveService()) : h.object(source.constData());
        QVERIFY(sender);
        auto* canvas = h.canvas(); QVERIFY(canvas);
        canvas->beginLoupe(20, 20);
        QVERIFY(canvas->loupeActive());
        LoupeProbe probe(canvas);
        QVERIFY(QObject::connect(sender, signal.constData(), &probe, SLOT(hit())));
        QVERIFY(fire(h, id));
        QVERIFY2(probe.hits > 0, "the handler's effect never fired");
        QVERIFY2(!probe.loupeAtFirstHit, "the handler acted before Main.qml ended the loupe");
        QVERIFY(!canvas->loupeActive());
    }

    // ---- Main.qml still ends the loupe for a command another component handles.
    void aMovedCommandStillEndsTheLoupe() {
        Album album; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openAlbum(h, album));
        auto* canvas = h.canvas(); QVERIFY(canvas);
        for (const char* id : {"view-details", "copy-folder-path", "cut-selection", "delete-current-file"}) {
            canvas->beginLoupe(20, 20);
            QVERIFY2(canvas->loupeActive(), id);
            FIRE(h, id);
            QVERIFY2(!canvas->loupeActive(), id);
        }
        h.viewer.copyMoveService()->clearClipboard();
        QMetaObject::invokeMethod(h.object("deleteFlow"), "closeConfirm");
        QTRY_VERIFY_WITH_TIMEOUT(!flag(h, "modalOpen"), 10000);
    }
};

QTEST_MAIN(CommandHandlersUiTests)
#include "command_handlers_ui_tests.moc"
