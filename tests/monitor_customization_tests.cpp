#include <QtTest>
#include <QTemporaryDir>
#include <QGuiApplication>
#include <QScreen>
#include <QJsonDocument>
#include <QFile>
#include <QLockFile>
#include "glancelot/monitor_customization.hpp"
#include "glancelot/display_manager.hpp"
#include "glancelot/pane_layout.hpp"
#include "glancelot/settings_file.hpp"
#include "monitor_customization_state.hpp"
#include "presentation_window.hpp"
#include "image_canvas.hpp"
#include "monitor_customization_topology.hpp"
#include "canvas_texture_admission.hpp"
#include "settings_policy.hpp"

class MonitorCustomizationTests:public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> config;
    struct Harness {
        glancelot::DisplayManager manager;
        PaneLayout panes;
        glancelot::MonitorCustomization customization{&manager,&panes};
        Harness(){panes.setSlot(1);customization.setSlot(1);}
    };
private slots:
    void initTestCase(){glancelot::registerPresentationWindowFactory();}
    void init(){config=std::make_unique<QTemporaryDir>();qputenv("GLANCELOT_CONFIG_DIR",config->path().toUtf8());}
    void cleanup(){qunsetenv("GLANCELOT_CONFIG_DIR");config.reset();}
    void optionsRejectWholeInvalidPatch() {
        Harness h;const auto before=h.customization.outputOptions();
        QVERIFY(!h.customization.setOutputOptions({{"guides",true},{"fitMode","invalid"}}));
        QCOMPARE(h.customization.outputOptions(),before);
        QVERIFY(!h.customization.setOutputOptions({{"guides",1}}));
        QVERIFY(!h.customization.setOutputOptions({{"backgroundColor","#gg0000"}}));
        QVERIFY(h.customization.setOutputOptions({{"guides",true},{"fitMode","fill"}}));
        QTRY_COMPARE(h.customization.outputOptions().value("fitMode").toString(),QString("fill"));
        Harness restored;QCOMPARE(restored.customization.outputOptions().value("guides").toBool(),true);
        QVERIFY(!restored.manager.isPresentationActive());
    }
    void snapshotsValidateWithoutPartialMutation() {
        Harness h;auto original=h.panes.snapshot(),bad=original;auto center=bad["center"].toMap();center["ratio"]=2.;bad["center"]=center;
        QVERIFY(!h.panes.restoreSnapshot(bad));QCOMPARE(h.panes.snapshot(),original);
        auto next=original;next["locked"]=true;QVERIFY(h.panes.restoreSnapshot(next));QVERIFY(h.panes.locked());
    }
    void presetsCrudIsolationAndUnknownFields() {
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){root["future"]=42;root["monitorCustomization"]=QJsonObject{{"version",1},{"future","retained"}};}).ok());
        Harness h;QVERIFY(h.customization.savePreset({},"Presentation","presentation"));QTRY_COMPARE(h.customization.presets().size(),1);
        const auto id=h.customization.presets()[0].toMap().value("id").toString();
        QVERIFY(!h.customization.savePreset({},"presentation","workspace"));
        QVERIFY(h.customization.renamePreset(id,"Renamed"));QTRY_COMPARE(h.customization.presets()[0].toMap().value("name").toString(),QString("Renamed"));
        QVERIFY(h.customization.setOutputOptions({{"fitMode","actual"}}));QTRY_COMPARE(h.customization.outputOptions().value("fitMode").toString(),QString("actual"));
        QVERIFY(h.customization.applyPreset(id));QTRY_COMPARE(h.customization.outputOptions().value("fitMode").toString(),QString("shrink"));
        h.customization.setSlot(2);QCOMPARE(h.customization.presets().size(),0);
        h.customization.setSlot(1);QCOMPARE(h.customization.presets().size(),1);
        QVERIFY(h.customization.deletePreset(id));QTRY_COMPARE(h.customization.presets().size(),0);
        QJsonObject root;QVERIFY(glancelot::readSettings(root).ok());QCOMPARE(root.value("future").toInt(),42);QCOMPARE(root.value("monitorCustomization").toObject().value("future").toString(),QString("retained"));
    }
    void freezeLockAndEmergencyRecovery() {
        Harness h;QVERIFY(!h.manager.displays().isEmpty());const auto display=h.manager.displays().first().id;
        QVERIFY(h.customization.detachPane("tree",display));QTRY_VERIFY(h.customization.paneRoutes()["tree"].toMap().value("detached").toBool());
        QVERIFY(h.customization.setPaneFrozen("tree",true));QTRY_VERIFY(h.panes.pane("tree").value("frozen").toBool());
        QVERIFY(!h.customization.redockPane("tree"));
        h.customization.setLayoutLocked(true);QTRY_VERIFY(h.customization.layoutLocked());
        QVERIFY(!h.customization.setPaneFrozen("tree",false));
        h.customization.recoverToController();QVERIFY(!h.customization.paneRoutes()["tree"].toMap().value("detached").toBool());
        QVERIFY(h.customization.layoutLocked());QVERIFY(h.customization.paneRoutes()["tree"].toMap().value("frozen").toBool());
        QVERIFY(h.customization.resetPreferences());QTRY_VERIFY(!h.customization.layoutLocked());QVERIFY(!h.panes.pane("tree").value("frozen").toBool());
    }
    void newerVersionIsNeverOverwritten() {
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){root["monitorCustomization"]=QJsonObject{{"version",2},{"future",123}};}).ok());
        Harness h;QVERIFY(!h.customization.setOutputOptions({{"guides",true}}));QVERIFY(!h.customization.resetPreferences());
        h.customization.recoverToController();QJsonObject root;QVERIFY(glancelot::readSettings(root).ok());QCOMPARE(root["monitorCustomization"].toObject().value("version").toInt(),2);
    }
    void simultaneousSlotEditRefusesStaleOverwrite() {
        Harness first,second;QVERIFY(first.customization.setOutputOptions({{"guides",true}}));QTRY_VERIFY(first.customization.outputOptions().value("guides").toBool());
        QVERIFY(!second.customization.setOutputOptions({{"imageInfo",true}}));
        QVERIFY(!second.customization.outputOptions().value("imageInfo").toBool());
        QJsonObject root;QVERIFY(glancelot::readSettings(root).ok());QVERIFY(root["monitorCustomization"].toObject()["slots"].toObject()["1"].toObject()["output"].toObject()["guides"].toBool());
    }
    void optionsReachRealPresentationAndPreserveBudgets() {
        Harness h;QImage image(400,300,QImage::Format_RGB32);image.fill(Qt::red);h.manager.updateImage("generated.png",image,1);
        QVERIFY(h.manager.openPresentation());
        QVERIFY(h.customization.setOutputOptions({{"fitMode","actual"},{"imageInfo",true},{"guides",true},{"background","checker"},{"cursorPolicy","hidden"},{"windowMode","windowed"}}));
        auto* window=qobject_cast<glancelot::PresentationWindow*>(h.manager.presentationWindow());QVERIFY(window);
        QTRY_COMPARE(window->primaryCanvas()->fitMode(),QString("actual"));QCOMPARE(window->primaryCanvas()->zoom(),qreal(1));QVERIFY(window->primaryCanvas()->checkerboard());
        QVERIFY(window->primaryCanvas()->textureBudget()+window->secondaryCanvas()->textureBudget()<=glancelot::kPresentationTextureBudget-1024*1024);
        QCOMPARE(window->cursor().shape(),Qt::BlankCursor);
        QVERIFY(h.customization.setOutputOptions({{"viewportLink","linked"}}));
        QVERIFY(!h.customization.syncControllerViewport({{"fit","actual"}},0));
        QVERIFY(h.customization.syncControllerViewport({{"fit","fit"},{"rotation",90}},1));QCOMPARE(window->primaryCanvas()->rotation(),90);
        QVERIFY(h.customization.setOutputOptions({{"viewportLink","independent"}}));QVERIFY(!h.customization.syncControllerViewport({{"fit","actual"}},1));
        h.manager.closePresentation();QVERIFY(!h.manager.isPresentationActive());
    }
    void heldAndTimedRoutingKeepLatestGenerationBounded() {
        Harness h;QImage first(20,20,QImage::Format_RGB32);first.fill(Qt::red);QImage second=first.copy();second.fill(Qt::blue);
        h.manager.updateImage("/folder/a.png",first,1);QVERIFY(h.manager.openPresentation());
        QVERIFY(h.customization.setOutputOptions({{"followSelection",false}}));h.manager.updateImage("/folder/b.png",second,2);
        QCOMPARE(h.manager.currentPath(),QString("/folder/a.png"));
        QVERIFY(h.customization.setOutputOptions({{"followSelection",true},{"slideshowTiming","independent"}}));
        h.manager.updateImage("/folder/b.png",second,3);QCOMPARE(h.manager.currentPath(),QString("/folder/a.png"));
        QTRY_COMPARE_WITH_TIMEOUT(h.manager.currentPath(),QString("/folder/b.png"),4500);
        QVERIFY(h.manager.retainedImageBytes()<=glancelot::DisplayManager::imageBudgetBytes());
        QVERIFY(h.customization.setOutputOptions({{"slideshowTiming","shared"},{"folderChange","hold"}}));
        h.manager.updateImage("/folder/c.png",first,4);h.manager.updateImage("/other/d.png",second,5);QCOMPARE(h.manager.currentPath(),QString("/folder/c.png"));
    }
    void generatedTopologiesRecoverMissingAndReusedIdentities() {
        QList<glancelot::DisplayInfo> displays;
        for(int i=0;i<3;++i){glancelot::DisplayInfo display;display.id=QString("display-%1").arg(i);display.availableGeometry=QRect((i-1)*1920,0,1920,1080);display.geometry=display.availableGeometry;display.devicePixelRatio=i==1?2:1;displays.append(display);
            const auto placed=glancelot::monitor_topology::place(display.id,QRect(10000000,-10000000,9000,7000),displays,"display-0",true);
            QCOMPARE(placed.displayId,display.id);QVERIFY(display.availableGeometry.contains(placed.geometry));
        }
        const auto missing=glancelot::monitor_topology::place("removed",QRect(4000,0,800,600),displays,"display-0",false);
        QVERIFY(missing.recovered);QCOMPARE(missing.displayId,QString("display-0"));QVERIFY(displays[0].availableGeometry.contains(missing.geometry));
        const auto reused=glancelot::monitor_topology::place("display-2",QRect(4000,0,800,600),displays,"display-0",false);
        QVERIFY(reused.recovered);QCOMPARE(reused.displayId,QString("display-0"));
        const auto minimum=glancelot::monitor_topology::clamp(QRect(100,100,20,20),QRect(0,0,100,100));QCOMPARE(minimum.size(),QSize(360,280));
    }
    void rendererIncarnationsShareAdmissionUntilOldTextureReleases() {
        CanvasTextureAdmission shared;
        QVERIFY(shared.reserve(200,256));QVERIFY(!shared.reserve(100,256));QCOMPARE(shared.bytes(),std::int64_t(200));
        shared.release(200);QVERIFY(shared.reserve(100,256));shared.release(100);QCOMPARE(shared.bytes(),std::int64_t(0));
    }
    void nestedUnknownFieldsSurviveEditsAndPortableImport() {
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){root["monitorCustomization"]=QJsonObject{{"version",1},{"slots",QJsonObject{{"1",QJsonObject{{"future",99},{"output",QJsonObject{{"future-output",17}}}}}}}};}).ok());
        Harness h;QVERIFY(h.customization.setOutputOptions({{"guides",true}}));QTRY_VERIFY(h.customization.outputOptions().value("guides").toBool());
        QVERIFY(h.customization.setOutputOptions({{"imageInfo",true}}));
        QJsonObject root;QVERIFY(glancelot::readSettings(root).ok());const auto slot=root["monitorCustomization"].toObject()["slots"].toObject()["1"].toObject();QCOMPARE(slot.value("future").toInt(),99);QCOMPARE(slot.value("output").toObject().value("future-output").toInt(),17);
        const auto path=config->filePath("portable.json");QVERIFY(glancelot::exportSettings(path));QVERIFY(glancelot::resetSection("all"));QVERIFY(glancelot::importSettings(path));
        h.customization.reloadPreferences();QVERIFY(h.customization.outputOptions().value("guides").toBool());
    }
    void safeLayoutRestoreOwnsMonitorFieldsAndPreservesOtherSlots() {
        Harness h;QVERIFY(h.customization.setOutputOptions({{"guides",true}}));QJsonObject source;QVERIFY(glancelot::readSettings(source).ok());
        auto current=source;auto feature=current["monitorCustomization"].toObject();auto slotsObject=feature["slots"].toObject();slotsObject["2"]=QJsonObject{{"future",77}};auto first=slotsObject["1"].toObject();first["future"]=88;slotsObject["1"]=first;feature["slots"]=slotsObject;current["monitorCustomization"]=feature;
        glancelot::recovery::policy::Selection selection;selection.slotSection=glancelot::recovery::SlotSection::Layout;selection.slot=1;
        const auto merged=glancelot::recovery::policy::merge(current,source,selection);QVERIFY(merged.replacement);QCOMPARE(merged.replacement->value("monitorCustomization").toObject()["slots"].toObject()["2"],slotsObject["2"]);
        QCOMPARE(merged.replacement->value("monitorCustomization").toObject()["slots"].toObject()["1"].toObject().value("future").toInt(),88);
    }
};
QTEST_MAIN(MonitorCustomizationTests)
#include "monitor_customization_tests.moc"
