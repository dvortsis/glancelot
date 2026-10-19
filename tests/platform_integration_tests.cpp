#include "glancelot/platform_integration.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>

class PlatformIntegrationTests : public QObject {
    Q_OBJECT
#ifndef Q_OS_WIN
    QTemporaryDir launchers;
    QByteArray originalPath, originalTerminal, originalCapture;
    bool hadPath{}, hadTerminal{}, hadCapture{};
    QString capturePath;
    void verifyLaunch(const QString& launcher, const QStringList& arguments, const QString& cwd) {
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(capturePath), 3000);
        QFile file(capturePath); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto json = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        QCOMPARE(json.value("launcher").toString(), launcher);
        QCOMPARE(json.value("arguments").toArray(), QJsonArray::fromStringList(arguments));
        QCOMPARE(json.value("cwd").toString(), cwd);
        QVERIFY(QFile::remove(capturePath));
    }
    void verifyReveal(const QString& path) {
        const QFileInfo info(path);
        verifyLaunch("xdg-open", {info.isDir() ? info.absoluteFilePath() : info.absolutePath()}, QDir::currentPath());
    }
#endif
private slots:
    void initTestCase() {
#ifndef Q_OS_WIN
        QVERIFY(launchers.isValid());
        capturePath = launchers.filePath("request.json");
        hadPath = qEnvironmentVariableIsSet("PATH"); originalPath = qgetenv("PATH");
        hadTerminal = qEnvironmentVariableIsSet("TERMINAL"); originalTerminal = qgetenv("TERMINAL");
        hadCapture = qEnvironmentVariableIsSet("GLANCELOT_TEST_LAUNCH_CAPTURE"); originalCapture = qgetenv("GLANCELOT_TEST_LAUNCH_CAPTURE");
        for (const auto* name : {"xdg-open", "x-terminal-emulator"})
            QVERIFY(QFile::link(QCoreApplication::applicationFilePath(), launchers.filePath(name)));
        qputenv("PATH", launchers.path().toUtf8()); qunsetenv("TERMINAL");
        qputenv("GLANCELOT_TEST_LAUNCH_CAPTURE", capturePath.toUtf8());
#endif
    }
    void cleanupTestCase() {
#ifndef Q_OS_WIN
        if (hadPath) qputenv("PATH", originalPath); else qunsetenv("PATH");
        if (hadTerminal) qputenv("TERMINAL", originalTerminal); else qunsetenv("TERMINAL");
        if (hadCapture) qputenv("GLANCELOT_TEST_LAUNCH_CAPTURE", originalCapture); else qunsetenv("GLANCELOT_TEST_LAUNCH_CAPTURE");
#endif
    }

    void revealInvalidOrMissingPathReturnsFalse() {
        QVERIFY(!glancelot::PlatformIntegration::revealInFileManager(""));
        QVERIFY(!glancelot::PlatformIntegration::revealInFileManager("C:/nonexistent_file_path_12345.png"));
#ifndef Q_OS_WIN
        QVERIFY(!QFileInfo::exists(capturePath));
#endif
    }

    void openTerminalInvalidOrMissingPathReturnsFalse() {
        QVERIFY(!glancelot::PlatformIntegration::openTerminal(""));
        QVERIFY(!glancelot::PlatformIntegration::openTerminal("C:/nonexistent_dir_path_12345"));
#ifndef Q_OS_WIN
        QVERIFY(!QFileInfo::exists(capturePath));
#endif
    }

    void revealValidTempDirReturnsTrue() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QVERIFY(QFileInfo(tempDir.path()).isDir());
        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(tempDir.path()));
#ifndef Q_OS_WIN
        verifyReveal(tempDir.path());
#endif
    }

    void openTerminalRootDirectoryReturnsTrue() {
        const QString root = QDir::rootPath();
        QVERIFY(QFileInfo::exists(root));
        QVERIFY(glancelot::PlatformIntegration::openTerminal(root));
#ifndef Q_OS_WIN
        verifyLaunch("x-terminal-emulator", {}, root);
#endif
    }

    void revealDirectoryVsFileBehaviors() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString dirPath = tempDir.path();
        QVERIFY(QFileInfo(dirPath).isDir());
        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(dirPath));
#ifndef Q_OS_WIN
        verifyReveal(dirPath);
#endif

        QFile tempFile(QDir(dirPath).filePath("sample.png"));
        QVERIFY(tempFile.open(QIODevice::WriteOnly));
        tempFile.close();
        const QString filePath = tempFile.fileName();
        QVERIFY(QFileInfo(filePath).isFile());
        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(filePath));
#ifndef Q_OS_WIN
        verifyReveal(filePath);
#endif
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

        // Validate reveal on directory containing spaces and unicode characters
        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(subPath));
#ifndef Q_OS_WIN
        verifyReveal(subPath);
#endif
    }
#ifndef Q_OS_WIN
    void terminalFileParentAndLiteralPath() {
        QTemporaryDir root; QVERIFY(root.isValid());
        const auto name = QString::fromUtf8("space ' ; $() \u2602 \u00e9");
        QVERIFY(QDir(root.path()).mkdir(name));
        const auto directory = QDir(root.path()).filePath(name);
        QFile file(QDir(directory).filePath("photo ' ; $.png"));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        QVERIFY(glancelot::PlatformIntegration::openTerminal(file.fileName()));
        verifyLaunch("x-terminal-emulator", {}, directory);
        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(file.fileName()));
        verifyReveal(file.fileName());
    }
    void terminalOverrideAndMissingLaunchers() {
        QTemporaryDir folder; QVERIFY(folder.isValid());
        const auto custom = launchers.filePath("custom-terminal");
        QVERIFY(QFile::link(QCoreApplication::applicationFilePath(), custom));
        qputenv("TERMINAL", custom.toUtf8());
        QVERIFY(glancelot::PlatformIntegration::openTerminal(folder.path()));
        verifyLaunch("custom-terminal", {}, folder.path());
        // A missing override falls back to the supported PATH candidate.
        qputenv("TERMINAL", launchers.filePath("missing-terminal").toUtf8());
        QVERIFY(glancelot::PlatformIntegration::openTerminal(folder.path()));
        verifyLaunch("x-terminal-emulator", {}, folder.path());
        qunsetenv("TERMINAL");
        QVERIFY(QFile::remove(launchers.filePath("x-terminal-emulator")));
        QVERIFY(QFile::remove(launchers.filePath("xdg-open")));
        QVERIFY(!glancelot::PlatformIntegration::openTerminal(folder.path()));
        QVERIFY(!glancelot::PlatformIntegration::revealInFileManager(folder.path()));
        QVERIFY(!QFileInfo::exists(capturePath));
    }
#endif
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
#ifndef Q_OS_WIN
    const auto name = QFileInfo(QString::fromLocal8Bit(argv[0])).fileName();
    if (name == "xdg-open" || name == "x-terminal-emulator" || name == "custom-terminal") {
        const auto destination = qEnvironmentVariable("GLANCELOT_TEST_LAUNCH_CAPTURE");
        if (destination.isEmpty()) return 91;
        QSaveFile file(destination);
        const auto bytes = QJsonDocument(QJsonObject{{"launcher", name},
            {"arguments", QJsonArray::fromStringList(app.arguments().mid(1))},
            {"cwd", QDir::currentPath()}}).toJson();
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit() ? 0 : 92;
    }
#endif
    PlatformIntegrationTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "platform_integration_tests.moc"
