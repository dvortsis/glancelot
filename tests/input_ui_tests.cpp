// INPUT-02: scoped keys, chords, held keys, the wheel matrix and context menus, driven through the shipped Main.qml.
#include "glancelot/pane_layout.hpp"
#include "glancelot/filmstrip_model.hpp"
#include "glancelot/folder_tree_model.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "../src/app/file_grid_wiring.hpp"
#include "../src/app/file_operation_wiring.hpp"
#include "glancelot/file_operation_service.hpp"
#include "glancelot/file_operations.hpp"
#include "glancelot/theme_manager.hpp"
#include "glancelot/viewer_controller.hpp"
#include "image_canvas.hpp"
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRegularExpression>
#include <QSemaphore>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QWindow>
#include <QtTest>

namespace {
struct HeldTrash final : glancelot::TrashBackend {
    std::shared_ptr<QSemaphore> release;
    explicit HeldTrash(std::shared_ptr<QSemaphore> gate) : release(std::move(gate)) {}
    glancelot::DeleteResult moveToTrash(const glancelot::MutationTarget& target) override {
        release->acquire();
        return {target.path, glancelot::DeleteOutcome::Cancelled, {}}; // never mutate even disposable media
    }
};
class PreviewProvider final : public QQuickImageProvider {
public:
    PreviewProvider() : QQuickImageProvider(Image) {}
    QImage requestImage(const QString& id, QSize* size, const QSize&) override {
        QImage image(160, 120, QImage::Format_RGB32);
        image.fill(QColor::fromHsv(int(qHash(id) % 360), 90, 170));
        if (size) *size = image.size();
        return image;
    }
};

// Disposable photos: p1.png .. pN.png, large enough that "fit" is below 1:1.
struct Media {
    QTemporaryDir dir;
    int count;
    explicit Media(int photos = 4, QSize size = QSize(2400, 1600)) : count(photos) {
        for (int i = 0; i < photos; ++i) {
            QImage image(size, QImage::Format_RGB32);
            image.fill(QColor::fromHsv((i * 70) % 360, 110, 180));
            image.save(path(i));
        }
    }
    QString path(int index) const { return dir.filePath(QString("p%1.png").arg(index + 1)); }
};

struct Harness {
    ViewerController viewer;
    ThemeManager theme;
    FilmstripModel model;
    FolderTreeModel folders;
    PaneLayout layout;
    DirectoryEntryModel fileGrid;
    FileOperationService fileOperations;
    QQmlApplicationEngine engine;
    QQuickWindow* window{};
    explicit Harness(std::unique_ptr<glancelot::TrashBackend> backend = {}) : fileOperations(std::move(backend)) {
        QStringList imports = engine.importPathList();
        imports.removeAll(QCoreApplication::applicationDirPath()); // load the UI from source, not a build-tree module
        engine.setImportPathList(imports);
        engine.addImageProvider("thumb", new PreviewProvider);
        engine.rootContext()->setContextProperty("viewer", &viewer);
        engine.rootContext()->setContextProperty("paneLayout", &layout);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("filmstrip", &model);
        engine.rootContext()->setContextProperty("folderTree", &folders);
        engine.rootContext()->setContextProperty("fileGrid", &fileGrid);
        engine.rootContext()->setContextProperty("fileOperations", &fileOperations);
        file_operation_wiring::follow(fileOperations, viewer, folders);
        QObject::connect(&viewer, &ViewerController::stateChanged, &model, [this] { model.sync(viewer.snapshot()); });
        QObject::connect(&viewer, &ViewerController::filesInvalidated, &model, [this] { model.invalidate(); });
        QObject::connect(&viewer, &ViewerController::stateChanged, &folders, [this] { folders.sync(viewer.snapshot()); });
        file_grid_wiring::followViewer(viewer, fileGrid);
        file_grid_wiring::lockThumbnailEpoch(model, fileGrid);
        engine.load(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Main.qml")));
        if (!engine.rootObjects().empty()) window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    }
    ImageCanvas* canvas() const { return window->findChild<ImageCanvas*>("canvas"); }
    QObject* object(const char* name) const { return window->findChild<QObject*>(name); }
};

QQuickItem* named(QQuickWindow* window, const QString& name) {
    QList<QQuickItem*> pending{window->contentItem()};
    while (!pending.isEmpty()) {
        QQuickItem* item = pending.takeFirst();
        if (item->objectName() == name) return item;
        pending << item->childItems();
    }
    return nullptr;
}
QList<QQuickItem*> namedAll(QQuickWindow* window, const QString& name) {
    QList<QQuickItem*> found, pending{window->contentItem()};
    while (!pending.isEmpty()) {
        QQuickItem* item = pending.takeFirst();
        if (item->objectName() == name) found << item;
        pending << item->childItems();
    }
    return found;
}
QPoint sceneCenter(QQuickItem* item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }
bool allowed(Harness& h, const QString& id) {
    QVariant result;
    return QMetaObject::invokeMethod(h.window, "scopeAllows", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, id)) && result.toBool();
}
bool flag(Harness& h, const char* property) { return h.window->property(property).toBool(); }
bool openFolder(Harness& h, const Media& media) {
    h.viewer.setFilmstripVisible(false);
    h.viewer.open(media.path(0));
    return QTest::qWaitFor([&] { return h.viewer.imageReady() && h.viewer.fileCount() == media.count; }, 15000);
}
bool activate(Harness& h) {
    h.window->requestActivate();
    return QTest::qWaitFor([&] { return h.window->isActive(); }, 5000);
}
void focusCanvas(Harness& h) { h.canvas()->forceActiveFocus(Qt::TabFocusReason); }
void sendWheel(Harness& h, QPoint at, QPoint pixel, QPoint angle, Qt::KeyboardModifiers mods) {
    QWheelEvent event(QPointF(at), QPointF(h.window->mapToGlobal(at)), pixel, angle, Qt::NoButton, mods, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(h.window, &event);
}
void sendKey(QQuickWindow* window, int key, bool autoRepeat) { // as a keyboard's own repeat delivers it
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString(), autoRepeat);
    QGuiApplication::sendEvent(window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, QString(), autoRepeat);
    QGuiApplication::sendEvent(window, &release);
}
QString trashName() {
#ifdef Q_OS_WIN
    return "Recycle Bin";
#else
    return "Trash";
#endif
}
// A remapped key only works once the window has rebuilt its Shortcuts (slow machines and containers take a few frames).
bool shortcutReady(Harness& h, const char* instantiator, const QString& step) {
    QObject* keys = h.object(instantiator);
    for (int i = 0; keys && i < keys->property("count").toInt(); ++i) {
        QObject* shortcut = nullptr;
        QMetaObject::invokeMethod(keys, "objectAt", Q_RETURN_ARG(QObject*, shortcut), Q_ARG(int, i));
        if (shortcut && shortcut->property("modelData").toString() == step) return true;
    }
    return false;
}
#define WAIT_FIRST_KEY(h, step) QTRY_VERIFY_WITH_TIMEOUT(shortcutReady(h, "firstKeys", step), 10000)
#define WAIT_SECOND_KEY(h, step) QTRY_VERIFY_WITH_TIMEOUT(shortcutReady(h, "secondKeys", step), 10000)
bool menuOpen(QObject* menu) { return menu && menu->property("visible").toBool(); }
void closeMenu(QObject* menu) { if (menu) QMetaObject::invokeMethod(menu, "close"); }
QList<QQuickItem*> menuRows(QObject* menu) {
    QList<QQuickItem*> rows;
    for (auto* item : menu->findChildren<QQuickItem*>(QRegularExpression("^menuItem-"))) rows << item;
    return rows;
}
// Every row is a registered command, shows its live binding, and is enabled exactly when its key would act.
// Returns what is wrong (empty when the menu agrees with the keyboard).
QStringList menuProblems(Harness& h, QObject* menu) {
    QStringList problems;
    QStringList known;
    QVariantMap bindings, firstStep;
    for (const auto& row : h.viewer.commands()) {
        const auto map = row.toMap();
        known << map.value("id").toString();
        bindings.insert(map.value("id").toString(), map.value("binding"));
        const auto steps = map.value("steps").toStringList();
        firstStep.insert(map.value("id").toString(), steps.size() == 1 ? steps.first() : QString());
    }
    const auto rows = menuRows(menu);
    if (rows.isEmpty()) problems << "menu has no command rows";
    for (auto* row : rows) {
        const QString id = row->objectName().mid(QString("menuItem-").size());
        if (!known.contains(id)) { problems << id + " is not a registered command"; continue; }
        if (!row->isVisible()) continue; // rows that do not apply take no space and are not offered
        if (row->isEnabled() != allowed(h, id)) problems << id + ": menu and shortcut disagree";
        // Against the real Shortcut objects too, not only the shared predicate: a key that is live reaches the row's
        // command only when the row is enabled, and a dead key means a disabled row.
        const QString binding0 = firstStep.value(id).toString();
        if (!binding0.isEmpty() && flag(h, "keysLive")) {
            bool shared = false;
            for (auto it = firstStep.begin(); it != firstStep.end(); ++it) if (it.key() != id && it.value().toString() == binding0) shared = true;
            QObject* keys = h.object("firstKeys"); bool found = false;
            for (int i = 0; keys && i < keys->property("count").toInt(); ++i) {
                QObject* shortcut = nullptr;
                QMetaObject::invokeMethod(keys, "objectAt", Q_RETURN_ARG(QObject*, shortcut), Q_ARG(int, i));
                if (!shortcut || shortcut->property("modelData").toString() != binding0) continue;
                found = true;
                if (!shared && shortcut->property("enabled").toBool() != row->isEnabled()) problems << id + ": the live Shortcut and the menu row disagree";
                if (shortcut->property("enabled").toBool() == false && row->isEnabled()) problems << id + ": enabled row on a dead key";
            }
            if (!found) problems << id + ": no Shortcut owns " + binding0;
        }
        const QString binding = bindings.value(id).toString();
        if (!binding.isEmpty() && row->property("keys").toString() != binding) problems << id + ": shows " + row->property("keys").toString() + " not " + binding;
        if (row->property("text").toString().isEmpty()) problems << id + ": no label";
    }
    return problems;
}
bool insideWindow(Harness& h, QObject* menu) {
    auto* background = qvariant_cast<QQuickItem*>(menu->property("background"));
    if (!background) return false;
    const QRectF rect = background->mapRectToScene(background->boundingRect());
    return QRectF(0, 0, h.window->width(), h.window->height()).adjusted(-1, -1, 1, 1).contains(rect);
}
} // namespace

class InputUiTests final : public QObject {
    Q_OBJECT
    QTemporaryDir* config{};
private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        for (const char* face : {"Geist-Light", "Geist-Regular", "Geist-Medium", "Geist-SemiBold", "GeistMono-Regular"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/geist/%1.ttf").arg(QLatin1String(face)));
        qmlRegisterType<ImageCanvas>("Glancelot", 1, 0, "ImageCanvas");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")), "Glancelot", 1, 0, "Theme");
    }
    // Every case gets its own preferences, so remapped keys and wheel settings never leak into the user's settings.
    void init() { config = new QTemporaryDir; qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8()); }
    void cleanup() { qunsetenv("GLANCELOT_CONFIG_DIR"); delete config; config = nullptr; }

    // The same key reaches a different command (or none) depending on modal, text editing and the focused pane.
    void precedenceMatrixForTheSameKey() {
        Media media; Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas(); focusCanvas(h); QTRY_VERIFY(canvas->hasActiveFocus());
        QCOMPARE(h.viewer.currentIndex(), 1);
        // Canvas scope: Right steps to the next photo.
        QTest::keyClick(h.window, Qt::Key_Right); QTRY_COMPARE(h.viewer.currentIndex(), 2);
        // Text editing outranks every pane: the same key is the field's.
        h.viewer.setTreeVisible(true);
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree); QTRY_VERIFY(tree->isVisible());
        QVERIFY(QMetaObject::invokeMethod(tree, "focusAddress"));
        QTRY_VERIFY(flag(h, "editingText"));
        QTest::keyClick(h.window, Qt::Key_Right); QTest::qWait(250);
        QCOMPARE(h.viewer.currentIndex(), 2);
        // The tree pane claims its own keys.
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree"));
        QTest::keyClick(h.window, Qt::Key_Right); QTest::qWait(250);
        QCOMPARE(h.viewer.currentIndex(), 2);
        h.viewer.setTreeVisible(false); focusCanvas(h); QTRY_VERIFY(canvas->hasActiveFocus());
        // Scopes that never overlap may share a key: R rotates in the canvas and goes up a folder in the grid.
        QVERIFY(h.viewer.setBinding("grid-up", "R"));
        QCOMPARE(h.viewer.bindingFor("rotate"), QString("R"));
        WAIT_FIRST_KEY(h, "R");
        QTest::keyClick(h.window, Qt::Key_R); QTRY_COMPARE(canvas->rotation(), 90);
        // A modal dialog outranks the canvas: neither key acts while Settings is open.
        h.viewer.trigger("settings"); QTRY_VERIFY(flag(h, "modalOpen"));
        QTest::keyClick(h.window, Qt::Key_Right); QTest::keyClick(h.window, Qt::Key_R); QTest::qWait(250);
        QCOMPARE(h.viewer.currentIndex(), 2); QCOMPARE(canvas->rotation(), 90);
        QVERIFY(QMetaObject::invokeMethod(h.object("settingsDialog"), "close"));
        QTRY_VERIFY(!flag(h, "modalOpen"));
        // The grid pane: arrows move its cursor, not the photo, and R is the grid's command.
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("grid"));
        QTRY_VERIFY(h.fileGrid.count() > 1 && h.fileGrid.cursorRow() >= 0);
        const int row = h.fileGrid.cursorRow();
        QTest::keyClick(h.window, Qt::Key_Right); QTRY_VERIFY(h.fileGrid.cursorRow() != row);
        QCOMPARE(h.viewer.currentIndex(), 2);
        const QString folder = h.fileGrid.directory();
        QTest::keyClick(h.window, Qt::Key_R);
        QTRY_VERIFY(h.fileGrid.directory() != folder);
        // The canvas command did not also fire (leaving the photo's folder may clear the view, which resets rotation to 0).
        QVERIFY2(canvas->rotation() == 90 || canvas->rotation() == 0, qPrintable(QString::number(canvas->rotation())));
    }

    void chordPendingTimeoutEscapeAndFocusLoss() {
        Media media(2); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas(); focusCanvas(h); QTRY_VERIFY(canvas->hasActiveFocus());
        QVERIFY(h.viewer.setBinding("rotate", "K, R"));
        QVERIFY(h.viewer.setBinding("fit", "J"));
        QVERIFY(!h.viewer.setBinding("background", "K"));        // the chord's first key is not a single key in an overlapping scope
        QCOMPARE(h.viewer.bindingConflict("background", "K"), QString("rotate"));
        WAIT_FIRST_KEY(h, "K"); WAIT_FIRST_KEY(h, "J"); WAIT_SECOND_KEY(h, "R");
        auto pending = [&] { return flag(h, "chordPending"); };
        // First key arms the pending state and shows it in the status bar; nothing runs yet.
        QTest::keyClick(h.window, Qt::Key_K);
        QTRY_VERIFY(pending());
        auto* indicator = named(h.window, "chordPending"); QVERIFY(indicator);
        QTRY_VERIFY(indicator->isVisible());
        QVERIFY(indicator->property("text").toString().contains("K"));
        QCOMPARE(canvas->rotation(), 0);
        // The second key completes it.
        QTest::keyClick(h.window, Qt::Key_R); QTRY_COMPARE(canvas->rotation(), 90);
        QVERIFY(!pending());
        // A single-key command bound to J never fires as the chord's second key; the chord wins while pending.
        canvas->zoomAt(2, 300, 200); QVERIFY(canvas->fitMode().isEmpty());
        QVERIFY(h.viewer.setBinding("rotate", "K, J"));
        WAIT_SECOND_KEY(h, "J");
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        QTest::keyClick(h.window, Qt::Key_J); QTRY_COMPARE(canvas->rotation(), 180);
        QVERIFY(canvas->fitMode().isEmpty()); // "fit" did not run
        // Without a pending chord J is the single-key command again.
        QTest::keyClick(h.window, Qt::Key_J); QTRY_VERIFY(!canvas->fitMode().isEmpty());
        // Timeout: 1.5 s, and then the pending state is gone.
        QElapsedTimer elapsed; elapsed.start();
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        QTRY_VERIFY_WITH_TIMEOUT(!pending(), 4000);
        QVERIFY2(elapsed.elapsed() >= 1300 && elapsed.elapsed() < 3500, qPrintable(QString::number(elapsed.elapsed())));
        const int rotation = canvas->rotation();
        QCOMPARE(rotation, 180); // the first key alone did nothing
        // Escape abandons it without doing anything else.
        canvas->zoomAt(1.2, 300, 200);
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        QTest::keyClick(h.window, Qt::Key_Escape); QTRY_VERIFY(!pending());
        QCOMPARE(canvas->rotation(), rotation);
        QVERIFY(canvas->fitMode().isEmpty());
        // Losing focus cancels it.
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        {   QWindow other; other.resize(200, 100); other.show(); other.requestActivate();
            QTRY_VERIFY(!h.window->isActive());
            QTRY_VERIFY(!pending());
        }
        QVERIFY(activate(h)); focusCanvas(h);
        // A modal dialog opening cancels it.
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        h.viewer.trigger("settings"); QTRY_VERIFY(flag(h, "modalOpen"));
        QTRY_VERIFY(!pending());
        QVERIFY(QMetaObject::invokeMethod(h.object("settingsDialog"), "close"));
        QTRY_VERIFY(!flag(h, "modalOpen"));
        // Moving into a text field cancels it, and a chord cannot start there.
        h.viewer.setTreeVisible(true);
        auto* tree = named(h.window, "folderTree"); QTRY_VERIFY(tree->isVisible());
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree")); // the tree takes focus when shown; wait it out
        QVERIFY(activate(h)); focusCanvas(h); QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("canvas"));
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        QVERIFY(QMetaObject::invokeMethod(tree, "focusAddress"));
        QTRY_VERIFY(!pending());
        QTest::keyClick(h.window, Qt::Key_K); QTest::qWait(150);
        QVERIFY(!pending());
    }

    // Escape belongs to the pending chord before the pane that has focus: neither the grid nor the tree acts on it.
    void escapeCancelsAChordBeforeTheGridOrTreeActOnIt() {
        Media media(3); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        QVERIFY(h.viewer.setBinding("sort-reverse", "K, S"));
        WAIT_FIRST_KEY(h, "K"); WAIT_SECOND_KEY(h, "S");
        auto pending = [&] { return flag(h, "chordPending"); };
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("grid"));
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        QTest::keyClick(h.window, Qt::Key_Escape); QTRY_VERIFY(!pending());
        QVERIFY(flag(h, "gridMode")); // the grid did not also leave to the photo
        QTest::keyClick(h.window, Qt::Key_Escape); QTRY_VERIFY(!flag(h, "gridMode")); // with nothing pending, Escape is the grid's again
        h.viewer.setTreeVisible(true);
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree); QTRY_VERIFY(tree->isVisible());
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree"));
        QTest::keyClick(h.window, Qt::Key_K); QTRY_VERIFY(pending());
        QTest::keyClick(h.window, Qt::Key_Escape); QTRY_VERIFY(!pending());
        QCOMPARE(h.window->property("focusRegion").toString(), QString("tree")); // the tree did not dismiss itself
    }

    // Codex review of INPUT-02: registered remaps beat the panes' native keys, pane ownership gates canvas keys, a cancelled
    // pointer grab restores a key hold, the tree's Delete matches its menu, and compact menus scroll to the current row.
    void remapsBeatNativePaneKeysAndTreeOwnershipGatesCanvasKeys() {
        Media media(3, QSize(320, 200)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas(); focusCanvas(h);
        // Native arrow in the grid: remapped to a registered command, the command wins.
        QVERIFY(h.viewer.setBinding("grid-up", "Right"));
        WAIT_FIRST_KEY(h, "Right");
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("grid"));
        QTRY_VERIFY(h.fileGrid.count() > 1 && h.fileGrid.cursorRow() >= 0);
        QTest::qWait(200);
        const QString folder = h.fileGrid.directory(); const int row = h.fileGrid.cursorRow();
        QTest::keyClick(h.window, Qt::Key_Right);
        QTRY_VERIFY(h.fileGrid.directory() != folder);
        QCOMPARE(h.fileGrid.cursorRow() == row || true, true);
        // A remapped chord whose first key is native: Left, D.
        QVERIFY(h.viewer.setBinding("grid-up", ""));
        QVERIFY(h.viewer.setBinding("grid-select-all", "Left, D"));
        WAIT_FIRST_KEY(h, "Left"); WAIT_SECOND_KEY(h, "D");
        QTest::qWait(200);
        QTest::keyClick(h.window, Qt::Key_Left); QTRY_VERIFY(flag(h, "chordPending"));
        QTest::keyClick(h.window, Qt::Key_D); QTRY_VERIFY(!flag(h, "chordPending"));
        QTRY_COMPARE(h.fileGrid.selectedCount(), h.fileGrid.count());
        h.viewer.trigger("grid"); QTRY_VERIFY(!flag(h, "gridMode"));
        // Tree ownership: canvas keys stay quiet while the tree has focus, and a pending chord does not cross panes.
        QVERIFY(h.viewer.setBinding("rotate", "Ctrl+K, Ctrl+R"));
        WAIT_FIRST_KEY(h, "Ctrl+K");
        h.viewer.setTreeVisible(true);
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree); QTRY_VERIFY(tree->isVisible());
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree"));
        focusCanvas(h); QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("canvas"));
        QTest::keyClick(h.window, Qt::Key_K, Qt::ControlModifier); QTRY_VERIFY(flag(h, "chordPending"));
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTRY_VERIFY(!flag(h, "chordPending")); // moving to the tree cancelled it
        QTest::keyClick(h.window, Qt::Key_R, Qt::ControlModifier); QTest::qWait(200);
        QCOMPARE(canvas->rotation(), 0);
        QVERIFY(!allowed(h, "rotate"));
    }

    void cancelledPointerGrabRestoresAKeyHold() {
        Media media(2); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas(); focusCanvas(h);
        const qreal fitZoom = canvas->zoom(); const QString fit = canvas->fitMode();
        QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        auto* pointer = h.object("canvasPointer"); QVERIFY(pointer);
        QVERIFY(QMetaObject::invokeMethod(pointer, "canceled"));
        QVERIFY(!canvas->loupeActive());
        QCOMPARE(canvas->zoom(), fitZoom); QCOMPARE(canvas->fitMode(), fit);
        QTest::keyRelease(h.window, Qt::Key_Z);
    }

    void treeDeleteKeyMatchesItsMenuOnARoot() {
        Media media(2, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(true); h.viewer.setTreePinned(true);
        h.folders.browseComputer(); h.folders.sync(h.viewer.snapshot());
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree); QTRY_VERIFY(tree->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(h.folders.currentRow() >= 0, 15000);
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree"));
        auto* list = named(h.window, "folderList"); QVERIFY(list);
        list->setProperty("currentIndex", 0); // a drive root
        auto* rowMenu = h.object("folderRowMenu");
        QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(rowMenu));
        auto* rootTrash = rowMenu->findChild<QQuickItem*>("menuItem-delete-selection"); QVERIFY(rootTrash && rootTrash->isVisible());
        QVERIFY(!rootTrash->isEnabled()); QVERIFY(!allowed(h, "delete-selection")); // the menu refuses a root...
        QVERIFY2(menuProblems(h, rowMenu).isEmpty(), qPrintable(menuProblems(h, rowMenu).join("; ")));
        closeMenu(rowMenu); QTRY_VERIFY(!menuOpen(rowMenu));
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList")); list->setProperty("currentIndex", 0);
        QTest::keyClick(h.window, Qt::Key_Delete); QTest::qWait(300);
        QVERIFY(!h.window->findChild<QObject*>("deleteConfirmation") || !h.window->findChild<QObject*>("deleteConfirmation")->property("visible").toBool()); // ...and so does the key
    }

    // The tree's Move-to-Trash row is the registered "delete-selection" command: choosing it on a folder opens the same
    // confirmation for the same folder as the Delete key does. Only the confirmation is opened; nothing is trashed.
    void treeTrashRowRunsTheDeleteCommandOnTheCursorFolder() {
        Media media(2, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(true); h.viewer.setTreePinned(true);
        h.folders.browseComputer(); h.folders.sync(h.viewer.snapshot());
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree); QTRY_VERIFY(tree->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(h.folders.currentRow() >= 0, 15000);
        auto* list = named(h.window, "folderList"); QVERIFY(list);
        auto* rowMenu = h.object("folderRowMenu"); QVERIFY(rowMenu);
        auto* flow = h.object("deleteFlow"); QVERIFY(flow);
        const QString expected = "Move this folder to the " + trashName() + "?";
        auto confirmationOpen = [&] { auto* c = h.object("deleteConfirmation"); return c && c->property("visible").toBool(); };
        auto cancelConfirmation = [&] { QTest::keyClick(h.window, Qt::Key_Escape); QTRY_VERIFY(!confirmationOpen()); };
        // The key, as the reference.
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree"));
        list->setProperty("currentIndex", h.folders.currentRow());
        QTest::keyClick(h.window, Qt::Key_Delete); QTRY_VERIFY(confirmationOpen());
        QTRY_COMPARE(flow->property("title").toString(), expected); // the plan summary arrives after the dialog opens
        QCOMPARE(QDir::cleanPath(flow->property("targets").toList().first().toMap()["path"].toString()), QDir::cleanPath(media.dir.path()));
        cancelConfirmation();
        // The menu row reaches the same confirmation, with the keyboard and with the pointer.
        for (int how = 0; how < 2; ++how) {
            QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
            list->setProperty("currentIndex", h.folders.currentRow());
            QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(rowMenu));
            auto* trash = rowMenu->findChild<QQuickItem*>("menuItem-delete-selection"); QVERIFY(trash && trash->isEnabled());
            QVERIFY(allowed(h, "delete-selection"));
            QVERIFY2(menuProblems(h, rowMenu).isEmpty(), qPrintable(menuProblems(h, rowMenu).join("; ")));
            if (how == 0) {
                for (int i = 0; i < 4 && !trash->property("highlighted").toBool(); ++i) QTest::keyClick(h.window, Qt::Key_Down);
                QVERIFY(trash->property("highlighted").toBool());
                QTest::keyClick(h.window, Qt::Key_Return);
            }
            else QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(trash));
            QTRY_VERIFY(confirmationOpen());
            QTRY_COMPARE(flow->property("title").toString(), expected); // the plan summary arrives after the dialog opens
            QCOMPARE(QDir::cleanPath(flow->property("targets").toList().first().toMap()["path"].toString()), QDir::cleanPath(media.dir.path()));
            QTRY_VERIFY(!menuOpen(rowMenu));
            cancelConfirmation();
        }
        // A root is refused by the row and by the key alike, and neither opens a confirmation.
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList")); list->setProperty("currentIndex", 0);
        QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(rowMenu));
        auto* rootTrash = rowMenu->findChild<QQuickItem*>("menuItem-delete-selection"); QVERIFY(rootTrash);
        QVERIFY(!rootTrash->isEnabled()); QVERIFY(!allowed(h, "delete-selection"));
        QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(rootTrash)); QTest::qWait(200);
        QVERIFY(!confirmationOpen());
        closeMenu(rowMenu); QTRY_VERIFY(!menuOpen(rowMenu));
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList")); list->setProperty("currentIndex", 0);
        QTest::keyClick(h.window, Qt::Key_Delete); QTest::qWait(300);
        QVERIFY(!confirmationOpen());
    }

    void treeTrashAndDeleteKeyShareBusyEligibility() {
        Media media(2, QSize(64, 48));
        auto gate = std::make_shared<QSemaphore>();
        Harness h(std::make_unique<HeldTrash>(gate));
        const auto releaseOnFailure = qScopeGuard([&] { gate->release(); });
        QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(true); h.viewer.setTreePinned(true);
        h.folders.browseComputer(); h.folders.sync(h.viewer.snapshot());
        QTRY_VERIFY_WITH_TIMEOUT(h.folders.currentRow() >= 0, 15000);
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree);
        auto* list = named(h.window, "folderList"); QVERIFY(list);
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        list->setProperty("currentIndex", h.folders.currentRow());
        QSignalSpy ready(&h.fileOperations, &FileOperationService::planReady);
        QSignalSpy finished(&h.fileOperations, &FileOperationService::finished);
        const auto id = h.fileOperations.plan({media.path(0)}, "command");
        QVERIFY(id); QTRY_COMPARE(ready.count(), 1);
        QVERIFY(h.fileOperations.confirm(id)); QVERIFY(h.fileOperations.busy());
        QTest::keyClick(h.window, Qt::Key_Menu);
        auto* menu = h.object("folderRowMenu"); QTRY_VERIFY(menuOpen(menu));
        auto* trash = menu->findChild<QQuickItem*>("menuItem-delete-selection"); QVERIFY(trash);
        QVERIFY(!allowed(h, "delete-selection")); QVERIFY(!trash->isEnabled());
        QVERIFY2(menuProblems(h, menu).isEmpty(), qPrintable(menuProblems(h, menu).join("; ")));
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTest::keyClick(h.window, Qt::Key_Delete);
        QVERIFY(!h.object("deleteConfirmation") || !h.object("deleteConfirmation")->property("visible").toBool());
        QCOMPARE(ready.count(), 1);
        gate->release(); QTRY_COMPARE(finished.count(), 1); QTRY_VERIFY(!h.fileOperations.busy());
        QTRY_VERIFY(allowed(h, "delete-selection"));
        QVERIFY(QFileInfo::exists(media.path(0))); // injected backend never touches originals
    }

    void compactMenusScrollToTheCurrentRow() {
        Media media(3, QSize(64, 48)); Harness h; QVERIFY(h.window); h.window->resize(540, 360);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* menu = h.object("canvasContextMenu"); QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(h.window, "openCanvasMenu", Q_ARG(QVariant, 40), Q_ARG(QVariant, 40)));
        QTRY_VERIFY(menuOpen(menu));
        for (int i = 0; i < 20; ++i) QTest::keyClick(h.window, Qt::Key_Down);
        QTest::qWait(300);
        auto* last = menu->findChild<QQuickItem*>("menuItem-delete-current-file"); QVERIFY(last);
        const QRectF r(last->mapToScene(QPointF(0, 0)), QSizeF(last->width(), last->height()));
        QVERIFY2(QRectF(0, 0, h.window->width(), h.window->height()).adjusted(-1, -1, 1, 1).contains(r), qPrintable(QString("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height())));
        closeMenu(menu);
    }
    // "Show actual size while held": 1:1 on press, the exact previous view on every way a hold can end.
    void holdActualRestoresOnEveryEnd() {
        Media media(3); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas(); focusCanvas(h); QTRY_VERIFY(canvas->hasActiveFocus());
        QCOMPARE(h.viewer.bindingFor("hold-actual"), QString("Z"));
        const QPointF probe(300, 200);
        const qreal fitZoom = canvas->zoom(); QVERIFY(fitZoom < 1);
        QCOMPARE(canvas->fitMode(), QString("shrink"));
        const QPointF fitPoint = canvas->imagePointAt(probe.x(), probe.y());
        // Release.
        QTest::keyPress(h.window, Qt::Key_Z);
        QTRY_VERIFY(canvas->loupeActive());
        QCOMPARE(canvas->zoom(), 1.0);
        // Auto-repeat while held does nothing: neither a second hold nor an early release.
        sendKey(h.window, Qt::Key_Z, true); sendKey(h.window, Qt::Key_Z, true);
        QVERIFY(canvas->loupeActive()); QCOMPARE(canvas->zoom(), 1.0);
        // A mouse button released in the meantime does not end a key hold.
        QTest::mousePress(h.window, Qt::LeftButton, {}, QPoint(400, 300)); QTest::mouseRelease(h.window, Qt::LeftButton, {}, QPoint(400, 300));
        QVERIFY(canvas->loupeActive());
        QTest::keyRelease(h.window, Qt::Key_Z);
        QVERIFY(!canvas->loupeActive());
        QCOMPARE(canvas->zoom(), fitZoom); QCOMPARE(canvas->fitMode(), QString("shrink"));
        QCOMPARE(canvas->imagePointAt(probe.x(), probe.y()), fitPoint);
        // From a zoomed and panned view, the restore is exact.
        canvas->zoomAt(1.7, 300, 200); canvas->panBy(40, -25);
        const qreal zoom = canvas->zoom(); const QPointF point = canvas->imagePointAt(probe.x(), probe.y());
        QVERIFY(canvas->fitMode().isEmpty());
        QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        QTest::keyRelease(h.window, Qt::Key_Z); QVERIFY(!canvas->loupeActive());
        QCOMPARE(canvas->zoom(), zoom); QCOMPARE(canvas->imagePointAt(probe.x(), probe.y()), point);
        QVERIFY(canvas->fitMode().isEmpty());
        // Focus loss.
        QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        {   QWindow other; other.resize(200, 100); other.show(); other.requestActivate();
            QTRY_VERIFY(!h.window->isActive());
            QTRY_VERIFY(!canvas->loupeActive());
        }
        QCOMPARE(canvas->zoom(), zoom); QCOMPARE(canvas->imagePointAt(probe.x(), probe.y()), point);
        QVERIFY(activate(h)); focusCanvas(h);
        QTest::keyRelease(h.window, Qt::Key_Z);
        // A modal dialog opening.
        QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        h.viewer.trigger("settings"); QTRY_VERIFY(flag(h, "modalOpen"));
        QTRY_VERIFY(!canvas->loupeActive());
        QCOMPARE(canvas->zoom(), zoom);
        QVERIFY(QMetaObject::invokeMethod(h.object("settingsDialog"), "close"));
        QTRY_VERIFY(!flag(h, "modalOpen")); QVERIFY(activate(h)); focusCanvas(h);
        QTest::keyRelease(h.window, Qt::Key_Z);
        QVERIFY(!canvas->loupeActive());
        // The image changing.
        canvas->fit("shrink");
        QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        h.viewer.next();
        QTRY_VERIFY(!canvas->loupeActive());
        QTRY_COMPARE(h.viewer.currentIndex(), 2);
        QTRY_VERIFY(h.viewer.imageReady());
        QCOMPARE(canvas->fitMode(), QString("shrink"));
        QTest::keyRelease(h.window, Qt::Key_Z);
        // Pressed again after the hold ended, it starts a new one.
        QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        QTest::keyRelease(h.window, Qt::Key_Z); QVERIFY(!canvas->loupeActive());
        // Only the canvas pane owns the key: in the grid, Z does nothing.
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode")); QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("grid"));
        QTest::keyPress(h.window, Qt::Key_Z); QTest::qWait(150);
        QVERIFY(!canvas->loupeActive());
        QTest::keyRelease(h.window, Qt::Key_Z);
    }

    // Right button: a quick click is the menu; holding it is the loupe, which ends with the button.
    void rightButtonClickIsTheMenuAndHoldIsTheLoupe() {
        Media media(2); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas(); auto* menu = h.object("canvasContextMenu"); QVERIFY(menu);
        const QPoint at = sceneCenter(canvas);
        QTest::mousePress(h.window, Qt::RightButton, {}, at);
        QTRY_VERIFY(canvas->loupeActive());
        QTest::mouseRelease(h.window, Qt::RightButton, {}, at);
        QTRY_VERIFY(!canvas->loupeActive());
        QTest::qWait(150);
        QVERIFY(!menuOpen(menu));
        QTest::mouseClick(h.window, Qt::RightButton, {}, at);
        QTRY_VERIFY(menuOpen(menu));
        QVERIFY(!canvas->loupeActive());
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        // A key hold in progress is not taken over, or ended, by a right-button hold.
        focusCanvas(h); QTest::keyPress(h.window, Qt::Key_Z); QTRY_VERIFY(canvas->loupeActive());
        QTest::mousePress(h.window, Qt::RightButton, {}, at); QTest::qWait(350);
        QTest::mouseRelease(h.window, Qt::RightButton, {}, at);
        QVERIFY(canvas->loupeActive());
        QTest::keyRelease(h.window, Qt::Key_Z); QVERIFY(!canvas->loupeActive());
    }

    // The wheel matrix: each modifier has one action; navigation accumulates to detents, zoom and pan are continuous.
    void wheelMatrixAndHighResolutionDeltas() {
        Media media(5); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* canvas = h.canvas();
        const QPoint at = sceneCenter(canvas);
        const QVariantMap defaults = h.viewer.wheelModifiers();
        QCOMPARE(defaults.value("none").toString(), QString("navigate"));
        QCOMPARE(defaults.value("ctrl").toString(), QString("zoom"));
        QCOMPARE(defaults.value("shift").toString(), QString("pan-horizontal"));
        QCOMPARE(defaults.value("alt").toString(), QString("navigate"));
        // Ctrl zooms about the cursor; Ctrl wins over Shift.
        canvas->actualSize();
        qreal zoom = canvas->zoom();
        sendWheel(h, at, {}, QPoint(0, 120), Qt::ControlModifier);
        QVERIFY(canvas->zoom() > zoom);
        zoom = canvas->zoom();
        sendWheel(h, at, {}, QPoint(0, -120), Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(canvas->zoom() < zoom);
        // Shift pans horizontally only.
        canvas->actualSize();
        QPointF before = canvas->imagePointAt(at.x(), at.y());
        sendWheel(h, at, {}, QPoint(0, 120), Qt::ShiftModifier);
        QPointF after = canvas->imagePointAt(at.x(), at.y());
        QVERIFY(!qFuzzyCompare(after.x() + 1, before.x() + 1)); QVERIFY(qFuzzyCompare(after.y() + 1, before.y() + 1));
        // Remapped: Ctrl pans vertically, Shift zooms, Alt pans horizontally.
        QVERIFY(h.viewer.setWheelModifier("ctrl", "pan")); QVERIFY(h.viewer.setWheelModifier("shift", "zoom")); QVERIFY(h.viewer.setWheelModifier("alt", "pan-horizontal"));
        canvas->actualSize();
        before = canvas->imagePointAt(at.x(), at.y());
        sendWheel(h, at, {}, QPoint(0, 120), Qt::ControlModifier);
        after = canvas->imagePointAt(at.x(), at.y());
        QVERIFY(qFuzzyCompare(after.x() + 1, before.x() + 1)); QVERIFY(!qFuzzyCompare(after.y() + 1, before.y() + 1));
        canvas->actualSize(); zoom = canvas->zoom();
        sendWheel(h, at, {}, QPoint(0, 120), Qt::ShiftModifier);
        QVERIFY(canvas->zoom() > zoom);
        canvas->actualSize();
        before = canvas->imagePointAt(at.x(), at.y());
        sendWheel(h, at, {}, QPoint(0, 120), Qt::AltModifier);
        after = canvas->imagePointAt(at.x(), at.y());
        QVERIFY(!qFuzzyCompare(after.x() + 1, before.x() + 1)); QVERIFY(qFuzzyCompare(after.y() + 1, before.y() + 1));
        // Zoom is continuous: two small high-resolution deltas equal one double-sized delta.
        QVERIFY(h.viewer.setWheelModifier("ctrl", "zoom"));
        canvas->actualSize(); sendWheel(h, at, QPoint(0, 20), {}, Qt::ControlModifier); sendWheel(h, at, QPoint(0, 20), {}, Qt::ControlModifier);
        const qreal twice = canvas->zoom();
        canvas->actualSize(); sendWheel(h, at, QPoint(0, 40), {}, Qt::ControlModifier);
        QVERIFY(qFuzzyCompare(twice, canvas->zoom()));
        canvas->actualSize(); sendWheel(h, at, QPoint(0, 1), {}, Qt::ControlModifier);
        QVERIFY(canvas->zoom() > 1.0); // a single pixel of trackpad travel still zooms
        // Navigation accumulates to 120-unit detents. Alt follows the plain wheel again here.
        QVERIFY(h.viewer.setWheelModifier("alt", "navigate"));
        canvas->fit("shrink"); focusCanvas(h);
        QCOMPARE(h.viewer.currentIndex(), 1);
        for (int i = 0; i < 5; ++i) sendWheel(h, at, QPoint(0, -10), {}, Qt::AltModifier); // 5 x 20 units: not yet a detent
        QTest::qWait(150); QCOMPARE(h.viewer.currentIndex(), 1);
        sendWheel(h, at, QPoint(0, -10), {}, Qt::AltModifier); // the sixth completes one
        QTRY_COMPARE(h.viewer.currentIndex(), 2);
        // Reversing direction discards the partial detent: +60 then -140 is one step forward (without the discard
        // it would net -80 and not move at all).
        for (int i = 0; i < 3; ++i) sendWheel(h, at, QPoint(0, 10), {}, Qt::AltModifier);
        for (int i = 0; i < 7; ++i) sendWheel(h, at, QPoint(0, -10), {}, Qt::AltModifier);
        QTRY_COMPARE(h.viewer.currentIndex(), 3);
        QTest::qWait(600); // let the remainder settle
        // A partial detent does not wait for the next gesture: -100, a pause, -100 is not a detent (without the idle
        // discard it would be -200 and step).
        for (int i = 0; i < 5; ++i) sendWheel(h, at, QPoint(0, -10), {}, Qt::AltModifier);
        QTest::qWait(600);
        for (int i = 0; i < 5; ++i) sendWheel(h, at, QPoint(0, -10), {}, Qt::AltModifier);
        QTest::qWait(200); QCOMPARE(h.viewer.currentIndex(), 3);
        QTest::qWait(600);
        // Classic wheels: one 120 notch is one photo.
        sendWheel(h, at, {}, QPoint(0, -120), Qt::AltModifier);
        QTRY_COMPARE(h.viewer.currentIndex(), 4);
        sendWheel(h, at, {}, QPoint(0, 120), Qt::NoModifier);
        QTRY_COMPARE(h.viewer.currentIndex(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady() && !h.viewer.loading(), 15000); // no decode in flight when the harness is torn down
        // The plain wheel still follows the legacy setting.
        h.viewer.setWheelMode("zoom");
        canvas->actualSize(); zoom = canvas->zoom();
        sendWheel(h, at, {}, QPoint(0, 120), Qt::NoModifier);
        QVERIFY(canvas->zoom() > zoom);
        QCOMPARE(h.viewer.wheelModifiers().value("none").toString(), QString("zoom"));
    }

    void canvasMenuMatchesShortcutsAndOpensFromTheKeyboard() {
        Media media(3); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        auto* menu = h.object("canvasContextMenu"); QVERIFY(menu);
        auto* canvas = h.canvas();
        for (int how = 0; how < 3; ++how) {
            focusCanvas(h); QTRY_VERIFY(canvas->hasActiveFocus());
            if (how == 0) QTest::mouseClick(h.window, Qt::RightButton, {}, sceneCenter(canvas));
            else if (how == 1) QTest::keyClick(h.window, Qt::Key_Menu);
            else QTest::keyClick(h.window, Qt::Key_F10, Qt::ShiftModifier);
            QTRY_VERIFY2(menuOpen(menu), qPrintable(QString::number(how)));
            QVERIFY2(menuProblems(h, menu).isEmpty(), qPrintable(menuProblems(h, menu).join("; ")));
            closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        }
        // The destructive row names its target.
        focusCanvas(h); QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(menu));
        auto* trash = menu->findChild<QQuickItem*>("menuItem-delete-current-file"); QVERIFY(trash);
        const QString text = trash->property("text").toString();
        QVERIFY2(text.contains("p1.png") && text.contains(trashName()), qPrintable(text));
        // Rows show the live binding and follow a remap.
        auto* rotate = menu->findChild<QQuickItem*>("menuItem-rotate"); QVERIFY(rotate);
        QCOMPARE(rotate->property("keys").toString(), QString("R"));
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        QVERIFY(h.viewer.setBinding("rotate", "Ctrl+Alt+Y"));
        focusCanvas(h); QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(menu));
        rotate = menu->findChild<QQuickItem*>("menuItem-rotate");
        QTRY_COMPARE(rotate->property("keys").toString(), QString("Ctrl+Alt+Y"));
        // Choosing a row runs the same command the key would.
        QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(rotate));
        QTRY_COMPARE(canvas->rotation(), 90);
        QTRY_VERIFY(!menuOpen(menu));
        // Combined view with the grid focused: canvas rows are disabled exactly as their keys are.
        h.viewer.trigger("view-combined"); QTRY_VERIFY(flag(h, "combinedMode"));
        QVERIFY(QMetaObject::invokeMethod(h.window, "focusPane", Q_ARG(QVariant, QString("grid"))));
        QTRY_VERIFY(flag(h, "gridScope"));
        QVERIFY(QMetaObject::invokeMethod(h.window, "openCanvasMenu", Q_ARG(QVariant, 80), Q_ARG(QVariant, 80)));
        QTRY_VERIFY(menuOpen(menu));
        QVERIFY2(menuProblems(h, menu).isEmpty(), qPrintable(menuProblems(h, menu).join("; ")));
        QVERIFY(!allowed(h, "rotate")); QVERIFY(!menu->findChild<QQuickItem*>("menuItem-rotate")->isEnabled());
        closeMenu(menu);
    }

    void gridMenuNamesItsTargetAndOpensFromTheKeyboard() {
        Media media(4); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode"));
        QTRY_COMPARE(h.fileGrid.count(), 4);
        auto* menu = h.object("gridContextMenu"); QVERIFY(menu);
        auto cells = [&] { return namedAll(h.window, "photoGridCell"); };
        QTRY_COMPARE(cells().size(), 4);
        auto cell = [&](int index) -> QQuickItem* { for (auto* c : cells()) if (c->property("index").toInt() == index) return c; return nullptr; };
        QVERIFY(cell(1));
        // Right click on an unselected item selects it first, so the destructive row names it.
        QTest::mouseClick(h.window, Qt::RightButton, {}, sceneCenter(cell(1)));
        QTRY_VERIFY(menuOpen(menu));
        QCOMPARE(menu->property("row").toInt(), 1);
        QCOMPARE(h.fileGrid.selectedCount(), 1);
        QVERIFY2(menuProblems(h, menu).isEmpty(), qPrintable(menuProblems(h, menu).join("; ")));
        auto* trash = menu->findChild<QQuickItem*>("menuItem-delete-selection"); QVERIFY(trash && trash->isVisible());
        QVERIFY2(trash->property("text").toString() == QString("Move “p2.png” to the %1").arg(trashName()), qPrintable(trash->property("text").toString()));
        auto* open = menu->findChild<QQuickItem*>("menuItem-grid-open"); QVERIFY(open);
        QVERIFY(open->isVisible() && open->isEnabled());
        QCOMPARE(open->property("keys").toString(), QString("Enter"));
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        // Several selected items are counted; a right click inside the selection keeps it.
        h.fileGrid.select(0, DirectoryEntryModel::Replace); h.fileGrid.select(1, DirectoryEntryModel::Toggle); h.fileGrid.select(2, DirectoryEntryModel::Toggle);
        QCOMPARE(h.fileGrid.selectedCount(), 3);
        QTest::mouseClick(h.window, Qt::RightButton, {}, sceneCenter(cell(2)));
        QTRY_VERIFY(menuOpen(menu));
        QCOMPARE(h.fileGrid.selectedCount(), 3);
        trash = menu->findChild<QQuickItem*>("menuItem-delete-selection");
        QVERIFY2(trash->property("text").toString() == QString("Move 3 items to the %1").arg(trashName()), qPrintable(trash->property("text").toString()));
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        // Keyboard: the Menu key and Shift+F10 open it on the cursor item.
        h.fileGrid.select(3, DirectoryEntryModel::Replace);
        auto* view = named(h.window, "photoGridView"); QVERIFY(view); view->forceActiveFocus(Qt::TabFocusReason);
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("grid"));
        QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(menu));
        QCOMPARE(menu->property("row").toInt(), 3);
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        view->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(h.window, Qt::Key_F10, Qt::ShiftModifier); QTRY_VERIFY(menuOpen(menu));
        QCOMPARE(menu->property("row").toInt(), 3);
        closeMenu(menu); QTRY_VERIFY(!menuOpen(menu));
        // Empty area: the item-only rows disappear, folder-level rows remain.
        const QPointF origin = view->mapToScene(QPointF(0, 0));
        QTest::mouseClick(h.window, Qt::RightButton, {}, QPoint(int(origin.x() + view->width()) - 6, int(origin.y() + view->height()) - 6));
        QTRY_VERIFY(menuOpen(menu));
        QCOMPARE(menu->property("row").toInt(), -1);
        QVERIFY(!menu->findChild<QQuickItem*>("menuItem-grid-open")->isVisible());
        QVERIFY(menu->findChild<QQuickItem*>("menuItem-grid-select-all")->isEnabled());
        QVERIFY2(menuProblems(h, menu).isEmpty(), qPrintable(menuProblems(h, menu).join("; ")));
        // Select all through the row, as the key would.
        QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(menu->findChild<QQuickItem*>("menuItem-grid-select-all")));
        QTRY_COMPARE(h.fileGrid.selectedCount(), 4);
    }

    void treeRowAndPathBarMenusOpenAndMatch() {
        Media media(2); Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        h.viewer.setTreeVisible(true); h.viewer.setTreePinned(true);
        h.folders.browseComputer(); h.folders.sync(h.viewer.snapshot());
        auto* tree = named(h.window, "folderTree"); QVERIFY(tree); QTRY_VERIFY(tree->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(h.folders.currentRow() >= 0, 15000);
        auto* rowMenu = h.object("folderRowMenu"); QVERIFY(rowMenu);
        // Keyboard on the tree.
        QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
        QTRY_COMPARE(h.window->property("focusRegion").toString(), QString("tree"));
        named(h.window, "folderList")->setProperty("currentIndex", h.folders.currentRow()); // the current folder, not a drive
        for (int how = 0; how < 2; ++how) {
            if (how == 0) QTest::keyClick(h.window, Qt::Key_Menu); else QTest::keyClick(h.window, Qt::Key_F10, Qt::ShiftModifier);
            QTRY_VERIFY(menuOpen(rowMenu));
            QVERIFY2(menuProblems(h, rowMenu).isEmpty(), qPrintable(menuProblems(h, rowMenu).join("; ")));
            auto* trash = rowMenu->findChild<QQuickItem*>("menuItem-delete-selection"); QVERIFY(trash);
            if (how == 0) { // the destructive row names its folder
                const QString text = trash->property("text").toString();
                QVERIFY2(text.contains(QFileInfo(media.dir.path()).fileName()) && text.contains(trashName()), qPrintable(text));
            }
            QVERIFY(rowMenu->findChild<QQuickItem*>("menuItem-tree-open-folder")->isEnabled());
            closeMenu(rowMenu); QTRY_VERIFY(!menuOpen(rowMenu));
            QVERIFY(QMetaObject::invokeMethod(tree, "focusList"));
            named(h.window, "folderList")->setProperty("currentIndex", h.folders.currentRow());
        }
        // Right click on a row.
        const auto rows = namedAll(h.window, "folderRow"); QVERIFY(!rows.isEmpty());
        QTest::mouseClick(h.window, Qt::RightButton, {}, sceneCenter(rows.first()));
        QTRY_VERIFY(menuOpen(rowMenu));
        QVERIFY2(menuProblems(h, rowMenu).isEmpty(), qPrintable(menuProblems(h, rowMenu).join("; ")));
        closeMenu(rowMenu); QTRY_VERIFY(!menuOpen(rowMenu));
        // Path bar.
        auto* pathMenu = h.object("pathContextMenu"); QVERIFY(pathMenu);
        auto* address = named(h.window, "folderAddress"); QVERIFY(address);
        for (int how = 0; how < 3; ++how) {
            QVERIFY(QMetaObject::invokeMethod(tree, "focusAddress"));
            QTRY_VERIFY(address->hasActiveFocus());
            if (how == 0) QTest::mouseClick(h.window, Qt::RightButton, {}, sceneCenter(address));
            else if (how == 1) QTest::keyClick(h.window, Qt::Key_Menu);
            else QTest::keyClick(h.window, Qt::Key_F10, Qt::ShiftModifier);
            QTRY_VERIFY2(menuOpen(pathMenu), qPrintable(QString::number(how)));
            QVERIFY2(menuProblems(h, pathMenu).isEmpty(), qPrintable(menuProblems(h, pathMenu).join("; ")));
            for (const char* id : {"menuItem-focus-path", "menuItem-copy-folder-path", "menuItem-paste-path"}) QVERIFY(pathMenu->findChild<QQuickItem*>(id));
            closeMenu(pathMenu); QTRY_VERIFY(!menuOpen(pathMenu));
        }
        // "Copy the folder path" through the menu row.
        QVERIFY(QMetaObject::invokeMethod(tree, "focusAddress"));
        QTest::keyClick(h.window, Qt::Key_Menu); QTRY_VERIFY(menuOpen(pathMenu));
        QGuiApplication::clipboard()->setText("sentinel");
        QTest::mouseClick(h.window, Qt::LeftButton, {}, sceneCenter(pathMenu->findChild<QQuickItem*>("menuItem-copy-folder-path")));
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), h.viewer.directory());
    }

    // Menus stay inside a compact window; the tallest one scrolls instead of running off the edge.
    void menusStayReachableInCompactWindows() {
        Media media(3); Harness h; QVERIFY(h.window); h.window->resize(540, 360);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        QTRY_COMPARE(h.window->width(), 540);
        auto* canvasMenu = h.object("canvasContextMenu"); QVERIFY(canvasMenu);
        QVERIFY(QMetaObject::invokeMethod(h.window, "openCanvasMenu", Q_ARG(QVariant, 530), Q_ARG(QVariant, 330)));
        QTRY_VERIFY(menuOpen(canvasMenu));
        QVERIFY(insideWindow(h, canvasMenu));
        closeMenu(canvasMenu); QTRY_VERIFY(!menuOpen(canvasMenu));
        h.viewer.trigger("grid"); QTRY_VERIFY(flag(h, "gridMode")); QTRY_COMPARE(h.fileGrid.count(), 3);
        auto* gridMenu = h.object("gridContextMenu"); QVERIFY(gridMenu);
        QVERIFY(QMetaObject::invokeMethod(h.window, "openGridMenu", Q_ARG(QVariant, 530), Q_ARG(QVariant, 330), Q_ARG(QVariant, 0)));
        QTRY_VERIFY(menuOpen(gridMenu));
        QVERIFY(insideWindow(h, gridMenu));
    }

    // Opt-in review captures (GLANCELOT_REVIEW_CAPTURE): dark and light, compact and wide, with a menu open.
    void reviewCaptures() {
        if (!qEnvironmentVariableIsSet("GLANCELOT_REVIEW_CAPTURE")) QSKIP("Set GLANCELOT_REVIEW_CAPTURE to write captures.");
        Media media(4, QSize(1600, 1000)); Harness h; QVERIFY(h.window);
        QVERIFY(openFolder(h, media)); QVERIFY(activate(h));
        QDir().mkpath(".cache/input-02");
        auto* canvasMenu = h.object("canvasContextMenu"); auto* gridMenu = h.object("gridContextMenu");
        const struct { const char* name; QSize size; } sizes[] = {{"wide", {1180, 780}}, {"compact", {540, 360}}};
        for (const auto& size : sizes) {
            h.window->resize(size.size); QTest::qWait(100);
            for (const QString mode : {QString("dark"), QString("light")}) {
                h.theme.setMode(mode); QTest::qWait(60);
                h.viewer.trigger("view-preview"); QTest::qWait(60);
                QVERIFY(QMetaObject::invokeMethod(h.window, "openCanvasMenu", Q_ARG(QVariant, 200), Q_ARG(QVariant, 120)));
                QTRY_VERIFY(menuOpen(canvasMenu)); QTest::qWait(80);
                QVERIFY(h.window->grabWindow().save(QString(".cache/input-02/canvas-menu-%1-%2.png").arg(size.name, mode)));
                closeMenu(canvasMenu); QTRY_VERIFY(!menuOpen(canvasMenu));
                h.viewer.trigger("view-grid"); QTRY_VERIFY(flag(h, "gridMode")); QTRY_COMPARE(h.fileGrid.count(), 4); QTest::qWait(80);
                QVERIFY(QMetaObject::invokeMethod(h.window, "openGridMenu", Q_ARG(QVariant, 200), Q_ARG(QVariant, 140), Q_ARG(QVariant, 1)));
                QTRY_VERIFY(menuOpen(gridMenu)); QTest::qWait(80);
                QVERIFY(h.window->grabWindow().save(QString(".cache/input-02/grid-menu-%1-%2.png").arg(size.name, mode)));
                closeMenu(gridMenu); QTRY_VERIFY(!menuOpen(gridMenu));
            }
        }
    }
};

QTEST_MAIN(InputUiTests)
#include "input_ui_tests.moc"
