// STACK-F049-PRESETS: shipped-QML checks for pane layout presets next to the MONITOR-CUSTOM-01 coordinator.
// Offscreen/software rendering proves logic and layout only, not physical display behaviour.
#include <QtTest>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QFontDatabase>
#include <QDir>
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

class PresetTestThumbnails final:public QQuickImageProvider {
public:PresetTestThumbnails():QQuickImageProvider(Image){}
    QImage requestImage(const QString&,QSize* size,const QSize&) override {QImage image(24,24,QImage::Format_RGB32);image.fill(Qt::gray);if(size)*size=image.size();return image;}
};
class LayoutPresetsUiTests:public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> config;
    struct Harness {
        ViewerController viewer;ThemeManager theme;PaneLayout layout;FilmstripModel filmstrip;FolderTreeModel tree;
        DirectoryEntryModel grid;FileOperationService operations;
        glancelot::MonitorCustomization monitor{viewer.displayManager(),&layout};
        QQmlApplicationEngine engine;QQuickWindow* window{};
        Harness(){
            auto imports=engine.importPathList();imports.removeAll(QCoreApplication::applicationDirPath());engine.setImportPathList(imports);
            engine.addImageProvider("thumb",new PresetTestThumbnails);
            layout.setSlot(1);monitor.setSlot(1);
            file_grid_wiring::followViewer(viewer,grid);file_grid_wiring::lockThumbnailEpoch(filmstrip,grid);
            auto* context=engine.rootContext();
            context->setContextProperty("viewer",&viewer);context->setContextProperty("theme",&theme);
            context->setContextProperty("paneLayout",&layout);context->setContextProperty("monitorCustomization",&monitor);
            context->setContextProperty("filmstrip",&filmstrip);context->setContextProperty("folderTree",&tree);
            context->setContextProperty("fileGrid",&grid);context->setContextProperty("fileOperations",&operations);
            engine.load(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Main.qml")));
            if(!engine.rootObjects().isEmpty())window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            monitor.setControllerWindow(window);
        }
        template<class T=QQuickItem> T* named(const char* name) const {return window->findChild<T*>(QString::fromLatin1(name));}
    };
    static QString evidence(){return qEnvironmentVariable("GLANCELOT_F049_UI_EVIDENCE");}
private slots:
    void initTestCase() {
        for(const auto& font:QDir(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/assets/fonts/geist")).entryList({"*.ttf"},QDir::Files))
            QFontDatabase::addApplicationFont(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/assets/fonts/geist/")+font);
        QQuickStyle::setStyle("Basic");qmlRegisterType<ImageCanvas>("Glancelot",1,0,"ImageCanvas");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")),"Glancelot",1,0,"Theme");
    }
    void init(){config=std::make_unique<QTemporaryDir>();qputenv("GLANCELOT_CONFIG_DIR",config->path().toUtf8());}
    void cleanup(){qunsetenv("GLANCELOT_CONFIG_DIR");config.reset();}
    // The zoom navigator follows paneAvailable("preview"): it shows in, and moves with, a detached preview window and
    // returns to the controller on re-dock.
    void navigatorFollowsDetachedPreview() {
        QTemporaryDir media;QImage generated(1600,1200,QImage::Format_RGB32);generated.fill(Qt::darkCyan);
        const auto path=media.filePath("generated.png");QVERIFY(generated.save(path));
        Harness h;QVERIFY(h.window);h.window->resize(1180,780);h.window->show();QTRY_VERIFY(h.window->isExposed());
        h.viewer.open(path);QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(),15000);
        auto* canvas=h.named<ImageCanvas>("canvas");auto* nav=h.named("zoomNavigator");QVERIFY(canvas);QVERIFY(nav);
        h.viewer.trigger("navigator");h.viewer.trigger("zoom-200");QTRY_COMPARE(canvas->zoom(),2.0);QTRY_VERIFY(nav->isVisible());
        const auto display=h.viewer.displayManager()->displays().first().id;
        QVERIFY(h.monitor.detachPane("preview",display));QTRY_VERIFY(canvas->window()!=h.window);
        QTRY_COMPARE(nav->window(),canvas->window());
        QTRY_VERIFY_WITH_TIMEOUT(canvas->window()->isExposed(),5000);QTRY_VERIFY(canvas->width()>200&&canvas->height()>200);
        h.viewer.trigger("zoom-200");QTRY_VERIFY(nav->isVisible());
        const QRectF bounds=canvas->mapRectToScene(QRectF(0,0,canvas->width(),canvas->height()));
        const QRectF navBounds=nav->mapRectToScene(QRectF(0,0,nav->width(),nav->height()));
        QVERIFY2(bounds.contains(navBounds),qPrintable(QString("canvas %1,%2 %3x%4 nav %5,%6 %7x%8").arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height()).arg(navBounds.x()).arg(navBounds.y()).arg(navBounds.width()).arg(navBounds.height())));
        if(!evidence().isEmpty()){QTest::qWait(150);QVERIFY(canvas->window()->grabWindow().save(QDir(evidence()).filePath("navigator-detached-preview.png")));}
        QVERIFY(h.monitor.redockPane("preview"));QTRY_COMPARE(canvas->window(),h.window);QTRY_COMPARE(nav->window(),h.window);
        h.viewer.trigger("zoom-200");QTRY_VERIFY(nav->isVisible());
    }
    // Settings names pane layout presets apart from display workspace presets, and gates Apply on the canonical lock and
    // freeze (set here through the monitor coordinator) with a visible reason.
    void settingsPresetRowObeysCanonicalLockAndFreeze() {
        Harness h;QVERIFY(h.window);h.window->resize(1180,780);h.window->show();QTRY_VERIFY(h.window->isExposed());
        auto* settings=h.window->findChild<QObject*>("settingsDialog");QVERIFY(settings);
        QVERIFY(QMetaObject::invokeMethod(settings,"openSection",Q_ARG(QVariant,QString("browsing")),Q_ARG(QVariant,false)));
        QTRY_VERIFY(settings->property("opened").toBool());
        auto* heading=h.named("layoutPresetHeading");auto* apply=h.named("layoutPresetApply");auto* blocked=h.named("layoutPresetBlocked");
        QVERIFY(heading);QVERIFY(apply);QVERIFY(blocked);
        QCOMPARE(heading->property("text").toString(),QString("Pane layout presets"));
        QTRY_VERIFY(apply->isEnabled());QVERIFY(!blocked->isVisible());
        h.monitor.setLayoutLocked(true);QTRY_VERIFY(h.layout.locked());
        QTRY_VERIFY(!apply->isEnabled());QTRY_VERIFY(blocked->isVisible());QVERIFY(blocked->property("text").toString().contains("Unlock"));
        h.monitor.setLayoutLocked(false);QTRY_VERIFY(!h.layout.locked());
        QVERIFY(h.monitor.setPaneFrozen("tree",true));QTRY_VERIFY(h.layout.pane("tree")["frozen"].toBool());
        QTRY_VERIFY(!apply->isEnabled());QVERIFY(blocked->property("text").toString().contains("Unfreeze"));
        // A coordinator transaction still saving also holds the preset back.
        QVERIFY(h.monitor.setPaneFrozen("tree",false));QTRY_VERIFY(!h.layout.pane("tree")["frozen"].toBool());QTRY_VERIFY(apply->isEnabled());
        QVERIFY(glancelot::updateSettings([](QJsonObject&){}).ok());
        FileLock lock(glancelot::settingsFilePath()+".lock");QVERIFY(lock.acquire(0));
        QVERIFY(h.monitor.setPaneFrozen("grid",true));QVERIFY(h.monitor.saving());
        QTRY_VERIFY(!apply->isEnabled());QVERIFY(blocked->property("text").toString().contains("saving"));
        lock.release();QTRY_VERIFY_WITH_TIMEOUT(!h.monitor.saving(),5000);
    }
    // Evidence captures: dark and light, compact and wide, with the blocked reason showing.
    void settingsPresetEvidence() {
        if(evidence().isEmpty())QSKIP("Set GLANCELOT_F049_UI_EVIDENCE to a directory to capture screenshots.");
        for(const auto* mode:{"dark","light"})for(const auto size:QList<QSize>{{560,640},{1180,780}}) {
            Harness h;QVERIFY(h.window);h.theme.setMode(mode);h.window->resize(size);h.window->show();QTRY_VERIFY(h.window->isExposed());
            QVERIFY(h.layout.freeze("tree",true));
            auto* settings=h.window->findChild<QObject*>("settingsDialog");QVERIFY(settings);
            QVERIFY(QMetaObject::invokeMethod(settings,"openSection",Q_ARG(QVariant,QString("browsing")),Q_ARG(QVariant,false)));
            QTRY_VERIFY(settings->property("opened").toBool());
            auto* heading=h.named("layoutPresetHeading");QVERIFY(heading);
            // Scroll the page so the preset block is in view.
            for(auto* item=heading->parentItem();item;item=item->parentItem())
                if(item->property("contentY").isValid()&&item->property("contentHeight").toDouble()>item->height()){item->setProperty("contentY",std::clamp(heading->y()-48,0.,item->property("contentHeight").toDouble()-item->height()));break;}
            QTest::qWait(300);
            QVERIFY(h.window->grabWindow().save(QDir(evidence()).filePath(QString("settings-presets-%1-%2.png").arg(mode).arg(size.width()))));
        }
    }
};
QTEST_MAIN(LayoutPresetsUiTests)
#include "layout_presets_ui_tests.moc"
