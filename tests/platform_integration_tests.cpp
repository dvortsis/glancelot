#include "glancelot/platform_integration.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QStandardPaths>

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

    void revealValidTempDirReturnsTrue() {
#ifndef Q_OS_WIN
        if (QStandardPaths::findExecutable("xdg-open").isEmpty()) {
            QSKIP("No file manager (xdg-open) available in this environment.");
        }
#endif
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QVERIFY(QFileInfo(tempDir.path()).isDir());
        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(tempDir.path()));
    }

    void openTerminalRootDirectoryReturnsTrue() {
#ifndef Q_OS_WIN
        bool hasTerminal = !qEnvironmentVariable("TERMINAL").isEmpty();
        if (!hasTerminal) {
            for (const QString& candidate : {"x-terminal-emulator", "gnome-terminal", "konsole", "xfce4-terminal", "xterm"}) {
                if (!QStandardPaths::findExecutable(candidate).isEmpty()) { hasTerminal = true; break; }
            }
        }
        if (!hasTerminal) {
            QSKIP("No terminal emulator available in this environment.");
        }
#endif
        const QString root = QDir::rootPath();
        QVERIFY(QFileInfo::exists(root));
        QVERIFY(glancelot::PlatformIntegration::openTerminal(root));
    }

    void revealDirectoryVsFileBehaviors() {
#ifndef Q_OS_WIN
        if (QStandardPaths::findExecutable("xdg-open").isEmpty()) {
            QSKIP("No file manager (xdg-open) available in this environment.");
        }
#endif
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString dirPath = tempDir.path();
        QVERIFY(QFileInfo(dirPath).isDir());
        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(dirPath));

        QFile tempFile(QDir(dirPath).filePath("sample.png"));
        QVERIFY(tempFile.open(QIODevice::WriteOnly));
        tempFile.close();
        const QString filePath = tempFile.fileName();
        QVERIFY(QFileInfo(filePath).isFile());
        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(filePath));
    }

    void pathsWithSpacesAndUnicodeValidation() {
#ifndef Q_OS_WIN
        if (QStandardPaths::findExecutable("xdg-open").isEmpty()) {
            QSKIP("No file manager (xdg-open) available in this environment.");
        }
#endif
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QDir dir(tempDir.path());
        const QString unicodeDirName = QString::fromUtf8("folder with spaces \u2602 \u00e9");
        QVERIFY(dir.mkdir(unicodeDirName));
        const QString subPath = dir.filePath(unicodeDirName);
        QVERIFY(QFileInfo::exists(subPath));
        QFileInfo fi(subPath);
        QVERIFY(fi.isDir());

        // Validate reveal on directory containing spaces and unicode characters
        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(subPath));
    }
};

QTEST_MAIN(PlatformIntegrationTests)
#include "platform_integration_tests.moc"
