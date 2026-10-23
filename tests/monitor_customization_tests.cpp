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
#include "monitor_customization_screens.hpp"
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
    // STACK-F049-PRESETS: controller layout presets obey the coordinator's canonical lock/freeze, the derived routes follow,
    // workspace snapshots capture the result, and the two preset kinds never share a name.
    void layoutPresetsObeyCanonicalLockAndFreeze() {
        Harness h;
        h.customization.setLayoutLocked(true);QTRY_VERIFY(h.customization.layoutLocked());
        QVERIFY(!h.panes.applyPreset("Review"));QVERIFY(h.panes.pane("topbar")["visible"].toBool());
        h.customization.setLayoutLocked(false);QTRY_VERIFY(!h.customization.layoutLocked());
        QVERIFY(h.customization.setPaneFrozen("grid",true));QTRY_VERIFY(h.customization.paneRoutes()["grid"].toMap()["frozen"].toBool());
        QVERIFY(!h.panes.applyPreset("Review"));QVERIFY(h.panes.feedback().contains("Unfreeze"));
        QVERIFY(h.customization.setPaneFrozen("grid",false));QTRY_VERIFY(!h.panes.pane("grid")["frozen"].toBool());
        QVERIFY(h.panes.applyPreset("Review"));QVERIFY(!h.customization.paneRoutes()["grid"].toMap()["frozen"].toBool());
        QVERIFY(h.customization.savePreset({},"Canonical review","workspace"));QTRY_COMPARE(h.customization.presets().size(),1);
        const auto dock=h.customization.presets()[0].toMap()["snapshot"].toMap()["dock"].toMap();
        QVERIFY(!dock["panes"].toMap()["topbar"].toMap()["visible"].toBool());QVERIFY(!dock["locked"].toBool());
        // Distinct names in both directions, case-insensitive.
        QVERIFY(!h.customization.savePreset({},"review","presentation"));QVERIFY(h.customization.feedback().contains("pane layout preset"));
        QVERIFY(h.panes.savePreset("Studio"));QVERIFY(!h.customization.savePreset({},"studio","workspace"));
        QVERIFY(!h.customization.renamePreset(h.customization.presets()[0].toMap()["id"].toString(),"Browse"));
        QVERIFY(!h.panes.savePreset("Canonical review"));QVERIFY(h.panes.feedback().contains("display workspace"));
        QCOMPARE(h.customization.presets()[0].toMap()["name"].toString(),QString("Canonical review"));
        // Another slot's routes and layout are untouched by this slot's presets.
        Harness other;other.panes.setSlot(2);other.customization.setSlot(2);
        QVERIFY(other.panes.pane("topbar")["visible"].toBool());QCOMPARE(other.customization.presets().size(),0);
    }
    // A layout preset applied while a coordinator transaction is still saving never leaves a second live state: the
    // runtime layout matches layouts[slot] afterwards, and a superseded edit is reported rather than dropped silently.
    void layoutPresetDuringPendingTransactionStaysCoherent() {
        for(int order=0;order<2;++order) { // 0: freeze transaction first, then the preset; 1: preset first, then the freeze
            init();Harness h;QVERIFY(glancelot::updateSettings([](QJsonObject&){}).ok());
            FileLock lock(glancelot::settingsFilePath()+".lock");QVERIFY(lock.acquire(0));
            if(order==1)QVERIFY(h.panes.applyPreset("Review"));
            QVERIFY(h.customization.setPaneFrozen("tree",true));QVERIFY(h.customization.saving());
            if(order==0)QVERIFY(h.panes.applyPreset("Review"));
            lock.release();
            QTRY_VERIFY_WITH_TIMEOUT(!h.customization.saving(),5000);
            QTRY_VERIFY_WITH_TIMEOUT(!glancelot::settings_detail::hasPendingSettings(&h.panes),5000);
            // Compare what layouts[slot] loads as, not raw JSON: a preset that lands first writes no "frozen" key.
            PaneLayout disk;disk.setSlot(1);QVERIFY(disk.feedback().isEmpty());QCOMPARE(disk.locked(),h.panes.locked());
            for(const auto& id:QStringList{"tree","topbar","filmstrip","grid"})
                for(const auto* key:{"visible","frozen","region","extent","order"})
                    QCOMPARE(disk.pane(id).value(key),h.panes.pane(id).value(key));
            // Whichever write lands first decides; every outcome is explicit and coherent.
            const auto feedback=h.customization.feedback();const bool frozen=h.panes.pane("tree")["frozen"].toBool(),preset=!h.panes.pane("topbar")["visible"].toBool();
            const bool superseded=feedback.contains("replaced")&&frozen&&!preset&&order==0;
            const bool refused=feedback.contains("changed while saving")&&!frozen&&preset;
            const bool both=feedback.isEmpty()&&frozen&&preset&&order==1;
            qInfo("order %d: %s",order,superseded?"transaction superseded the preset":refused?"preset landed first; transaction refused":"both applied in order");
            QVERIFY2(superseded||refused||both,qPrintable(QString("order %1 feedback \"%2\" frozen %3 preset %4").arg(order).arg(feedback).arg(frozen).arg(preset)));
            cleanup();
        }
        init(); // the harness's own cleanup() runs after this function
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
    void canonicalFreezeIsSharedBySettingsArrangementAndReset() {
        Harness h;const auto display=h.manager.displays().first().id;QVERIFY(h.customization.detachPane("grid",display));QVERIFY(h.customization.setPaneFrozen("grid",true));QVERIFY(h.panes.pane("grid")["frozen"].toBool());
        QVERIFY(h.panes.freeze("grid",false));QVERIFY(!h.customization.paneRoutes()["grid"].toMap()["frozen"].toBool());QVERIFY(h.customization.redockPane("grid"));
        QVERIFY(h.customization.setPaneFrozen("grid",true));h.panes.reset();QVERIFY(!h.customization.paneRoutes()["grid"].toMap()["frozen"].toBool());QVERIFY(h.customization.detachPane("grid",display));
        QJsonObject root;QVERIFY(glancelot::readSettings(root).ok());QVERIFY(!root["monitorCustomization"].toObject()["slots"].toObject()["1"].toObject()["panes"].toObject()["grid"].toObject().contains("frozen"));
        QVERIFY(h.panes.freeze("grid",true));QVERIFY(h.customization.savePreset({},"Canonical workspace","workspace"));QVERIFY(h.customization.presets().first().toMap()["snapshot"].toMap()["dock"].toMap()["panes"].toMap()["grid"].toMap()["frozen"].toBool());
    }
    void workspacePresetAlsoHonorsControllerOnlyPaneFreeze() {
        Harness h;QVERIFY(h.customization.savePreset({},"Workspace","workspace"));const auto id=h.customization.presets().first().toMap()["id"].toString();QVERIFY(h.panes.freeze("topbar",true));QVERIFY(!h.customization.applyPreset(id));
        QVERIFY(h.customization.savePreset({},"Presentation","presentation"));const auto presentation=h.customization.presets().last().toMap()["id"].toString();QVERIFY(h.customization.applyPreset(presentation));QVERIFY(h.panes.pane("topbar")["frozen"].toBool());
    }
    void layoutUnlockAndFreezePreserveUnsupportedMonitorData() {
        Harness h;h.customization.setLayoutLocked(true);QVERIFY(h.customization.layoutLocked());
        QVERIFY(glancelot::updateSettings([](QJsonObject& root){root["monitorCustomization"]=QJsonObject{{"version",2},{"future","retain"}};}).ok());h.customization.reloadPreferences();
        h.customization.setLayoutLocked(false);QTRY_VERIFY(!h.customization.layoutLocked());QVERIFY(h.customization.setPaneFrozen("tree",true));QVERIFY(h.customization.paneRoutes()["tree"].toMap()["frozen"].toBool());
        QJsonObject root;QVERIFY(glancelot::readSettings(root).ok());QCOMPARE(root["monitorCustomization"].toObject(),(QJsonObject{{"version",2},{"future","retain"}}));
    }
    void realCoordinatorMovesSwapsAndRecoversAcrossSyntheticScreens() {
        if(QGuiApplication::platformName()!="offscreen")QSKIP("Synthetic QPA topology belongs only to the isolated offscreen backend.");
        MonitorSyntheticScreens topology;Harness h;QCOMPARE(h.manager.displayCount(),1);auto* primary=QGuiApplication::primaryScreen();const auto primaryId=glancelot::DisplayManager::computeDisplayId(primary);
        auto* second=topology.add({1000,0,1600,1200},"strong-A",2);auto* screenA=second->screen();const auto idA=glancelot::DisplayManager::computeDisplayId(screenA);QCOMPARE(h.manager.displayCount(),2);
        QQuickWindow controller;controller.setScreen(primary);controller.setGeometry(primary->availableGeometry().adjusted(20,20,-20,-20));h.customization.setControllerWindow(&controller);
        QImage image(400,300,QImage::Format_RGBA8888);image.fill(Qt::red);h.manager.updateImage("/generated/a.png",image,1);h.manager.pinCurrentImage();QVERIFY(h.manager.openPresentation(idA,glancelot::PresentationMode::Pinned));
        QVERIFY(h.customization.setOutputOptions({{"fitMode","actual"},{"windowMode","windowed"}}));auto* output=qobject_cast<glancelot::PresentationWindow*>(h.manager.presentationWindow());QVERIFY(output);
        QTRY_COMPARE(output->visibility(),QWindow::Windowed);QVERIFY2(screenA->availableGeometry().contains(output->frameGeometry()),qPrintable(QString("frame%1,%2 %3x%4 outside%5,%6 %7x%8").arg(output->frameGeometry().x()).arg(output->frameGeometry().y()).arg(output->frameGeometry().width()).arg(output->frameGeometry().height()).arg(screenA->availableGeometry().x()).arg(screenA->availableGeometry().y()).arg(screenA->availableGeometry().width()).arg(screenA->availableGeometry().height())));
        QCOMPARE(output->primaryCanvas()->zoom(),qreal(1));output->primaryCanvas()->rotateClockwise();const auto view=output->viewportState();auto dock=h.panes.snapshot();dock["locked"]=true;QVERIFY(h.panes.restoreSnapshot(dock));
        QVERIFY(h.customization.swapControllerAndOutput());QCOMPARE(h.manager.activeDisplayId(),primaryId);QCOMPARE(controller.screen(),screenA);QCOMPARE(output->viewportState(),view);QVERIFY(h.panes.locked());QCOMPARE(h.manager.presentationMode(),glancelot::PresentationMode::Pinned);
        QVERIFY(h.customization.moveOutput(idA));QCOMPARE(h.manager.activeDisplayId(),idA);QCOMPARE(output->viewportState(),view);QCOMPARE(output->primaryCanvas()->image().cacheKey(),image.cacheKey());QVERIFY(screenA->availableGeometry().contains(output->frameGeometry()));
        output->setScreen(primary);QCOMPARE(h.manager.activeDisplayId(),primaryId);QVERIFY(h.customization.setOutputOptions({{"guides",true}}));QCOMPARE(output->screen(),primary);QCOMPARE(output->viewportState(),view);QVERIFY(h.customization.moveOutput(idA));
        h.customization.setLayoutLocked(false);QVERIFY(h.customization.detachPane("tree",idA));
        auto* third=topology.add({3000,0,1920,1080},"",1);const auto weakId=glancelot::DisplayManager::computeDisplayId(third->screen());QCOMPARE(h.manager.displayCount(),3);
        const auto dragged=third->screen()->availableGeometry().adjusted(20,20,-20,-20);QVariantMap geometry{{"x",dragged.x()},{"y",dragged.y()},{"width",dragged.width()},{"height",dragged.height()}};
        QVERIFY(h.customization.updatePaneGeometry("tree",geometry));QCOMPARE(h.customization.paneRoutes()["tree"].toMap()["displayId"].toString(),weakId);QCOMPARE(h.customization.paneRoutes()["tree"].toMap()["effectiveDisplayId"].toString(),weakId);
        topology.remove(third);QVERIFY(h.customization.paneRoutes()["tree"].toMap()["recovered"].toBool());third=topology.add({3000,0,1920,1080},"",1);QCOMPARE(glancelot::DisplayManager::computeDisplayId(third->screen()),weakId);QVERIFY(h.customization.paneRoutes()["tree"].toMap()["recovered"].toBool());
        QVERIFY(h.customization.detachPane("tree",idA));topology.remove(second);QVERIFY(!h.manager.isPresentationActive());QVERIFY(h.customization.paneRoutes()["tree"].toMap()["recovered"].toBool());
        second=topology.add({1000,0,1600,1200},"strong-A",2);QCOMPARE(glancelot::DisplayManager::computeDisplayId(second->screen()),idA);QVERIFY(!h.customization.paneRoutes()["tree"].toMap()["recovered"].toBool());QCOMPARE(h.customization.paneRoutes()["tree"].toMap()["effectiveDisplayId"].toString(),idA);QVERIFY(!h.manager.isPresentationActive());
        QVERIFY(h.customization.setPaneFrozen("tree",true));h.customization.setLayoutLocked(true);h.customization.recoverToController();QVERIFY(!h.customization.paneRoutes()["tree"].toMap()["detached"].toBool());QVERIFY(h.customization.paneRoutes()["tree"].toMap()["frozen"].toBool());QVERIFY(h.customization.layoutLocked());
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
        window->showMaximized();QTRY_COMPARE(window->visibility(),QWindow::Maximized);
        QVERIFY(h.customization.setOutputOptions({{"guides",false}}));QTRY_COMPARE(window->visibility(),QWindow::Maximized);
        window->showMinimized();QTRY_COMPARE(window->visibility(),QWindow::Minimized);
        QVERIFY(h.customization.setOutputOptions({{"imageInfo",false}}));QTRY_COMPARE(window->visibility(),QWindow::Minimized);window->showNormal();
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
    void windowedNativeFrameFitsNegativeOriginWorkArea() {
        const QRect work(-1920,-1200,1920,1080);const QMargins frame(8,31,8,8);
        for(bool initial:{false,true}) {
            const auto client=glancelot::monitor_topology::windowedClient(QRect(5000,5000,5000,4000),work,frame,initial);
            QVERIFY(work.contains(client.marginsAdded(frame)));QVERIFY(client.width()>=320);QVERIFY(client.height()>=240);
            if(initial)QCOMPARE(client.size(),QSize(1520,825));
        }
        const QRect reachable(-1800,-1100,800,600);
        QCOMPARE(glancelot::monitor_topology::windowedClient(reachable,work,frame,false),reachable);
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
