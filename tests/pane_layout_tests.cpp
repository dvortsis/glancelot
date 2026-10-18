#include "glancelot/pane_layout.hpp"
#include "glancelot/settings_file.hpp"
#include <QTemporaryDir>
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
    void invalidAndUnknownFields() {
        QTemporaryDir config;qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(glancelot::updateSettings([](QJsonObject& s) {
            s["unrelated"]=7;
            s["layouts"]=QJsonObject{{"1",QJsonObject{{"version",1},{"future",true},{"panes",QJsonObject{
                {"tree",QJsonObject{{"region","bad"},{"extent",-3},{"order",1.5},{"frozen",true}}},
                {"future-pane",QJsonObject{{"custom",42}}}}}}}};
        }).ok());
        PaneLayout model;model.setSlot(1);QCOMPARE(model.pane("tree")["extent"].toInt(),280);
        QCOMPARE(model.pane("tree")["region"].toString(),QString("left"));
        QVERIFY(!model.resize("tree",std::numeric_limits<double>::infinity()));
        QVERIFY(!model.move("missing","left",0));model.reset();
        QJsonObject saved;QVERIFY(glancelot::readSettings(saved).ok());QCOMPARE(saved["unrelated"].toInt(),7);
        const auto layout=saved["layouts"].toObject()["1"].toObject();
        QVERIFY(layout["future"].toBool());QCOMPARE(layout["panes"].toObject()["future-pane"].toObject()["custom"].toInt(),42);
        qunsetenv("GLANCELOT_CONFIG_DIR");
    }
};
QTEST_GUILESS_MAIN(PaneLayoutTests)
#include "pane_layout_tests.moc"
