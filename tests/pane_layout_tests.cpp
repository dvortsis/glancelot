#include "glancelot/pane_layout.hpp"
#include "glancelot/settings_file.hpp"
#include <QTemporaryDir>
#include "file_lock.hpp"
#include <QtTest>
#include <limits>
#include <QJsonArray>
class PaneLayoutTests:public QObject {
    Q_OBJECT
private slots:
    void persistenceAndConcurrentFieldMerges() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        PaneLayout first,stale;first.setSlot(3);stale.setSlot(3);
        QVERIFY(first.move("tree","right",2));QVERIFY(stale.resize("tree",352));
        QVERIFY(first.freeze("tree",true));
        PaneLayout restored;restored.setSlot(3);
        QCOMPARE(restored.pane("tree")["region"].toString(),QString("right"));
        QCOMPARE(restored.pane("tree")["extent"].toInt(),352);
        QVERIFY(restored.pane("tree")["frozen"].toBool());
        QVERIFY(!restored.move("tree","left",0));QVERIFY(!restored.resize("tree",400));
        QVERIFY(!restored.setVisible("tree",false));QVERIFY(restored.freeze("tree",false));
        restored.setLocked(true);QVERIFY(!restored.freeze("tree",true));
        restored.setLocked(false);QVERIFY(restored.move("tree","left",0));
        PaneLayout session;QVERIFY(session.resize("tree",512));
        PaneLayout sameSlot;sameSlot.setSlot(3);QCOMPARE(sameSlot.pane("tree")["extent"].toInt(),352);
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    // F049: built-in and user presets apply as one step, are shared by every window, never carry lock or freeze, and are refused
    // while the layout is locked or a pane is frozen.
    void layoutPresets() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        PaneLayout a;a.setSlot(1);
        QCOMPARE(a.presetNames().mid(0,3),(QStringList{"Default","Review","Browse"}));
        QVERIFY(a.isBuiltInPreset("review")); // case-insensitive
        QVERIFY(a.applyPreset("Browse"));QCOMPARE(a.contentMode(),QString("combined"));
        QVERIFY(a.applyPreset("Review"));
        QVERIFY(!a.pane("tree")["visible"].toBool());QVERIFY(!a.pane("topbar")["visible"].toBool());
        QCOMPARE(a.pane("filmstrip")["region"].toString(),QString("bottom"));
        QCOMPARE(a.contentMode(),QString("preview"));
        { PaneLayout again;again.setSlot(1);QVERIFY(!again.pane("tree")["visible"].toBool()); } // applied presets persist per window
        QVERIFY(a.applyPreset("Default"));QVERIFY(a.pane("tree")["visible"].toBool());
        // User presets: save what is on screen, change it, bring it back.
        QVERIFY(a.move("tree","right",0));QVERIFY(a.resize("tree",333));QVERIFY(a.setCenterOrientation("vertical"));QVERIFY(a.setContentMode("combined"));
        QVERIFY(a.savePreset("Wide tree"));
        QVERIFY(a.presetNames().contains("Wide tree"));
        QVERIFY(a.applyPreset("Default"));QCOMPARE(a.pane("tree")["region"].toString(),QString("left"));
        QVERIFY(a.applyPreset("Wide tree"));
        QCOMPARE(a.pane("tree")["region"].toString(),QString("right"));QCOMPARE(a.pane("tree")["extent"].toInt(),333);
        QCOMPARE(a.centerSplit()["orientation"].toString(),QString("vertical"));QCOMPARE(a.contentMode(),QString("combined"));
        // Another window sees it; deleting works for user presets only.
        PaneLayout b;b.setSlot(2);QVERIFY(b.presetNames().contains("Wide tree"));
        QVERIFY(b.applyPreset("Wide tree"));QCOMPARE(b.pane("tree")["extent"].toInt(),333);
        QVERIFY(!b.deletePreset("Review"));QVERIFY(b.deletePreset("Wide tree"));
        QVERIFY(!a.presetNames().contains("Wide tree"));QVERIFY(!a.applyPreset("Wide tree"));
        // Names and limits.
        QVERIFY(!a.savePreset(""));QVERIFY(!a.savePreset(" padded "));QVERIFY(!a.savePreset(QString(41,'x')));QVERIFY(!a.savePreset("Review"));
        for(int i=0;i<12;++i)QVERIFY(a.savePreset(QString("p%1").arg(i)));
        QVERIFY(!a.savePreset("one too many"));QVERIFY(a.savePreset("p3")); // replacing an existing name is not "one more"
        // A preset is never applied over a lock or a freeze, and it never stores one.
        QVERIFY(a.freeze("grid",true));QVERIFY(a.savePreset("p0"));QVERIFY(a.freeze("grid",false));
        a.setLocked(true);QVERIFY(!a.applyPreset("Default"));QVERIFY(a.feedback().contains("Unlock"));a.setLocked(false);
        QVERIFY(a.freeze("tree",true));QVERIFY(!a.applyPreset("Default"));QVERIFY(a.feedback().contains("Unfreeze"));QVERIFY(a.freeze("tree",false));
        QVERIFY(a.applyPreset("p0"));QVERIFY(!a.pane("grid")["frozen"].toBool());
        QVERIFY(!a.applyPreset("no such preset"));
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    void centerContractAndWriteFailure() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        PaneLayout first,stale;first.setSlot(1);stale.setSlot(1);
        QVERIFY(first.setCenterOrientation("vertical"));QVERIFY(stale.setCenterRatio(0.7));
        PaneLayout restored;restored.setSlot(1);
        QCOMPARE(restored.centerSplit()["orientation"].toString(),QString("vertical"));
        QCOMPARE(restored.centerSplit()["ratio"].toDouble(),0.7);
        QVERIFY(!restored.setCenterRatio(std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(!restored.setCenterRatio(0.99));QVERIFY(!restored.setCenterOrientation("other"));
        QVERIFY(restored.freeze("preview",true));QVERIFY(!restored.setCenterRatio(0.6));
        restored.reset();QCOMPARE(restored.centerSplit()["ratio"].toDouble(),0.5);
        FileLock lock(glancelot::settingsFilePath()+".lock");QVERIFY(lock.acquire(0));
        QVERIFY(restored.resize("tree",400));QVERIFY(!restored.feedback().isEmpty());
        QCOMPARE(restored.pane("tree")["extent"].toInt(),400);lock.release();
        PaneLayout disk;disk.setSlot(1);QCOMPARE(disk.pane("tree")["extent"].toInt(),280);
        QVERIFY(disk.setVisible("tree",false));QVERIFY(disk.freeze("tree",true));QVERIFY(disk.pane("tree")["visible"].toBool());
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    void newerVersionsCorruptionAndNoOps() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        PaneLayout session;QSignalSpy changes(&session,&PaneLayout::changed);
        QVERIFY(session.resize("tree",280));QCOMPARE(changes.count(),0);
        QVERIFY(glancelot::updateSettings([](QJsonObject& s){s["layouts"]=QJsonObject{{"2","broken"},{"4",QJsonObject{{"version",9},{"marker",123}}}};}).ok());
        PaneLayout malformed;malformed.setSlot(2);QVERIFY(!malformed.feedback().isEmpty());
        PaneLayout future;future.setSlot(4);QVERIFY(!future.feedback().isEmpty());
        QVERIFY(future.resize("tree",400));QVERIFY(!future.feedback().isEmpty());
        QJsonObject saved;QVERIFY(glancelot::readSettings(saved).ok());
        QCOMPARE(saved["layouts"].toObject()["4"].toObject()["version"].toInt(),9);
        QCOMPARE(saved["layouts"].toObject()["4"].toObject()["marker"].toInt(),123);
        PaneLayout other;other.setSlot(5);QVERIFY(other.resize("tree",420));
        PaneLayout untouched;untouched.setSlot(4);QCOMPARE(untouched.pane("tree")["extent"].toInt(),280);
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    void corruptSettingsFileFallsBackWithFeedback() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QDir().mkpath(QFileInfo(glancelot::settingsFilePath()).absolutePath());
        QFile file(glancelot::settingsFilePath());QVERIFY(file.open(QIODevice::WriteOnly));file.write("{not json");file.close();
        PaneLayout model;model.setSlot(2);QVERIFY(!model.feedback().isEmpty());
        QCOMPARE(model.pane("tree")["region"].toString(),QString("left"));QVERIFY(!model.locked());
        QVERIFY(model.move("tree","right",0)); // the session stays usable
        QCOMPARE(model.pane("tree")["region"].toString(),QString("right"));QVERIFY(!model.feedback().isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("{not json")); // never overwritten
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    void malformedVersionIsReplaceableAndOrdersRenumber() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(glancelot::updateSettings([](QJsonObject& s){s["layouts"]=QJsonObject{{"6",QJsonObject{{"version","1"}}}};}).ok());
        PaneLayout model;model.setSlot(6);QVERIFY(!model.feedback().isEmpty());
        QVERIFY(model.resize("tree",333));QVERIFY(model.feedback().isEmpty());
        PaneLayout restarted;restarted.setSlot(6);QCOMPARE(restarted.pane("tree")["extent"].toInt(),333);
        // Inserting before an occupied position renumbers instead of tying.
        QVERIFY(restarted.move("topbar","left",0));
        QCOMPARE(restarted.pane("topbar")["order"].toInt(),0);QCOMPARE(restarted.pane("tree")["order"].toInt(),1);
        QVERIFY(restarted.move("tree","left",0)); // "earlier" now swaps with the neighbour
        QCOMPARE(restarted.pane("tree")["order"].toInt(),0);QCOMPARE(restarted.pane("topbar")["order"].toInt(),1);
        PaneLayout disk;disk.setSlot(6);
        QCOMPARE(disk.pane("tree")["order"].toInt(),0);QCOMPARE(disk.pane("topbar")["order"].toInt(),1);
        QCOMPARE(disk.pane("topbar")["region"].toString(),QString("left"));
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    // GRID-PREVIEW-01: content mode is a per-slot view preference stored in the center object.
    void contentModeRoundTripsPerSlot() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        PaneLayout one,two;one.setSlot(1);two.setSlot(2);
        QCOMPARE(one.contentMode(),QString("preview"));
        QSignalSpy changes(&one,&PaneLayout::changed);
        QVERIFY(one.setContentMode("combined"));QCOMPARE(changes.count(),1);
        QVERIFY(one.setContentMode("combined"));QCOMPARE(changes.count(),1); // no-op when equal
        QVERIFY(!one.setContentMode("both"));QVERIFY(!one.setContentMode(""));QCOMPARE(one.contentMode(),QString("combined"));
        QVERIFY(two.setContentMode("grid"));
        PaneLayout restoredOne,restoredTwo,fresh;restoredOne.setSlot(1);restoredTwo.setSlot(2);fresh.setSlot(3);
        QCOMPARE(restoredOne.contentMode(),QString("combined"));QCOMPARE(restoredTwo.contentMode(),QString("grid"));
        QCOMPARE(fresh.contentMode(),QString("preview"));
        // Slot 0 keeps the mode for the session only and never writes it.
        PaneLayout session;QVERIFY(session.setContentMode("grid"));QCOMPARE(session.contentMode(),QString("grid"));
        QJsonObject saved;QVERIFY(glancelot::readSettings(saved).ok());
        QVERIFY(!saved["layouts"].toObject().contains("0"));
        QCOMPARE(saved["layouts"].toObject()["1"].toObject()["center"].toObject()["mode"].toString(),QString("combined"));
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    void unknownContentModeFallsBackIndividually() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(glancelot::updateSettings([](QJsonObject& s) {
            s["layouts"]=QJsonObject{{"1",QJsonObject{{"version",1},{"center",QJsonObject{{"mode","sideways"},{"orientation","vertical"},{"ratio",0.3}}}}},
                                     {"2",QJsonObject{{"version",1},{"center",QJsonObject{{"mode",7},{"ratio",0.65}}}}}};
        }).ok());
        PaneLayout model;model.setSlot(1);
        QCOMPARE(model.contentMode(),QString("preview"));
        QCOMPARE(model.centerSplit()["orientation"].toString(),QString("vertical"));
        QCOMPARE(model.centerSplit()["ratio"].toDouble(),0.3);
        QVERIFY(model.feedback().isEmpty());
        PaneLayout numeric;numeric.setSlot(2);
        QCOMPARE(numeric.contentMode(),QString("preview"));QCOMPARE(numeric.centerSplit()["ratio"].toDouble(),0.65);
        // Choosing a valid mode replaces only that field.
        QVERIFY(model.setContentMode("combined"));
        QJsonObject saved;QVERIFY(glancelot::readSettings(saved).ok());
        const auto center=saved["layouts"].toObject()["1"].toObject()["center"].toObject();
        QCOMPARE(center["mode"].toString(),QString("combined"));QCOMPARE(center["orientation"].toString(),QString("vertical"));
        QCOMPARE(center["ratio"].toDouble(),0.3);
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    void contentModeAllowedUnderLockAndFreeze() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        PaneLayout model;model.setSlot(1);
        model.setLocked(true);
        QVERIFY(model.setContentMode("combined"));QCOMPARE(model.contentMode(),QString("combined"));
        QVERIFY(!model.setCenterRatio(0.7));QVERIFY(!model.setCenterOrientation("vertical"));
        QCOMPARE(model.centerSplit()["ratio"].toDouble(),0.5);
        model.setLocked(false);QVERIFY(model.freeze("grid",true));
        QVERIFY(model.setContentMode("grid"));QVERIFY(!model.setCenterRatio(0.7));
        PaneLayout restored;restored.setSlot(1);QCOMPARE(restored.contentMode(),QString("grid"));
        QCOMPARE(restored.centerSplit()["ratio"].toDouble(),0.5);
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    void resetRestoresPreviewAndMergesKeepOtherWindowsMode() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        PaneLayout first,stale;first.setSlot(4);stale.setSlot(4); // two windows that loaded the same slot
        QVERIFY(first.setContentMode("combined"));
        QVERIFY(stale.setCenterRatio(0.35));QVERIFY(stale.resize("tree",320)); // from an old snapshot
        PaneLayout disk;disk.setSlot(4);
        QCOMPARE(disk.contentMode(),QString("combined"));QCOMPARE(disk.centerSplit()["ratio"].toDouble(),0.35);
        QCOMPARE(disk.pane("tree")["extent"].toInt(),320);
        PaneLayout other;other.setSlot(5);QVERIFY(other.setContentMode("grid"));
        first.reset();QCOMPARE(first.contentMode(),QString("preview"));
        PaneLayout afterReset,otherSlot;afterReset.setSlot(4);otherSlot.setSlot(5);
        QCOMPARE(afterReset.contentMode(),QString("preview"));QCOMPARE(afterReset.centerSplit()["ratio"].toDouble(),0.5);
        QCOMPARE(otherSlot.contentMode(),QString("grid")); // reset touches only this slot
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    void invalidAndUnknownFields() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(glancelot::updateSettings([](QJsonObject& s) {
            s["unrelated"]=7;
            s["layouts"]=QJsonObject{{"1",QJsonObject{{"version",1},{"future",true},{"panes",QJsonObject{
                {"tree",QJsonObject{{"region","bad"},{"extent",-3},{"order",1.5},{"frozen",true},{"futureField",17}}},
                {"future-pane",QJsonObject{{"custom",42}}}}}}}};
        }).ok());
        PaneLayout model;model.setSlot(1);QCOMPARE(model.pane("tree")["extent"].toInt(),280);
        QCOMPARE(model.pane("tree")["region"].toString(),QString("left"));
        QVERIFY(!model.resize("tree",std::numeric_limits<double>::infinity()));
        QVERIFY(!model.move("missing","left",0));model.reset();
        QJsonObject saved;QVERIFY(glancelot::readSettings(saved).ok());QCOMPARE(saved["unrelated"].toInt(),7);
        const auto layout=saved["layouts"].toObject()["1"].toObject();
        QCOMPARE(layout["panes"].toObject()["tree"].toObject()["futureField"].toInt(),17);
        QVERIFY(layout["future"].toBool());QCOMPARE(layout["panes"].toObject()["future-pane"].toObject()["custom"].toInt(),42);
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    // STACK-F049-PRESETS: applying a preset merges known placement into layouts[slot]; unknown root, pane and center fields,
    // snapshotExtras_ and newer data survive, and the preset never writes the lock or a freeze.
    void presetWritePreservesUnknownAndNewerData() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(glancelot::updateSettings([](QJsonObject& s) {
            s["unrelated"]=7;
            s["layouts"]=QJsonObject{
                {"1",QJsonObject{{"version",1},{"future",true},{"panes",QJsonObject{
                    {"tree",QJsonObject{{"region","left"},{"futureField",17}}},{"future-pane",QJsonObject{{"custom",42}}}}},
                    {"center",QJsonObject{{"orientation","vertical"},{"futureCenter","kept"}}}}},
                {"4",QJsonObject{{"version",9},{"marker",123}}},
                {"6",QJsonObject{{"version","1"},{"junk",1}}}};
        }).ok());
        PaneLayout model;model.setSlot(1);QVERIFY(model.applyPreset("Review"));QVERIFY(model.feedback().isEmpty());
        QJsonObject saved;QVERIFY(glancelot::readSettings(saved).ok());QCOMPARE(saved["unrelated"].toInt(),7);
        auto layout=saved["layouts"].toObject()["1"].toObject();
        QVERIFY(layout["future"].toBool());QVERIFY(!layout.contains("locked"));
        QCOMPARE(layout["panes"].toObject()["tree"].toObject()["futureField"].toInt(),17);
        QCOMPARE(layout["panes"].toObject()["tree"].toObject()["visible"].toBool(),false);
        QCOMPARE(layout["panes"].toObject()["future-pane"].toObject()["custom"].toInt(),42);
        QCOMPARE(layout["center"].toObject()["futureCenter"].toString(),QString("kept"));
        QCOMPARE(layout["center"].toObject()["orientation"].toString(),QString("horizontal"));
        for(const auto& pane:layout["panes"].toObject())QVERIFY(!pane.toObject().contains("frozen"));
        // A newer layout version is never rewritten; the preset applies for this session with feedback.
        PaneLayout future;future.setSlot(4);QVERIFY(future.applyPreset("Review"));QVERIFY(future.feedback().contains("newer"));
        QVERIFY(!future.pane("tree")["visible"].toBool());
        QVERIFY(glancelot::readSettings(saved).ok());
        QCOMPARE(saved["layouts"].toObject()["4"].toObject(),(QJsonObject{{"version",9},{"marker",123}}));
        // A malformed layout is replaceable, as for every other layout command.
        PaneLayout malformed;malformed.setSlot(6);QVERIFY(malformed.applyPreset("Browse"));QVERIFY(malformed.feedback().isEmpty());
        PaneLayout reread;reread.setSlot(6);QCOMPARE(reread.contentMode(),QString("combined"));QVERIFY(reread.feedback().isEmpty());
        // snapshotExtras_ (unknown root fields restored by the monitor coordinator) survive when no layout is stored yet.
        PaneLayout restored;restored.setSlot(8);auto snapshot=restored.snapshot();snapshot["coordinatorExtra"]=QVariantMap{{"a",1}};
        QVERIFY(restored.restoreSnapshot(snapshot));QVERIFY(restored.applyPreset("Browse"));
        QVERIFY(glancelot::readSettings(saved).ok());
        QCOMPARE(saved["layouts"].toObject()["8"].toObject()["coordinatorExtra"].toObject()["a"].toInt(),1);
        QCOMPARE(restored.snapshot()["coordinatorExtra"].toMap()["a"].toInt(),1);
        // Unknown runtime pane fields from a restored snapshot survive an apply too.
        auto panes=snapshot["panes"].toMap();auto tree=panes["tree"].toMap();tree["futureRuntime"]=5;panes["tree"]=tree;snapshot["panes"]=panes;
        QVERIFY(restored.restoreSnapshot(snapshot));QVERIFY(restored.applyPreset("Review"));
        QCOMPARE(restored.pane("tree")["futureRuntime"].toInt(),5);QVERIFY(!restored.pane("tree")["visible"].toBool());
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    // A busy settings file queues the preset write on the layout's own retry channel; it lands after earlier edits, keeps a
    // lock another window set meanwhile, and never touches another slot.
    void presetWriteRetriesAndKeepsOtherSlotsAndLock() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        PaneLayout one,two;one.setSlot(1);two.setSlot(2);QVERIFY(two.resize("tree",333));QVERIFY(two.freeze("grid",true));
        QJsonObject before;QVERIFY(glancelot::readSettings(before).ok());const QJsonValue otherSlot=before.value("layouts").toObject().value("2");
        FileLock lock(glancelot::settingsFilePath()+".lock");QVERIFY(lock.acquire(0));
        QVERIFY(one.resize("tree",300));QVERIFY(one.applyPreset("Review"));
        QVERIFY(!one.feedback().isEmpty()); // "Saving layout preferences…"
        QVERIFY(!one.pane("tree")["visible"].toBool());
        lock.release();
        PaneLayout window;window.setSlot(1);window.setLocked(true); // another window of slot 1 locks it while the preset waits
        QTRY_VERIFY_WITH_TIMEOUT(one.feedback().isEmpty(),5000);
        QJsonObject saved;QVERIFY(glancelot::readSettings(saved).ok());
        const auto layout=saved["layouts"].toObject()["1"].toObject();
        QVERIFY(layout["locked"].toBool());
        QVERIFY(!layout["panes"].toObject()["tree"].toObject()["visible"].toBool());
        QCOMPARE(layout["panes"].toObject()["tree"].toObject()["extent"].toInt(),280); // the preset's, after the earlier resize
        QCOMPARE(saved["layouts"].toObject()["2"],otherSlot);
        PaneLayout reread;reread.setSlot(2);QCOMPARE(reread.pane("tree")["extent"].toInt(),333);QVERIFY(reread.pane("grid")["frozen"].toBool());
        // A pending write that completes after the window changed slot does not report into the new slot.
        QVERIFY(lock.acquire(0));PaneLayout moving;moving.setSlot(3);QVERIFY(moving.applyPreset("Browse"));
        moving.setSlot(5);lock.release();QTest::qWait(300);QVERIFY(moving.feedback().isEmpty());QCOMPARE(moving.contentMode(),QString("preview"));
        PaneLayout three;three.setSlot(3);QCOMPARE(three.contentMode(),QString("combined"));
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    // Lock and freeze are refused before anything changes or is written; a saved preset never carries either.
    void presetsObeyLockAndFreezeAuthority() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        PaneLayout model;model.setSlot(1);QVERIFY(model.freeze("tree",true));model.setLocked(true);
        QVERIFY(model.savePreset("Captured"));
        QJsonObject saved;QVERIFY(glancelot::readSettings(saved).ok());
        const auto entry=saved["layoutPresets"].toObject()["Captured"].toObject();
        QVERIFY(!entry.contains("locked"));
        for(const auto& pane:entry["panes"].toObject())QVERIFY(!pane.toObject().contains("frozen"));
        const auto layoutBefore=saved["layouts"];
        QVERIFY(!model.applyPreset("Review"));QVERIFY(model.feedback().contains("Unlock"));QVERIFY(model.pane("topbar")["visible"].toBool());
        model.setLocked(false);
        QVERIFY(!model.applyPreset("Review"));QVERIFY(model.feedback().contains("Unfreeze"));QVERIFY(model.pane("topbar")["visible"].toBool());
        QVERIFY(model.freeze("tree",false));QVERIFY(model.applyPreset("Captured"));
        QVERIFY(!model.locked());QVERIFY(!model.pane("tree")["frozen"].toBool());
        PaneLayout other;other.setSlot(2);QVERIFY(other.applyPreset("Captured"));
        QVERIFY(!other.locked());QVERIFY(!other.pane("tree")["frozen"].toBool());
        Q_UNUSED(layoutBefore);
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
    // Newer or unreadable preset data is preserved; names never collide with display workspace presets.
    void presetStoreMalformedNewerAndNameScopes() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        const QJsonObject future{{"version",2},{"panes",QJsonObject{}},{"hologram",true}};
        QVERIFY(glancelot::updateSettings([&](QJsonObject& s) {
            s["layoutPresets"]=QJsonObject{{"Future",future},{"Old",QJsonObject{{"version",1},{"note","kept"},{"panes",QJsonObject{{"tree",QJsonObject{{"region","right"}}}}}}}};
            s["monitorCustomization"]=QJsonObject{{"version",1},{"slots",QJsonObject{{"3",QJsonObject{{"presets",QJsonArray{QJsonObject{{"id","a"},{"name","Studio"},{"kind","presentation"}}}}}}}}};
        }).ok());
        PaneLayout model;model.setSlot(1);QVERIFY(model.presetNames().contains("Future"));
        QVERIFY(!model.applyPreset("Future"));QVERIFY(model.feedback().contains("newer"));
        QVERIFY(!model.savePreset("Future"));QVERIFY(model.feedback().contains("newer"));
        QVERIFY(!model.savePreset("studio"));QVERIFY(model.feedback().contains("display workspace"));
        QVERIFY(model.applyPreset("Old"));QCOMPARE(model.pane("tree")["region"].toString(),QString("right"));
        QVERIFY(model.savePreset("Old")); // replacing a current record keeps its unknown fields
        QJsonObject saved;QVERIFY(glancelot::readSettings(saved).ok());
        QCOMPARE(saved["layoutPresets"].toObject()["Future"].toObject(),future);
        QCOMPARE(saved["layoutPresets"].toObject()["Old"].toObject()["note"].toString(),QString("kept"));
        QVERIFY(glancelot::updateSettings([](QJsonObject& s){s["layoutPresets"]=QJsonArray{1,2};}).ok());
        QVERIFY(!model.savePreset("Anything"));QVERIFY(model.feedback().contains("preserved"));QVERIFY(!model.deletePreset("Anything"));
        QVERIFY(glancelot::readSettings(saved).ok());QCOMPARE(saved["layoutPresets"],QJsonValue(QJsonArray{1,2}));
        QCOMPARE(model.presetNames(),(QStringList{"Default","Review","Browse"}));
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
};
QTEST_GUILESS_MAIN(PaneLayoutTests)
#include "pane_layout_tests.moc"
