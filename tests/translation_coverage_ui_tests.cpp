// STACK-F054-COVERAGE: the en_XA pseudo-locale catalog, read straight from translations/glancelot_en_XA.ts, is installed
// and every named surface is inspected for text that stayed untranslated or is clipped, in left-to-right and
// right-to-left layout. Offscreen/software rendering proves layout and coverage only, not physical display behaviour.
// Screenshots go to GLANCELOT_F054_UI_EVIDENCE when it names a directory.
#include <QtTest>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QFontDatabase>
#include <QDir>
#include <QFile>
#include <QTranslator>
#include <QXmlStreamReader>
#include <QQuickImageProvider>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJSValue>
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

namespace {
// A QTranslator over the source .ts, so the test checks the committed catalog rather than a build product.
class TsTranslator final:public QTranslator {
    QHash<QString,QString> messages;
public:
    // Clipping pass: pad every translation by about 40% inside its brackets, as longer languages would.
    bool expand=false;
    bool loadTs(const QString& path) {
        QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;
        QXmlStreamReader xml(&file);QString context,source;
        while(!xml.atEnd()) {
            if(!xml.readNextStartElement())continue;
            if(xml.name()==u"name")context=xml.readElementText();
            else if(xml.name()==u"source")source=xml.readElementText();
            else if(xml.name()==u"translation"){const auto text=xml.readElementText();if(!text.isEmpty())messages.insert(context+QChar(0x1f)+source,text);}
        }
        return !xml.hasError()&&!messages.isEmpty();
    }
    bool isEmpty() const override {return messages.isEmpty();}
    QString translate(const char* context,const char* source,const char*,int) const override {
        auto text=messages.value(QString::fromUtf8(context)+QChar(0x1f)+QString::fromUtf8(source));
        if(expand&&text.endsWith(']'))text.insert(text.size()-1,QString((text.size()*2+4)/5,QChar(0x2022)));
        return text;
    }
    qsizetype size() const {return messages.size();}
};
class Thumbnails final:public QQuickImageProvider {
public:Thumbnails():QQuickImageProvider(Image){}
    QImage requestImage(const QString&,QSize* size,const QSize&) override {QImage image(24,24,QImage::Format_RGB32);image.fill(Qt::gray);if(size)*size=image.size();return image;}
};
struct Finding {QString text,where;};
QList<QQuickItem*> descendants(QQuickItem* root) {
    QList<QQuickItem*> all,work{root};
    while(!work.isEmpty()){auto* item=work.takeLast();all<<item;work<<item->childItems();}
    return all;
}
// Visible text items under root: those still in plain English (no pseudo-locale bracket) and those clipped without an
// intentional elide or wrap.
void inspect(QQuickItem* item,const QString& path,QList<Finding>& plain,QList<Finding>& clipped,const QStringList& dynamic) {
    if(!item||!item->isVisible()||item->opacity()<=0.01)return;
    const auto where=path+"/"+(item->objectName().isEmpty()?QString::fromLatin1(item->metaObject()->className()).section('_',0,0):item->objectName());
    if(item->inherits("QQuickText")) {
        const auto text=item->property("text").toString().trimmed();
        static const QRegularExpression words("[A-Za-z]{2,}");
        if(!text.isEmpty()&&words.match(text).hasMatch()&&!text.contains('[')&&!dynamic.contains(text))plain.append({text,where});
        const bool wraps=item->property("wrapMode").toInt()!=0;const bool elides=item->property("elide").toInt()!=Qt::ElideNone;
        if(!text.isEmpty()&&!wraps&&!elides&&item->width()>0&&item->implicitWidth()>item->width()+1)clipped.append({text,where});
        if(!text.isEmpty()&&elides&&item->property("truncated").toBool())clipped.append({text+" (elided)",where});
        // Laid out past the nearest clipping ancestor (or the window) horizontally: cut off rather than wrapped.
        if(!text.isEmpty()&&item->width()>0) {
            const auto box=item->mapRectToScene(QRectF(0,0,std::min(item->width(),item->implicitWidth()),item->height()));
            QRectF bounds(QPointF(),item->window()?QSizeF(item->window()->size()):QSizeF(1e9,1e9));
            for(auto* parent=item->parentItem();parent;parent=parent->parentItem())
                if(parent->clip()){bounds=bounds.intersected(parent->mapRectToScene(QRectF(0,0,parent->width(),parent->height())));break;}
            if(box.left()<bounds.left()-1||box.right()>bounds.right()+1)clipped.append({text+" (outside its clip)",where});
        }
    }
    if(item->inherits("QQuickTextInput")||item->inherits("QQuickTextEdit"))return; // user input, not interface text
    if(item->inherits("QQuickListView")&&item->objectName()!="commandRows")return; // model rows (metadata tags and the like) are data
    for(auto* child:item->childItems())inspect(child,where,plain,clipped,dynamic);
}
}

class TranslationCoverageUiTests:public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> config;
    TsTranslator translator;
    struct Harness {
        ViewerController viewer;ThemeManager theme;PaneLayout layout;FilmstripModel filmstrip;FolderTreeModel tree;
        DirectoryEntryModel grid;FileOperationService operations;
        glancelot::MonitorCustomization monitor{viewer.displayManager(),&layout};
        QQmlApplicationEngine engine;QQuickWindow* window{};
        explicit Harness(const QString& mode,QSize size){
            auto imports=engine.importPathList();imports.removeAll(QCoreApplication::applicationDirPath());engine.setImportPathList(imports);
            engine.addImageProvider("thumb",new Thumbnails);theme.setMode(mode);
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
            if(window){window->resize(size);window->show();}
        }
    };
    QStringList plainSeen,clippedSeen;
    QStringList deferredToMainQml{"Needs the folder grid","Needs the folder tree","Needs the photo view","Needs a photo"};
    QString evidence() const {return qEnvironmentVariable("GLANCELOT_F054_UI_EVIDENCE");}
    void capture(QQuickWindow* window,const QString& name) {
        if(evidence().isEmpty()||!window)return;
        QTest::qWait(250);QVERIFY(window->grabWindow().save(QDir(evidence()).filePath(name+".png")));
    }
    // Inspects one surface and records what it found; the caller asserts.
    QList<Finding> check(QQuickItem* root,const QString& surface,const QStringList& dynamic={}) {
        QList<Finding> plain,clipped;inspect(root,surface,plain,clipped,dynamic);
        for(const auto& f:plain){plainSeen<<surface+": "+f.text;qInfo("untranslated %s: \"%s\" at %s",qPrintable(surface),qPrintable(f.text),qPrintable(f.where));}
        for(const auto& f:clipped){clippedSeen<<surface+": "+f.text;qInfo("clipped %s: \"%s\" at %s",qPrintable(surface),qPrintable(f.text),qPrintable(f.where));}
        return plain;
    }
    static QString describe(const QList<Finding>& findings) {QStringList lines;for(const auto& f:findings)lines<<f.text+" at "+f.where;return lines.join('\n');}
    static QQuickItem* popupItem(QObject* popup) {
        auto* content=popup?popup->property("contentItem").value<QQuickItem*>():nullptr;
        return content?content->parentItem():nullptr;
    }
    QStringList commandLabels(const ViewerController& viewer) const {
        // Command labels (context Commands, looked up at run time) and their key sequences are data on these surfaces.
        QStringList labels;for(const auto& row:viewer.commands()){const auto map=row.toMap();labels<<map.value("label").toString()<<map.value("binding").toString()<<map.value("binding").toString().split(", ");}
        return labels;
    }
    void walkMainSurfaces(Qt::LayoutDirection direction,const QString& mode,QSize size,bool fullCoverage) {
        QGuiApplication::setLayoutDirection(direction);
        const auto tag=QString("%1-%2-%3").arg(direction==Qt::RightToLeft?"rtl":"ltr",mode).arg(size.width())+(translator.expand?"-expanded":"");
        QTemporaryDir media;QImage generated(640,480,QImage::Format_RGB32);generated.fill(Qt::darkCyan);
        const auto path=media.filePath("generated.png");QVERIFY(generated.save(path));
        Harness h(mode,size);QVERIFY(h.window);QTRY_VERIFY(h.window->isExposed());
        h.viewer.open(path);QTRY_VERIFY_WITH_TIMEOUT(h.viewer.imageReady(),15000);
        // Values that are data rather than interface text: display names, file names, command labels (context Commands,
        // looked up at run time) and the Qt display identity strings of the offscreen platform.
        QStringList dynamic=commandLabels(h.viewer);
        for(const auto& display:h.viewer.displayManager()->property("displays").toList())dynamic<<display.toMap().value("label").toString()<<display.toMap().value("name").toString();
        dynamic<<"generated.png"<<QFileInfo(path).absolutePath()<<"Glancelot";
        for(const auto& family:h.theme.families())dynamic<<family.toMap().value("name").toString()<<family.toMap().value("label").toString(); // theme catalog data
        dynamic<<h.layout.presetNames(); // pane layout preset names are identifiers (built-ins in PaneLayout, user names)
        // Main.qml routing reasons ("Needs the folder grid"): wrapped in the Main.qml step after EN-06 integrates.
        dynamic<<deferredToMainQml;
        // Display and routing status lines are composed in C++ (DisplayManager, Track D) and are data on this surface.
        dynamic<<h.viewer.displayManager()->property("outputColorStatus").toString()<<h.monitor.outputRoutingStatus();
        // Settings, every section.
        auto* settings=h.window->findChild<QObject*>("settingsDialog");QVERIFY(settings);
        for(const auto* section:{"appearance","viewing","browsing","controls","about"}) {
            if(!fullCoverage&&QString(section)!="browsing")continue;
            QVERIFY(QMetaObject::invokeMethod(settings,"openSection",Q_ARG(QVariant,QString(section)),Q_ARG(QVariant,false)));
            QTRY_VERIFY(settings->property("opened").toBool());QTest::qWait(200);
            const auto plain=check(popupItem(settings),QString("settings-%1").arg(section),dynamic);
            if(fullCoverage)QVERIFY2(plain.isEmpty(),qPrintable(describe(plain)));
            capture(h.window,QString("settings-%1-%2").arg(section,tag));
        }
        QVERIFY(QMetaObject::invokeMethod(settings,"close"));QTRY_VERIFY(!settings->property("opened").toBool());
        // Displays and workspaces, every page.
        auto* monitor=h.window->findChild<QObject*>("monitorSettings");QVERIFY(monitor);
        for(int page=0;page<4;++page) {
            if(!fullCoverage&&page!=2)continue;
            QVERIFY(QMetaObject::invokeMethod(monitor,"openPage",Q_ARG(QVariant,page),Q_ARG(QVariant,QString())));
            QTRY_VERIFY(monitor->property("opened").toBool());QTest::qWait(200);
            const auto plain=check(popupItem(monitor),QString("monitor-%1").arg(page),dynamic);
            if(fullCoverage)QVERIFY2(plain.isEmpty(),qPrintable(describe(plain)));
            capture(h.window,QString("monitor-page%1-%2").arg(page).arg(tag));
        }
        QVERIFY(QMetaObject::invokeMethod(monitor,"close"));QTRY_VERIFY(!monitor->property("opened").toBool());
        // Metadata inspector.
        h.layout.setVisible("metadata",true);h.viewer.setMetadataVisible(true);auto* hud=h.window->findChild<QQuickItem*>("metadataHud");QVERIFY(hud);
        QVariant rect;QMetaObject::invokeMethod(h.window,"paneRect",Q_RETURN_ARG(QVariant,rect),Q_ARG(QVariant,QString("metadata")));
        QTRY_VERIFY2(hud->isVisible(),qPrintable(QString("metadata %1 %2x%3 rect %4 detached %5").arg(h.viewer.isMetadataVisible()).arg(hud->width()).arg(hud->height()).arg(QString::fromUtf8(QJsonDocument::fromVariant(rect.value<QJSValue>().toVariant()).toJson(QJsonDocument::Compact))).arg(h.monitor.paneRoutes()["metadata"].toMap()["detached"].toBool())));
        QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.metadataModel()->property("loading").toBool(),5000);QTest::qWait(200);
        for(const auto* summary:{"cameraSummary","exposureSummary","dimensionsSummary","locationSummary"})dynamic<<h.viewer.metadataModel()->property(summary).toString(); // parsed data
        {const auto plain=check(hud,"metadata",dynamic);if(fullCoverage)QVERIFY2(plain.isEmpty(),qPrintable(describe(plain)));}
        capture(h.window,"metadata-"+tag);
        h.viewer.setMetadataVisible(false);
        // Rename and new-folder dialogs.
        auto* rename=h.window->findChild<QObject*>("renameFlow");QVERIFY(rename);
        QVERIFY(QMetaObject::invokeMethod(rename,"openRename",Q_ARG(QVariant,path)));QTRY_VERIFY(rename->property("modal").toBool());QTest::qWait(200);
        for(auto* dialog:rename->findChildren<QObject*>())if(dialog->inherits("QQuickDialog")&&dialog->property("visible").toBool()){const auto plain=check(popupItem(dialog),"rename",dynamic);if(fullCoverage)QVERIFY2(plain.isEmpty(),qPrintable(describe(plain)));}
        capture(h.window,"rename-"+tag);
        for(auto* dialog:rename->findChildren<QObject*>())if(dialog->inherits("QQuickDialog"))QMetaObject::invokeMethod(dialog,"close");
        QTRY_VERIFY(!rename->property("modal").toBool());
        QVERIFY(QMetaObject::invokeMethod(rename,"openNewFolder",Q_ARG(QVariant,media.path())));QTRY_VERIFY(rename->property("modal").toBool());QTest::qWait(200);
        for(auto* dialog:rename->findChildren<QObject*>())if(dialog->inherits("QQuickDialog")&&dialog->property("visible").toBool()){const auto plain=check(popupItem(dialog),"new-folder",dynamic);if(fullCoverage)QVERIFY2(plain.isEmpty(),qPrintable(describe(plain)));}
        capture(h.window,"new-folder-"+tag);
        for(auto* dialog:rename->findChildren<QObject*>())if(dialog->inherits("QQuickDialog"))QMetaObject::invokeMethod(dialog,"close");
        // A detached pane host: translated chrome, mirrored in right-to-left layout.
        const auto display=h.viewer.displayManager()->displays().first().id;
        QVERIFY(h.monitor.detachPane("metadata",display));auto* host=h.window->findChild<QQuickWindow*>("detached-metadata");
        if(!host)for(auto* window:QGuiApplication::topLevelWindows())if(window->objectName()=="detached-metadata")host=qobject_cast<QQuickWindow*>(window);
        QVERIFY(host);QTRY_VERIFY_WITH_TIMEOUT(host->isExposed(),5000);QTest::qWait(200);
        QVERIFY(host->title().startsWith('['));
        {const auto plain=check(host->contentItem(),"detached",dynamic);QVERIFY2(plain.isEmpty(),qPrintable(describe(plain)));}
        // Right-to-left: the Settings button moves to the left of the pane title; the resize grip stays bottom-right.
        QQuickItem* title=nullptr;QQuickItem* settingsText=nullptr;QQuickItem* grip=nullptr;
        const auto settingsLabel=qApp->translate("DetachedPaneHost","Settings");
        for(auto* item:descendants(host->contentItem())) {
            if(item->inherits("QQuickLabel")&&item->property("text").toString()==host->property("paneTitle").toString())title=item;
            if(item->inherits("QQuickText")&&item->property("text").toString()==settingsLabel)settingsText=item;
            if(item->inherits("QQuickMouseArea")&&item->property("cursorShape").toInt()==Qt::SizeFDiagCursor)grip=item;
        }
        QVERIFY(title);QVERIFY(settingsText);QVERIFY(grip);
        const auto titleX=title->mapToScene(QPointF()).x(),settingsX=settingsText->mapToScene(QPointF()).x(),gripX=grip->mapToScene(QPointF()).x();
        if(direction==Qt::RightToLeft)QVERIFY2(settingsX<titleX,qPrintable(QString("settings %1 title %2").arg(settingsX).arg(titleX)));
        else QVERIFY2(settingsX>titleX,qPrintable(QString("settings %1 title %2").arg(settingsX).arg(titleX)));
        QVERIFY(gripX>host->width()/2.);
        capture(host,"detached-metadata-"+tag);
        QVERIFY(h.monitor.redockPane("metadata"));
    }
    // Stand-alone surfaces whose live state needs hardware or an operation in flight: driven with property maps.
    void standaloneSurfaces(Qt::LayoutDirection direction) {
        QGuiApplication::setLayoutDirection(direction);
        const auto tag=QString(direction==Qt::RightToLeft?"rtl":"ltr");
        QQmlPropertyMap animation,viewer,service;ThemeManager theme; // outlive the engine and its bindings
        QQmlEngine engine;auto imports=engine.importPathList();imports.removeAll(QCoreApplication::applicationDirPath());engine.setImportPathList(imports);
        engine.rootContext()->setContextProperty("theme",&theme);
        for(const auto& [key,value]:std::initializer_list<std::pair<const char*,QVariant>>{{"active",true},{"isAnimated",true},{"playing",false},{"currentFrame",2},{"frameCount",12},{"speed",1.5},{"loop",true},{"frameLabel","Frame 3 / 12"}})animation.insert(key,value);
        viewer.insert("animationController",QVariant::fromValue<QObject*>(&animation));
        for(const auto& [key,value]:std::initializer_list<std::pair<const char*,QVariant>>{{"active",true},{"paused",false},{"progress",0.4},{"completedItems",4},{"totalItems",10},{"transferredBytes",4.5*1024*1024},{"totalBytes",11.0*1024*1024},{"statusMessage","Copying 10 items"}})service.insert(key,value);
        engine.rootContext()->setContextProperty("viewer",&viewer);
        QQuickWindow window;window.resize(560,160);window.setColor(QColor("#101418"));window.show();QTRY_VERIFY(window.isExposed());
        QQmlComponent bar(&engine,QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/AnimationBar.qml")));
        std::unique_ptr<QQuickItem> barItem(qobject_cast<QQuickItem*>(bar.create()));QVERIFY2(barItem,qPrintable(bar.errorString()));
        barItem->setParentItem(window.contentItem());barItem->setPosition({16,16});barItem->setWidth(520);
        QCOMPARE(barItem->property("label").toString(),QString("[Fràmé 3 / 12]"));
        QTest::qWait(100);
        {const auto plain=check(barItem.get(),"animation-bar");QVERIFY2(plain.isEmpty(),qPrintable(describe(plain)));}
        // Media controls keep their order in right-to-left layout.
        QQuickItem* previous=nullptr;QQuickItem* next=nullptr;
        for(auto* item:descendants(barItem.get())){const auto name=item->property("Accessible.name");Q_UNUSED(name);
            if(item->objectName().isEmpty()&&item->inherits("QQuickRectangle")){const auto text=item->childItems().isEmpty()?QString():item->childItems().first()->property("text").toString();if(text==QString::fromUtf8("◀"))previous=item;if(text==QString::fromUtf8("▶"))next=item;}}
        QVERIFY(previous);QVERIFY(next);QVERIFY(previous->mapToScene(QPointF()).x()<next->mapToScene(QPointF()).x());
        QQmlComponent hud(&engine,QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/TransferHud.qml")));
        std::unique_ptr<QQuickItem> hudItem(qobject_cast<QQuickItem*>(hud.createWithInitialProperties({{"service",QVariant::fromValue<QObject*>(&service)}})));
        QVERIFY2(hudItem,qPrintable(hud.errorString()));hudItem->setParentItem(window.contentItem());hudItem->setPosition({16,72});hudItem->setWidth(520);
        QTest::qWait(150);
        // The status line is the operation's own message (C++), so it is data here.
        {const auto plain=check(hudItem.get(),"transfer-hud",{"Copying 10 items"});QVERIFY2(plain.isEmpty(),qPrintable(describe(plain)));}
        capture(&window,"animation-transfer-"+tag);
    }
private slots:
    void initTestCase() {
        for(const auto& font:QDir(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/assets/fonts/geist")).entryList({"*.ttf"},QDir::Files))
            QFontDatabase::addApplicationFont(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/assets/fonts/geist/")+font);
        QQuickStyle::setStyle("Basic");qmlRegisterType<ImageCanvas>("Glancelot",1,0,"ImageCanvas");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")),"Glancelot",1,0,"Theme");
        QVERIFY(translator.loadTs(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/translations/glancelot_en_XA.ts")));
        QVERIFY(QCoreApplication::installTranslator(&translator));
    }
    void init(){config=std::make_unique<QTemporaryDir>();qputenv("GLANCELOT_CONFIG_DIR",config->path().toUtf8());}
    void cleanup(){qunsetenv("GLANCELOT_CONFIG_DIR");config.reset();QGuiApplication::setLayoutDirection(Qt::LeftToRight);translator.expand=false;}
    void catalogCoversEveryInterfaceMessage() {
        // lupdate extracted everything; the pseudo pass finished every message and kept placeholders.
        QFile file(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/translations/glancelot_en_XA.ts"));QVERIFY(file.open(QIODevice::ReadOnly));
        const auto text=QString::fromUtf8(file.readAll());QVERIFY(!text.contains("type=\"unfinished\""));QVERIFY(!text.contains("type=\"vanished\""));
        QXmlStreamReader xml(text);QString source;int checked=0;
        static const QRegularExpression placeholders("%L?\\d+|%n");
        while(!xml.atEnd()){if(!xml.readNextStartElement())continue;
            if(xml.name()==u"source")source=xml.readElementText();
            else if(xml.name()==u"translation"){const auto translation=xml.readElementText();++checked;
                QStringList a,b;for(auto it=placeholders.globalMatch(source);it.hasNext();)a<<it.next().captured();for(auto it=placeholders.globalMatch(translation);it.hasNext();)b<<it.next().captured();
                a.sort();b.sort();QVERIFY2(a==b,qPrintable(source));QVERIFY2(translation.startsWith('[')&&translation.endsWith(']'),qPrintable(source));}}
        QVERIFY(checked>400);
        QCOMPARE(qApp->translate("MonitorSettings","Displays and workspaces"),QString::fromUtf8("[Dîsplàýs àñd wõrkspàçés]"));
        QCOMPARE(qApp->translate("TransferHud","%1 of %2 items (%3 / %4) — %5%").arg(1).arg(2).arg("a").arg("b").arg(50),QString::fromUtf8("[1 õf 2 îtéms (a / b) — 50%]"));
    }
    void pseudoLocaleLeftToRightDark(){walkMainSurfaces(Qt::LeftToRight,"dark",{1180,780},true);}
    void pseudoLocaleRightToLeftDark(){walkMainSurfaces(Qt::RightToLeft,"dark",{1180,780},false);}
    void pseudoLocaleCompactLight(){walkMainSurfaces(Qt::LeftToRight,"light",{560,640},false);}
    void standaloneLeftToRight(){standaloneSurfaces(Qt::LeftToRight);}
    // Text 40% longer than English: every surface is walked again and clipping candidates are reported, not asserted.
    void expandedTextClipping(){translator.expand=true;walkMainSurfaces(Qt::LeftToRight,"dark",{1180,780},false);walkMainSurfaces(Qt::LeftToRight,"light",{560,640},false);translator.expand=false;}
    void standaloneRightToLeft(){standaloneSurfaces(Qt::RightToLeft);}
    void cleanupTestCase() {
        const auto destination=evidence();if(destination.isEmpty())return;
        QFile report(QDir(destination).filePath("coverage-report.txt"));QVERIFY(report.open(QIODevice::WriteOnly|QIODevice::Text));
        QTextStream out(&report);out<<"# Generated by translation_coverage_ui_tests (en_XA pseudo-locale, offscreen)\n";
        out<<"catalog messages: "<<translator.size()<<"\n\n## Untranslated visible text (named surfaces)\n";
        plainSeen.removeDuplicates();for(const auto& line:plainSeen)out<<line<<"\n";if(plainSeen.isEmpty())out<<"none\n";
        out<<"\n## Clipping candidates (text wider than its box without wrap or elide, or elided)\n";
        clippedSeen.removeDuplicates();for(const auto& line:clippedSeen)out<<line<<"\n";if(clippedSeen.isEmpty())out<<"none\n";
    }
};
QTEST_MAIN(TranslationCoverageUiTests)
#include "translation_coverage_ui_tests.moc"
