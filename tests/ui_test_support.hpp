#pragma once
// Shared by the *_ui_tests: the harness that loads the shipped ui/Main.qml, and small item/input helpers.
#include "glancelot/monitor_customization.hpp"
#include "glancelot/pane_layout.hpp"
#include "glancelot/filmstrip_model.hpp"
#include "glancelot/folder_tree_model.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "../src/app/file_grid_wiring.hpp"
#include "../src/app/file_operation_wiring.hpp"
#include "../src/app/viewer_context.hpp"
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
    glancelot::MonitorCustomization monitor{viewer.displayManager(), &layout};
    DirectoryEntryModel fileGrid;
    FileOperationService fileOperations;
    FavoritesModel favorites;
    FolderHistory history;
    QQmlApplicationEngine engine;
    QQuickWindow* window{};
    explicit Harness(std::unique_ptr<glancelot::TrashBackend> backend = {}) : fileOperations(std::move(backend)) {
        QStringList imports = engine.importPathList();
        imports.removeAll(QCoreApplication::applicationDirPath()); // load the UI from source, not a build-tree module
        engine.setImportPathList(imports);
        engine.addImageProvider("thumb", new PreviewProvider);
        monitor.setSlot(1);
        installViewerContext(*engine.rootContext(), {.viewer = viewer, .paneLayout = layout, .monitorCustomization = monitor,
            .filmstrip = model, .folderTree = folders, .favoritesModel = favorites, .folderHistory = history,
            .fileGrid = fileGrid, .theme = theme, .fileOperations = fileOperations});
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
} // namespace
