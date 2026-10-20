#include "glancelot/command_registry.hpp"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

namespace {
// The controller captures the registry during static initialization. This call
// deliberately originates in another translation unit before main/QCoreApp.
const auto earlyRegistrySize=glancelot::commandRegistry().size();
}

// This executable remains QCoreApplication-only: maintenance validation must
// not construct a viewer, GUI application, window claim or preference writer.
class RecoveryStartupTests final : public QObject {
    Q_OBJECT
    QTemporaryDir config;
private slots:
    void initTestCase() {
        QVERIFY(config.isValid());
        qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QVERIFY(!qobject_cast<QGuiApplication*>(QCoreApplication::instance()));
    }
    void registryDefaultsNeedNoViewerOrPreferences() {
        QCOMPARE(earlyRegistrySize,glancelot::commandRegistry().size());
        QSet<QString> ids;
        for(const auto& command:glancelot::commandRegistry()) {
            const auto id=QString::fromStdString(command.id);
            QVERIFY(!id.isEmpty());QVERIFY(!command.label.empty());
            QVERIFY2(!ids.contains(id),qPrintable(id));ids.insert(id);
        }
        QVERIFY(!ids.isEmpty());
        const auto resolved=glancelot::resolveCommandBindings({});
        QVERIFY2(resolved.valid,qPrintable(resolved.error));
        QCOMPARE(resolved.effective.size(),ids.size());
        QCOMPARE(resolved.effective.value("refresh").toString(),QString("F5"));
    }
    void restoredUserChoiceWinsOverNewCommandDefault() {
        const auto resolved=glancelot::resolveCommandBindings({{"next","Ctrl+Shift+P"}});
        QVERIFY2(resolved.valid,qPrintable(resolved.error));
        QCOMPARE(resolved.effective.value("next").toString(),QString("Ctrl+Shift+P"));
        QVERIFY(resolved.effective.value("palette").toString().isEmpty());
    }
    void reservedEscapeAndPrefixConflictsRefuse() {
        const auto escape=glancelot::resolveCommandBindings({{"next","Escape"}});
        QVERIFY(!escape.valid);QVERIFY(!escape.error.isEmpty());
        const auto prefix=glancelot::resolveCommandBindings({{"next","Ctrl+K"},{"previous","Ctrl+K, Ctrl+C"}});
        QVERIFY(!prefix.valid);QVERIFY(!prefix.error.isEmpty());
    }
    void unknownFutureBindingNeverBecomesActive() {
        const QJsonObject stored{{"future-command",QJsonObject{{"version",2},{"keys",QJsonArray{"Escape"}}}}};
        const auto resolved=glancelot::resolveCommandBindings(stored);
        QVERIFY2(resolved.valid,qPrintable(resolved.error));
        QVERIFY(!resolved.effective.contains("future-command"));
        QCOMPARE(resolved.effective.value("next").toString(),QString("Right"));
        QVERIFY(stored.value("future-command").isObject());
    }
    void ordinaryLoaderKeepsLegacyWrongTypeFallback() {
        // Recovery must separately reject selected invalid source types. The
        // shared ordinary loader still ignores wrong types exactly as before.
        const auto resolved=glancelot::resolveCommandBindings({{"next",true}});
        QVERIFY2(resolved.valid,qPrintable(resolved.error));
        QCOMPARE(resolved.effective.value("next").toString(),QString("Right"));
    }
    void cleanupTestCase() {
        for(const auto* name:{"settings.json","settings.json.bak","settings.json.damaged","sessions.json"})
            QVERIFY(!QFile::exists(config.filePath(QLatin1String(name))));
        QVERIFY(!qobject_cast<QGuiApplication*>(QCoreApplication::instance()));
    }
};
QTEST_GUILESS_MAIN(RecoveryStartupTests)
#include "recovery_startup_tests.moc"
