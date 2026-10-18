#include "glancelot/filmstrip_model.hpp"
#include "glancelot/folder_tree_model.hpp"
#include "glancelot/settings_file.hpp"
#include <QJsonObject>
#include "glancelot/theme_manager.hpp"
#include "glancelot/viewer_controller.hpp"
#include "image_canvas.hpp"
#include <QGuiApplication>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSignalSpy>
#include <QTemporaryDir>
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
    void initTestCase() { qmlRegisterType<ImageCanvas>("Glancelot", 1, 0, "ImageCanvas"); }
    void init() {
        config = std::make_unique<QTemporaryDir>();
        QVERIFY(config->isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8());
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





