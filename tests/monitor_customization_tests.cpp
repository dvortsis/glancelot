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
#include "file_lock.hpp"
#include "settings_retry.hpp"
#include <QDir>

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
    void invalidSlotDoesNotEraseValidPresets() {
        Harness first;QVERIFY(first.customization.savePreset({},"Keep","presentation"));
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){auto feature=root["monitorCustomization"].toObject();auto rows=feature["slots"].toObject();auto slot=rows["1"].toObject();auto output=slot["output"].toObject();output["fitMode"]="invalid";slot["output"]=output;rows["1"]=slot;feature["slots"]=rows;root["monitorCustomization"]=feature;}).ok());
        QJsonObject before;QVERIFY(glancelot::readSettings(before).ok());Harness second;
        QVERIFY(!second.customization.setOutputOptions({{"guides",true}}));
        QJsonObject after;QVERIFY(glancelot::readSettings(after).ok());QCOMPARE(after,before);
        QVERIFY(second.customization.resetPreferences());QTRY_COMPARE(second.customization.presets().size(),0);
    }
    void busySaveRollsBackUntilCommittedAndCancellationIsFenced() {
        Harness h;QVERIFY(glancelot::updateSettings([](QJsonObject&){}).ok());
        FileLock lock(glancelot::settingsFilePath()+".lock");QVERIFY(lock.acquire(0));
        QVERIFY(h.customization.setOutputOptions({{"guides",true}}));QVERIFY(!h.customization.outputOptions()["guides"].toBool());
        QVERIFY(!h.customization.setOutputOptions({{"imageInfo",true}}));lock.release();
        QTRY_VERIFY_WITH_TIMEOUT(h.customization.outputOptions()["guides"].toBool(),5000);
        QVERIFY(lock.acquire(0));QVERIFY(h.customization.setOutputOptions({{"imageInfo",true}}));
        h.customization.recoverToController();lock.release();QTest::qWait(200);
        QVERIFY(!h.customization.outputOptions()["imageInfo"].toBool());
        Harness restored;QVERIFY(!restored.customization.outputOptions()["imageInfo"].toBool());
    }
    void writeFailureDoesNotMutateRuntime() {
        Harness h;const auto original=h.customization.outputOptions();
        QVERIFY(QDir().mkpath(glancelot::settingsFilePath()+".lock"));
        QVERIFY(!h.customization.setOutputOptions({{"guides",true}}));QCOMPARE(h.customization.outputOptions(),original);
    }
    void presetAndDisplayBoundsAreAtomic() {
        Harness h;QVERIFY(h.customization.savePreset({},QString(80,QChar(0x03bb)),"presentation"));
        QVERIFY(!h.customization.savePreset({},QString(81,'x'),"presentation"));
        for(int i=1;i<64;++i)QVERIFY(h.customization.savePreset({},QString("Preset%1").arg(i),"presentation"));
        QCOMPARE(h.customization.presets().size(),64);QVERIFY(!h.customization.savePreset({},"Overflow","presentation"));
        auto state=glancelot::monitor_state::options();QVERIFY(!glancelot::monitor_state::patchOptions({{"displayId",QString(257,'x')}},state));
        QVariantMap normalized;auto raw=QVariantMap{{"output",state},{"panes",glancelot::monitor_state::routes()},{"presets",QVariantList{}}};QVariantMap outputs;
        for(int i=0;i<16;++i)outputs[QString::number(i)]=state;raw["outputsByDisplay"]=outputs;QVERIFY(glancelot::monitor_state::normalize(raw,normalized));
        outputs["17"]=state;raw["outputsByDisplay"]=outputs;QVERIFY(!glancelot::monitor_state::normalize(raw,normalized));
    }
    void serializedBoundsPreserveOriginalSettings() {
        Harness h;QVERIFY(glancelot::updateSettings([](QJsonObject& root){root["monitorCustomization"]=QJsonObject{{"version",1},{"future",QString(256*1024,'x')}};}).ok());
        h.customization.reloadPreferences();QJsonObject before;QVERIFY(glancelot::readSettings(before).ok());
        QVERIFY(!h.customization.setOutputOptions({{"guides",true}}));QJsonObject after;QVERIFY(glancelot::readSettings(after).ok());QCOMPARE(after,before);
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){root.remove("monitorCustomization");root["future"]=QString(1024*1024-300,'x');}).ok());
        h.customization.reloadPreferences();QVERIFY(glancelot::readSettings(before).ok());QVERIFY(!h.customization.savePreset({},"Would overflow","workspace"));QVERIFY(glancelot::readSettings(after).ok());QCOMPARE(after,before);
    }
    void outputMovePreservesTransformAndLayoutAndMissingPreferenceCanClear() {
        Harness h;QImage image(300,200,QImage::Format_RGBA8888);image.fill(Qt::red);h.manager.updateImage("/a.png",image,1);QVERIFY(h.manager.openPresentation());
        QVERIFY(h.customization.setOutputOptions({{"windowMode","windowed"},{"fitMode","fill"}}));
        auto* output=qobject_cast<glancelot::PresentationWindow*>(h.manager.presentationWindow());QVERIFY(output);output->primaryCanvas()->rotateClockwise();const auto state=output->viewportState();
        auto dock=h.panes.snapshot();dock["locked"]=true;QVERIFY(h.panes.restoreSnapshot(dock));
        QVERIFY(h.customization.moveOutput(h.manager.displays().first().id));QCOMPARE(output->viewportState(),state);QVERIFY(h.panes.locked());
        QVERIFY(h.customization.setOutputOptions({{"displayId",""}}));QVERIFY(h.manager.isPresentationActive());
        QVERIFY(!h.customization.moveOutput("removed"));QCOMPARE(output->viewportState(),state);
        QVERIFY(!h.customization.swapControllerAndOutput());QVERIFY(h.manager.isPresentationActive());
        h.manager.closePresentation();QVERIFY(!h.customization.moveOutput(h.manager.displays().first().id));
    }
    void folderResumeReopenAndStaleGenerations() {
        Harness h;QImage a(20,20,QImage::Format_RGBA8888);a.fill(Qt::red);QImage b=a.copy();b.fill(Qt::blue);
        h.manager.updateImage("/first/a.png",a,1);QVERIFY(h.manager.openPresentation());
        QVERIFY(h.customization.setOutputOptions({{"folderChange","hold"},{"viewportLink","linked"}}));
        h.manager.updateImage("/second/b.png",b,2);QCOMPARE(h.manager.outputGeneration(),quint64(1));
        QVERIFY(!h.customization.syncControllerViewport({{"fit","actual"}},2));
        QVERIFY(h.customization.setOutputOptions({{"folderChange","follow"}}));QCOMPARE(h.manager.currentPath(),QString("/second/b.png"));QCOMPARE(h.manager.outputGeneration(),quint64(2));
        QVERIFY(h.customization.setOutputOptions({{"folderChange","disable"}}));h.manager.updateImage("/third/c.png",a,3);QVERIFY(!h.manager.isPresentationActive());
        QVERIFY(h.manager.openPresentation());h.manager.updateImage("/third/d.png",b,4);QVERIFY(h.manager.isPresentationActive());
        h.manager.updateImage("/old.png",a,2);QCOMPARE(h.manager.currentGeneration(),quint64(4));QCOMPARE(h.manager.currentPath(),QString("/third/d.png"));
        QVERIFY(h.customization.setOutputOptions({{"followSelection",false},{"viewportLink","independent"}}));h.manager.updateImage("/third/e.png",a,5);
        QVERIFY(h.customization.setOutputOptions({{"followSelection",true}}));QCOMPARE(h.manager.currentPath(),QString("/third/e.png"));
        h.manager.releaseCachedImages();QVERIFY(h.manager.retainedImageBytes()<=glancelot::DisplayManager::imageBudgetBytes());
    }
    void identifyIsBoundedAndPaneGeometryRetainsPreference() {
        Harness h;const auto count=QGuiApplication::topLevelWindows().size();QVERIFY(h.customization.identifyDisplays());QVERIFY(h.customization.identifyDisplays());
        QVERIFY(QGuiApplication::topLevelWindows().size()<=count+h.manager.displayCount());QTRY_COMPARE_WITH_TIMEOUT(QGuiApplication::topLevelWindows().size(),count,3500);
        const auto id=h.manager.displays().first().id;QVERIFY(h.customization.detachPane("metadata",id));
        const QVariantMap geometry{{"x",10000000},{"y",-10000000},{"width",9000},{"height",7000}};QVERIFY(h.customization.updatePaneGeometry("metadata",geometry));
        QCOMPARE(h.customization.paneRoutes()["metadata"].toMap()["geometry"].toMap(),geometry);QVERIFY(h.customization.redockPane("metadata"));
        Harness restored;QCOMPARE(restored.customization.paneRoutes()["metadata"].toMap()["geometry"].toMap(),geometry);
    }
    void dockTransactionRetainsLatestUnknownFieldsWithoutDuplicateOwner() {
        Harness h;const auto layout=QJsonObject::fromVariantMap(h.panes.snapshot());
        QVERIFY(glancelot::updateSettings([layout](QJsonObject& root){auto row=layout;row["future"]=42;auto center=row["center"].toObject();center["future"]=43;row["center"]=center;auto panes=row["panes"].toObject();auto tree=panes["tree"].toObject();tree["future"]=44;panes["tree"]=tree;row["panes"]=panes;root["layouts"]=QJsonObject{{"1",row}};}).ok());
        h.customization.setLayoutLocked(true);QTRY_VERIFY(h.customization.layoutLocked());QJsonObject root;QVERIFY(glancelot::readSettings(root).ok());const auto saved=root["layouts"].toObject()["1"].toObject();QCOMPARE(saved["future"].toInt(),42);QCOMPARE(saved["center"].toObject()["future"].toInt(),43);QCOMPARE(saved["panes"].toObject()["tree"].toObject()["future"].toInt(),44);
        QVERIFY(!root["monitorCustomization"].toObject()["slots"].toObject()["1"].toObject().contains("dock"));
    }
    void pinnedOutputIgnoresFollowFolderAndTimingPolicy() {
        Harness h;QImage pin(40,30,QImage::Format_RGBA8888);pin.fill(Qt::red);QImage latest=pin.copy();latest.fill(Qt::blue);
        h.manager.updateImage("/first/a.png",pin,1);h.manager.pinCurrentImage();QVERIFY(h.manager.openPresentation({},glancelot::PresentationMode::Pinned));
        QVERIFY(h.customization.setOutputOptions({{"followSelection",false},{"folderChange","disable"},{"slideshowTiming","independent"}}));
        h.manager.updateImage("/second/b.png",latest,2);QVERIFY(h.manager.isPresentationActive());QCOMPARE(h.manager.outputGeneration(),quint64(1));
        auto* window=qobject_cast<glancelot::PresentationWindow*>(h.manager.presentationWindow());QVERIFY(window);QCOMPARE(window->primaryCanvas()->image().cacheKey(),pin.cacheKey());
        h.manager.setPresentationMode("mirror");QVERIFY(h.customization.setOutputOptions({{"followSelection",true},{"slideshowTiming","shared"}}));
        h.manager.updateImage("/second/c.png",latest,3);QVERIFY(h.manager.isPresentationActive());QCOMPARE(h.manager.currentPath(),QString("/second/c.png"));
    }
    void linkedComparisonRotationAndOutputGestureUseDisplayedGeneration() {
        Harness h;QImage image(400,200,QImage::Format_RGBA8888);image.fill(Qt::red);h.manager.updateImage("/a.png",image,1);h.manager.pinCurrentImage();h.manager.setPresentationMode("comparison");QVERIFY(h.manager.openPresentation({},glancelot::PresentationMode::Comparison));
        QVERIFY(h.customization.setOutputOptions({{"viewportLink","linked"}}));auto* window=qobject_cast<glancelot::PresentationWindow*>(h.manager.presentationWindow());QVERIFY(window);
        QVERIFY(h.customization.syncControllerViewport({{"fit","actual"},{"rotation",90}},1));QCOMPARE(window->secondaryCanvas()->rotation(),90);
        QSignalSpy gestures(&h.customization,&glancelot::MonitorCustomization::controllerViewportRequested);window->viewportCommand("rotate");QCOMPARE(gestures.size(),1);QCOMPARE(gestures.first()[1].toULongLong(),quint64(1));
        QCOMPARE(window->primaryCanvas()->rotation(),180);QCOMPARE(window->secondaryCanvas()->rotation(),180);
        QVERIFY(h.customization.setOutputOptions({{"viewportLink","independent"}}));window->viewportCommand("rotate");QCOMPARE(gestures.size(),1);
    }
    void duplicateAndPlaceholderSerialsAreWeak() {
        glancelot::DisplayInfo a;a.id="a";a.serialNumber="unique";QList<glancelot::DisplayInfo> displays{a};QVERIFY(glancelot::monitor_topology::strongIdentity(a,displays));
        auto b=a;b.id="b";displays.append(b);QVERIFY(!glancelot::monitor_topology::strongIdentity(a,displays));a.serialNumber="0000";QVERIFY(!glancelot::monitor_topology::strongIdentity(a,{a}));
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
