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
#include "file_lock.hpp"
#include "glancelot/settings_file.hpp"
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
        QTemporaryDir media;QVERIFY(QDir().mkpath(destination));Harness h;QVERIFY(h.window);h.window->show();h.window->raise();h.window->requestActivate();QTRY_VERIFY_WITH_TIMEOUT(h.window->isExposed(),5000);
        QSignalSpy frames(h.window,&QQuickWindow::frameSwapped);h.window->update();QTRY_VERIFY_WITH_TIMEOUT(frames.size()>0,5000);
        auto* dialog=h.window->findChild<QObject*>("monitorSettings");QVERIFY(dialog);
        for(const auto& mode:QStringList{"dark","light"})for(const auto& size:QList<QSize>{{720,540},{1280,900}}){
            h.theme.setMode(mode);h.viewer.setTextScale(size.width()==720?1.5:1.0);h.window->resize(size);
            for(int page=0;page<4;++page){QVERIFY(QMetaObject::invokeMethod(dialog,"openPage",Q_ARG(QVariant,page),Q_ARG(QVariant,"")));QTest::qWait(150);const auto screenshot=h.window->grabWindow();QVERIFY(!screenshot.isNull());QVERIFY(screenshot.save(QDir(destination).filePath(QString("%1-%2-page%3.png").arg(mode).arg(size.width()).arg(page))));
                if(page==1||page==2){auto* scroll=h.window->findChild<QQuickItem*>(page==1?"monitorPresentationScroll":"monitorPanesScroll");QVERIFY(scroll);auto* flickable=qvariant_cast<QQuickItem*>(scroll->property("contentItem"));QVERIFY(flickable);flickable->setProperty("contentY",std::max(0.,flickable->property("contentHeight").toDouble()-flickable->height()));QTest::qWait(150);QVERIFY(h.window->grabWindow().save(QDir(destination).filePath(QString("%1-%2-page%3-bottom.png").arg(mode).arg(size.width()).arg(page))));}
            }
            QMetaObject::invokeMethod(dialog,"close");
        }
        qInfo("Native settings captures complete; loading generated image");
        QImage generated(1200,800,QImage::Format_RGB32);generated.fill(QColor("#f02020"));const auto path=media.filePath("render.png");QVERIFY(generated.save(path));h.viewer.open(path);QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(),15000);
        auto* canvas=h.window->findChild<ImageCanvas*>("canvas");QVERIFY(canvas);const auto display=h.viewer.displayManager()->displays().first().id;
        qInfo("Native generated image ready; beginning migrations");
        for(int i=0;i<6;++i){qInfo("Native detach %d",i);QVERIFY(h.monitor.detachPane("preview",display));QTRY_VERIFY(canvas->window()!=h.window);qInfo("Native detached");auto* host=canvas->window();QTRY_VERIFY_WITH_TIMEOUT(host->isExposed(),5000);QTest::qWait(150);qInfo("Native exposed; grab");const auto rendered=host->grabWindow();QVERIFY(!rendered.isNull());const auto point=canvas->mapToScene(QPointF(canvas->width()/2,canvas->height()/2));const auto factor=qreal(rendered.width())/host->width();const auto pixel=rendered.pixelColor(std::clamp(int(point.x()*factor),0,rendered.width()-1),std::clamp(int(point.y()*factor),0,rendered.height()-1));QVERIFY(pixel.red()>200&&pixel.green()<80&&pixel.blue()<80);QVERIFY(canvas->admittedTextureBytes()>0);QVERIFY(canvas->admittedTextureBytes()<=canvas->textureBudget());QVERIFY(canvas->rendererError().isEmpty());qInfo("Native redock");QVERIFY(h.monitor.redockPane("preview"));QTRY_COMPARE(canvas->window(),h.window);}
        QVERIFY(h.window->grabWindow().save(QDir(destination).filePath("preview-after-six-native-migrations.png")));
        QTRY_COMPARE_WITH_TIMEOUT(canvas->admittedTextureBytes(),qint64(canvas->image().sizeInBytes()),5000);
    }
    void recoveryKeyPointerAndPaletteShareLockedRouting() {
        Harness h;QVERIFY(h.window);h.window->show();const auto display=h.viewer.displayManager()->displays().first().id;
        auto* grid=h.window->findChild<QQuickItem*>("photoGrid");QVERIFY(grid);QVERIFY(h.viewer.setBinding("monitor-recover","Ctrl+Alt+9"));
        QVERIFY(h.monitor.detachPane("grid",display));QTRY_VERIFY(grid->window()!=h.window);QVERIFY(h.monitor.setPaneFrozen("grid",true));h.monitor.setLayoutLocked(true);QTRY_VERIFY(h.monitor.layoutLocked());
        auto* host=grid->window();host->requestActivate();QTRY_VERIFY(host->isActive());QTest::keyClick(host,Qt::Key_9,Qt::ControlModifier|Qt::AltModifier);QTRY_COMPARE(grid->window(),h.window);QVERIFY(h.monitor.layoutLocked());QVERIFY(h.monitor.paneRoutes()["grid"].toMap()["frozen"].toBool());
        h.monitor.setLayoutLocked(false);QVERIFY(h.monitor.setPaneFrozen("grid",false));QVERIFY(h.monitor.detachPane("grid",display));QTRY_VERIFY(grid->window()!=h.window);QVERIFY(h.monitor.setPaneFrozen("grid",true));
        QVERIFY(QMetaObject::invokeMethod(h.window,"invoke",Q_ARG(QVariant,"monitor-settings")));auto* button=h.window->findChild<QQuickItem*>("monitorRecover");QVERIFY(button);h.window->requestActivate();QTRY_VERIFY(button->isVisible());
        QTest::mouseClick(h.window,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());QTRY_COMPARE(grid->window(),h.window);QVERIFY(h.monitor.paneRoutes()["grid"].toMap()["frozen"].toBool());
        QVERIFY(QMetaObject::invokeMethod(h.window,"invoke",Q_ARG(QVariant,"monitor-recover")));QVERIFY(h.monitor.paneRoutes()["grid"].toMap()["frozen"].toBool());
    }
    void realGridRetainsFocusScrollAndContentAcrossMigration() {
        QTemporaryDir media;Harness h;QVERIFY(h.window);h.window->resize(1180,780);h.window->show();QImage generated(64,48,QImage::Format_RGB32);generated.fill(Qt::red);
        for(int i=0;i<80;++i)QVERIFY(generated.save(media.filePath(QString("photo-%1.png").arg(i,3,10,QChar('0')))));
        h.layout.setContentMode("combined");h.viewer.open(media.filePath("photo-000.png"));QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(),15000);QTRY_COMPARE(h.grid.count(),80);
        auto* panel=h.window->findChild<QQuickItem*>("photoGrid");auto* view=h.window->findChild<QQuickItem*>("photoGridView");QVERIFY(panel);QVERIFY(view);panel->setProperty("bucketIndex",1);h.grid.select(2);QTRY_COMPARE(h.viewer.filePath(),media.filePath("photo-002.png"));QTRY_VERIFY(h.viewer.imageReady());QTest::qWait(100);view->setProperty("contentY",120.);h.window->requestActivate();view->forceActiveFocus();QTRY_VERIFY(view->hasActiveFocus());const auto scroll=view->property("contentY").toDouble();QVERIFY(scroll>0);
        const auto selected=h.grid.selectedPaths();const auto display=h.viewer.displayManager()->displays().first().id;
        QVERIFY(h.monitor.detachPane("grid",display));QTRY_VERIFY(panel->window()!=h.window);QTRY_VERIFY(view->hasActiveFocus());QTRY_VERIFY(std::abs(view->property("contentY").toDouble()-scroll)<1.);
        QVERIFY(h.monitor.redockPane("grid"));QTRY_COMPARE(panel->window(),h.window);QTRY_VERIFY(view->hasActiveFocus());QTRY_VERIFY(std::abs(view->property("contentY").toDouble()-scroll)<1.);QCOMPARE(panel->property("bucketIndex").toInt(),1);QCOMPARE(h.grid.selectedPaths(),selected);
        QVERIFY(h.monitor.setPaneFrozen("grid",true));QVERIFY(QMetaObject::invokeMethod(h.window,"invoke",Q_ARG(QVariant,"layout-reset")));QVERIFY(!h.monitor.paneRoutes()["grid"].toMap()["frozen"].toBool());
    }
    void busyDragFailureRestoresTheSavedHostGeometry() {
        Harness h;QVERIFY(h.window);h.window->show();const auto display=h.viewer.displayManager()->displays().first().id;QVERIFY(h.monitor.detachPane("grid",display));auto* panel=h.window->findChild<QQuickItem*>("photoGrid");QVERIFY(panel);QTRY_VERIFY(panel->window()!=h.window);auto* host=panel->window();QTest::qWait(100);const auto original=host->geometry();const auto preference=h.monitor.paneRoutes()["grid"].toMap()["geometry"];
        FileLock lock(glancelot::settingsFilePath()+".lock");QVERIFY(lock.acquire(0));host->setPosition(original.topLeft()+QPoint(12,16));QVERIFY(QMetaObject::invokeMethod(host,"commitUserGeometry"));QVERIFY(h.monitor.saving());QVERIFY(host->position()!=original.topLeft());
        QTRY_VERIFY_WITH_TIMEOUT(!h.monitor.saving(),5500);QTRY_COMPARE(host->geometry(),original);QCOMPARE(h.monitor.paneRoutes()["grid"].toMap()["geometry"],preference);lock.release();
    }
    void settingsWorkspaceEntryOpensMonitorDialog() {
        for(const auto size:QList<QSize>{{540,360},{720,540},{1180,780}}) {
            Harness h;QVERIFY(h.window);h.window->resize(size);h.viewer.setTextScale(size.width()==720?1.5:1.0);h.window->show();h.window->raise();h.window->requestActivate();QTRY_VERIFY(h.window->isExposed());QTRY_VERIFY(h.window->isActive());
            auto* settings=h.window->findChild<QObject*>("settingsDialog");QVERIFY(settings);
            QVERIFY(QMetaObject::invokeMethod(settings,"openSection",Q_ARG(QVariant,QString("browsing")),Q_ARG(QVariant,false)));QTRY_VERIFY(settings->property("opened").toBool());
            auto* action=h.window->findChild<QQuickItem*>("openMonitorSettings");QVERIFY(action);QTRY_VERIFY(action->isVisible());QTest::qWait(100);
            const auto destination=qEnvironmentVariable("GLANCELOT_MONITOR_UI_EVIDENCE");if(!destination.isEmpty())QVERIFY(h.window->grabWindow().save(QDir(destination).filePath(QString("workspace-entry-%1.png").arg(size.width()))));
            const auto point=action->mapToScene(QPointF(action->width()/2,action->height()/2));QVERIFY(QRectF(QPointF(),h.window->size()).contains(point));
            QTest::mouseClick(h.window,Qt::LeftButton,Qt::NoModifier,point.toPoint());auto* monitor=h.window->findChild<QObject*>("monitorSettings");QVERIFY(monitor);QTRY_VERIFY(monitor->property("opened").toBool());QTRY_VERIFY(!settings->property("opened").toBool());
        }
    }
    void parameterizedDetachConfirmsTheDestinationBeforeMoving() {
        Harness h;QVERIFY(h.window);h.window->show();QVERIFY(QMetaObject::invokeMethod(h.window,"invoke",Q_ARG(QVariant,"pane-detach-metadata")));auto* dialog=h.window->findChild<QObject*>("monitorSettings");QVERIFY(dialog);QTRY_VERIFY(dialog->property("opened").toBool());
        auto* choice=h.window->findChild<QQuickItem*>("monitorPaneDestination");auto* label=h.window->findChild<QObject*>("monitorPaneDestinationLabel");QVERIFY(choice);QVERIFY(label);QVERIFY(label->property("text").toString().contains("metadata"));QTRY_VERIFY(choice->hasActiveFocus());QVERIFY(!h.monitor.paneRoutes()["metadata"].toMap()["detached"].toBool());
        QVERIFY(QMetaObject::invokeMethod(choice,"activated",Q_ARG(int,choice->property("currentIndex").toInt())));QTRY_VERIFY(h.window->activeFocusItem()&&h.window->activeFocusItem()->objectName()=="paneDetach-metadata");auto* action=h.window->activeFocusItem();QVERIFY(action);QTest::mouseClick(h.window,Qt::LeftButton,Qt::NoModifier,action->mapToScene(QPointF(action->width()/2,action->height()/2)).toPoint());QTRY_VERIFY(h.monitor.paneRoutes()["metadata"].toMap()["detached"].toBool());
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
