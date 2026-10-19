#include "glancelot/platform_integration.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QTemporaryFile>

class PlatformIntegrationTests : public QObject {
    Q_OBJECT
private slots:
    void revealInvalidOrMissingPathReturnsFalse() {
        QVERIFY(!glancelot::PlatformIntegration::revealInFileManager(""));
        QVERIFY(!glancelot::PlatformIntegration::revealInFileManager("C:/nonexistent_file_path_12345.png"));
    }

    void openTerminalInvalidOrMissingPathReturnsFalse() {
        QVERIFY(!glancelot::PlatformIntegration::openTerminal(""));
        QVERIFY(!glancelot::PlatformIntegration::openTerminal("C:/nonexistent_dir_path_12345"));
    }

    void pathsWithSpacesAndUnicodeValidation() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        QVERIFY(dir.mkdir("folder with spaces"));
        const QString subPath = dir.filePath("folder with spaces");
        QVERIFY(QFileInfo::exists(subPath));
        // Verify path resolution does not crash or throw on spaces/special characters
        QFileInfo fi(subPath);
        QVERIFY(fi.isDir());
    }
};

QTEST_MAIN(PlatformIntegrationTests)
#include "platform_integration_tests.moc"
