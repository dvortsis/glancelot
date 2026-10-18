#include "glancelot/pane_layout.hpp"
#include "glancelot/settings_file.hpp"
#include <QTemporaryDir>
#include "file_lock.hpp"
#include <QtTest>
#include <limits>
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
};
QTEST_GUILESS_MAIN(PaneLayoutTests)
#include "pane_layout_tests.moc"
