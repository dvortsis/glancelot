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

    void revealValidTempFileReturnsTrue() {
        QTemporaryFile tempFile;
        QVERIFY(tempFile.open());
        tempFile.close();
        QVERIFY(QFileInfo::exists(tempFile.fileName()));
        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(tempFile.fileName()));
    }

    void openTerminalValidTempDirReturnsTrue() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QVERIFY(glancelot::PlatformIntegration::openTerminal(tempDir.path()));
    }

    void pathsWithSpacesAndUnicodeValidation() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        const QString unicodeDirName = QString::fromUtf8("folder with spaces \u2602 \u00e9");
        QVERIFY(dir.mkdir(unicodeDirName));
        const QString subPath = dir.filePath(unicodeDirName);
        QVERIFY(QFileInfo::exists(subPath));
        QFileInfo fi(subPath);
        QVERIFY(fi.isDir());

        QFile tempFile(dir.filePath(unicodeDirName + "/test photo \u2603.png"));
        QVERIFY(tempFile.open(QIODevice::WriteOnly));
        tempFile.close();
        QVERIFY(QFileInfo::exists(tempFile.fileName()));

        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(tempFile.fileName()));
        QVERIFY(glancelot::PlatformIntegration::openTerminal(subPath));
    }
};

QTEST_MAIN(PlatformIntegrationTests)
#include "platform_integration_tests.moc"
