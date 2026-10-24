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
#include "settings_retry.hpp"
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
    static QRectF sceneBox(const QQuickItem* item){return item->mapRectToScene(QRectF(0,0,item->width(),item->height()));}
    // The Settings page's Flickable that scrolls item.
    static QQuickItem* scroller(QQuickItem* item) {
        for(auto* parent=item->parentItem();parent;parent=parent->parentItem())
            if(parent->property("contentY").isValid()&&parent->property("contentItem").value<QQuickItem*>())return parent;
        return nullptr;
    }
    // Opens Settings > Browsing and scrolls so that item is the last row in view, as when a user has just scrolled to it.
    static QQuickItem* openBrowsingWithLastInView(Harness& h,const char* name) {
        auto* settings=h.window->findChild<QObject*>("settingsDialog");if(!settings)return nullptr;
        QMetaObject::invokeMethod(settings,"openSection",Q_ARG(QVariant,QString("browsing")),Q_ARG(QVariant,false));
        if(!QTest::qWaitFor([&]{return settings->property("opened").toBool();}))return nullptr;
        auto* item=h.named(name);auto* flick=item?scroller(item):nullptr;if(!flick)return nullptr;
        QTest::qWait(200);
        const double y=item->mapToItem(flick->property("contentItem").value<QQuickItem*>(),QPointF()).y();
        flick->setProperty("contentY",std::max(0.,y+item->height()+4-flick->height()));
        QTest::qWait(100);return item;
    }
    // Visible text items under the open Settings page that show text exactly.
    static QList<QQuickItem*> textsShowing(Harness& h,const QString& text) {
        QList<QQuickItem*> found,work{h.window->contentItem()}; // the dialog sits in the window's overlay, under contentItem
        while(!work.isEmpty()) {
            auto* item=work.takeLast();if(!item->isVisible())continue;
            if(item->inherits("QQuickText")&&item->property("text").toString()==text)found<<item;
            work<<item->childItems();
        }
        return found;
    }
    // Some visible item shows text inside the page's viewport, directly under the control above it (within one gap).
    static QString besideControl(Harness& h,const QString& text,QQuickItem* control) {
        const auto viewport=sceneBox(scroller(control)),above=sceneBox(control);QStringList seen;
        for(auto* item:textsShowing(h,text)) {
            const auto box=sceneBox(item);
            if(viewport.contains(box)&&box.top()>=above.bottom()-1&&box.top()-above.bottom()<=24)return {};
            seen<<QString("at y=%1-%2").arg(box.top()).arg(box.bottom());
        }
        return QString("\"%1\" is not shown under the control (y=%2) inside the viewport (y=%3-%4); shown %5")
            .arg(text).arg(above.bottom()).arg(viewport.top()).arg(viewport.bottom()).arg(seen.isEmpty()?QString("nowhere"):seen.join(", "));
    }
    static void click(Harness& h,QQuickItem* item){QTest::mouseClick(h.window,Qt::LeftButton,{},sceneBox(item).center().toPoint());}
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
        QCOMPARE(h.window->property("presetPendingRequest").toInt(),0);
    }
    // Follow-up R2: a refused Save (name collision) or Apply (newer preset record) says why right under the control that
    // was used, inside the page's viewport at 560 px, not in the general note at the end of "Pane placement".
    void presetRefusalsShowBesideTheirControls() {
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){
            root["layoutPresets"]=QJsonObject{{"Future",QJsonObject{{"version",2},{"panes",QJsonObject{}}}}};}).ok());
        Harness h;QVERIFY(h.window);h.window->resize(560,640);h.window->show();QTRY_VERIFY(h.window->isExposed());
        QVERIFY(h.monitor.savePreset({},"Studio","workspace"));QTRY_VERIFY(!h.monitor.saving());QCOMPARE(h.monitor.presets().size(),1);
        // Save: the name belongs to a display workspace preset.
        auto* field=openBrowsingWithLastInView(h,"layoutPresetName");QVERIFY(field);auto* save=h.named("layoutPresetSave");QVERIFY(save);
        field->setProperty("text","studio");QTRY_VERIFY(save->isEnabled());
        click(h,save);
        const QString collision("A display workspace preset already uses that name. Choose another name.");
        QTRY_COMPARE(h.layout.feedback(),collision);
        QTRY_VERIFY2(besideControl(h,collision,field).isEmpty(),qPrintable(besideControl(h,collision,field)));
        QCOMPARE(field->property("text").toString(),QString("studio")); // the entry is kept for correction
        // Said once: the general layout note does not repeat it. Screen readers get it as the field's description.
        QCOMPARE(textsShowing(h,collision).size(),1);
        QString spoken;for(auto* child:field->children())if(child->inherits("QQuickAccessibleAttached"))spoken=child->property("description").toString();
        QCOMPARE(spoken,collision);
        // Editing the name clears the refusal.
        field->forceActiveFocus();QTest::keyClick(h.window,Qt::Key_X);
        QTRY_VERIFY(textsShowing(h,collision).isEmpty());
        // Apply: a preset record a newer version saved is preserved and not applied.
        auto* choice=h.named("layoutPresetChoice");auto* apply=h.named("layoutPresetApply");QVERIFY(choice);QVERIFY(apply);
        auto* flick=scroller(choice);flick->setProperty("contentY",std::max(0.,choice->mapToItem(flick->property("contentItem").value<QQuickItem*>(),QPointF()).y()+choice->height()+4-flick->height()));
        QTest::qWait(100);
        const int future=h.layout.presetNames().indexOf("Future");QVERIFY(future>=0);choice->setProperty("currentIndex",future);
        QTRY_VERIFY(apply->isEnabled());click(h,apply);
        const QString newer("A newer version of Glancelot saved that preset; it was preserved and not applied.");
        QTRY_COMPARE(h.layout.feedback(),newer);
        QTRY_VERIFY2(besideControl(h,newer,choice).isEmpty(),qPrintable(besideControl(h,newer,choice)));
        QVERIFY(h.layout.pane("topbar")["visible"].toBool()); // nothing applied
        if(!evidence().isEmpty()) {
            flick->setProperty("contentY",std::max(0.,field->mapToItem(flick->property("contentItem").value<QQuickItem*>(),QPointF()).y()+field->height()+4-flick->height()));
            QTest::qWait(100);field->setProperty("text","studio");click(h,save);QTRY_COMPARE(h.layout.feedback(),collision);QTest::qWait(300);
            QVERIFY(h.window->grabWindow().save(QDir(evidence()).filePath("settings-presets-refusal-dark-560.png")));
        }
    }
    // Follow-up R3: the preset and save rows end at the same right edge as the lock switch and the pane controls, and the
    // name field starts where the preset choice does.
    void presetControlRowsShareTheColumnEdges() {
        for(const int width:{560,1180}) {
            Harness h;QVERIFY(h.window);h.window->resize(width,width==560?640:780);h.window->show();QTRY_VERIFY(h.window->isExposed());
            QVERIFY(openBrowsingWithLastInView(h,"layoutPresetName"));
            const QStringList names{"layoutLock","layoutPresetDelete","layoutPresetSave","layoutPaneChoice"};QList<double> rights;
            for(const auto& name:names){auto* item=h.named(name.toLatin1().constData());QVERIFY(item);rights<<sceneBox(item).right();}
            const QString edges=QString("width %1: right edges lock %2, delete %3, save %4, pane %5").arg(width).arg(rights[0]).arg(rights[1]).arg(rights[2]).arg(rights[3]);
            for(const double right:rights)QVERIFY2(qAbs(right-rights[0])<=1,qPrintable(edges));
            const auto fieldLeft=sceneBox(h.named("layoutPresetName")).left(),choiceLeft=sceneBox(h.named("layoutPresetChoice")).left();
            QVERIFY2(qAbs(fieldLeft-choiceLeft)<=1,qPrintable(QString("width %1: name field left %2, preset choice left %3").arg(width).arg(fieldLeft).arg(choiceLeft)));
        }
    }
    // Follow-up R4: the main window shows only the outcome of the press's own preset write, never a later, unrelated
    // layout message, both when the outcome is immediate and while the write is waiting.
    void presetCommandShowsOnlyItsOwnWriteOutcome() {
        // Immediate: a newer saved layout is preserved, said with the preset at once; nothing stays armed.
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){root["layouts"]=QJsonObject{{"1",QJsonObject{{"version",2}}}};}).ok());
        {
            Harness h;QVERIFY(h.window);h.window->resize(1180,780);h.window->show();QTRY_VERIFY(h.window->isExposed());
            h.viewer.trigger("layout-preset-next");h.viewer.trigger("layout-preset-next"); // Default, then Review
            QTRY_VERIFY(!h.layout.pane("topbar")["visible"].toBool());QTRY_VERIFY(h.window->property("splitMessage").toString().startsWith("Layout: Review"));
            QVERIFY(!h.layout.move("tree","center",0));QVERIFY(h.layout.feedback().contains("center is reserved")); // unrelated, later
            QTest::qWait(100);
            QVERIFY2(!h.window->property("splitMessage").toString().contains("center is reserved"),qPrintable(h.window->property("splitMessage").toString()));
            QVERIFY2(h.window->property("splitMessage").toString().contains("newer saved layout was preserved"),qPrintable(h.window->property("splitMessage").toString()));
        }
        cleanup();init();
        // Waiting: an unrelated refusal while the write waits is not shown, and the write's lost outcome never appears late.
        Harness h;QVERIFY(h.window);h.window->resize(1180,780);h.window->show();QTRY_VERIFY(h.window->isExposed());
        QVERIFY(glancelot::updateSettings([](QJsonObject&){}).ok());
        FileLock lock(glancelot::settingsFilePath()+".lock");QVERIFY(lock.acquire(0));
        h.viewer.trigger("layout-preset-next");h.viewer.trigger("layout-preset-next");
        QTRY_COMPARE(h.window->property("splitMessage").toString(),QString("Layout: Review"));
        QVERIFY(h.window->property("presetPendingRequest").toInt()>0);
        QVERIFY(!h.layout.move("tree","center",0));QVERIFY(h.layout.feedback().contains("center is reserved"));
        QTest::qWait(100);
        QCOMPARE(h.window->property("splitMessage").toString(),QString("Layout: Review"));
        QCOMPARE(h.window->property("presetPendingRequest").toInt(),0);
        lock.release();QTRY_VERIFY_WITH_TIMEOUT(!glancelot::settings_detail::hasPendingSettings(&h.layout),5000);
        QVERIFY(!h.layout.move("tree","center",0));QTest::qWait(100);
        QVERIFY2(!h.window->property("splitMessage").toString().contains("center is reserved"),qPrintable(h.window->property("splitMessage").toString()));
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
