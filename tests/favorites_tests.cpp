#include "glancelot/favorites_model.hpp"
#include "glancelot/settings_file.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class FavoritesTests : public QObject {
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

    void initialEmptyDefaults() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        FavoritesModel model;
        QCOMPARE(model.count(), 0);
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.isFavorite("/some/path"));
    }

    void addAndDuplicatePrevention() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        FavoritesModel model;
        QSignalSpy countSpy(&model, &FavoritesModel::countChanged);

        const QString pathA = tempDir.filePath("Photos");
        QVERIFY(model.addFavorite(pathA, "My Holiday Photos"));
        QCOMPARE(model.count(), 1);
        QCOMPARE(countSpy.count(), 1);
        QVERIFY(model.isFavorite(pathA));
        QCOMPARE(model.data(model.index(0), FavoritesModel::NameRole).toString(), QStringLiteral("My Holiday Photos"));
        QCOMPARE(model.data(model.index(0), FavoritesModel::AliasRole).toString(), QStringLiteral("My Holiday Photos"));

        // Adding same path again does not create duplicate
        QVERIFY(model.addFavorite(pathA, "Updated Alias"));
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.data(model.index(0), FavoritesModel::NameRole).toString(), QStringLiteral("Updated Alias"));

        // Add second path without alias
        const QString pathB = tempDir.filePath("Work");
        QVERIFY(model.addFavorite(pathB));
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.data(model.index(1), FavoritesModel::NameRole).toString(), QStringLiteral("Work"));
        QCOMPARE(model.data(model.index(1), FavoritesModel::AliasRole).toString(), QString());
    }

    void removeByPathAndIndex() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        FavoritesModel model;
        const QString p1 = tempDir.filePath("One");
        const QString p2 = tempDir.filePath("Two");
        const QString p3 = tempDir.filePath("Three");

        model.addFavorite(p1);
        model.addFavorite(p2);
        model.addFavorite(p3);
        QCOMPARE(model.count(), 3);

        // Remove by path
        QVERIFY(model.removeFavorite(p2));
        QCOMPARE(model.count(), 2);
        QVERIFY(!model.isFavorite(p2));
        QVERIFY(model.isFavorite(p1));
        QVERIFY(model.isFavorite(p3));

        // Remove by index
        QVERIFY(model.removeAt(0));
        QCOMPARE(model.count(), 1);
        QVERIFY(!model.isFavorite(p1));
        QVERIFY(model.isFavorite(p3));

        // Invalid index
        QVERIFY(!model.removeAt(5));
        QVERIFY(!model.removeAt(-1));
    }

    void reorderMoveAndBounds() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        FavoritesModel model;
        const QString pA = tempDir.filePath("A");
        const QString pB = tempDir.filePath("B");
        const QString pC = tempDir.filePath("C");

        model.addFavorite(pA, "A");
        model.addFavorite(pB, "B");
        model.addFavorite(pC, "C");

        // Out of bounds checks
        QVERIFY(!model.moveUp(0));
        QVERIFY(!model.moveDown(2));
        QVERIFY(!model.move(-1, 1));
        QVERIFY(!model.move(0, 5));

        // Move B up: B becomes index 0
        QVERIFY(model.moveUp(1));
        QCOMPARE(model.data(model.index(0), FavoritesModel::NameRole).toString(), QStringLiteral("B"));
        QCOMPARE(model.data(model.index(1), FavoritesModel::NameRole).toString(), QStringLiteral("A"));
        QCOMPARE(model.data(model.index(2), FavoritesModel::NameRole).toString(), QStringLiteral("C"));

        // Move B down: B returns to index 1
        QVERIFY(model.moveDown(0));
        QCOMPARE(model.data(model.index(0), FavoritesModel::NameRole).toString(), QStringLiteral("A"));
        QCOMPARE(model.data(model.index(1), FavoritesModel::NameRole).toString(), QStringLiteral("B"));
        QCOMPARE(model.data(model.index(2), FavoritesModel::NameRole).toString(), QStringLiteral("C"));

        // Move A to end: index 0 -> index 2
        QVERIFY(model.move(0, 2));
        QCOMPARE(model.data(model.index(0), FavoritesModel::NameRole).toString(), QStringLiteral("B"));
        QCOMPARE(model.data(model.index(1), FavoritesModel::NameRole).toString(), QStringLiteral("C"));
        QCOMPARE(model.data(model.index(2), FavoritesModel::NameRole).toString(), QStringLiteral("A"));
    }

    void aliasUpdate() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        FavoritesModel model;
        const QString path = tempDir.filePath("Travel2026");
        model.addFavorite(path);
        QCOMPARE(model.data(model.index(0), FavoritesModel::NameRole).toString(), QStringLiteral("Travel2026"));

        // Set custom alias
        QVERIFY(model.setAlias(path, "Japan Trip"));
        QCOMPARE(model.data(model.index(0), FavoritesModel::NameRole).toString(), QStringLiteral("Japan Trip"));
        QCOMPARE(model.aliasForPath(path), QStringLiteral("Japan Trip"));

        // Clear custom alias -> restores folder name
        QVERIFY(model.setAlias(path, ""));
        QCOMPARE(model.data(model.index(0), FavoritesModel::NameRole).toString(), QStringLiteral("Travel2026"));
        QCOMPARE(model.aliasForPath(path), QString());
    }

    void currentDirectoryTracking() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        FavoritesModel model;
        const QString p1 = tempDir.filePath("One");
        const QString p2 = tempDir.filePath("Two");
        model.addFavorite(p1);
        model.addFavorite(p2);

        QSignalSpy dirSpy(&model, &FavoritesModel::currentDirectoryChanged);
        model.setCurrentDirectory(p1);
        QCOMPARE(dirSpy.count(), 1);
        QCOMPARE(model.data(model.index(0), FavoritesModel::IsCurrentRole).toBool(), true);
        QCOMPARE(model.data(model.index(1), FavoritesModel::IsCurrentRole).toBool(), false);

        model.setCurrentDirectory(p2);
        QCOMPARE(model.data(model.index(0), FavoritesModel::IsCurrentRole).toBool(), false);
        QCOMPARE(model.data(model.index(1), FavoritesModel::IsCurrentRole).toBool(), true);
    }

    void settingsPersistenceAndPreservation() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        // Pre-populate settings with other keys
        glancelot::updateSettings([](QJsonObject& obj) {
            obj["schema"] = 1;
            obj["theme"] = "dark";
            obj["custom_plugin_key"] = "keep_me";
        });

        {
            FavoritesModel model;
            model.addFavorite("/storage/photos", "My Photos");
            model.addFavorite("/storage/art");
        }

        // Verify settings.json
        QJsonObject root;
        QCOMPARE(glancelot::readSettings(root).code, glancelot::SettingsStatus::Ok);
        QCOMPARE(root.value("schema").toInt(), 1);
        QCOMPARE(root.value("theme").toString(), QStringLiteral("dark"));
        QCOMPARE(root.value("custom_plugin_key").toString(), QStringLiteral("keep_me"));

        const QJsonArray favs = root.value("favorites").toArray();
        QCOMPARE(favs.size(), 2);
        QCOMPARE(favs[0].toObject().value("alias").toString(), QStringLiteral("My Photos"));
    }

    void missingMountHandlingNoProbe() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        // Add paths that do not exist on disk
        const QString missingPathA = tempDir.filePath("non_existent_folder_xyz");
        const QString missingPathB = "/mnt/unmounted_drive/photos";

        FavoritesModel model;
        QVERIFY(model.addFavorite(missingPathA, "Offline Folder A"));
        QVERIFY(model.addFavorite(missingPathB, "Offline Share B"));

        // Reload model from disk
        model.reload();
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.data(model.index(0), FavoritesModel::NameRole).toString(), QStringLiteral("Offline Folder A"));
        QCOMPARE(model.data(model.index(1), FavoritesModel::NameRole).toString(), QStringLiteral("Offline Share B"));

        // Can still reorder and remove without disk errors
        QVERIFY(model.moveUp(1));
        QCOMPARE(model.data(model.index(0), FavoritesModel::NameRole).toString(), QStringLiteral("Offline Share B"));
        QVERIFY(model.removeAt(0));
        QCOMPARE(model.count(), 1);
    }

    void backwardCompatibleStringArray() {
        QTemporaryDir tempDir;
        qputenv("GLANCELOT_CONFIG_DIR", tempDir.path().toUtf8());

        glancelot::updateSettings([](QJsonObject& obj) {
            QJsonArray arr;
            arr.append("/media/usb1");
            arr.append("/media/usb2");
            obj["favorites"] = arr;
        });

        FavoritesModel model;
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.data(model.index(0), FavoritesModel::NameRole).toString(), QStringLiteral("usb1"));
        QCOMPARE(model.data(model.index(1), FavoritesModel::NameRole).toString(), QStringLiteral("usb2"));
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    FavoritesTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "favorites_tests.moc"

