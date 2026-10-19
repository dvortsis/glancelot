#include "glancelot/settings_file.hpp"
#include "glancelot/viewer_controller.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class UiSettingsTests : public QObject {
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

    void initialDefaults() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        ViewerController viewer;
        QCOMPARE(viewer.textScale(), 1.0);
        QCOMPARE(viewer.reducedMotion(), false);
    }

    void textScaleClampingAndPersistence() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        ViewerController viewer;
        QSignalSpy spy(&viewer, &ViewerController::stateChanged);

        // Standard step
        viewer.setTextScale(1.25);
        QCOMPARE(viewer.textScale(), 1.25);
        QVERIFY(spy.count() > 0);

        // Clamping lower bound
        viewer.setTextScale(0.4);
        QCOMPARE(viewer.textScale(), 0.75);

        // Clamping upper bound
        viewer.setTextScale(3.5);
        QCOMPARE(viewer.textScale(), 2.0);

        // Persistence in settings.json
        QJsonObject root;
        QCOMPARE(glancelot::readSettings(root).code, glancelot::SettingsStatus::Ok);
        QCOMPARE(root.value("textScale").toDouble(), 2.0);

        // Re-read in a second controller
        ViewerController viewer2;
        QCOMPARE(viewer2.textScale(), 2.0);
    }

    void reducedMotionTogglingAndPersistence() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        ViewerController viewer;
        QSignalSpy spy(&viewer, &ViewerController::stateChanged);

        viewer.setReducedMotion(true);
        QCOMPARE(viewer.reducedMotion(), true);
        QVERIFY(spy.count() > 0);

        QJsonObject root;
        QCOMPARE(glancelot::readSettings(root).code, glancelot::SettingsStatus::Ok);
        QCOMPARE(root.value("reducedMotion").toBool(), true);

        // Re-read in a second controller
        ViewerController viewer2;
        QCOMPARE(viewer2.reducedMotion(), true);

        // Toggle off
        viewer2.setReducedMotion(false);
        QCOMPARE(viewer2.reducedMotion(), false);
    }

    void exportSettingsValidation() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        ViewerController viewer;
        viewer.setTextScale(1.25);
        viewer.setReducedMotion(true);

        const QString exportPath = tempDir.filePath("exported_settings.json");

        // Valid export
        QVERIFY(viewer.exportSettings(exportPath));
        QVERIFY(QFile::exists(exportPath));

        // Read exported file and verify contents
        QFile file(exportPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonParseError parseErr;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
        QCOMPARE(parseErr.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());
        const auto obj = doc.object();
        QCOMPARE(obj.value("schema").toInt(), 1);
        QCOMPARE(obj.value("textScale").toDouble(), 1.25);
        QCOMPARE(obj.value("reducedMotion").toBool(), true);

        // Invalid empty path
        QVERIFY(!viewer.exportSettings(""));
        QVERIFY(!viewer.status().isEmpty());

        // Invalid nonexistent nested directory path
        QVERIFY(!viewer.exportSettings(tempDir.filePath("nonexistent_subfolder/settings.json")));
    }

    void importSettingsValidationAndMerging() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        ViewerController viewer;
        viewer.setTextScale(1.0);
        viewer.setWheelMode("navigate");

        // Write an unknown key to settings to ensure it is preserved
        glancelot::updateSettings([](QJsonObject& root) {
            root["customPluginOption"] = QStringLiteral("preserved_value");
        });

        // Create an import file with changed preferences
        const QString importPath = tempDir.filePath("import_payload.json");
        QJsonObject importData;
        importData["schema"] = 1;
        importData["wheel"] = QStringLiteral("pan");
        importData["boundary"] = QStringLiteral("loop");
        importData["textScale"] = 1.5;
        importData["reducedMotion"] = true;
        importData["importedSpecialKey"] = QStringLiteral("new_value");

        QFile file(importPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(importData).toJson());
        file.close();

        // Perform import
        QVERIFY(viewer.importSettings(importPath));
        QCOMPARE(viewer.wheelMode(), QStringLiteral("pan"));
        QCOMPARE(viewer.boundaryMode(), QStringLiteral("loop"));
        QCOMPARE(viewer.textScale(), 1.5);
        QCOMPARE(viewer.reducedMotion(), true);

        // Verify preservation of existing unknown key and presence of new unknown key
        QJsonObject merged;
        glancelot::readSettings(merged);
        QCOMPARE(merged.value("customPluginOption").toString(), QStringLiteral("preserved_value"));
        QCOMPARE(merged.value("importedSpecialKey").toString(), QStringLiteral("new_value"));

        // Reject invalid JSON
        const QString badJsonPath = tempDir.filePath("bad.json");
        QFile badFile(badJsonPath);
        QVERIFY(badFile.open(QIODevice::WriteOnly));
        badFile.write("{ this is not valid json : [");
        badFile.close();
        QVERIFY(!viewer.importSettings(badJsonPath));

        // Reject future schema
        const QString futurePath = tempDir.filePath("future.json");
        QFile futureFile(futurePath);
        QVERIFY(futureFile.open(QIODevice::WriteOnly));
        futureFile.write("{\"schema\": 999}");
        futureFile.close();
        QVERIFY(!viewer.importSettings(futurePath));
    }

    void sectionReset() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        ViewerController viewer;
        viewer.setTextScale(1.5);
        viewer.setReducedMotion(true);
        viewer.setWheelMode("zoom");

        // Reset appearance section only
        QVERIFY(viewer.resetSection("appearance"));
        QCOMPARE(viewer.textScale(), 1.0);
        QCOMPARE(viewer.reducedMotion(), false);
        // Viewing setting remains unaffected
        QCOMPARE(viewer.wheelMode(), QStringLiteral("zoom"));

        // Reset viewing section
        QVERIFY(viewer.resetSection("viewing"));
        QCOMPARE(viewer.wheelMode(), QStringLiteral("navigate"));
    }

    void resetAllSettings() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        ViewerController viewer;
        viewer.setTextScale(1.5);
        viewer.setReducedMotion(true);
        viewer.setWheelMode("pan");
        viewer.setBoundaryMode("loop");

        QVERIFY(viewer.resetAllSettings());
        QCOMPARE(viewer.textScale(), 1.0);
        QCOMPARE(viewer.reducedMotion(), false);
        QCOMPARE(viewer.wheelMode(), QStringLiteral("navigate"));
        QCOMPARE(viewer.boundaryMode(), QStringLiteral("continue"));

        QJsonObject root;
        glancelot::readSettings(root);
        QCOMPARE(root.value("schema").toInt(), 1);
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    UiSettingsTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ui_settings_tests.moc"
