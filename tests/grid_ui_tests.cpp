#include "glancelot/pane_layout.hpp"
#include "glancelot/filmstrip_model.hpp"
#include "glancelot/folder_tree_model.hpp"
#include "glancelot/settings_file.hpp"
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

class PreviewProvider final : public QQuickImageProvider {
public:
    PreviewProvider() : QQuickImageProvider(Image) {}
    std::atomic_int requests{0};
    QImage requestImage(const QString&, QSize* size, const QSize& requested) override {
        ++requests;
        if (size) *size = QSize(32, 24);
        Q_UNUSED(requested);
        // Deliberately fail previews; each cell must remain named and activatable.
        return {};
    }
};

struct Harness {
    QTemporaryDir config;
    ViewerController viewer;
    ThemeManager theme;
    FilmstripModel model;
    FolderTreeModel folders;
    PaneLayout layout;
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
        // Tests that inject a listing with fill() must not be overwritten by the (empty) controller snapshot.
        if (live) QObject::connect(&viewer, &ViewerController::stateChanged, &model, [&] { model.sync(viewer.snapshot()); });
        engine.load(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Main.qml")));
        if (!engine.rootObjects().empty()) window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    }
    QQuickItem* grid() const { return window->findChild<QQuickItem*>("photoGrid"); }
    QQuickItem* view() const { return window->findChild<QQuickItem*>("photoGridView"); }
    void showGrid() { viewer.trigger("grid"); window->requestActivate(); }
    void fill(int count, int current = 0) {
        glancelot::NavigationSnapshot snapshot;
        snapshot.directory = "generated";
        for (int i=0;i<count;++i) snapshot.files.emplace_back("generated/Photo " + std::to_string(i) + ".png");
        if (count) snapshot.current = snapshot.files[current];
        model.sync(snapshot);
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
    void init() {
        config = std::make_unique<QTemporaryDir>();
        QVERIFY(config->isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8());
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
        h.showGrid(); QTRY_VERIFY(h.view()->hasActiveFocus());
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
        Harness h; QVERIFY(h.window); h.fill(100, 0); h.showGrid(); QTRY_VERIFY(h.view()->hasActiveFocus());
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
                QCOMPARE(choices, 5); // appearance mode; fit/wheel; file order/folder boundary
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
private:
    std::unique_ptr<QTemporaryDir> config;
};

QTEST_MAIN(GridUiTests)
#include "grid_ui_tests.moc"





