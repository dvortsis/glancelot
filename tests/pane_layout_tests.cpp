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
