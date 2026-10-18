#include "glancelot/pane_layout.hpp"
#include "glancelot/filmstrip_model.hpp"
#include "glancelot/folder_tree_model.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "glancelot/settings_file.hpp"
#include "../src/app/thumbnail_id.hpp"
#include "../src/app/file_grid_wiring.hpp"
#include <QMutex>
#include <QProcess>
#include <cmath>
#include <map>
#include <QJsonObject>
#include "glancelot/theme_manager.hpp"
#include "glancelot/viewer_controller.hpp"
#include "image_canvas.hpp"
#include <QFontDatabase>
#include <QGuiApplication>
#include <QClipboard>
#include <QQuickStyle>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QtTest>
#include <atomic>
#include <cmath>

class PreviewProvider final : public QQuickImageProvider {
public:
    PreviewProvider() : QQuickImageProvider(Image) {}
    std::atomic_int requests{0};
    std::atomic_bool succeed{false};
    QMutex mutex;
    QStringList paths; // every decoded request path, to prove non-images never reach the thumbnail pipeline
    QStringList requested() { QMutexLocker lock(&mutex); return paths; }
    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override {
        ++requests;
        const auto parts = thumbnail_id::decode(id);
        { QMutexLocker lock(&mutex); paths << (parts ? parts->path : QStringLiteral("<malformed>")); }
        if (size) *size = QSize(32, 24);
        Q_UNUSED(requested);
        // By default previews fail deliberately; each cell must remain named and activatable. Review captures use
        // generated previews instead, except for names containing "broken".
        if (!succeed || (parts && parts->path.contains("broken"))) return {};
        QImage image(160, 120, QImage::Format_RGB32);
        const uint seed = qHash(parts ? parts->path : id);
        image.fill(QColor::fromHsv(int(seed % 360), 90, 170));
        for (int y = 0; y < 120; ++y) for (int x = 0; x < 160; x += 1) if (y > 70 + int(20 * std::sin(x / 18.0 + seed))) image.setPixelColor(x, y, QColor::fromHsv(int((seed / 7) % 360), 120, 90));
        if (size) *size = image.size();
        return image;
    }
};

// Disposable folders of empty "Photo N.png" files for tests that only need a listing; shared across cases so the
// 10,000-entry fixture is generated once.
QTemporaryDir* listingFixture(int count) {
    static std::map<int, std::unique_ptr<QTemporaryDir>> fixtures;
    if (count < 0) { fixtures.clear(); return nullptr; }
    auto& slot = fixtures[count];
    if (!slot) {
        slot = std::make_unique<QTemporaryDir>();
        for (int i = 0; i < count; ++i) { QFile file(slot->filePath(QString("Photo %1.png").arg(i))); if (file.open(QIODevice::WriteOnly)) file.close(); }
    }
    return slot.get();
}
QString gridKey(const QString& path) { return file_grid_wiring::directoryKey(path); }

struct Harness {
    QTemporaryDir config;
    ViewerController viewer;
    ThemeManager theme;
    FilmstripModel model;
    FolderTreeModel folders;
    PaneLayout layout;
    DirectoryEntryModel fileGrid;
    QQmlApplicationEngine engine;
    PreviewProvider* provider = new PreviewProvider;
    QQuickWindow* window{};
    explicit Harness(bool live = false) {
        // Load the UI from source: drop the executable's directory so a build-tree "Glancelot" module cannot shadow it.
        QStringList imports = engine.importPathList();
        imports.removeAll(QCoreApplication::applicationDirPath());
        engine.setImportPathList(imports);
        engine.addImageProvider("thumb", provider);
        engine.rootContext()->setContextProperty("viewer", &viewer);
        engine.rootContext()->setContextProperty("paneLayout", &layout);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("filmstrip", &model);
        engine.rootContext()->setContextProperty("folderTree", &folders);
        engine.rootContext()->setContextProperty("fileGrid", &fileGrid);
        // Tests that inject a listing with fill() must not be overwritten by the (empty) controller snapshot.
        // Live wiring is app/main.cpp's: the same file_grid_wiring helpers, plus the filmstrip connections.
        if (live) {
            QObject::connect(&viewer, &ViewerController::stateChanged, &model, [&] { model.sync(viewer.snapshot()); });
            QObject::connect(&viewer, &ViewerController::filesInvalidated, &model, [&] { model.invalidate(); });
            file_grid_wiring::followViewer(viewer, fileGrid);
        }
        file_grid_wiring::lockThumbnailEpoch(model, fileGrid);
        engine.load(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Main.qml")));
        if (!engine.rootObjects().empty()) window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    }
    QQuickItem* grid() const { return window->findChild<QQuickItem*>("photoGrid"); }
    QQuickItem* view() const { return window->findChild<QQuickItem*>("photoGridView"); }
    void showGrid() { viewer.trigger("grid"); window->requestActivate(); }
    // Injects a real generated listing into the grid (non-live harness) with `current` as the viewed photo.
    void fill(int count, int current = 0) {
        QTemporaryDir* dir = listingFixture(count);
        fileGrid.setCurrentImage(count ? dir->filePath(QString("Photo %1.png").arg(current)) : QString{});
        fileGrid.setDirectory(dir->path());
    }
    int selected() const { return grid()->property("selectedIndex").toInt(); }
    // Delegates are visual (not QObject) children, so walk the item tree.
    QList<QQuickItem*> cells() const {
        QList<QQuickItem*> found; QList<QQuickItem*> pending{view()};
        while (!pending.isEmpty()) {
            QQuickItem* item = pending.takeFirst();
            if (item->objectName() == "photoGridCell") found << item;
            pending << item->childItems();
        }
        return found;
    }
    QQuickItem* cell(int index) const { for (auto* c : cells()) if (c->property("index").toInt() == index) return c; return nullptr; }
};

// TOPBAR-THEME-01 helpers: find a named item anywhere in the visual tree, and WCAG contrast.
static QQuickItem* namedItem(QQuickWindow* window, const QString& name) {
    QList<QQuickItem*> pending{window->contentItem()};
    while (!pending.isEmpty()) {
        QQuickItem* item = pending.takeFirst();
        if (item->objectName() == name) return item;
        pending << item->childItems();
    }
    return nullptr;
}
static double relativeLuminance(const QColor& color) {
    const auto channel = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}
static double contrastRatio(const QColor& a, const QColor& b) {
    const double x = relativeLuminance(a), y = relativeLuminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}
static QColor over(const QColor& top, const QColor& below) { // source-over compositing onto an opaque color
    const double a = top.alphaF();
    return QColor::fromRgbF(top.redF() * a + below.redF() * (1 - a), top.greenF() * a + below.greenF() * (1 - a), top.blueF() * a + below.blueF() * (1 - a));
}
static QColor itemColor(QQuickItem* item, const char* property = "color") { return item ? item->property(property).value<QColor>() : QColor(); }
static QQuickItem* buttonBackground(QQuickItem* button) { return button ? button->property("background").value<QQuickItem*>() : nullptr; }
static QQuickItem* buttonIcon(QQuickItem* button) {
    QList<QQuickItem*> pending{button};
    while (!pending.isEmpty()) {
        QQuickItem* item = pending.takeFirst();
        if (item != button && item->property("paths").isValid() && item->property("name").toString().size()) return item;
        pending << item->childItems();
    }
    return nullptr;
}

class GridUiTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic"); // the style the application uses
        for (const char* face : {"Geist-Light", "Geist-Regular", "Geist-Medium", "Geist-SemiBold", "GeistMono-Regular"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/geist/%1.ttf").arg(QLatin1String(face)));
        qmlRegisterType<ImageCanvas>("Glancelot", 1, 0, "ImageCanvas");
        // The source-loaded UI has no generated qmldir, so the token singleton is registered as the app module does.
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")), "Glancelot", 1, 0, "Theme");
    }
    void resetRetainsScrolledViewport() {
        QTemporaryDir media; QVERIFY(media.isValid());
        for (int i = 0; i < 400; ++i) {
            QFile file(media.filePath(QString("file-%1.txt").arg(i, 4, 10, QLatin1Char('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.fileGrid.setDirectory(media.path()); h.showGrid();
        QTRY_VERIFY(!h.fileGrid.loading() && h.fileGrid.count() == 400);
        QTRY_COMPARE(h.selected(), 0);
        QVERIFY(QMetaObject::invokeMethod(h.view(), "positionViewAtIndex", Q_ARG(int, 200), Q_ARG(int, 0)));
        QTest::qWait(100);
        const qreal before = h.view()->property("contentY").toReal(); QVERIFY(before > 1000);
        QSignalSpy resets(&h.fileGrid, &QAbstractItemModel::modelReset);
        for (int i = 1; i < 80; i += 2)
            QVERIFY(QFile::remove(media.filePath(QString("file-%1.txt").arg(i, 4, 10, QLatin1Char('0')))));
        h.fileGrid.refresh();
        QTRY_VERIFY(!h.fileGrid.loading() && h.fileGrid.count() == 360);
        QTest::qWait(100);
        QCOMPARE(resets.count(), 1); QCOMPARE(h.selected(), 0);
        const qreal after = h.view()->property("contentY").toReal();
        qInfo() << "reset viewport before=" << before << "after=" << after;
        QVERIFY2(after > 1000, "A background reset jumped to an offscreen cursor instead of preserving the browsed viewport");
    }
    void cleanupTestCase() { listingFixture(-1); }
    void init() {
        config = std::make_unique<QTemporaryDir>();
        QVERIFY(config->isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8());
    }
    // TOPBAR-THEME-01: every built-in variant, switched live on one window, reaches every top-bar item with no stale
    // color, and the derived interaction roles keep their contrast.
    void topBarFollowsEveryThemeVariantLive() {
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.window->setProperty("frameless", true); // the window controls are part of the bar under test
        h.window->requestActivate(); QTRY_VERIFY(h.window->isActive());
        QObject* tokens = h.engine.singletonInstance<QObject*>("Glancelot", "Theme"); QVERIFY(tokens);
        auto* bar = namedItem(h.window, "topBar"); auto* edge = namedItem(h.window, "topBarEdge");
        auto* divider = namedItem(h.window, "topBarDivider"); auto* title = namedItem(h.window, "topBarTitle");
        auto* brand = namedItem(h.window, "brandMark"); auto* settings = namedItem(h.window, "settingsButton");
        auto* grid = namedItem(h.window, "gridToggle");
        auto* minimize = namedItem(h.window, "minimizeButton"); auto* close = namedItem(h.window, "closeButton");
        QVERIFY(bar && edge && divider && title && brand && settings && grid && minimize && close);
        QTRY_VERIFY(minimize->isVisible() && close->isVisible());
        QVERIFY(!grid->isEnabled()); // no folder: the disabled state is the shared disabled opacity
        auto* marker = settings->findChild<QQuickItem*>("selectionMarker"); QVERIFY(marker);
        int variants = 0;
        for (const auto& entry : h.theme.families()) {
            const auto id = entry.toMap().value("id").toString();
            for (bool dark : {true, false}) {
                h.theme.setFamily(id); h.theme.setMode(dark ? "dark" : "light"); QCoreApplication::processEvents();
                const auto palette = h.theme.preview(id, dark);
                const auto role = [&](const char* name) { return QColor(palette.value(QLatin1String(name)).toString()); };
                const auto token = [&](const char* name) { return tokens->property(name).value<QColor>(); };
                const QString context = id + (dark ? " dark" : " light");
                QVERIFY2(itemColor(bar) == role("surface"), qPrintable(context));
                QCOMPARE(h.window->color(), role("background"));
                QCOMPARE(itemColor(edge), role("border"));
                QCOMPARE(itemColor(divider), role("border"));
                QCOMPARE(itemColor(title), role("muted")); // "No photo open" placeholder
                QCOMPARE(itemColor(settings, "foreground"), role("text"));
                QCOMPARE(itemColor(buttonIcon(minimize)), role("text"));
                QCOMPARE(itemColor(buttonIcon(close)), role("text"));
                QCOMPARE(grid->opacity(), tokens->property("disabledOpacity").toReal());
                QVERIFY2(brand->property("source").toUrl().toString().contains(dark ? "/dark/" : "/light/"), qPrintable(context));
                // Derived roles as the Theme singleton computes them (selection is translucent over the bar).
                const QColor pressed = token("pressed"), selection = over(token("selection"), role("surface"));
                QVERIFY2(contrastRatio(role("text"), pressed) >= 4.5, qPrintable(context + " text/pressed"));
                QVERIFY2(contrastRatio(role("text"), selection) >= 4.5, qPrintable(context + " text/selection"));
                QVERIFY2(contrastRatio(token("focusRing"), role("surface")) >= 3, qPrintable(context + " focus ring"));
                QVERIFY2(contrastRatio(role("primary"), selection) >= 3, qPrintable(context + " on-marker/selection"));
                // Exercise the actual shared button marker; the pressed fill must not erase its selected state.
                settings->setProperty("selected", true); settings->setProperty("down", true);
                QTRY_COMPARE(itemColor(buttonBackground(settings)), pressed);
                QVERIFY(marker->isVisible());
                QVERIFY2(contrastRatio(itemColor(marker), itemColor(buttonBackground(settings))) >= 3,
                         qPrintable(context + " on-marker/pressed"));
                settings->setProperty("down", false);
                QCOMPARE(itemColor(marker), role("primary"));
                settings->setProperty("selected", false);
                QVERIFY2(contrastRatio(role("background"), role("danger")) >= 3, qPrintable(context + " close glyph"));
                ++variants;
            }
        }
        QCOMPARE(variants, 80);
    }
    // Hover, pressed, focus and danger-hover backgrounds follow a live theme change while the state is held.
    void topBarStatesFollowLiveThemeChanges() {
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.window->setProperty("frameless", true);
        h.window->requestActivate(); QTRY_VERIFY(h.window->isActive());
        QObject* tokens = h.engine.singletonInstance<QObject*>("Glancelot", "Theme"); QVERIFY(tokens);
        const auto token = [&](const char* name) { return tokens->property(name).value<QColor>(); };
        auto* settings = namedItem(h.window, "settingsButton"); auto* close = namedItem(h.window, "closeButton");
        auto* title = namedItem(h.window, "topBarTitle");
        QVERIFY(settings && close && title); QTRY_VERIFY(close->isVisible());
        const auto centre = [](QQuickItem* item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };
        const QList<QPair<QString, bool>> variants{{"slate-ice", true}, {"ruby", false}, {"graphite", true}, {"amber", false}};
        for (const auto& [family, dark] : variants) {
            h.theme.setFamily(family); h.theme.setMode(dark ? "dark" : "light");
            QTest::mouseMove(h.window, centre(settings));
            QTRY_COMPARE(itemColor(buttonBackground(settings)), token("hover"));
            QTest::mousePress(h.window, Qt::LeftButton, Qt::NoModifier, centre(settings));
            QTRY_COMPARE(itemColor(buttonBackground(settings)), token("pressed"));
            QTest::mouseMove(h.window, centre(title));
            QTest::mouseRelease(h.window, Qt::LeftButton, Qt::NoModifier, centre(title)); // released away: no click
            QTest::mouseMove(h.window, centre(close));
            QTRY_COMPARE(itemColor(buttonBackground(close)), token("danger"));
            QCOMPARE(itemColor(buttonIcon(close)), token("background"));
            // Switch while the pointer stays on Close: the held state re-resolves to the new roles.
            h.theme.setMode(dark ? "light" : "dark");
            QTRY_COMPARE(itemColor(buttonBackground(close)), token("danger"));
            QCOMPARE(itemColor(buttonIcon(close)), token("background"));
            QTest::mouseMove(h.window, centre(title));
            QTRY_COMPARE(itemColor(buttonBackground(close)).alpha(), 0);
            QCOMPARE(itemColor(buttonIcon(close)), token("text"));
        }
        namedItem(h.window, "canvas")->forceActiveFocus(); // move focus away first so the Tab reason is recorded
        settings->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(settings->property("visualFocus").toBool());
        QObject* border = buttonBackground(settings)->property("border").value<QObject*>(); QVERIFY(border);
        QCOMPARE(border->property("color").value<QColor>(), token("focusRing"));
        h.theme.setMode(h.theme.dark() ? "light" : "dark");
        QTRY_COMPARE(border->property("color").value<QColor>(), token("focusRing"));
        QCOMPARE(border->property("width").toInt(), tokens->property("focusWidth").toInt());
    }
    // Frameless title bar: each control performs its window action; drag area double-click maximizes; the bar
    // cannot be hidden while it is the only way to move or close the window.
    void framelessWindowControlsDriveTheWindow() {
        qGuiApp->setQuitOnLastWindowClosed(false);
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780);
        h.window->requestActivate(); QTRY_VERIFY(h.window->isActive());
        auto* minimize = namedItem(h.window, "minimizeButton"); auto* maximize = namedItem(h.window, "maximizeButton");
        auto* close = namedItem(h.window, "closeButton"); auto* showFrame = namedItem(h.window, "showFrameButton");
        auto* title = namedItem(h.window, "topBarTitle");
        QVERIFY(minimize && maximize && close && showFrame && title);
        QVERIFY(!minimize->isVisible() && !maximize->isVisible() && !close->isVisible()); // the system frame owns them
        QVERIFY(!(h.window->flags() & Qt::FramelessWindowHint));
        h.window->setProperty("frameless", true);
        QTRY_VERIFY(h.window->flags() & Qt::FramelessWindowHint);
        QTRY_VERIFY(minimize->isVisible() && maximize->isVisible() && close->isVisible() && showFrame->isVisible());
        QTRY_VERIFY(minimize->x() + minimize->width() <= maximize->x() && maximize->x() + maximize->width() <= close->x()); // laid out
        // At the minimum window size every window control stays inside the bar and the window.
        auto* bar = namedItem(h.window, "topBar"); QVERIFY(bar);
        const auto rightEdge = [&](QQuickItem* item) { return item->mapToItem(bar, QPointF(item->width(), item->height())).x(); };
        h.window->resize(540, 360);
        QTRY_COMPARE(bar->width(), 540.0);
        QTRY_VERIFY(!showFrame->isVisible());
        QTRY_VERIFY(rightEdge(close) <= bar->width() - 8 && rightEdge(minimize) < rightEdge(maximize));
        QVERIFY(namedItem(h.window, "topBarTitle")->width() > 0); // the title keeps some room
        // The lock indicator joins the bar at the minimum size: it collapses to its icon so the controls still fit.
        auto* lock = namedItem(h.window, "layoutLockIndicator"); QVERIFY(lock);
        h.layout.setLocked(true);
        QTRY_VERIFY(lock->isVisible());
        QTRY_VERIFY(lock->property("iconOnly").toBool());
        QTRY_VERIFY(rightEdge(close) <= bar->width() - 8 && rightEdge(lock) <= close->mapToItem(bar, QPointF()).x());
        QVERIFY(namedItem(h.window, "topBarTitle")->width() > 0);
        h.window->resize(1180, 780);
        QTRY_VERIFY(showFrame->isVisible());
        QTRY_VERIFY(!lock->property("iconOnly").toBool()); // a wide bar names the state in words
        h.layout.setLocked(false);
        QTRY_VERIFY(!lock->isVisible());
        QTRY_VERIFY(maximize->x() + maximize->width() <= close->x());
        const auto centre = [](QQuickItem* item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };
        QCOMPARE(maximize->property("text").toString(), QString("Maximize"));
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, centre(maximize));
        QTRY_COMPARE(h.window->visibility(), QWindow::Maximized);
        QTRY_COMPARE(maximize->property("text").toString(), QString("Restore"));
        QCOMPARE(buttonIcon(maximize)->property("name").toString(), QString("restore"));
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, centre(maximize));
        QTRY_COMPARE(h.window->visibility(), QWindow::Windowed);
        // Empty title-bar space (the title label passes pointer input through) toggles maximize on double-click.
        QTest::mouseDClick(h.window, Qt::LeftButton, Qt::NoModifier, centre(title));
        QTRY_COMPARE(h.window->visibility(), QWindow::Maximized);
        QTest::mouseDClick(h.window, Qt::LeftButton, Qt::NoModifier, centre(title));
        QTRY_COMPARE(h.window->visibility(), QWindow::Windowed);
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, centre(minimize));
        QTRY_COMPARE(h.window->visibility(), QWindow::Minimized);
        h.window->showNormal(); h.window->requestActivate(); QTRY_COMPARE(h.window->visibility(), QWindow::Windowed);
        // Full screen keeps minimize and close; maximize has no meaning there.
        h.viewer.trigger("fullscreen");
        QTRY_COMPARE(h.window->visibility(), QWindow::FullScreen);
        QTRY_VERIFY(!maximize->isVisible()); QVERIFY(minimize->isVisible() && close->isVisible());
        h.viewer.trigger("fullscreen");
        QTRY_COMPARE(h.window->visibility(), QWindow::Windowed);
        QTRY_VERIFY(maximize->isVisible());
        // Hiding the top bar pane would leave no way to move or close a frameless window, so it stays.
        QVERIFY(h.layout.setVisible("topbar", false));
        QCoreApplication::processEvents();
        QVERIFY(namedItem(h.window, "topBar")->isVisible()); QVERIFY(close->isVisible());
        // Show frame returns the system title bar and its controls.
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, centre(showFrame));
        QTRY_VERIFY(!(h.window->flags() & Qt::FramelessWindowHint));
        QTRY_VERIFY(!namedItem(h.window, "topBar")->isVisible()); // the user's hidden pane applies again
        QVERIFY(h.layout.setVisible("topbar", true));
        h.window->setProperty("frameless", true);
        QTRY_VERIFY(close->isVisible());
        QSignalSpy closing(h.window, SIGNAL(closing(QQuickCloseEvent*)));
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, centre(close));
        QTRY_COMPARE(closing.count(), 1);
        QTRY_VERIFY(!h.window->isVisible());
        qGuiApp->setQuitOnLastWindowClosed(true);
    }
    // An inactive window mutes its title and window-control glyphs like a native caption; activation restores them.
    void inactiveWindowMutesTitleBar() {
        QTemporaryDir media; QVERIFY(media.isValid());
        QImage image(64, 48, QImage::Format_RGB32); image.fill(QColor(56, 130, 190)); QVERIFY(image.save(media.filePath("title.png")));
        Harness h(true); QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.open(media.filePath("title.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        h.window->setProperty("frameless", true);
        QObject* tokens = h.engine.singletonInstance<QObject*>("Glancelot", "Theme"); QVERIFY(tokens);
        const auto token = [&](const char* name) { return tokens->property(name).value<QColor>(); };
        auto* title = namedItem(h.window, "topBarTitle"); auto* minimize = namedItem(h.window, "minimizeButton");
        QVERIFY(title && minimize);
        h.window->requestActivate(); QTRY_VERIFY(h.window->isActive());
        QTRY_COMPARE(itemColor(title), token("text"));
        QCOMPARE(itemColor(buttonIcon(minimize)), token("text"));
        QWindow other; other.resize(200, 100); other.show(); other.requestActivate();
        QTRY_VERIFY(!h.window->isActive());
        QTRY_COMPARE(itemColor(title), token("muted"));
        QCOMPARE(itemColor(buttonIcon(minimize)), token("muted"));
        h.theme.setMode(h.theme.dark() ? "light" : "dark"); // live change while inactive
        QTRY_COMPARE(itemColor(title), token("muted"));
        other.hide(); h.window->requestActivate(); QTRY_VERIFY(h.window->isActive());
        QTRY_COMPARE(itemColor(title), token("text"));
        QCOMPARE(itemColor(buttonIcon(minimize)), token("text"));
    }
    // Opt-in review captures (GLANCELOT_REVIEW_CAPTURE): dark/light, compact/wide, framed/frameless, active/inactive.
    // These are client-area captures; the native caption is captured separately (docs/verification/TOPBAR-THEME-01.md).
    void topBarReviewCaptures() {
        if (!qEnvironmentVariableIsSet("GLANCELOT_REVIEW_CAPTURE")) QSKIP("Set GLANCELOT_REVIEW_CAPTURE to write captures.");
        QTemporaryDir media; QVERIFY(media.isValid());
        QImage image(640, 400, QImage::Format_RGB32);
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) image.setPixelColor(x, y, QColor(40 + x / 5, 90 + y / 6, 150));
        QVERIFY(image.save(media.filePath("Harbour at dusk.png")));
        Harness h(true); QVERIFY(h.window);
        h.viewer.open(media.filePath("Harbour at dusk.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        QDir().mkpath(".cache/topbar");
        QWindow other; other.resize(200, 100);
        for (const QSize size : {QSize(540, 360), QSize(1180, 780)})
            for (const auto& mode : {QString("dark"), QString("light")})
                for (bool frameless : {false, true})
                    for (bool active : {true, false}) {
                        h.window->resize(size); h.theme.setMode(mode); h.window->setProperty("frameless", frameless);
                        h.window->setProperty("hudVisible", true);
                        if (active) { other.hide(); h.window->requestActivate(); QTRY_VERIFY(h.window->isActive()); }
                        else { other.show(); other.requestActivate(); QTRY_VERIFY(!h.window->isActive()); }
                        QTest::qWait(200);
                        QVERIFY(h.window->grabWindow().save(QString(".cache/topbar/%1-%2-%3-%4.png").arg(mode).arg(size.width())
                            .arg(frameless ? "frameless" : "framed", active ? "active" : "inactive")));
                    }
    }
    void everyPaneDockRegionStaysInWindow() {
        QTemporaryDir media;QVERIFY(media.isValid());
        QImage image(64,48,QImage::Format_RGB32);image.fill(QColor(56,130,190));
        for(int x=0;x<64;x+=8)for(int y=0;y<48;y+=8)image.setPixelColor(x,y,Qt::white);
        for(const auto& name:{"a.png","b.png","c.png"})QVERIFY(image.save(media.filePath(name)));
        Harness h(true);QVERIFY(h.window);h.viewer.open(media.filePath("a.png"));
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(),15000);QTRY_COMPARE(h.model.count(),3);
        h.viewer.setTreeVisible(true);h.viewer.setTreePinned(true);
        int placements=0;
        for(const QSize size:{QSize(540,360),QSize(1180,780)})for(const auto& mode:{QString("dark"),QString("light")}) {
            h.window->resize(size);h.theme.setMode(mode);
            for(const auto& id:{QString("tree"),QString("preview"),QString("grid"),QString("filmstrip"),QString("topbar"),QString("statusbar")}) {
                h.window->setProperty("gridMode",id=="grid");
                for(const auto& region:{QString("left"),QString("right"),QString("top"),QString("bottom"),QString("center")}) {
                    if(region=="center"&&id!="preview"&&id!="grid")continue;
                    h.layout.reset();QVERIFY(h.layout.move(id,region,0));
                    h.window->setProperty("hudVisible",true); // auto-hide must not race the assertions or captures
                    QCoreApplication::processEvents();
                    const auto layout=h.window->property("workspace").toMap();
                    const auto rect=layout.value(id).toMap();
                    QVERIFY2(rect.value("width").toDouble()>0,qPrintable(id+"/"+region));
                    QVERIFY(rect.value("height").toDouble()>0);
                    QVERIFY(rect.value("x").toDouble()>=-0.1);QVERIFY(rect.value("y").toDouble()>=-0.1);
                    QVERIFY(rect.value("x").toDouble()+rect.value("width").toDouble()<=size.width()+0.1);
                    QVERIFY(rect.value("y").toDouble()+rect.value("height").toDouble()<=size.height()+0.1);
                    if(qEnvironmentVariableIsSet("GLANCELOT_REVIEW_CAPTURE")) {
                        QTest::qWait(50);QDir().mkpath(".cache/panes-matrix");
                        QVERIFY(h.window->grabWindow().save(QString(".cache/panes-matrix/%1-%2-%3-%4.png").arg(id,region,mode).arg(size.width())));
                    }
                    ++placements;
                }
            }
        }
        QCOMPARE(placements,104);
    }
    void paneArrangementKeyboardCommands() {
        Harness h;QVERIFY(h.window);h.viewer.setTreeVisible(true);h.window->requestActivate();
        h.window->setProperty("layoutEditing",true);
        QQuickItem* grip=nullptr;QList<QQuickItem*> pending{h.window->contentItem()};
        while(!pending.isEmpty()) {
            auto* item=pending.takeFirst();if(item->objectName()=="paneGrip-tree"){grip=item;break;}
            pending<<item->childItems();
        }
        QVERIFY(grip);QTest::qWait(100);grip->forceActiveFocus();QTRY_VERIFY(grip->hasActiveFocus());
        QTest::keyClick(h.window,Qt::Key_Right);QCOMPARE(h.layout.pane("tree")["region"].toString(),QString("right"));
        QTest::keyClick(h.window,Qt::Key_Right,Qt::ControlModifier);QCOMPARE(h.layout.pane("tree")["extent"].toInt(),296);
        QTest::keyClick(h.window,Qt::Key_Space);QVERIFY(h.layout.pane("tree")["frozen"].toBool());
        QTest::keyClick(h.window,Qt::Key_Space);QVERIFY(!h.layout.pane("tree")["frozen"].toBool());
        QTest::keyClick(h.window,Qt::Key_Escape);QTRY_VERIFY(!h.window->property("layoutEditing").toBool());
    }
    void paneCommandsLockIndicatorAndDragCancellation() {
        Harness h;QVERIFY(h.window);h.window->resize(1180,780);h.viewer.setTreeVisible(true);h.viewer.setTreePinned(true);
        h.window->requestActivate();
        const auto find=[&](const QString& name)->QQuickItem* {
            QList<QQuickItem*> pending{h.window->contentItem()};
            while(!pending.isEmpty()){auto* item=pending.takeFirst();if(item->objectName()==name)return item;pending<<item->childItems();}
            return nullptr;
        };
        QStringList ids;for(const auto& row:h.viewer.commands())ids<<row.toMap()["id"].toString();
        for(const auto& id:{"arrange-panes","layout-lock","layout-reset"}){QVERIFY(ids.contains(id));QVERIFY(h.viewer.bindingFor(id).isEmpty());}
        auto* indicator=find("layoutLockIndicator");QVERIFY(indicator);QVERIFY(!indicator->isVisible());
        h.viewer.trigger("layout-lock");QTRY_VERIFY(h.layout.locked());QTRY_VERIFY(indicator->isVisible());
        if(qEnvironmentVariableIsSet("GLANCELOT_REVIEW_CAPTURE"))for(const auto& mode:{QString("dark"),QString("light")}) {
            h.theme.setMode(mode);QTest::qWait(50);QDir().mkpath(".cache/panes-ui");
            QVERIFY(h.window->grabWindow().save(".cache/panes-ui/locked-"+mode+".png"));
        }
        // A locked layout can still be inspected; arrangement explains the lock instead of silently doing nothing.
        h.viewer.trigger("arrange-panes");QTRY_VERIFY(h.window->property("layoutEditing").toBool());
        auto* note=find("arrangementNote");QVERIFY(note);QTRY_VERIFY(note->isVisible());
        QVERIFY(note->property("text").toString().contains("locked"));
        h.window->setProperty("layoutEditing",false);
        QMetaObject::invokeMethod(indicator,"clicked");QTRY_VERIFY(!h.layout.locked());QTRY_VERIFY(!indicator->isVisible());
        QVERIFY(h.layout.resize("tree",360));h.viewer.trigger("layout-reset");QTRY_COMPARE(h.layout.pane("tree")["extent"].toInt(),280);
        h.viewer.trigger("arrange-panes");QTRY_VERIFY(h.window->property("layoutEditing").toBool());
        if(qEnvironmentVariableIsSet("GLANCELOT_REVIEW_CAPTURE"))for(const QSize size:{QSize(540,360),QSize(1180,780)})for(const auto& mode:{QString("dark"),QString("light")}) {
            h.window->resize(size);h.theme.setMode(mode);QTest::qWait(150);QDir().mkpath(".cache/panes-ui");
            QVERIFY(h.window->grabWindow().save(QString(".cache/panes-ui/arrange-%1-%2.png").arg(mode).arg(size.width())));
        }
        auto* grip=find("paneGrip-tree");QVERIFY(grip);QTRY_VERIFY(grip->isVisible());
        const auto origin=grip->mapToScene(QPointF(grip->width()/2,grip->height()/2)).toPoint();
        const QPoint target(int(h.window->width()*0.85),h.window->height()/2);
        // Escape mid-drag leaves arrangement and discards the drop.
        QTest::mousePress(h.window,Qt::LeftButton,Qt::NoModifier,origin);QTest::mouseMove(h.window,target,30);
        QTest::keyClick(h.window,Qt::Key_Escape);QTRY_VERIFY(!h.window->property("layoutEditing").toBool());
        QTest::mouseRelease(h.window,Qt::LeftButton,Qt::NoModifier,target);
        QCOMPARE(h.layout.pane("tree")["region"].toString(),QString("left"));
        // Locking mid-drag cancels the gesture.
        h.window->setProperty("layoutEditing",true);QTRY_VERIFY(grip->isVisible());
        QTest::mousePress(h.window,Qt::LeftButton,Qt::NoModifier,origin);QTest::mouseMove(h.window,target,30);
        h.layout.setLocked(true);QTest::mouseRelease(h.window,Qt::LeftButton,Qt::NoModifier,target);
        QCOMPARE(h.layout.pane("tree")["region"].toString(),QString("left"));
        h.layout.setLocked(false);
        // A completed drag commits exactly once.
        QSignalSpy changes(&h.layout,&PaneLayout::changed);
        QTest::mousePress(h.window,Qt::LeftButton,Qt::NoModifier,origin);QTest::mouseMove(h.window,target,30);
        QTest::mouseRelease(h.window,Qt::LeftButton,Qt::NoModifier,target);
        QTRY_COMPARE(h.layout.pane("tree")["region"].toString(),QString("right"));QCOMPARE(changes.count(),1);
        QVERIFY(grip->hasActiveFocus());
        // Leaving arrangement hands focus back to content; arrow keys and Space no longer edit the layout.
        QTest::keyClick(h.window,Qt::Key_Escape);QTRY_VERIFY(!h.window->property("layoutEditing").toBool());
        QTRY_VERIFY(!grip->hasActiveFocus());
        auto* canvas=h.window->findChild<QQuickItem*>("canvas");QVERIFY(canvas);QTRY_VERIFY(canvas->hasActiveFocus());
        QTest::keyClick(h.window,Qt::Key_Left);QTest::keyClick(h.window,Qt::Key_Space);
        QCOMPARE(h.layout.pane("tree")["region"].toString(),QString("right"));QVERIFY(!h.layout.pane("tree")["frozen"].toBool());
        h.layout.reset();
    }
    void hudAutoHideNeverReflowsCanvas() {
        Harness h;QVERIFY(h.window);h.window->resize(1180,780);
        auto* canvas=h.window->findChild<QQuickItem*>("canvas");QVERIFY(canvas);
        h.window->setProperty("hudVisible",true);QCoreApplication::processEvents();
        const QRectF awake(canvas->x(),canvas->y(),canvas->width(),canvas->height());
        h.window->setProperty("hudVisible",false);QCoreApplication::processEvents();
        QCOMPARE(QRectF(canvas->x(),canvas->y(),canvas->width(),canvas->height()),awake);
        QCOMPARE(awake,QRectF(0,0,1180,780));
        // A frozen bar stays up, so it reserves its space instead of covering the photo.
        QVERIFY(h.layout.freeze("statusbar",true));QCoreApplication::processEvents();
        QTRY_VERIFY(canvas->height()<780);const qreal reserved=canvas->height();
        h.window->setProperty("hudVisible",true);QCoreApplication::processEvents();QCOMPARE(canvas->height(),reserved);
        h.layout.reset();
        const auto rectOf=[](const QVariant& v){const auto m=v.toMap();return QRectF(m["x"].toDouble(),m["y"].toDouble(),m["width"].toDouble(),m["height"].toDouble());};
        // Notices and toasts use the unobstructed area, which excludes the visible overlaid bars.
        h.window->setProperty("hudVisible",true);QCoreApplication::processEvents();
        auto ws=h.window->property("workspace").toMap();
        QVERIFY(!rectOf(ws["_clear"]).intersects(rectOf(ws["statusbar"])));QVERIFY(!rectOf(ws["_clear"]).intersects(rectOf(ws["topbar"])));
        // A docked tree inside auto-hiding vertical bars keeps its place and never covers the canvas.
        h.viewer.setTreeVisible(true);h.viewer.setTreePinned(true);
        QVERIFY(h.layout.move("topbar","left",0));QVERIFY(h.layout.move("statusbar","right",0));QCoreApplication::processEvents();
        auto* tree=h.window->findChild<QQuickItem*>("folderTree");QVERIFY(tree);QTRY_VERIFY(tree->isVisible());
        const qreal treeX=tree->x();QVERIFY(tree->x()+tree->width()<=canvas->x()+0.1);
        h.window->setProperty("hudVisible",false);QCoreApplication::processEvents();
        QCOMPARE(tree->x(),treeX);QVERIFY(tree->x()+tree->width()<=canvas->x()+0.1);
        h.layout.reset();h.viewer.setTreePinned(false);h.viewer.setTreeVisible(false);
    }
    void recoveredLayoutIsNotEditableAndKeepsPreferences() {
        Harness h;QVERIFY(h.window);h.viewer.setTreeVisible(true);h.viewer.setTreePinned(true);
        QVERIFY(h.layout.move("tree","right",0));QVERIFY(h.layout.resize("tree",400));
        QVERIFY(h.layout.move("topbar","left",0));QVERIFY(h.layout.move("statusbar","right",1));
        h.window->resize(540,360);QTRY_VERIFY(h.window->property("workspace").toMap().value("_recovered").toBool());
        {   // The recovery notice never covers a toolbar.
            auto* notice=h.window->findChild<QQuickItem*>("layoutRecoveryNotice");QVERIFY(notice);QTRY_VERIFY(notice->isVisible());
            const auto top=h.window->property("workspace").toMap()["topbar"].toMap();
            const QRectF bar(top["x"].toDouble(),top["y"].toDouble(),top["width"].toDouble(),top["height"].toDouble());
            QVERIFY(!QRectF(notice->x(),notice->y(),notice->width(),notice->height()).intersects(bar));
        }
        h.window->requestActivate();h.window->setProperty("layoutEditing",true);
        QQuickItem* grip=nullptr;QList<QQuickItem*> pending{h.window->contentItem()};
        while(!pending.isEmpty()){auto* item=pending.takeFirst();if(item->objectName()=="paneGrip-tree"){grip=item;break;}pending<<item->childItems();}
        QVERIFY(grip);QTest::qWait(100);grip->forceActiveFocus();QTRY_VERIFY(grip->hasActiveFocus());
        QTest::keyClick(h.window,Qt::Key_Right,Qt::ControlModifier);QTest::keyClick(h.window,Qt::Key_Down,Qt::ShiftModifier);
        QCOMPARE(h.layout.pane("tree")["extent"].toInt(),400);QCOMPARE(h.layout.pane("tree")["region"].toString(),QString("right"));
        QTest::keyClick(h.window,Qt::Key_Escape);QTRY_VERIFY(!h.window->property("layoutEditing").toBool());
        h.window->resize(1180,780);QTRY_VERIFY(!h.window->property("workspace").toMap().value("_recovered").toBool());
        QTRY_COMPARE(qRound(h.window->property("workspace").toMap().value("tree").toMap().value("width").toDouble()),400);
        h.layout.reset();h.viewer.setTreePinned(false);h.viewer.setTreeVisible(false);
    }
    void paneArrangementBoundsAndGestures() {
        Harness h;QVERIFY(h.window);h.window->resize(540,360);h.viewer.setTreeVisible(true);h.viewer.setTreePinned(true);
        for(const auto& id:{"tree","topbar","statusbar","filmstrip"}) {
            QVERIFY(h.layout.move(id,"top",0));QVERIFY(h.layout.resize(id,4096));
        }
        auto* canvas=h.window->findChild<QQuickItem*>("canvas");QVERIFY(canvas);
        QTRY_VERIFY(canvas->height()>=119);QVERIFY(canvas->y()+canvas->height()<=h.window->height()+0.1);
        QCOMPARE(h.layout.pane("tree")["region"].toString(),QString("top"));
        QCOMPARE(h.layout.pane("tree")["extent"].toInt(),4096); // responsive recovery never persists
        h.layout.reset();
        QVERIFY(h.layout.move("topbar","left",0));QVERIFY(h.layout.resize("topbar",32));
        QVERIFY(h.layout.resize("tree",4096));
        auto* treeBefore=h.window->findChild<QQuickItem*>("folderTree");QVERIFY(treeBefore);
        const auto workspace=h.window->property("workspace").toMap();
        const auto toolbarRect=workspace.value("topbar").toMap();
        QTRY_VERIFY(treeBefore->x()>=toolbarRect.value("x").toDouble()+toolbarRect.value("width").toDouble()-0.1);
        QVERIFY(toolbarRect.value("width").toDouble()>=200);
        QVERIFY(h.layout.move("statusbar","right",0));
        QTRY_VERIFY(treeBefore->width()>=200);
        const auto adapted=h.window->property("workspace").toMap();
        QVERIFY(adapted.value("_recovered").toBool());
        QCOMPARE(h.layout.pane("statusbar")["region"].toString(),QString("right"));
        h.layout.reset();h.window->resize(1180,780);h.window->requestActivate();h.window->setProperty("layoutEditing",true);
        // Arrangement is disabled while recovered, so wait for the larger window to apply first.
        QTRY_COMPARE(h.window->width(),1180);QTRY_COMPARE(h.window->height(),780);QTRY_VERIFY(!h.window->property("workspace").toMap().value("_recovered").toBool());
        QQuickItem* handle=nullptr;
        QList<QQuickItem*> pending{h.window->contentItem()};
        while(!pending.isEmpty()) {
            auto* item=pending.takeFirst();
            if(item->objectName()=="paneResize-tree"){handle=item;break;}
            pending<<item->childItems();
        }
        QVERIFY(handle);
        QTRY_VERIFY(handle->isVisible());QTRY_VERIFY(handle->isEnabled());
        QTRY_VERIFY(handle->mapRectToScene(QRectF(0,0,handle->width(),handle->height())).bottom()>600);
        const auto origin=handle->mapToScene(QPointF(handle->width()/2,handle->height()/2)).toPoint();
        QTest::mousePress(h.window,Qt::LeftButton,Qt::NoModifier,origin);
        QTest::mouseMove(h.window,origin+QPoint(40,0),30);
        auto* tree=h.window->findChild<QQuickItem*>("folderTree");QVERIFY(tree);
        QTRY_VERIFY(tree->width()>300);
        QTest::mouseRelease(h.window,Qt::LeftButton,Qt::NoModifier,origin+QPoint(40,0));
        QTRY_VERIFY(h.layout.pane("tree")["extent"].toDouble()>300);
        QVERIFY(h.layout.freeze("tree",true));h.viewer.setTreeVisible(false);
        QTRY_VERIFY(tree->isVisible());
        QTest::keyClick(h.window,Qt::Key_Escape);QTRY_VERIFY(!h.window->property("layoutEditing").toBool());
    }
    void paneGeometryMovesFreezesAndResets() {
        Harness h;QVERIFY(h.window);h.window->resize(1180,780);h.viewer.setTreeVisible(true);
        auto* tree=h.window->findChild<QQuickItem*>("folderTree");QVERIFY(tree);
        QTRY_VERIFY(tree->isVisible());
        QVERIFY(h.layout.move("tree","right",0));
        QTRY_VERIFY(tree->x()>h.window->width()/2);
        QVERIFY(h.layout.resize("tree",320));QTRY_COMPARE(qRound(tree->width()),320);
        QVERIFY(h.layout.freeze("tree",true));
        QVERIFY(!h.layout.move("tree","left",0));QVERIFY(!h.layout.resize("tree",400));
        QCOMPARE(qRound(tree->width()),320);
        h.window->setProperty("hudVisible",false);QTRY_VERIFY(tree->isVisible());
        h.layout.setLocked(true);QVERIFY(!h.layout.freeze("tree",false));
        h.layout.reset();h.window->setProperty("hudVisible",true);
        QTRY_COMPARE(qRound(tree->x()),0);QTRY_COMPARE(qRound(tree->width()),280);
        for(const auto& mode:{QString("dark"),QString("light")}) {
            h.theme.setMode(mode);h.window->resize(540,360);QTest::qWait(100);
            QVERIFY(tree->width()>0&&tree->height()>0);
            QVERIFY(tree->x()+tree->width()<=h.window->width());
            if(qEnvironmentVariableIsSet("GLANCELOT_REVIEW_CAPTURE")) {
                QDir().mkpath(".cache/panes-ui");
                QVERIFY(h.window->grabWindow().save(".cache/panes-ui/tree-"+mode+".png"));
            }
        }
    }
    void addressEditingClipboardAndEscape() {
        QTemporaryDir media; QVERIFY(media.isValid());
        QDir().mkpath(media.filePath("empty folder"));
        Harness h(true);QVERIFY(h.window);h.window->resize(540,360);
        h.viewer.setTreeVisible(true);h.window->requestActivate();
        auto* field=h.window->findChild<QQuickItem*>("folderAddress");QVERIFY(field);
        auto* copy=h.window->findChild<QQuickItem*>("copyFolderAddress");QVERIFY(copy);
        auto* list=h.window->findChild<QQuickItem*>("folderList");QVERIFY(list);
        QTRY_VERIFY(list->hasActiveFocus()); // treeShown queues its initial focus restoration
        field->forceActiveFocus();QTRY_VERIFY(field->hasActiveFocus());
        QGuiApplication::clipboard()->setText(media.filePath("empty folder"));
        QTest::keyClick(h.window,Qt::Key_V,Qt::ControlModifier);
        QCOMPARE(field->property("text").toString(),media.filePath("empty folder"));
        QVERIFY(h.viewer.directory().isEmpty()); // paste is an edit, not navigation
        QTest::keyClick(h.window,Qt::Key_Return);
        QTRY_COMPARE(QDir::cleanPath(h.viewer.directory()),QDir::cleanPath(media.filePath("empty folder")));
        QTRY_VERIFY(!h.viewer.directoryPending());
        QTest::mouseClick(h.window,Qt::LeftButton,Qt::NoModifier,copy->mapToScene(QPointF(copy->width()/2,copy->height()/2)).toPoint());
        QCOMPARE(QGuiApplication::clipboard()->text(),h.viewer.directory());
        field->forceActiveFocus();QTest::keyClick(h.window,Qt::Key_A,Qt::ControlModifier);
        for(char key:QByteArray("GTS"))QTest::keyClick(h.window,key); // global grid/filmstrip/sort bindings must not run
        QVERIFY(!h.window->property("gridMode").toBool());QVERIFY(h.viewer.filmstripVisible());
        QCOMPARE(h.viewer.sortKey(),QString("name"));
        QTest::keyClick(h.window,Qt::Key_Escape);
        QCOMPARE(field->property("text").toString(),h.viewer.directory());
        QTRY_VERIFY(list->hasActiveFocus());QVERIFY(h.viewer.treeVisible());
        field->forceActiveFocus();QTest::keyClick(h.window,Qt::Key_A,Qt::ControlModifier);
        for(char key:QByteArray("missing-address-target"))QTest::keyClick(h.window,key);QTest::keyClick(h.window,Qt::Key_Return);
        QTRY_VERIFY(!h.viewer.directoryPending()&&!h.viewer.directoryError().isEmpty());
        QVERIFY(field->property("text").toString().contains("missing-address-target"));
        for(const auto& mode:{QString("dark"),QString("light")}) {
            h.theme.setMode(mode);QTest::qWait(150);
            QDir().mkpath(".cache/path-bar-ui");
            const auto capture=h.window->grabWindow();
            QVERIFY(!capture.isNull());
            if(qEnvironmentVariableIsSet("GLANCELOT_REVIEW_CAPTURE"))
                QVERIFY(capture.save(".cache/path-bar-ui/error-"+mode+".png"));
        }
    }
    void reviewAllLocationsAtMinimumSize() {
        Harness h; QVERIFY(h.window);
        h.folders.setLister([](const std::filesystem::path&,std::stop_token){return glancelot::FolderListing{};});
        h.folders.browseComputer();
        h.window->resize(540,360); h.viewer.setTreeVisible(true);
        h.window->requestActivate();
        auto* panel=h.window->findChild<QQuickItem*>("folderTree"); QVERIFY(panel);
        auto* button=h.window->findChild<QQuickItem*>("allLocations"); QVERIFY(button);
        auto* list=h.window->findChild<QQuickItem*>("folderList"); QVERIFY(list);
        QTRY_VERIFY(panel->isVisible()&&button->isVisible());
        const QPointF local=button->mapToItem(panel,QPointF(0,0));
        QVERIFY(local.x()>=0&&local.y()>=0);
        QVERIFY(local.x()+button->width()<=panel->width());
        QVERIFY(local.y()+button->height()<panel->height());
        QTest::mouseClick(h.window,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());
        QTRY_VERIFY(list->hasActiveFocus());
        QVERIFY(h.folders.count()>0);
        QDir().mkpath(".cache/review");
        h.theme.setMode("dark"); QTest::qWait(200);
        QVERIFY(h.window->grabWindow().save(".cache/review/tree-compact-dark.png"));
        h.theme.setMode("light"); QTest::qWait(200);
        QVERIFY(h.window->grabWindow().save(".cache/review/tree-compact-light.png"));
    }
    void foldersButtonWorksWithoutPhoto() {
        Harness h; QVERIFY(h.window);
        auto* button=h.window->findChild<QQuickItem*>("foldersToggle"); QVERIFY(button);
        QVERIFY(button->isEnabled()); QVERIFY(!h.viewer.treeVisible());
        const auto point=button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint();
        QTest::mouseClick(h.window,Qt::LeftButton,Qt::NoModifier,point);
        QTRY_VERIFY(h.viewer.treeVisible());
        auto* panel=h.window->findChild<QQuickItem*>("folderTree");QVERIFY(panel);QTRY_VERIFY(panel->isVisible());
        QTest::mouseClick(h.window,Qt::LeftButton,Qt::NoModifier,point);
        QTRY_VERIFY(!h.viewer.treeVisible());
        QTest::keyClick(h.window,Qt::Key_B,Qt::ControlModifier);
        QTRY_VERIFY(h.viewer.treeVisible());
    }
    void auditReadyCurrentPhotoDoesNotReopen() {
        QTemporaryDir media; QVERIFY(media.isValid());
        QImage image(8,8,QImage::Format_RGB32);image.fill(Qt::red);QVERIFY(image.save(media.filePath("ready.png")));
        Harness h(true);QVERIFY(h.window);h.viewer.open(media.filePath("ready.png"));
        QTRY_COMPARE(h.viewer.fileCount(),1);QTRY_VERIFY(h.viewer.imageReady());
        h.showGrid();QTRY_VERIFY(h.view()->hasActiveFocus());
        QVERIFY(h.viewer.isCurrentImage(h.model.pathAt(0)));
#ifdef Q_OS_WIN
        QVERIFY(h.viewer.isCurrentImage(QDir::toNativeSeparators(h.viewer.filePath()).toUpper()));
#endif
        QSignalSpy changed(&h.viewer,&ViewerController::imageChanged);
        QVERIFY(QMetaObject::invokeMethod(h.grid(),"activate"));
        QVERIFY(!h.viewer.loading());QTest::qWait(80);QCOMPARE(changed.count(),0);
    }
    void auditFailedCurrentPhotoCanBeRetriedFromGrid() {
        QTemporaryDir media; QVERIFY(media.isValid());
        QFile broken(media.filePath("broken.png")); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("not a png"); broken.close();
        Harness h(true); QVERIFY(h.window); h.viewer.open(media.filePath("broken.png"));
        QTRY_COMPARE(h.viewer.fileCount(),1); QTRY_VERIFY(!h.viewer.loading());
        QVERIFY(!h.viewer.status().isEmpty());
        // Force identical spelling: Windows normally mixes slash forms and bypasses the no-op branch.
        auto snapshot=h.viewer.snapshot(); snapshot.files={std::filesystem::path(h.viewer.filePath().toStdWString())}; snapshot.current=snapshot.files.front(); h.model.sync(snapshot);
        h.showGrid(); QTRY_VERIFY(h.view()->hasActiveFocus()); QTRY_COMPARE(h.selected(),0);
        QCOMPARE(h.model.pathAt(0),h.viewer.filePath());
        QVERIFY(QMetaObject::invokeMethod(h.grid(),"activate"));
        QVERIFY2(h.viewer.loading(),"Grid activation must retry a failed current path");
        QTRY_VERIFY(!h.viewer.loading());
    }
    void auditTallGridQueueBound() {
        Harness h; QVERIFY(h.window); h.fill(10000); h.showGrid();
        QTRY_VERIFY(h.view()->hasActiveFocus());
        // Run this case with QT_QPA_PLATFORM=offscreen to bypass native desktop clamping.
        h.window->resize(7680,6000);
        QTest::qWait(500);
        if (h.grid()->height()<5000) QSKIP("Native window manager clamped the window; rerun with QT_QPA_PLATFORM=offscreen");
        const int live=h.grid()->property("liveDelegateCount").toInt();
        qInfo()<<"grid dimensions"<<h.grid()->width()<<h.grid()->height()<<"live"<<live;
        QVERIFY2(live<=256,qPrintable(QString("live delegates=%1 exceed thumbnail queue").arg(live)));
    }
    void virtualizedAndHidden() {
        Harness h; QVERIFY(h.window); h.fill(10000, 0); h.showGrid();
        QTRY_COMPARE(h.view()->property("count").toInt(), 10000);
        QTRY_VERIFY(h.view()->hasActiveFocus());
        QTest::qWait(100);
        const int requested = h.provider->requests.load();
        QVERIFY2(requested > 0 && requested < 150, qPrintable(QString::number(requested)));
        QVERIFY(h.cells().size() > 0 && h.cells().size() < 150);
        QCOMPARE(h.grid()->property("liveDelegateCount").toInt(), int(h.cells().size()));
        QTest::keyClick(h.window, Qt::Key_End);
        QTRY_COMPARE(h.selected(), 9999);
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_VERIFY(!h.window->property("gridMode").toBool());
        QTRY_COMPARE(h.view()->property("count").toInt(), 0);
        QCOMPARE(h.fileGrid.count(), 0); QVERIFY(!h.fileGrid.loading()); // hidden grid holds no listing
        QVERIFY(h.window->findChild<QQuickItem*>("canvas")->hasActiveFocus());
        QTest::qWait(100); const int hiddenRequests = h.provider->requests.load();
        QTest::qWait(100); QCOMPARE(h.provider->requests.load(), hiddenRequests);
        h.showGrid(); QTRY_COMPARE(h.selected(), 9999);
    }
    void keyboardRemappingAndModalFocus() {
        Harness h; QVERIFY(h.window); h.fill(100, 4); h.showGrid();
        QTRY_VERIFY(h.view()->hasActiveFocus()); QTRY_COMPARE(h.selected(),4);
        QSignalSpy commands(&h.viewer, &ViewerController::viewCommand);
        QVERIFY(h.viewer.setBinding("next", ""));
        QVERIFY(h.viewer.setBinding("rotate", "Right"));
        QTest::keyClick(h.window, Qt::Key_Right); QTRY_COMPARE(h.selected(),5);
        QCOMPARE(commands.size(),0);
        const int columns=h.grid()->property("columns").toInt();
        QTest::keyClick(h.window, Qt::Key_Down); QCOMPARE(h.selected(),5+columns);
        QTest::keyClick(h.window, Qt::Key_Home); QCOMPARE(h.selected(),0);
        QTest::keyClick(h.window, Qt::Key_PageDown); QVERIFY(h.selected()>=columns);
        QTest::keyClick(h.window, Qt::Key_End); QCOMPARE(h.selected(),99);
        QTest::keyClick(h.window, Qt::Key_Right); QCOMPARE(h.selected(),99);
        h.viewer.trigger("settings"); QTRY_VERIFY(h.window->property("modalOpen").toBool());
        QTest::keyClick(h.window, Qt::Key_Left); QCOMPARE(h.selected(),99);
        QTest::keyClick(h.window, Qt::Key_Escape); QTRY_VERIFY(!h.window->property("modalOpen").toBool());
        QTRY_VERIFY(h.view()->hasActiveFocus()); QVERIFY(h.window->property("gridMode").toBool());
        QVERIFY(h.viewer.setBinding("grid", "Ctrl+G"));
        QTest::keyClick(h.window, Qt::Key_G); QVERIFY(h.window->property("gridMode").toBool());
        QTest::keyClick(h.window, Qt::Key_G, Qt::ControlModifier); QTRY_VERIFY(!h.window->property("gridMode").toBool());
    }
    void activationAndSingleClick() {
        QTemporaryDir media; QVERIFY(media.isValid());
        QImage image(24,16,QImage::Format_RGB32); image.fill(Qt::green);
        for(int i=0;i<3;++i) QVERIFY(image.save(media.filePath(QString::number(i)+".png")));
        Harness h(true); QVERIFY(h.window); h.viewer.open(media.filePath("0.png"));
        QTRY_COMPARE(h.viewer.fileCount(),3); QTRY_VERIFY(!h.viewer.loading());
        h.showGrid(); QTRY_VERIFY(h.view()->hasActiveFocus()); QTRY_COMPARE(h.selected(),0); // the listing is asynchronous
        const QString original=h.viewer.filePath();
        QTest::keyClick(h.window, Qt::Key_Right); QCOMPARE(h.selected(),1); QCOMPARE(h.viewer.filePath(),original);
        QTRY_VERIFY(h.cell(2));
        QQuickItem* second=h.cell(2); QTRY_VERIFY(second->property("previewFailed").toBool());
        const QPoint point=second->mapToScene(QPointF(second->width()/2,second->height()/2)).toPoint();
        QTest::mouseClick(h.window,Qt::LeftButton,Qt::NoModifier,point);
        QCOMPARE(h.selected(),2); QCOMPARE(h.viewer.filePath(),original);
        QTest::keyClick(h.window, Qt::Key_Return);
        QTRY_VERIFY(!h.window->property("gridMode").toBool());
        QTRY_VERIFY(h.viewer.filePath().endsWith("2.png"));
        h.showGrid(); QTRY_VERIFY(h.view()->hasActiveFocus());
        QTRY_COMPARE(h.selected(),2);
        QTRY_VERIFY(h.cell(0));
        QQuickItem* first=h.cell(0);
        QTest::mouseDClick(h.window,Qt::LeftButton,Qt::NoModifier,first->mapToScene(QPointF(first->width()/2,first->height()/2)).toPoint());
        QTRY_VERIFY(!h.window->property("gridMode").toBool());
        QTRY_VERIFY(h.viewer.filePath().endsWith("0.png"));
    }
    void gridKeysBeatRemappedShortcutsAndCommandsAreScoped() {
        Harness h; QVERIFY(h.window); h.fill(100, 0); h.showGrid(); QTRY_VERIFY(h.view()->hasActiveFocus()); QTRY_COMPARE(h.selected(), 0);
        QVERIFY(h.viewer.setBinding("next", ""));
        QVERIFY(h.viewer.setBinding("tree", "Down"));
        QVERIFY(h.viewer.setBinding("palette", "Right"));
        QTest::keyClick(h.window, Qt::Key_Right); QTRY_COMPARE(h.selected(), 1);
        QVERIFY(!h.window->property("modalOpen").toBool());
        const int columns = h.grid()->property("columns").toInt();
        QTest::keyClick(h.window, Qt::Key_Down); QTRY_COMPARE(h.selected(), 1 + columns);
        QVERIFY(!h.viewer.treeVisible());
        // Commands that act on the hidden canvas are ignored from toolbar/palette routes while browsing.
        QVERIFY(QMetaObject::invokeMethod(h.window, "invoke", Q_ARG(QVariant, QVariant("rotate"))));
        QVERIFY(h.window->property("gridMode").toBool());
    }
    void currentItemIsRevealedAndModalsBlockInput() {
        Harness h; QVERIFY(h.window); h.fill(10000, 7000); h.showGrid(); QTRY_VERIFY(h.view()->hasActiveFocus());
        QTRY_COMPARE(h.selected(), 7000);
        QTRY_VERIFY(h.cell(7000));
        auto inView = [&](int index) {
            QQuickItem* cell = h.cell(index); if (!cell) return false;
            const QPointF top = cell->mapToItem(h.view(), QPointF(0, 0)), bottom = cell->mapToItem(h.view(), QPointF(0, cell->height()));
            return top.y() >= -1 && bottom.y() <= h.view()->height() + 1;
        };
        QVERIFY(inView(7000));
        QTest::keyClick(h.window, Qt::Key_Home); QTRY_COMPARE(h.selected(), 0); QTRY_VERIFY(inView(0));
        QTest::keyClick(h.window, Qt::Key_End); QTRY_COMPARE(h.selected(), 9999); QTRY_VERIFY(inView(9999));
        h.viewer.trigger("palette"); QTRY_VERIFY(h.window->property("modalOpen").toBool());
        QVERIFY(!h.grid()->property("inputEnabled").toBool());
    }
    void sortOrderFollowsControllerKeepsCurrentAndPersists() {
        QTemporaryDir media; QVERIFY(media.isValid());
        // Name order a<b<c; size order b<c<a; modified order is not asserted here (covered by core tests).
        const struct { const char* name; int edge; } files[]{{"a.png", 96}, {"b.png", 8}, {"c.png", 40}};
        for (const auto& file : files) {
            QImage image(file.edge, file.edge, QImage::Format_RGB32);
            for (int y = 0; y < file.edge; ++y) for (int x = 0; x < file.edge; ++x) image.setPixel(x, y, qRgb((x * 7) & 255, (y * 13) & 255, (x * y) & 255));
            QVERIFY(image.save(media.filePath(file.name)));
        }
        QVERIFY(QFileInfo(media.filePath("b.png")).size() < QFileInfo(media.filePath("c.png")).size());
        QVERIFY(QFileInfo(media.filePath("c.png")).size() < QFileInfo(media.filePath("a.png")).size());
        {
            Harness h(true); QVERIFY(h.window); h.viewer.open(media.filePath("a.png"));
            QTRY_COMPARE(h.viewer.fileCount(), 3); QTRY_VERIFY(!h.viewer.loading());
            QCOMPARE(h.viewer.sortKey(), QString("name"));
            QVERIFY(h.model.pathAt(0).endsWith("a.png"));
            h.viewer.trigger("sort");   // name -> modified
            QTRY_COMPARE(h.viewer.sortKey(), QString("modified"));
            h.viewer.setSortKey("size");
            QTRY_VERIFY(h.model.pathAt(0).endsWith("b.png"));
            QVERIFY(h.model.pathAt(2).endsWith("a.png"));
            QVERIFY(h.viewer.filePath().endsWith("a.png"));       // sorting never changes what is open
            QCOMPARE(h.viewer.currentIndex(), 3);
            h.viewer.trigger("sort-reverse");
            QTRY_VERIFY(h.model.pathAt(0).endsWith("a.png"));
            QVERIFY(h.viewer.sortDescending());
            h.viewer.next();   // next follows the visible order: a -> c
            QTRY_VERIFY(h.viewer.filePath().endsWith("c.png"));
            QTRY_VERIFY(!h.viewer.loading());
        }
        {   // A persisted order is applied to the first listing of a new run, with no extra command.
            Harness h(true); QVERIFY(h.window); h.viewer.open(media.filePath("a.png"));
            QTRY_COMPARE(h.viewer.fileCount(), 3);
            QCOMPARE(h.viewer.sortKey(), QString("size"));
            QTRY_VERIFY(h.model.pathAt(0).endsWith("a.png"));   // size descending: a (largest) first
            QVERIFY(h.model.pathAt(2).endsWith("b.png"));
        }
        QJsonObject stored; QVERIFY(glancelot::readSettings(stored).ok());
        QCOMPARE(stored.value("sort").toObject().value("key").toString(), QString("size"));
        QVERIFY(stored.value("sort").toObject().value("descending").toBool());
        ViewerController restarted;   // a new run starts in the saved order
        QCOMPARE(restarted.sortKey(), QString("size")); QVERIFY(restarted.sortDescending());
    }
    void sortChangesRacingOpensKeepTheAcceptedFolder() {
        QTemporaryDir one, two; QVERIFY(one.isValid() && two.isValid());
        const struct { const char* name; int edge; } shapes[]{{"a.png", 96}, {"b.png", 8}, {"c.png", 40}};
        for (const auto& shape : shapes) for (QTemporaryDir* dir : {&one, &two}) {
            QImage image(shape.edge, shape.edge, QImage::Format_RGB32);
            for (int y = 0; y < shape.edge; ++y) for (int x = 0; x < shape.edge; ++x) image.setPixel(x, y, qRgb((x * 7) & 255, (y * 13) & 255, (x * y) & 255));
            QVERIFY(image.save(dir->filePath(shape.name)));
        }
        QImage extra(4, 4, QImage::Format_RGB32); extra.fill(Qt::red);
        QVERIFY(extra.save(two.filePath("d.png"))); QVERIFY(extra.save(two.filePath("e.png")));
        Harness h(true); QVERIFY(h.window);
        // Sort changed while the very first scan is in flight: the folder is kept and ends up in the new order.
        h.viewer.open(one.filePath("c.png")); h.viewer.setSortKey("size");
        QTRY_COMPARE(h.viewer.fileCount(), 3);
        QTRY_VERIFY(h.model.pathAt(0).endsWith("b.png"));
        QVERIFY(h.model.pathAt(2).endsWith("a.png"));
        QVERIFY(h.viewer.filePath().endsWith("c.png"));
        // Opening another folder and changing the order before its scan lands must not resurrect the old folder.
        h.viewer.open(two.filePath("a.png")); h.viewer.setSortDescending(true);
        QTRY_COMPARE(h.viewer.fileCount(), 5);
        QTRY_VERIFY(h.model.pathAt(0).endsWith("a.png"));   // size descending
        QVERIFY(h.viewer.filePath().startsWith(two.path()));
        QTRY_VERIFY(!h.viewer.loading());
        h.viewer.next();
        QTRY_VERIFY(h.viewer.filePath().startsWith(two.path()) && !h.viewer.filePath().endsWith("a.png"));
        QCOMPARE(h.viewer.fileCount(), 5);
        QVERIFY(h.viewer.sortDescending() && h.viewer.sortKey() == "size");
    }
    static QList<QQuickItem*> descendants(QQuickItem* root) {
        QList<QQuickItem*> found; QList<QQuickItem*> pending{root};
        while (!pending.isEmpty()) { QQuickItem* item = pending.takeFirst(); found << item; pending << item->childItems(); }
        return found;
    }
    void settingsMergesThemesAndCommands() {
        Harness h; QVERIFY(h.window);
        h.window->resize(1180, 780); h.window->requestActivate();
        // The top bar keeps a single Settings entry: no separate Commands or Themes buttons.
        QVERIFY(h.window->findChild<QQuickItem*>("settingsButton"));
        for (QQuickItem* item : descendants(h.window->contentItem())) {
            const QString text = item->property("text").toString();
            QVERIFY2(!(item->inherits("QQuickAbstractButton") && (text == "Commands" || text == "Themes" || text == "Menu")), qPrintable(text));
        }
        auto* dialog = h.window->findChild<QObject*>("settingsDialog"); QVERIFY(dialog);
        // The palette command opens Settings > Controls with the search field focused.
        QSignalSpy commands(&h.viewer, &ViewerController::viewCommand);
        h.viewer.trigger("palette");
        QTRY_VERIFY(h.window->property("modalOpen").toBool());
        QCOMPARE(dialog->property("section").toString(), QString("controls"));
        auto* search = h.window->findChild<QQuickItem*>("commandSearch"); QVERIFY(search);
        QTRY_VERIFY(search->hasActiveFocus());
        for (const char c : std::string("rotat")) QTest::keyClick(h.window, c);
        QTest::keyClick(h.window, Qt::Key_Return);
        QTRY_VERIFY(!h.window->property("modalOpen").toBool());
        QVERIFY(commands.contains(QVariantList{QString("rotate")}));
        // --show-themes keeps working: appearance now lives in Settings.
        QVERIFY(QMetaObject::invokeMethod(h.window, "openThemes"));
        QTRY_VERIFY(h.window->property("modalOpen").toBool());
        QCOMPARE(dialog->property("section").toString(), QString("appearance"));
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_VERIFY(!h.window->property("modalOpen").toBool());
        // Commands that act on the hidden canvas are listed but not runnable from the grid view.
        h.fill(20); h.showGrid(); QTRY_VERIFY(h.view()->hasActiveFocus());
        commands.clear();
        h.viewer.trigger("palette"); QTRY_VERIFY(search->hasActiveFocus());
        for (const char c : std::string("rotat")) QTest::keyClick(h.window, c);
        QTest::keyClick(h.window, Qt::Key_Return);
        QTest::qWait(50);
        QVERIFY(!commands.contains(QVariantList{QString("rotate")}));
        QVERIFY(h.window->property("modalOpen").toBool());
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_VERIFY(h.view()->hasActiveFocus());
        // Clicking the current section keeps it marked (section items select, they do not toggle).
        h.viewer.trigger("settings"); QTRY_VERIFY(h.window->property("modalOpen").toBool());
        auto* nav = h.window->findChild<QQuickItem*>("settingsNav"); QVERIFY(nav);
        QQuickItem* current = nullptr;
        QTRY_VERIFY([&] { for (QQuickItem* item : descendants(nav)) if (item->property("selected").toBool()) { current = item; return true; } return false; }());
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, current->mapToScene(QPointF(current->width() / 2, current->height() / 2)).toPoint());
        QTest::qWait(30);
        QVERIFY(current->property("selected").toBool());
        QCOMPARE(dialog->property("section").toString(), QString("controls"));
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_VERIFY(!h.window->property("modalOpen").toBool());
        // Optional review captures of every section, wide and compact, both modes (set GLANCELOT_REVIEW_CAPTURE).
        if (qEnvironmentVariableIsSet("GLANCELOT_REVIEW_CAPTURE")) {
            QDir().mkpath(".cache/review");
            for (const QSize size : {QSize(1180, 780), QSize(540, 360)}) {
                h.window->resize(size);
                for (const char* mode : {"dark", "light"}) {
                    h.theme.setMode(mode);
                    for (const char* section : {"appearance", "viewing", "browsing", "controls", "about"}) {
                        QVERIFY(QMetaObject::invokeMethod(dialog, "openSection", Q_ARG(QVariant, QString(section)), Q_ARG(QVariant, false)));
                        QTest::qWait(300); // past the 150-200 ms state transitions
                        QVERIFY(h.window->grabWindow().save(QString(".cache/review/settings-%1-%2-%3.png").arg(section, mode).arg(size.width())));
                    }
                }
            }
        }
    }
    void paletteKeyboardCursorStaysInViewport() {
        Harness h; QVERIFY(h.window); h.window->resize(540,360); h.window->requestActivate();
        QObject* dialog=h.window->findChild<QObject*>("settingsDialog"); QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog,"openSection",Q_ARG(QVariant,QString("appearance")),Q_ARG(QVariant,false)));
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto* palettes=h.window->findChild<QQuickItem*>("paletteGrid"); QVERIFY(palettes);
        QTest::qWait(200); // let the dialog's deferred initial section focus settle
        palettes->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(palettes->hasActiveFocus());
        for(int i=0;i<16;++i) QTest::keyClick(h.window,Qt::Key_Down);
        QTRY_VERIFY(palettes->property("currentIndex").toInt()>15);
        auto* current=qvariant_cast<QQuickItem*>(palettes->property("currentItem")); QVERIFY(current);
        QTRY_VERIFY2([&] {
            for(auto* ancestor=palettes->parentItem();ancestor;ancestor=ancestor->parentItem()) {
                if(!ancestor->clip())continue;
                const auto point=current->mapToItem(ancestor,QPointF(current->width()/2,current->height()/2));
                if(point.y()<0 || point.y()>ancestor->height()) return false;
            }
            return true;
        }(),"Keyboard palette cursor is outside its scroll viewport");
    }
    void wheelModifiersOverrideBaseMode() {
        QTemporaryDir media; QVERIFY(media.isValid());
        QImage image(1600,1200,QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(media.filePath("wheel.png")));
        Harness h(true); QVERIFY(h.window); h.viewer.open(media.filePath("wheel.png")); QTRY_VERIFY(h.viewer.imageReady());
        auto* canvas=h.window->findChild<ImageCanvas*>("canvas"); QVERIFY(canvas); canvas->actualSize();
        h.viewer.setWheelMode("zoom");
        const QPointF point(500,400); const auto before=canvas->zoom();
        QWheelEvent shift(point,h.window->mapToGlobal(point.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::ShiftModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(h.window,&shift);
        QCOMPARE(canvas->zoom(),before);
        h.viewer.setWheelMode("pan");
        QWheelEvent control(point,h.window->mapToGlobal(point.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(h.window,&control);
        QVERIFY(canvas->zoom()>before);
    }
    void keyboardFocusedToolbarStaysVisible() {
        QTemporaryDir media; QVERIFY(media.isValid());
        QImage image(16,16,QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(media.filePath("focus.png")));
        Harness h(true); QVERIFY(h.window); h.viewer.open(media.filePath("focus.png"));
        QTRY_VERIFY(h.viewer.imageReady());
        h.viewer.setFilmstripVisible(false); h.viewer.setTreeVisible(false);
        h.window->requestActivate(); QTest::mouseMove(h.window,QPoint(500,400));
        auto* settings=h.window->findChild<QQuickItem*>("settingsButton"); QVERIFY(settings);
        settings->forceActiveFocus(Qt::TabFocusReason); QTRY_VERIFY(settings->hasActiveFocus());
        QVERIFY(QMetaObject::invokeMethod(h.window,"wakeHud"));
        QTest::qWait(2600);
        QVERIFY(settings->hasActiveFocus()); QVERIFY(settings->isVisible());
        QVERIFY(h.window->property("hudVisible").toBool());
    }
    void auditEverySettingsPageAndChoiceMenu() {
        QTemporaryDir media; QVERIFY(media.isValid());
        QImage image(48, 32, QImage::Format_RGB32); image.fill(Qt::cyan);
        const QString source = media.filePath("audit-photo.png"); QVERIFY(image.save(source));
        QFile original(source); QVERIFY(original.open(QIODevice::ReadOnly)); const QByteArray bytes = original.readAll(); original.close();
        Harness h(true); QVERIFY(h.window);
        h.viewer.open(source); QTRY_VERIFY(h.viewer.imageReady());
        QObject* dialog = h.window->findChild<QObject*>("settingsDialog"); QVERIFY(dialog);
        const bool capture = qEnvironmentVariableIsSet("GLANCELOT_REVIEW_CAPTURE");
        if (capture) QVERIFY(QDir().mkpath(".cache/phase1-ui"));
        auto shot = [&](const QString& name) {
            if (!capture) return true;
            return h.window->grabWindow().save(".cache/phase1-ui/" + name + ".png");
        };
        for (const QSize size : {QSize(540, 360), QSize(1180, 780)}) {
            h.window->resize(size); h.window->requestActivate();
            for (const char* mode : {"dark", "light"}) {
                h.theme.setMode(mode);
                const QString suffix = QString("-%1-%2").arg(mode).arg(size.width());
                QTest::qWait(200);
                QVERIFY(shot("canvas" + suffix));
                h.viewer.setTreeVisible(true); h.viewer.setTreePinned(true);
                h.folders.browseComputer(); h.folders.sync(h.viewer.snapshot());
                QTest::qWait(200); QVERIFY(shot("tree" + suffix));
                h.showGrid(); QTest::qWait(200); QVERIFY(shot("grid" + suffix));
                h.viewer.trigger("grid"); h.viewer.setTreeVisible(false);
                int choices = 0;
                for (const char* section : {"appearance", "viewing", "browsing", "controls", "about"}) {
                    QVERIFY(QMetaObject::invokeMethod(dialog, "openSection", Q_ARG(QVariant, QString(section)), Q_ARG(QVariant, false)));
                    QTRY_VERIFY(dialog->property("opened").toBool());
                    QTest::qWait(200);
                    QVERIFY(shot(QString("settings-%1").arg(section) + suffix));
                    QList<QQuickItem*> controls;
                    for (auto* item : descendants(h.window->contentItem())) {
                        if (item->isVisible() && item->isEnabled() && item->property("popup").isValid()
                            && item->property("currentIndex").isValid()) controls << item;
                    }
                    for (auto* control : controls) {
                        // Bring clipped settings rows into view before testing their actual menu.
                        for (auto* ancestor = control->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
                            if (!ancestor->property("contentY").isValid()) continue;
                            auto* content = qvariant_cast<QQuickItem*>(ancestor->property("contentItem"));
                            if (!content) continue;
                            const qreal y = control->mapToItem(content, QPointF()).y();
                            const qreal maximum = qMax<qreal>(0, ancestor->property("contentHeight").toReal() - ancestor->height());
                            ancestor->setProperty("contentY", qBound<qreal>(0, y - ancestor->height() / 3, maximum));
                            break;
                        }
                        control->forceActiveFocus(Qt::TabFocusReason); QTest::qWait(30);
                        auto* popup = qvariant_cast<QObject*>(control->property("popup")); QVERIFY(popup);
                        const int before = control->property("currentIndex").toInt();
                        QTest::keyClick(h.window, Qt::Key_Space);
                        QTRY_VERIFY(popup->property("visible").toBool());
                        QTest::qWait(160);
                        QVERIFY(shot(QString("menu-%1-%2").arg(section).arg(choices) + suffix));
                        QTest::keyClick(h.window, Qt::Key_Escape);
                        QTRY_VERIFY(!popup->property("visible").toBool());
                        QCOMPARE(control->property("currentIndex").toInt(), before);
                        QVERIFY(dialog->property("opened").toBool());
                        ++choices;
                    }
                    QMetaObject::invokeMethod(dialog, "close");
                    QTRY_VERIFY(!h.window->property("modalOpen").toBool());
                }
                QCOMPARE(choices, 7); // appearance mode; fit/wheel; file order/folder boundary
            }
        }
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), bytes);
    }
    void startupStatusAndEmptyState() {
        {   // Fresh start: the empty state carries the "how to begin" hint, so the toast stays hidden.
            Harness h; QVERIFY(h.window);
            auto* toast = h.window->findChild<QQuickItem*>("statusToast"); QVERIFY(toast);
            QCOMPARE(h.viewer.status(), QString("Open a JPEG or PNG to begin.")); // Main.qml suppresses exactly this text
            QTest::qWait(50); QVERIFY(!toast->isVisible());
        }
        {   // A settings file from a newer version is reported at start-up, even though the empty state is showing.
            QFile settings(QDir(qEnvironmentVariable("GLANCELOT_CONFIG_DIR")).filePath("settings.json"));
            QVERIFY(settings.open(QIODevice::WriteOnly)); settings.write("{\"schema\":99}"); settings.close();
            Harness h; QVERIFY(h.window);
            auto* toast = h.window->findChild<QQuickItem*>("statusToast"); QVERIFY(toast);
            QVERIFY(h.viewer.status() != "Open a JPEG or PNG to begin.");
            QTRY_VERIFY(toast->isVisible());
        }
    }
    void pinButtonAnswersEnter() {
        Harness h; QVERIFY(h.window);
        h.window->resize(1180, 780); h.viewer.setTreeVisible(true); h.window->requestActivate();
        auto* pin = h.window->findChild<QQuickItem*>("treePin"); QVERIFY(pin);
        QTRY_VERIFY(pin->isVisible());
        QVERIFY(!h.viewer.treePinned());
        pin->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(h.window, Qt::Key_Return);
        QTRY_VERIFY(h.viewer.treePinned());
        h.viewer.setTreePinned(false); // a change made elsewhere (Settings) is reflected on the button
        QTRY_VERIFY(!pin->property("checked").toBool());
    }
    void treeRowClickEnterAndChevron() {
        QTemporaryDir media; QVERIFY(media.isValid());
        QDir(media.path()).mkpath("album/inner");
        QImage image(8, 8, QImage::Format_RGB32); image.fill(Qt::blue);
        QVERIFY(image.save(media.filePath("top.png"))); QVERIFY(image.save(media.filePath("album/inner.png")));
        Harness h(true); QVERIFY(h.window);
        QObject::connect(&h.viewer, &ViewerController::stateChanged, &h.folders, [&] { h.folders.sync(h.viewer.snapshot()); });
        h.window->resize(1180, 780); h.window->requestActivate();
        h.viewer.setTreeVisible(true); h.viewer.setTreePinned(true);
        h.folders.browseComputer();
        h.viewer.open(media.filePath("top.png"));
        QTRY_COMPARE(h.viewer.fileCount(), 1);
        QTRY_VERIFY(h.folders.currentRow() >= 0);
        auto* list = h.window->findChild<QQuickItem*>("folderList"); QVERIFY(list);
        auto rowFor = [&](const QString& path) -> QQuickItem* {
            for (QQuickItem* item : descendants(list))
                if (item->objectName() == "folderRow" && QDir::cleanPath(item->property("path").toString()) == QDir::cleanPath(path)) return item;
            return nullptr;
        };
        // Rows below the viewport exist but are clipped; scroll one into view before pointing at it.
        auto reveal = [&](const QString& path) -> QQuickItem* {
            QQuickItem* row = rowFor(path); if (!row) return nullptr;
            QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, row->property("index").toInt()), Q_ARG(int, 4 /* ListView.Contain */));
            QTest::qWait(30);
            return rowFor(path);
        };
        const int current = h.folders.currentRow();
        h.folders.expand(current);
        QTRY_VERIFY(rowFor(media.filePath("album")));
        QQuickItem* album = reveal(media.filePath("album"));
        QVERIFY(!album->property("expanded").toBool());
        // The chevron expands without opening the folder.
        const QString opened = h.viewer.filePath();
        const qreal chevronX = album->property("levelX").toReal() + 6;
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, album->mapToScene(QPointF(chevronX, album->height() / 2)).toPoint());
        QTRY_VERIFY(rowFor(media.filePath("album")) && rowFor(media.filePath("album"))->property("expanded").toBool());
        QCOMPARE(h.viewer.filePath(), opened);
        // A click on the name opens the folder (non-root rows navigate).
        album = reveal(media.filePath("album"));
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, album->mapToScene(QPointF(album->width() - 24, album->height() / 2)).toPoint());
        QTRY_VERIFY(h.viewer.filePath().endsWith("inner.png"));
        QTRY_VERIFY(list->hasActiveFocus());
        // Enter on the keyboard cursor opens the selected folder; the original folder is one row up.
        QTRY_VERIFY(rowFor(media.path()));
        list->setProperty("currentIndex", rowFor(media.path())->property("index"));
        QTest::keyClick(h.window, Qt::Key_Return);
        QTRY_VERIFY(h.viewer.filePath().endsWith("top.png"));
        // Double-click on a non-root row, as in Explorer's navigation pane: the first click opens the folder,
        // the double-click toggles its expansion exactly once.
        QTRY_VERIFY(rowFor(media.filePath("album")));
        album = reveal(media.filePath("album"));
        const bool wasExpanded = album->property("expanded").toBool();
        QTest::mouseDClick(h.window, Qt::LeftButton, Qt::NoModifier, album->mapToScene(QPointF(album->width() - 24, album->height() / 2)).toPoint());
        QTRY_VERIFY(h.viewer.filePath().endsWith("inner.png"));
        QTRY_VERIFY(rowFor(media.filePath("album")) && rowFor(media.filePath("album"))->property("expanded").toBool() != wasExpanded);
        // Double-clicking the chevron toggles once too, instead of collapsing and relisting the folder.
        QTest::qWait(400); // past the double-click interval
        album = reveal(media.filePath("album"));
        const bool beforeChevron = album->property("expanded").toBool();
        QTest::mouseDClick(h.window, Qt::LeftButton, Qt::NoModifier, album->mapToScene(QPointF(album->property("levelX").toReal() + 6, album->height() / 2)).toPoint());
        QTest::qWait(100);
        QTRY_VERIFY(rowFor(media.filePath("album")) && rowFor(media.filePath("album"))->property("expanded").toBool() != beforeChevron);
    }
    void resizeTreeAndEmpty() {
        Harness h; QVERIFY(h.window); h.fill(50); h.showGrid(); QTRY_VERIFY(h.view()->hasActiveFocus());
        for(bool dark:{false,true}) {
            h.theme.setMode(dark ? "dark" : "light"); QCOMPARE(h.theme.dark(), dark);
            for(const QSize size:{QSize(540,360),QSize(1180,780),QSize(1920,1080)}) {
                h.window->resize(size); QTest::qWait(30);
                QVERIFY(h.grid()->width()>0); QVERIFY(h.grid()->height()>0);
                QVERIFY(h.grid()->property("columns").toInt()>=1);
                h.viewer.setTreeVisible(true); h.viewer.setTreePinned(true); QTest::qWait(30);
                auto* tree=h.window->findChild<QQuickItem*>("folderTree");
                QCOMPARE(h.grid()->x(),size.width()>=720 ? tree->width():0.0);
                QVERIFY(h.grid()->width()>0); h.viewer.setTreeVisible(false);
            }
        }
        h.fill(0); QTRY_COMPARE(h.selected(),-1); QTRY_COMPARE(h.view()->property("count").toInt(),0);
        QTest::keyClick(h.window,Qt::Key_Return); QVERIFY(h.window->property("gridMode").toBool());
    }
public: // helpers (moc treats everything under private slots as test functions)
    // ---- FILE-GRID-01: mixed folders, photos and other files ----
    static QQuickItem* named(QQuickItem* root, const QString& name) { for (auto* item : descendants(root)) if (item->objectName() == name) return item; return nullptr; }
    static QPoint centre(QQuickItem* item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }
    // Generated mixed folder: subfolders, a directory link where creatable, photos and unsupported files.
    struct MixedFolder {
        QTemporaryDir root; QString path; bool link{};
        MixedFolder() {
            path = root.filePath("Holiday 2026");
            QDir().mkpath(path + "/Day 1"); QDir().mkpath(path + "/Day 2/inner"); QDir().mkpath(root.filePath("elsewhere"));
            QFile log(path + "/Day 1/log.txt"); if (log.open(QIODevice::WriteOnly)) log.write("x");
            QImage image(48, 32, QImage::Format_RGB32); image.fill(QColor(70, 120, 170));
            for (const char* name : {"beach.png", "sunset.jpg", "broken-preview.png"}) image.save(path + "/" + name);
            for (const char* name : {"notes.txt", "itinerary.pdf", "README"}) { QFile file(path + "/" + name); if (file.open(QIODevice::WriteOnly)) file.write("not a photo"); }
#ifdef Q_OS_WIN
            QProcess mklink; mklink.start("cmd", {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(path + "/Shortcut to elsewhere"), QDir::toNativeSeparators(root.filePath("elsewhere"))});
            link = mklink.waitForFinished(10000) && mklink.exitCode() == 0;
#else
            link = QFile::link(root.filePath("elsewhere"), path + "/Shortcut to elsewhere");
#endif
            if (!link) qInfo() << "UNVERIFIED: directory link fixture could not be created";
        }
        QString file(const QString& name) const { return path + "/" + name; }
    };
    QQuickItem* gridCell(Harness& h, const QString& path) {
        // Polls: after a relisting the delegate for `path` may not exist yet. Callers QVERIFY the result.
        for (int attempt = 0; attempt < 100; ++attempt) {
            const int row = h.fileGrid.loading() ? -1 : h.fileGrid.rowOf(path);
            if (row >= 0) {
                QMetaObject::invokeMethod(h.view(), "positionViewAtIndex", Q_ARG(int, row), Q_ARG(int, 4 /* Contain */));
                QTest::qWait(30);
                if (QQuickItem* cell = h.cell(row)) return cell;
            } else QTest::qWait(20);
        }
        return nullptr;
    }
private slots:
    void mixedTilesFolderEntryAndParentNavigation() {
        MixedFolder media; QVERIFY(media.root.isValid());
        Harness h(true); QVERIFY(h.window); h.window->resize(1180, 780); h.window->requestActivate();
        QObject::connect(&h.viewer, &ViewerController::stateChanged, &h.folders, [&] { h.folders.sync(h.viewer.snapshot()); });
        h.viewer.setTreeVisible(true); h.viewer.setTreePinned(true); h.folders.browseComputer();
        h.viewer.open(media.file("beach.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        h.showGrid(); QTRY_VERIFY(h.view()->hasActiveFocus());
        const int total = media.link ? 9 : 8;
        QTRY_COMPARE(h.fileGrid.count(), total);
        // Folders first (natural), then every file in the viewer's name order; the cursor lands on the viewed photo.
        QStringList kinds; for (int i = 0; i < total; ++i) kinds << h.fileGrid.kindAt(i);
        const int folders = media.link ? 3 : 2;
        for (int i = 0; i < folders; ++i) QCOMPARE(kinds[i], QString("folder"));
        QCOMPARE(h.fileGrid.pathAt(folders), media.file("beach.png"));
        QTRY_VERIFY(!h.fileGrid.loading() && h.fileGrid.count() >= 8); // rowOf is -1 until the listing lands
        QTRY_COMPARE(h.selected(), h.fileGrid.rowOf(media.file("beach.png")));
        QCOMPARE(h.fileGrid.kindAt(h.fileGrid.rowOf(media.file("notes.txt"))), QString("file"));
        QCOMPARE(h.fileGrid.kindAt(h.fileGrid.rowOf(media.file("sunset.jpg"))), QString("image"));
        // Tiles: generic file icon with an extension label; link badge on the directory link; image previews only.
        QQuickItem* notes = gridCell(h, media.file("notes.txt")); QVERIFY(notes);
        QQuickItem* ext = named(notes, "gridExtension"); QVERIFY(ext); QVERIFY(ext->isVisible()); QCOMPARE(ext->property("text").toString(), QString("TXT"));
        QVERIFY(!named(notes, "gridLinkBadge")->isVisible());
        if (media.link) { QQuickItem* link = gridCell(h, media.file("Shortcut to elsewhere")); QVERIFY(link); QVERIFY(named(link, "gridLinkBadge")->isVisible()); }
        else qInfo() << "UNVERIFIED: link badge (no link fixture)";
        QTest::qWait(150);
        for (const auto& requested : h.provider->requested())
            QVERIFY2(requested.endsWith(".png") || requested.endsWith(".jpg"), qPrintable("non-image reached the thumbnail provider: " + requested));
        // Enter on a folder navigates through the controller; the grid stays open and the tree and path bar follow.
        h.fileGrid.select(h.fileGrid.rowOf(media.file("Day 2")));
        QTest::keyClick(h.window, Qt::Key_Return);
        QTRY_COMPARE(gridKey(h.viewer.directory()), gridKey(media.file("Day 2")));
        QTRY_COMPARE(gridKey(h.fileGrid.directory()), gridKey(media.file("Day 2")));
        QVERIFY(h.window->property("gridMode").toBool());
        QTRY_COMPARE(h.fileGrid.count(), 1); QCOMPARE(h.fileGrid.kindAt(0), QString("folder"));
        QTRY_VERIFY(h.folders.currentRow() >= 0);
        QCOMPARE(gridKey(h.folders.pathAt(h.folders.currentRow())), gridKey(media.file("Day 2")));
        auto* address = h.window->findChild<QQuickItem*>("folderAddress"); QVERIFY(address);
        QTRY_COMPARE(gridKey(address->property("text").toString()), gridKey(media.file("Day 2")));
        QTRY_VERIFY(h.view()->hasActiveFocus());
        // Backspace returns to the parent with the cursor on the folder just left; Alt+Up goes one more level.
        QTest::keyClick(h.window, Qt::Key_Backspace);
        QTRY_COMPARE(gridKey(h.fileGrid.directory()), gridKey(media.path));
        QTRY_COMPARE(h.fileGrid.count(), total);
        QTRY_COMPARE(h.selected(), h.fileGrid.rowOf(media.file("Day 2")));
        QTest::keyClick(h.window, Qt::Key_Up, Qt::AltModifier);
        QTRY_COMPARE(gridKey(h.fileGrid.directory()), gridKey(media.root.path()));
        QTRY_COMPARE(h.selected(), h.fileGrid.rowOf(media.path));
        QTRY_COMPARE(gridKey(address->property("text").toString()), gridKey(media.root.path()));
        // Mouse: double-click enters a folder, the header's Up button returns.
        QQuickItem* holiday = gridCell(h, media.path); QVERIFY(holiday);
        QTest::mouseDClick(h.window, Qt::LeftButton, Qt::NoModifier, centre(holiday));
        QTRY_COMPARE(gridKey(h.fileGrid.directory()), gridKey(media.path));
        QVERIFY(h.window->property("gridMode").toBool());
        auto* up = named(h.window->contentItem(), "gridParent"); QVERIFY(up); QVERIFY(up->isEnabled());
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, centre(up));
        QTRY_COMPARE(gridKey(h.fileGrid.directory()), gridKey(media.root.path()));
        // A directory link is listed but is never entered: the controller refuses it and the grid explains why.
        if (media.link) {
            QQuickItem* again = gridCell(h, media.path); QVERIFY(again);
            QTest::mouseDClick(h.window, Qt::LeftButton, Qt::NoModifier, centre(again));
            QTRY_COMPARE(gridKey(h.fileGrid.directory()), gridKey(media.path));
            QTRY_COMPARE(h.fileGrid.count(), total);
            h.fileGrid.select(h.fileGrid.rowOf(media.file("Shortcut to elsewhere")));
            QTest::keyClick(h.window, Qt::Key_Return);
            QTRY_VERIFY(!h.viewer.directoryPending() && !h.viewer.directoryError().isEmpty());
            QCOMPARE(gridKey(h.fileGrid.directory()), gridKey(media.path));
            auto* notice = named(h.window->contentItem(), "gridNotice"); QVERIFY(notice); QTRY_VERIFY(notice->isVisible());
        }
    }
    void unsupportedActivationNeverDecodes() {
        MixedFolder media; QVERIFY(media.root.isValid());
        Harness h(true); QVERIFY(h.window); h.window->resize(1180, 780);
        h.viewer.open(media.file("beach.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        h.showGrid(); QTRY_VERIFY(h.view()->hasActiveFocus());
        QTRY_VERIFY(!h.fileGrid.loading() && h.fileGrid.count() >= 8); // rowOf is -1 until the listing lands
        QTRY_COMPARE(h.selected(), h.fileGrid.rowOf(media.file("beach.png")));
        const QString opened = h.viewer.filePath();
        QSignalSpy images(&h.viewer, &ViewerController::imageChanged), states(&h.viewer, &ViewerController::stateChanged);
        auto* notice = named(h.window->contentItem(), "gridNotice"); QVERIFY(notice); QVERIFY(!notice->isVisible());
        for (const char* name : {"notes.txt", "README", "itinerary.pdf"}) {
            h.fileGrid.select(h.fileGrid.rowOf(media.file(name)));
            QTest::keyClick(h.window, Qt::Key_Return);
            QTRY_VERIFY(notice->isVisible());
            QVERIFY(notice->property("text").toString().contains(name));
            QVERIFY(!h.viewer.loading()); QCOMPARE(h.viewer.filePath(), opened);
        }
        QQuickItem* notes = gridCell(h, media.file("notes.txt")); QVERIFY(notes);
        QTest::mouseDClick(h.window, Qt::LeftButton, Qt::NoModifier, centre(notes));
        QTest::qWait(150);
        QVERIFY(h.window->property("gridMode").toBool()); // still browsing
        QCOMPARE(images.count(), 0); QCOMPARE(states.count(), 0); // the controller was never asked to open anything
        auto* openButton = named(h.window->contentItem(), "gridOpenSelected");
        QVERIFY(openButton); QVERIFY(!openButton->isEnabled()); // the cursor is on notes.txt
        for (const auto& requested : h.provider->requested()) {
            QVERIFY2(!requested.endsWith(".txt") && !requested.endsWith(".pdf") && !requested.endsWith("README"), qPrintable(requested));
        }
    }
    void multiSelectionKeyboardMouseAndCount() {
        Harness h; QVERIFY(h.window); h.window->resize(1180, 780); h.fill(30, 0); h.showGrid();
        QTRY_VERIFY(h.view()->hasActiveFocus()); QTRY_COMPARE(h.selected(), 0);
        // Grid keys win over window shortcuts remapped onto the same chords.
        QVERIFY(h.viewer.setBinding("tree", "Ctrl+A"));
        QVERIFY(h.viewer.setBinding("sort-reverse", ""));
        QVERIFY(h.viewer.setBinding("sort", "Shift+Right"));
        auto* label = named(h.window->contentItem(), "gridSelectionCount"); QVERIFY(label);
        QVERIFY(!label->isVisible());
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, centre(h.cell(2)));
        QCOMPARE(h.fileGrid.selectedCount(), 1); QCOMPARE(h.selected(), 2); QVERIFY(!label->isVisible());
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::ControlModifier, centre(h.cell(4)));
        QCOMPARE(h.fileGrid.selectedCount(), 2); QTRY_VERIFY(label->isVisible());
        QCOMPARE(label->property("text").toString(), QString("2 selected"));
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::ShiftModifier, centre(h.cell(7)));   // anchor 4 .. 7 replaces
        QCOMPARE(h.fileGrid.selectedCount(), 4); QCOMPARE(h.selected(), 7);
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier, centre(h.cell(0))); // adds 0 .. 4
        QCOMPARE(h.fileGrid.selectedCount(), 8); QCOMPARE(label->property("text").toString(), QString("8 selected"));
        QTest::keyClick(h.window, Qt::Key_Right, Qt::ShiftModifier);  // keyboard range from the anchor (4) to 1
        QCOMPARE(h.selected(), 1); QCOMPARE(h.fileGrid.selectedCount(), 4);
        QCOMPARE(h.viewer.sortKey(), QString("name"));
        QTest::keyClick(h.window, Qt::Key_End, Qt::ShiftModifier);
        QCOMPARE(h.selected(), 29); QCOMPARE(h.fileGrid.selectedCount(), 26);
        QTest::keyClick(h.window, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(h.fileGrid.selectedCount(), 30); QVERIFY(!h.viewer.treeVisible());
        QTest::keyClick(h.window, Qt::Key_Space, Qt::ControlModifier);
        QCOMPARE(h.fileGrid.selectedCount(), 29);
        QVERIFY(!h.fileGrid.data(h.fileGrid.index(29), DirectoryEntryModel::SelectedRole).toBool());
        // The status bar mirrors the count.
        bool statusShowsCount = false;
        const auto inHeader = descendants(label);
        for (auto* item : descendants(h.window->contentItem()))
            if (item->isVisible() && !inHeader.contains(item) && item->property("text").toString() == "29 selected") statusShowsCount = true;
        QVERIFY(statusShowsCount);
        // Plain arrows collapse to one item; Escape clears a multi-selection first, then leaves the grid.
        QTest::keyClick(h.window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(h.window, Qt::Key_Escape);
        QCOMPARE(h.fileGrid.selectedCount(), 0); QVERIFY(h.window->property("gridMode").toBool()); QCOMPARE(h.selected(), 29);
        QTest::keyClick(h.window, Qt::Key_Left); QCOMPARE(h.fileGrid.selectedCount(), 1); QCOMPARE(h.selected(), 28);
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::ControlModifier, centre(h.cell(27)));
        QTRY_VERIFY(label->isVisible());
        auto* clear = named(label, "gridClearSelection"); QVERIFY(clear);
        QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, centre(clear));
        QCOMPARE(h.fileGrid.selectedCount(), 0); QTRY_VERIFY(h.view()->hasActiveFocus());
        QTest::keyClick(h.window, Qt::Key_Escape);
        QTRY_VERIFY(!h.window->property("gridMode").toBool());
    }
    void externalRenameAndDeleteRefreshKeepSelection() {
        MixedFolder media; QVERIFY(media.root.isValid());
        Harness h(true); QVERIFY(h.window);
        h.viewer.open(media.file("beach.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        h.showGrid(); QTRY_VERIFY(h.view()->hasActiveFocus());
        QTRY_VERIFY(!h.fileGrid.loading() && h.fileGrid.count() >= 8); // rowOf is -1 until the listing lands
        QTRY_COMPARE(h.selected(), h.fileGrid.rowOf(media.file("beach.png")));
        // The cursor item (notes.txt) is deleted below; the surviving selected item must stay selected.
        h.fileGrid.select(h.fileGrid.rowOf(media.file("sunset.jpg")));
        h.fileGrid.select(h.fileGrid.rowOf(media.file("README")), DirectoryEntryModel::Toggle);
        h.fileGrid.select(h.fileGrid.rowOf(media.file("notes.txt")), DirectoryEntryModel::Toggle);
        QCOMPARE(h.fileGrid.selectedCount(), 3);
        QVERIFY(QFile::rename(media.file("README"), media.file("README.md")));
        QVERIFY(QFile::remove(media.file("itinerary.pdf")));
        QVERIFY(QFile::remove(media.file("notes.txt")));
        { QFile added(media.file("added.txt")); QVERIFY(added.open(QIODevice::WriteOnly)); }
        // The controller's folder watcher invalidates; the grid relists in place.
        QTRY_VERIFY_WITH_TIMEOUT(h.fileGrid.rowOf(media.file("added.txt")) >= 0, 10000);
        QTRY_COMPARE(h.fileGrid.rowOf(media.file("itinerary.pdf")), -1);
        QCOMPARE(h.fileGrid.rowOf(media.file("README")), -1);
        QVERIFY(h.fileGrid.rowOf(media.file("README.md")) >= 0);
        QTRY_COMPARE(h.fileGrid.rowOf(media.file("notes.txt")), -1);
        QTest::qWait(150); // past the grid's post-listing cursor placement
        QCOMPARE(h.fileGrid.selectedPaths(), QStringList{media.file("sunset.jpg")});
        QCOMPARE(h.selected(), -1); // the cursor's file is gone; nothing else was selected in its place
        QVERIFY(h.window->property("gridMode").toBool());
    }
    // Review I3: ThumbnailService accepts one epoch (the filmstrip's); grid thumbnail URLs must carry the same one
    // through folder changes and on-disk invalidation, or every grid preview is rejected in the real application.
    void gridThumbnailEpochFollowsFilmstrip() {
        QTemporaryDir one, two; QVERIFY(one.isValid() && two.isValid());
        QImage image(16, 12, QImage::Format_RGB32); image.fill(Qt::darkCyan);
        QVERIFY(image.save(one.filePath("a.png"))); QVERIFY(image.save(two.filePath("b.png")));
        Harness h(true); QVERIFY(h.window);
        const auto thumbEpoch = [&]() -> qint64 {
            QQuickItem* cell = h.cell(0); if (!cell) return -1;
            for (auto* item : descendants(cell)) if (item->objectName().startsWith("gridThumb")) {
                const auto parts = thumbnail_id::decode(item->property("source").toUrl().toString().mid(QString("image://thumb/").size()));
                return parts ? qint64(parts->epoch) : -1;
            }
            return -1;
        };
        h.viewer.open(one.filePath("a.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        h.showGrid(); QTRY_VERIFY(!h.fileGrid.loading() && h.fileGrid.count() == 1);
        QVERIFY(h.model.epoch() > 1); // the first folder sync already advanced the filmstrip epoch
        QCOMPARE(h.fileGrid.epoch(), h.model.epoch());
        QTRY_COMPARE(thumbEpoch(), qint64(h.model.epoch()));
        h.viewer.open(two.filePath("b.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady() && h.viewer.filePath().endsWith("b.png"), 15000);
        QTRY_VERIFY(!h.fileGrid.loading() && h.fileGrid.rowOf(two.filePath("b.png")) == 0);
        QCOMPARE(h.fileGrid.epoch(), h.model.epoch());
        QTRY_COMPARE(thumbEpoch(), qint64(h.model.epoch()));
        const quint64 before = h.model.epoch();
        QImage changed(16, 12, QImage::Format_RGB32); changed.fill(Qt::darkRed); QVERIFY(changed.save(two.filePath("b.png")));
        QTRY_VERIFY_WITH_TIMEOUT(h.model.epoch() > before, 10000); // the controller's watcher invalidated
        QCOMPARE(h.fileGrid.epoch(), h.model.epoch());
        QTRY_COMPARE(thumbEpoch(), qint64(h.model.epoch()));
    }
    void emptyInaccessibleAndNoFolderStates() {
        QTemporaryDir empty; QVERIFY(empty.isValid());
        Harness h; QVERIFY(h.window); h.window->resize(540, 360);
        auto* state = named(h.window->contentItem(), "gridState"); QVERIFY(state);
        h.showGrid(); QTRY_VERIFY(h.grid()->isVisible());
        QTRY_COMPARE(state->property("stateName").toString(), QString("none"));
        h.fileGrid.setDirectory(empty.path());
        QTRY_COMPARE(state->property("stateName").toString(), QString("empty")); QVERIFY(state->isVisible());
        QTest::keyClick(h.window, Qt::Key_Return); QVERIFY(h.window->property("gridMode").toBool()); // nothing to activate
        h.fileGrid.setDirectory(empty.filePath("gone"));
        QTRY_COMPARE(state->property("stateName").toString(), QString("inaccessible"));
        const bool capture = qEnvironmentVariableIsSet("GLANCELOT_REVIEW_CAPTURE");
        if (capture) {
            QDir().mkpath(".cache/file-grid");
            for (const auto& mode : {QString("dark"), QString("light")}) {
                h.theme.setMode(mode); QTest::qWait(120);
                QVERIFY(h.window->grabWindow().save(".cache/file-grid/inaccessible-" + mode + "-540.png"));
            }
            h.fileGrid.setDirectory(empty.path()); QTRY_COMPARE(state->property("stateName").toString(), QString("empty"));
            QTest::qWait(120); QVERIFY(h.window->grabWindow().save(".cache/file-grid/empty-light-540.png"));
        }
    }
    void reviewCapturesMixedFolder() {
        if (!qEnvironmentVariableIsSet("GLANCELOT_REVIEW_CAPTURE")) QSKIP("Set GLANCELOT_REVIEW_CAPTURE to write review captures");
        MixedFolder media; QVERIFY(media.root.isValid());
        for (int i = 0; i < 14; ++i) { QImage image(40, 30, QImage::Format_RGB32); image.fill(Qt::gray); image.save(media.file(QString("IMG_%1.png").arg(1000 + i))); }
        Harness h(true); QVERIFY(h.window); h.provider->succeed = true;
        h.viewer.open(media.file("beach.png")); QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(), 15000);
        h.showGrid(); QTRY_VERIFY(h.view()->hasActiveFocus());
        QTRY_VERIFY(h.fileGrid.count() >= 22);
        h.folders.browseComputer(); h.folders.sync(h.viewer.snapshot());
        h.fileGrid.select(h.fileGrid.rowOf(media.file("Day 2")));
        h.fileGrid.select(h.fileGrid.rowOf(media.file("beach.png")), DirectoryEntryModel::Range);
        QDir().mkpath(".cache/file-grid");
        for (const QSize size : {QSize(540, 360), QSize(1180, 780)}) for (const auto& mode : {QString("dark"), QString("light")}) {
            h.window->resize(size); h.theme.setMode(mode);
            h.viewer.setTreeVisible(size.width() > 1000); h.viewer.setTreePinned(true);
            QMetaObject::invokeMethod(h.view(), "positionViewAtBeginning"); QTest::qWait(300);
            QMetaObject::invokeMethod(h.grid(), "focusGrid"); QTest::qWait(100);
            QVERIFY(h.window->grabWindow().save(QString(".cache/file-grid/mixed-%1-%2.png").arg(mode).arg(size.width())));
        }
        h.fileGrid.select(h.fileGrid.rowOf(media.file("notes.txt")));
        QMetaObject::invokeMethod(h.view(), "positionViewAtIndex", Q_ARG(int, h.fileGrid.cursorRow()), Q_ARG(int, 4));
        QMetaObject::invokeMethod(h.grid(), "focusGrid"); QTRY_VERIFY(h.view()->hasActiveFocus());
        QTest::keyClick(h.window, Qt::Key_Return); QTRY_VERIFY(named(h.window->contentItem(), "gridNotice")->isVisible()); QTest::qWait(200);
        QVERIFY(h.window->grabWindow().save(".cache/file-grid/notice-light-1180.png"));
        h.provider->succeed = false;
    }
private:
    std::unique_ptr<QTemporaryDir> config;
};

QTEST_MAIN(GridUiTests)
#include "grid_ui_tests.moc"





