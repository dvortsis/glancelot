#include <QtTest>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QFontDatabase>
#include <QDir>
#include <QSignalSpy>
#include <QQuickImageProvider>
#include "glancelot/viewer_controller.hpp"
#include "glancelot/theme_manager.hpp"
#include "glancelot/pane_layout.hpp"
#include "glancelot/monitor_customization.hpp"
#include "glancelot/filmstrip_model.hpp"
#include "glancelot/folder_tree_model.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "glancelot/file_operation_service.hpp"
#include "file_grid_wiring.hpp"
#include "image_canvas.hpp"

class MonitorTestThumbnails final:public QQuickImageProvider {
public:MonitorTestThumbnails():QQuickImageProvider(Image){}
    QImage requestImage(const QString&,QSize* size,const QSize&) override {QImage image(24,24,QImage::Format_RGB32);image.fill(Qt::gray);if(size)*size=image.size();return image;}
};
class MonitorCustomizationUiTests:public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> config;
    struct Harness {
        ViewerController viewer;ThemeManager theme;PaneLayout layout;FilmstripModel filmstrip;FolderTreeModel tree;
        DirectoryEntryModel grid;FileOperationService operations;
        glancelot::MonitorCustomization monitor{viewer.displayManager(),&layout};
        QQmlApplicationEngine engine;QQuickWindow* window{};
        Harness(){
            auto imports=engine.importPathList();imports.removeAll(QCoreApplication::applicationDirPath());engine.setImportPathList(imports);
            engine.addImageProvider("thumb",new MonitorTestThumbnails);
            layout.setSlot(1);monitor.setSlot(1);
            file_grid_wiring::followViewer(viewer,grid);file_grid_wiring::lockThumbnailEpoch(filmstrip,grid);
            engine.rootContext()->setContextProperty("viewer",&viewer);engine.rootContext()->setContextProperty("theme",&theme);
            engine.rootContext()->setContextProperty("paneLayout",&layout);engine.rootContext()->setContextProperty("monitorCustomization",&monitor);
            engine.rootContext()->setContextProperty("filmstrip",&filmstrip);engine.rootContext()->setContextProperty("folderTree",&tree);
            engine.rootContext()->setContextProperty("fileGrid",&grid);engine.rootContext()->setContextProperty("fileOperations",&operations);
            engine.load(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Main.qml")));
            if(!engine.rootObjects().isEmpty())window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            monitor.setControllerWindow(window);
            QObject::connect(&viewer,&ViewerController::configurationReloaded,&monitor,&glancelot::MonitorCustomization::reloadPreferences);
        }
    };
private slots:
    void initTestCase(){for(const auto& font:QDir(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/assets/fonts/geist")).entryList({"*.ttf"},QDir::Files))QFontDatabase::addApplicationFont(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/assets/fonts/geist/")+font);QQuickStyle::setStyle("Basic");qmlRegisterType<ImageCanvas>("Glancelot",1,0,"ImageCanvas");qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")),"Glancelot",1,0,"Theme");}
    void init(){config=std::make_unique<QTemporaryDir>();qputenv("GLANCELOT_CONFIG_DIR",config->path().toUtf8());}
    void cleanup(){qunsetenv("GLANCELOT_CONFIG_DIR");config.reset();}
    void everyRealPaneMigratesAndReturnsWithoutDuplication() {
        Harness h;QVERIFY(h.window);h.window->resize(1180,780);h.window->show();
        const auto display=h.viewer.displayManager()->displays().first().id;
        const QStringList panes{"tree","grid","preview","metadata"},objects{"folderTree","photoGrid","canvas","metadataHud"};
        for(int i=0;i<panes.size();++i){auto* item=h.window->findChild<QQuickItem*>(objects[i]);QVERIFY(item);auto* original=item;
            QVERIFY(h.monitor.detachPane(panes[i],display));QTRY_VERIFY(item->window()!=h.window);QVERIFY(item->window());QCOMPARE(h.window->findChild<QQuickItem*>(objects[i]),original);
            QVERIFY(h.monitor.redockPane(panes[i]));QTRY_COMPARE(item->window(),h.window);
            auto* content=qvariant_cast<QQuickItem*>(h.window->property("contentItem"));QVERIFY(content);
            auto* pane=item;while(pane->parentItem()&&pane->parentItem()!=content)pane=pane->parentItem();
            QVERIFY2(pane->z()<h.window->findChild<QQuickItem*>("topBar")->z(),qPrintable(QString("%1 pane z=%2 toolbar z=%3").arg(panes[i]).arg(pane->z()).arg(h.window->findChild<QQuickItem*>("topBar")->z())));
        }
    }
    void nativeHostCloseAndEscapePreserveController() {
        Harness h;QVERIFY(h.window);h.window->resize(1100,760);h.window->setPosition(90,70);h.window->show();
        const auto display=h.viewer.displayManager()->displays().first().id;auto* grid=h.window->findChild<QQuickItem*>("photoGrid");QVERIFY(grid);
        QVERIFY(QMetaObject::invokeMethod(h.window,"invokeTarget",Q_ARG(QVariant,"pane-detach-grid"),Q_ARG(QVariant,display)));QTRY_VERIFY(grid->window()!=h.window);
        auto* host=grid->window();QVERIFY(host);QVERIFY(QMetaObject::invokeMethod(h.window,"escapeContent"));QVERIFY(h.monitor.paneRoutes()["grid"].toMap()["detached"].toBool());
        QVERIFY(host->close()==false);QTRY_COMPARE(grid->window(),h.window);QVERIFY(h.window->isVisible());
        QVERIFY(h.monitor.detachPane("preview",display));auto* canvas=h.window->findChild<QQuickItem*>("canvas");QTRY_VERIFY(canvas->window()!=h.window);
        const auto position=h.window->position();QPoint positionDuringClose;
        const auto connection=QObject::connect(h.window,&QQuickWindow::closing,h.window,[&]{positionDuringClose=h.window->position();});
        h.window->close();QObject::disconnect(connection);QTRY_COMPARE(canvas->window(),h.window);QCOMPARE(positionDuringClose,position);
    }
    void detachedCanvasPopupAndFrozenCloseHaveVisibleFeedback() {
        Harness h;QVERIFY(h.window);h.window->show();const auto display=h.viewer.displayManager()->displays().first().id;
        QVERIFY(h.monitor.detachPane("preview",display));auto* canvas=h.window->findChild<QQuickItem*>("canvas");QVERIFY(canvas);QTRY_VERIFY(canvas->window()!=h.window);
        QVERIFY(QMetaObject::invokeMethod(h.window,"openCanvasMenu",Q_ARG(QVariant,20),Q_ARG(QVariant,20)));
        auto* menu=h.window->findChild<QObject*>("canvasContextMenu");QVERIFY(menu);QTRY_VERIFY(menu->property("visible").toBool());QVERIFY(QMetaObject::invokeMethod(menu,"close"));
        QVERIFY(h.monitor.setPaneFrozen("preview",true));canvas->window()->close();QVERIFY(h.monitor.paneRoutes()["preview"].toMap()["detached"].toBool());
        QVERIFY(QMetaObject::invokeMethod(h.window,"invoke",Q_ARG(QVariant,"monitor-recover")));QTRY_COMPARE(canvas->window(),h.window);
    }
    void selectionAndDecodedCanvasSurviveRepeatedMigration() {
        QTemporaryDir media;Harness h;QVERIFY(h.window);QImage generated(160,120,QImage::Format_RGB32);generated.fill(Qt::red);const auto path=media.filePath("generated.png");QVERIFY(generated.save(path));
        h.layout.setContentMode("combined");h.viewer.open(path);QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(),15000);QTRY_VERIFY(h.grid.count()>0);h.grid.select(0);const auto selected=h.grid.selectedPaths();const auto image=h.viewer.image().cacheKey();
        const auto id=h.viewer.displayManager()->displays().first().id;auto* canvas=h.window->findChild<ImageCanvas*>("canvas");QVERIFY(canvas);
        for(int i=0;i<4;++i){QVERIFY(h.monitor.detachPane("preview",id));QTRY_VERIFY(canvas->window()!=h.window);QVERIFY(h.monitor.redockPane("preview"));QTRY_COMPARE(canvas->window(),h.window);QCOMPARE(h.grid.selectedPaths(),selected);QCOMPARE(h.viewer.image().cacheKey(),image);}
    }
    void nativeUiEvidence() {
        const auto destination=qEnvironmentVariable("GLANCELOT_MONITOR_UI_EVIDENCE");if(destination.isEmpty())QSKIP("Native screenshots are an explicit separate run.");
        QTemporaryDir media;QVERIFY(QDir().mkpath(destination));Harness h;QVERIFY(h.window);h.window->show();h.window->requestActivate();QTRY_VERIFY_WITH_TIMEOUT(h.window->isExposed(),5000);
        QSignalSpy frames(h.window,&QQuickWindow::frameSwapped);h.window->update();QTRY_VERIFY_WITH_TIMEOUT(frames.size()>0,5000);
        auto* dialog=h.window->findChild<QObject*>("monitorSettings");QVERIFY(dialog);
        for(const auto& mode:QStringList{"dark","light"})for(const auto& size:QList<QSize>{{720,540},{1280,900}}){
            h.theme.setMode(mode);h.viewer.setTextScale(size.width()==720?1.5:1.0);h.window->resize(size);
            for(int page=0;page<4;++page){QVERIFY(QMetaObject::invokeMethod(dialog,"openPage",Q_ARG(QVariant,page),Q_ARG(QVariant,"")));QTest::qWait(150);const auto screenshot=h.window->grabWindow();QVERIFY(!screenshot.isNull());QVERIFY(screenshot.save(QDir(destination).filePath(QString("%1-%2-page%3.png").arg(mode).arg(size.width()).arg(page))));}
            QMetaObject::invokeMethod(dialog,"close");
        }
        qInfo("Native settings captures complete; loading generated image");
        QImage generated(1200,800,QImage::Format_RGB32);generated.fill(QColor("#f02020"));const auto path=media.filePath("render.png");QVERIFY(generated.save(path));h.viewer.open(path);QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(),15000);
        auto* canvas=h.window->findChild<ImageCanvas*>("canvas");QVERIFY(canvas);const auto display=h.viewer.displayManager()->displays().first().id;
        qInfo("Native generated image ready; beginning migrations");
        for(int i=0;i<6;++i){qInfo("Native detach %d",i);QVERIFY(h.monitor.detachPane("preview",display));QTRY_VERIFY(canvas->window()!=h.window);qInfo("Native detached");auto* host=canvas->window();QTRY_VERIFY_WITH_TIMEOUT(host->isExposed(),5000);QTest::qWait(150);qInfo("Native exposed; grab");QVERIFY(!host->grabWindow().isNull());QVERIFY(canvas->rendererError().isEmpty());qInfo("Native redock");QVERIFY(h.monitor.redockPane("preview"));QTRY_COMPARE(canvas->window(),h.window);}
        QVERIFY(h.window->grabWindow().save(QDir(destination).filePath("preview-after-six-native-migrations.png")));
    }
    void actualImportAndResetReloadCoordinatorPreferences() {
        Harness h;QVERIFY(h.window);QVERIFY(h.monitor.savePreset({},"Portable","presentation"));
        const auto package=config->filePath("portable.json");QVERIFY(h.viewer.exportSettings(package));
        QVERIFY(h.monitor.setOutputOptions({{"guides",true}}));QVERIFY(h.monitor.outputOptions()["guides"].toBool());
        QSignalSpy reloads(&h.viewer,&ViewerController::configurationReloaded);QVERIFY(h.viewer.importSettings(package));QCOMPARE(reloads.size(),1);QVERIFY(!h.monitor.outputOptions()["guides"].toBool());QCOMPARE(h.monitor.presets().size(),1);
        QVERIFY(h.viewer.resetAllSettings());QCOMPARE(reloads.size(),2);QCOMPARE(h.monitor.presets().size(),0);QVERIFY(h.monitor.setOutputOptions({{"imageInfo",true}}));
    }
    void registryRouteAndLockedPredicateRemainShared() {
        Harness h;QVERIFY(h.window);QVERIFY(QMetaObject::invokeMethod(h.window,"invoke",Q_ARG(QVariant,"monitor-settings")));
        auto* dialog=h.window->findChild<QObject*>("monitorSettings");QVERIFY(dialog);QTRY_VERIFY(dialog->property("visible").toBool());
        QMetaObject::invokeMethod(dialog,"close");
        QVERIFY(h.monitor.setPaneFrozen("grid",true));
        QVariant blocked;QVERIFY(QMetaObject::invokeMethod(h.window,"commandBlockedReason",Q_RETURN_ARG(QVariant,blocked),Q_ARG(QVariant,"pane-detach-grid")));QVERIFY(!blocked.toString().isEmpty());
        QVERIFY(h.viewer.setBinding("monitor-recover","Ctrl+Alt+9"));QVERIFY(!h.viewer.bindingFor("monitor-recover").isEmpty());
    }
};
QTEST_MAIN(MonitorCustomizationUiTests)
#include "monitor_customization_ui_tests.moc"
