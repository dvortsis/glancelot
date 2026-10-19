#ifdef _WIN32
#include "native_platform_fixture.hpp"
#endif
#include "glancelot/platform_integration.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>

#ifdef Q_OS_WIN
class PlatformIntegrationTests : public QObject {
    Q_OBJECT
    native_fixture::Fixture fixture;
    QString directory,photo;
    QString native(const QString& path) const {return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());}
    bool resolves(const QString& name) {
        wchar_t found[32768]{};const auto n=SearchPathW(nullptr,reinterpret_cast<LPCWSTR>(name.utf16()),nullptr,32768,found,nullptr);
        return n>0 && n<32768 && native_fixture::samePath(QString::fromWCharArray(found,int(n)),QDir(fixture.sandbox).filePath(name));
    }
    void verify(const QString& launcher,const QStringList& arguments) {
        QVERIFY2(fixture.verify(launcher,arguments,fixture.sandbox),qPrintable(fixture.error));
    }
    void verifyShell() {
        const auto& capture=native_fixture::shellCapture();QCOMPARE(capture.calls,1);QCOMPARE(capture.count,UINT(1));QCOMPARE(capture.flags,DWORD(0));
        QVERIFY(native_fixture::samePath(capture.folder,directory));QCOMPARE(capture.children.size(),1);
        QVERIFY(native_fixture::samePath(capture.children.first(),photo));
    }
    void verifyApartmentUninitialized() {
        const HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
        if(SUCCEEDED(result)) CoUninitialize();QCOMPARE(result,HRESULT(S_OK));
    }
private slots:
    void initTestCase() {
        QVERIFY2(fixture.prepare(),qPrintable(fixture.error));
        for(const auto* name:{"explorer.exe","wt.exe","powershell.exe"}) QVERIFY(resolves(name));
        QVERIFY(native_fixture::samePath(QStandardPaths::findExecutable("wt.exe"),QDir(fixture.sandbox).filePath("wt.exe")));
        // Fail closed on copied runner/helper identity before invoking even a canary bare executable.
        QVERIFY(fixture.begin());QVERIFY(QProcess::startDetached("explorer.exe",{"--canary",native_fixture::environment(L"GLANCELOT_NATIVE_TOKEN")}));
        QVERIFY2(fixture.verify("explorer.exe",{"--canary",native_fixture::environment(L"GLANCELOT_NATIVE_TOKEN")},fixture.sandbox),qPrintable(fixture.error));
        directory=QDir(fixture.sandbox).filePath(QString::fromUtf8("folder ' ; $() \U0001f9ea \u2602 space"));
        QVERIFY(QDir().mkdir(directory));photo=QDir(directory).filePath(QString::fromUtf8("photo ' ; $() \U0001f642.png"));
        QFile file(photo);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("generated"),qint64(9));
        verifyApartmentUninitialized();
    }
    void init() {native_fixture::shellCapture()={};QVERIFY(fixture.begin());}
    void invalidPathsNeverLaunch() {
        QVERIFY(!glancelot::PlatformIntegration::revealInFileManager(""));
        QVERIFY(!glancelot::PlatformIntegration::revealInFileManager(QDir(fixture.sandbox).filePath("missing.png")));
        QVERIFY(!glancelot::PlatformIntegration::openTerminal(""));
        QVERIFY(!glancelot::PlatformIntegration::openTerminal(QDir(fixture.sandbox).filePath("missing")));
        QVERIFY(fixture.noCapture());QCOMPARE(native_fixture::shellCapture().calls,0);
    }
    void revealDirectoryCapturesNativeArgument() {
        QVERIFY(resolves("explorer.exe"));QVERIFY(glancelot::PlatformIntegration::revealInFileManager(directory));
        verify("explorer.exe",{native(directory)});QCOMPARE(native_fixture::shellCapture().calls,0);
    }
    void revealFileNativeSuccessBalancesNewSta() {
        verifyApartmentUninitialized();QVERIFY(glancelot::PlatformIntegration::revealInFileManager(photo));
        verifyShell();QVERIFY(fixture.noCapture());verifyApartmentUninitialized();
    }
    void revealFileNativeSuccessBalancesExistingSta() {
        const HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
        // Always balance the outer initialization even when a later assertion fails.
        struct Balance {HRESULT result;~Balance(){if(SUCCEEDED(result))CoUninitialize();}} balance{result};
        QCOMPARE(result,HRESULT(S_OK));QVERIFY(glancelot::PlatformIntegration::revealInFileManager(photo));verifyShell();QVERIFY(fixture.noCapture());
        const HRESULT nested=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
        if(SUCCEEDED(nested))CoUninitialize();QCOMPARE(nested,HRESULT(S_FALSE));
    }
    void revealFileNativeFailureFallsBackToOwnedExplorer() {
        native_fixture::shellCapture().result=E_FAIL;QVERIFY(resolves("explorer.exe"));
        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(photo));verifyShell();
        verify("explorer.exe",{"/select,",native(photo)});verifyApartmentUninitialized();
    }
    void terminalDirectoryAndFileParentCaptureWt() {
        QVERIFY(glancelot::PlatformIntegration::openTerminal(directory));verify("wt.exe",{"-d",native(directory)});
        QVERIFY(fixture.begin());QVERIFY(glancelot::PlatformIntegration::openTerminal(photo));verify("wt.exe",{"-d",native(directory)});
        QVERIFY(fixture.begin());QVERIFY(glancelot::PlatformIntegration::openTerminal(QDir::rootPath()));verify("wt.exe",{"-d",native(QDir::rootPath())});
    }
    void terminalPowerShellLiteralFallbackAndControlledFailure() {
        const QString wt=QDir(fixture.sandbox).filePath("wt.exe"),hidden=wt+".hidden";
        const QString ps=QDir(fixture.sandbox).filePath("powershell.exe"),saved=ps+".saved";
        struct Restore {QString wt,hidden,ps,saved;~Restore(){if(QFileInfo::exists(hidden))QFile::rename(hidden,wt);if(QFileInfo::exists(saved)){QFile::remove(ps);QFile::rename(saved,ps);}}} restore{wt,hidden,ps,saved};
        QVERIFY(QFile::rename(wt,hidden));QVERIFY(QStandardPaths::findExecutable("wt.exe").isEmpty());QVERIFY(resolves("powershell.exe"));
        QString escaped=native(directory);escaped.replace("'","''");
        QVERIFY(glancelot::PlatformIntegration::openTerminal(photo));
        verify("powershell.exe",{"-NoExit","-Command",QString("Set-Location -LiteralPath '%1'").arg(escaped)});
        QVERIFY(QFile::rename(ps,saved));QFile invalid(ps);QVERIFY(invalid.open(QIODevice::WriteOnly));QCOMPARE(invalid.write("not a PE executable"),qint64(19));invalid.close();
        // Controlled application-directory invalid image blocks fallback. This does not claim system PowerShell absent.
        QVERIFY(resolves("powershell.exe"));QVERIFY(QStandardPaths::findExecutable("wt.exe").isEmpty());QVERIFY(fixture.begin());
        QVERIFY(!glancelot::PlatformIntegration::openTerminal(directory));QVERIFY(fixture.noCapture());
    }
    void cleanupTestCase() {QVERIFY2(fixture.prepare(),qPrintable(fixture.error));verifyApartmentUninitialized();}
};
#else

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

#endif
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    if (native_fixture::environment(L"GLANCELOT_NATIVE_SANDBOX").isEmpty()) return native_fixture::bootstrap(app.arguments());
#endif
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
