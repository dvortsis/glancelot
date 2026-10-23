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
        QVERIFY2(blocked->property("text").toString().contains("Folder tree"),qPrintable(blocked->property("text").toString())); // names the pane
        // A coordinator transaction still saving also holds the preset back.
        QVERIFY(h.monitor.setPaneFrozen("tree",false));QTRY_VERIFY(!h.layout.pane("tree")["frozen"].toBool());QTRY_VERIFY(apply->isEnabled());
        QVERIFY(glancelot::updateSettings([](QJsonObject&){}).ok());
        FileLock lock(glancelot::settingsFilePath()+".lock");QVERIFY(lock.acquire(0));
        QVERIFY(h.monitor.setPaneFrozen("grid",true));QVERIFY(h.monitor.saving());
        QTRY_VERIFY(!apply->isEnabled());QVERIFY(blocked->property("text").toString().contains("saving"));
        // The lock and freeze switches and reset say why they are unavailable too, in their own text.
        auto* lockSwitch=h.named("layoutLock");auto* freezeSwitch=h.named("layoutFreeze");auto* reset=h.named("layoutReset");
        QVERIFY(lockSwitch);QVERIFY(freezeSwitch);QVERIFY(reset);
        QVERIFY(!lockSwitch->isEnabled());QVERIFY(!freezeSwitch->isEnabled());QVERIFY(!reset->isEnabled());
        QVERIFY(lockSwitch->property("description").toString().contains("saving"));
        QVERIFY(freezeSwitch->property("description").toString().contains("saving"));
        lock.release();QTRY_VERIFY_WITH_TIMEOUT(!h.monitor.saving(),5000);
        QTRY_VERIFY(lockSwitch->isEnabled());QVERIFY(!lockSwitch->property("description").toString().contains("saving"));
    }
    // Review round 1 design: the scope sentence says Apply changes only this window, the preset controls sit under it at a
    // readable measure (no trailing-column wrap), reset lives with the lock it clears, and the display presets say their scope.
    void settingsPresetBlockReadsAtEveryWidth() {
        for(const int width:{560,1180}) {
            Harness h;QVERIFY(h.window);h.window->resize(width,width==560?640:780);h.window->show();QTRY_VERIFY(h.window->isExposed());
            auto* settings=h.window->findChild<QObject*>("settingsDialog");QVERIFY(settings);
            QVERIFY(QMetaObject::invokeMethod(settings,"openSection",Q_ARG(QVariant,QString("browsing")),Q_ARG(QVariant,false)));
            QTRY_VERIFY(settings->property("opened").toBool());
            auto* scope=h.named("layoutPresetScope");auto* choice=h.named("layoutPresetChoice");auto* heading=h.named("layoutPresetHeading");
            QVERIFY(scope);QVERIFY(choice);QVERIFY(heading);
            QCOMPARE(scope->property("text").toString(),QString("Saved for every window; applies to this window's docked panes. Never changes lock, freeze or displays."));
            QTRY_VERIFY(scope->width()>0);
            // At most about 75 characters per line, and never the ~20-character column the side-by-side row gave.
            const double perLine=scope->property("text").toString().size()/std::max(1.,std::round(scope->property("lineCount").toDouble()));
            QVERIFY2(perLine>=40&&scope->width()<=settings->property("measure").toDouble()+0.5,qPrintable(QString("width %1: %2 chars/line, %3 px").arg(width).arg(perLine).arg(scope->width())));
            // The controls stack below the text instead of beside it.
            const auto scopeBox=scope->mapRectToScene(QRectF(0,0,scope->width(),scope->height())),choiceBox=choice->mapRectToScene(QRectF(0,0,choice->width(),choice->height()));
            QVERIFY2(choiceBox.top()>=scopeBox.bottom(),qPrintable(QString("width %1").arg(width)));
            // Reset sits before the preset heading, with the lock it also clears.
            auto* reset=h.named("layoutReset");QVERIFY(reset);
            QVERIFY(reset->mapToScene(QPointF(0,0)).y()<heading->mapToScene(QPointF(0,0)).y());
        }
        Harness h;QVERIFY(h.window);
        auto* monitorHeading=h.named("monitorPresetHeading");QVERIFY(monitorHeading);
        QCOMPARE(monitorHeading->property("text").toString(),QString("Display workspace presets for this window"));
        QVERIFY(h.named("monitorPresetScope")->property("text").toString().contains("Saved for this window only"));
    }
    // Review round 1: the keyboard command waits for a display transaction that is saving, as Settings does, and a preset
    // whose write had to wait reports its outcome in the main window.
    void presetCommandWaitsForDisplaySaveAndReportsOutcome() {
        Harness h;QVERIFY(h.window);h.window->resize(1180,780);h.window->show();QTRY_VERIFY(h.window->isExposed());
        QVERIFY(glancelot::updateSettings([](QJsonObject&){}).ok());
        FileLock lock(glancelot::settingsFilePath()+".lock");QVERIFY(lock.acquire(0));
        QVERIFY(h.monitor.setOutputOptions({{"guides",true}}));QVERIFY(h.monitor.saving());
        h.viewer.trigger("layout-preset-next");h.viewer.trigger("layout-preset-next"); // would be Default, then Review
        QTRY_VERIFY(h.window->property("splitMessage").toString().contains("Display preferences are saving"));
        QVERIFY(h.layout.pane("topbar")["visible"].toBool());QCOMPARE(h.window->property("presetCursor").toInt(),-1);
        lock.release();QTRY_VERIFY_WITH_TIMEOUT(!h.monitor.saving(),5000);
        // The preset applies but its write waits for the busy file; the write gives up after about four seconds.
        QVERIFY(lock.acquire(0));
        h.viewer.trigger("layout-preset-next");h.viewer.trigger("layout-preset-next");
        QTRY_VERIFY(!h.layout.pane("topbar")["visible"].toBool());
        QTRY_COMPARE(h.window->property("splitMessage").toString(),QString("Layout: Review"));
        QTRY_VERIFY_WITH_TIMEOUT(h.window->property("splitMessage").toString().contains("could not be saved"),8000);
        lock.release();
        QCOMPARE(h.window->property("presetSavingFeedback").toString(),QString());
    }
    // Evidence captures: dark and light, compact and wide, with the blocked reason showing; plus one while a display
    // transaction is saving, so the switches' and reset's own reasons show.
    void settingsPresetEvidence() {
        if(evidence().isEmpty())QSKIP("Set GLANCELOT_F049_UI_EVIDENCE to a directory to capture screenshots.");
        struct Shot{const char* mode;QSize size;bool saving;};
        for(const auto& shot:{Shot{"dark",{560,640},false},Shot{"dark",{1180,780},false},Shot{"light",{560,640},false},Shot{"light",{1180,780},false},Shot{"dark",{1180,780},true}}) {
            Harness h;QVERIFY(h.window);h.theme.setMode(shot.mode);h.window->resize(shot.size);h.window->show();QTRY_VERIFY(h.window->isExposed());
            QVERIFY(h.layout.freeze("tree",true));
            std::unique_ptr<FileLock> lock;
            if(shot.saving) {
                QVERIFY(h.layout.freeze("tree",false));QVERIFY(glancelot::updateSettings([](QJsonObject&){}).ok());
                lock=std::make_unique<FileLock>(glancelot::settingsFilePath()+".lock");QVERIFY(lock->acquire(0));
                QVERIFY(h.monitor.setOutputOptions({{"guides",true}}));QVERIFY(h.monitor.saving());
            }
            auto* settings=h.window->findChild<QObject*>("settingsDialog");QVERIFY(settings);
            QVERIFY(QMetaObject::invokeMethod(settings,"openSection",Q_ARG(QVariant,QString("browsing")),Q_ARG(QVariant,false)));
            QTRY_VERIFY(settings->property("opened").toBool());
            auto* top=h.named("layoutLock");QVERIFY(top);
            // Scroll the page so the lock, reset and preset block are in view.
            for(auto* item=top->parentItem();item;item=item->parentItem())
                if(item->property("contentY").isValid()&&item->property("contentHeight").toDouble()>item->height()){item->setProperty("contentY",std::clamp(top->y()-8,0.,item->property("contentHeight").toDouble()-item->height()));break;}
            QTest::qWait(300);
            const auto file=shot.saving?QString("settings-presets-saving-%1-%2.png").arg(shot.mode).arg(shot.size.width()):QString("settings-presets-%1-%2.png").arg(shot.mode).arg(shot.size.width());
            QVERIFY(h.window->grabWindow().save(QDir(evidence()).filePath(file)));
            if(lock){lock->release();QTRY_VERIFY_WITH_TIMEOUT(!h.monitor.saving(),5000);}
        }
    }
};
QTEST_MAIN(LayoutPresetsUiTests)
#include "layout_presets_ui_tests.moc"
