#include "glancelot/settings_file.hpp"
#include "glancelot/viewer_controller.hpp"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class ShortcutsHelpTests : public QObject {
    Q_OBJECT

private:
    QByteArray originalConfig;
    bool hadConfig{false};

private slots:
    void init() {
        originalConfig = qgetenv("GLANCELOT_CONFIG_DIR");
        hadConfig = qEnvironmentVariableIsSet("GLANCELOT_CONFIG_DIR");
    }

    void cleanup() {
        if (hadConfig) qputenv("GLANCELOT_CONFIG_DIR", originalConfig);
        else qunsetenv("GLANCELOT_CONFIG_DIR");
    }

    void commandIsRegisteredAndDefaultIsF1() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        ViewerController viewer;
        const auto commands = viewer.commands();
        bool found = false;
        for (const auto& item : commands) {
            const auto map = item.toMap();
            if (map.value("id").toString() == "shortcuts-help") {
                found = true;
                QCOMPARE(map.value("binding").toString(), QString("F1"));
                QCOMPARE(map.value("label").toString(), QString("Keyboard shortcuts cheat sheet"));
                break;
            }
        }
        QVERIFY(found);
    }

    void bindingLookupAndDynamicReflection() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        ViewerController viewer;
        QCOMPARE(viewer.bindingFor("shortcuts-help"), QString("F1"));

        QSignalSpy bindingsSpy(&viewer, &ViewerController::bindingsChanged);

        // Remap to an available key sequence
        QVERIFY(viewer.setBinding("shortcuts-help", "Ctrl+Shift+H"));
        QCOMPARE(viewer.bindingFor("shortcuts-help"), QString("Ctrl+Shift+H"));
        QCOMPARE(bindingsSpy.count(), 1);

        // Verify reloaded controller reads persisted binding
        {
            ViewerController reloaded;
            QCOMPARE(reloaded.bindingFor("shortcuts-help"), QString("Ctrl+Shift+H"));
        }

        // Reset to default
        viewer.resetBindings();
        QCOMPARE(viewer.bindingFor("shortcuts-help"), QString("F1"));
    }

    void categoryCoverageOfCoreCommands() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        ViewerController viewer;

        // Navigation
        QVERIFY(!viewer.bindingFor("next").isEmpty());
        QVERIFY(!viewer.bindingFor("previous").isEmpty());
        QVERIFY(!viewer.bindingFor("navigate-back").isEmpty());
        QVERIFY(!viewer.bindingFor("navigate-forward").isEmpty());

        // Viewing & Canvas
        QVERIFY(!viewer.bindingFor("fit").isEmpty());
        QVERIFY(!viewer.bindingFor("actual").isEmpty());
        QVERIFY(!viewer.bindingFor("zoom-in").isEmpty());
        QVERIFY(!viewer.bindingFor("zoom-out").isEmpty());
        QVERIFY(!viewer.bindingFor("rotate").isEmpty());
        QVERIFY(!viewer.bindingFor("background").isEmpty());
        QVERIFY(!viewer.bindingFor("fullscreen").isEmpty());

        // Panes
        QVERIFY(!viewer.bindingFor("grid").isEmpty());
        QVERIFY(!viewer.bindingFor("filmstrip").isEmpty());
        QVERIFY(!viewer.bindingFor("tree").isEmpty());

        // Selection & Files
        QVERIFY(!viewer.bindingFor("delete-selection").isEmpty());
        QVERIFY(!viewer.bindingFor("delete-current-file").isEmpty());
        QVERIFY(!viewer.bindingFor("delete-current-folder").isEmpty());
        QVERIFY(!viewer.bindingFor("copy-path").isEmpty());
        QVERIFY(!viewer.bindingFor("reveal-in-file-manager").isEmpty());
        QVERIFY(!viewer.bindingFor("open-terminal").isEmpty());
    }

    void shortcutConflictDetectionPreservesF1() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        ViewerController viewer;

        // Cannot assign F1 to another command without conflict
        QVERIFY(!viewer.setBinding("open", "F1"));
        QVERIFY(!viewer.status().isEmpty());

        // Original bindings stay intact
        QCOMPARE(viewer.bindingFor("shortcuts-help"), QString("F1"));
        QCOMPARE(viewer.bindingFor("open"), QString("Ctrl+O"));
    }
};

QTEST_MAIN(ShortcutsHelpTests)
#include "shortcuts_help_tests.moc"

