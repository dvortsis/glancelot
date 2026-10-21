#ifdef _WIN32
#include "platform_fixture_capture.hpp"
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
    QTemporaryDir sandbox{QDir::tempPath()+"/glancelot-argv-XXXXXX"};
    QString originalCwd,directory,photo;
    QByteArray originalPath;
    QString native(const QString& path) const {return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());}
    void verify(const QString& launcher,const QStringList& arguments) {
        const auto& capture=platform_fixture::state();QVERIFY2(capture.error.isEmpty(),qPrintable(capture.error));
        QCOMPARE(capture.launches.size(),1);QCOMPARE(capture.nativeRecords.size(),1);
        QCOMPARE(capture.launches.first().program,launcher);QCOMPARE(capture.launches.first().arguments,arguments);
        QVERIFY(capture.launches.first().workingDirectory.isEmpty());
        QVERIFY(platform_fixture::samePath(capture.nativeRecords.first().value("cwd").toString(),directory));
    }
    void verifyShell() {
        const auto& capture=platform_fixture::state().shell;
        QCOMPARE(capture.calls,1);QCOMPARE(capture.count,UINT(1));QCOMPARE(capture.flags,DWORD(0));
        QVERIFY(platform_fixture::samePath(capture.folder,directory));QCOMPARE(capture.children.size(),1);
        QVERIFY(platform_fixture::samePath(capture.children.first(),photo));
    }
    void verifyApartmentUninitialized() {
        const HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
        if(SUCCEEDED(result))CoUninitialize();QCOMPARE(result,HRESULT(S_OK));
    }
private slots:
    void initTestCase() {
        QVERIFY(sandbox.isValid());originalCwd=QDir::currentPath();originalPath=qgetenv("PATH");
        QVERIFY(QFileInfo::exists(platform_fixture::helperProgram()));
        const auto helper=QFileInfo(platform_fixture::helperProgram());
        const auto found=QStandardPaths::findExecutable(helper.fileName(),{helper.absolutePath()});
        QVERIFY(platform_fixture::samePath(found,helper.absoluteFilePath()));
        QVERIFY(helper.absolutePath().contains(QString::fromUtf8("☂ 🧪")));
        directory=sandbox.filePath(QString::fromUtf8("working ' ; $() ☂ 🧪 space"));QVERIFY(QDir().mkdir(directory));
        photo=QDir(directory).filePath(QString::fromUtf8("photo ' ; $() 🙂.png"));
        QFile file(photo);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("generated"),qint64(9));file.close();
        QVERIFY(QDir::setCurrent(directory));verifyApartmentUninitialized();
    }
    void init() {
        platform_fixture::state()={};auto& capture=platform_fixture::state();capture.captureRoot=sandbox.path();
        capture.terminalExecutable="C:/controlled Windows Terminal/wt.exe";
    }
    void invalidPathsNeverLaunch() {
        QVERIFY(!glancelot::PlatformIntegration::revealInFileManager(""));
        QVERIFY(!glancelot::PlatformIntegration::revealInFileManager(sandbox.filePath("missing.png")));
        QVERIFY(!glancelot::PlatformIntegration::openTerminal(""));
        QVERIFY(!glancelot::PlatformIntegration::openTerminal(sandbox.filePath("missing")));
        QVERIFY(platform_fixture::state().launches.isEmpty());QCOMPARE(platform_fixture::state().shell.calls,0);
    }
    void revealDirectoryNativeArgumentRoundtrip() {
        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(directory));verify("explorer.exe",{native(directory)});
        QCOMPARE(platform_fixture::state().shell.calls,0);
    }
    void revealFileNativeSuccessBalancesNewSta() {
        verifyApartmentUninitialized();QVERIFY(glancelot::PlatformIntegration::revealInFileManager(photo));verifyShell();
        QVERIFY(platform_fixture::state().launches.isEmpty());verifyApartmentUninitialized();
    }
    void revealFileNativeSuccessBalancesExistingSta() {
        {
            const HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
            struct Balance {HRESULT result;~Balance(){if(SUCCEEDED(result))CoUninitialize();}} balance{result};
            QCOMPARE(result,HRESULT(S_OK));QVERIFY(glancelot::PlatformIntegration::revealInFileManager(photo));verifyShell();
            QVERIFY(platform_fixture::state().launches.isEmpty());
            const HRESULT nested=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
            if(SUCCEEDED(nested))CoUninitialize();QCOMPARE(nested,HRESULT(S_FALSE));
        }
        verifyApartmentUninitialized();
    }
    void revealFileNativeFailureFallsBackToCapturedExplorer() {
        platform_fixture::state().shell.result=E_FAIL;
        QVERIFY(glancelot::PlatformIntegration::revealInFileManager(photo));verifyShell();
        verify("explorer.exe",{"/select,",native(photo)});verifyApartmentUninitialized();
    }
    void terminalDirectoryAndFileParentRoundtrip_data() {
        QTest::addColumn<bool>("file");QTest::newRow("directory")<<false;QTest::newRow("file")<<true;
    }
    void terminalDirectoryAndFileParentRoundtrip() {
        QFETCH(bool,file);QVERIFY(glancelot::PlatformIntegration::openTerminal(file?photo:directory));
        verify(platform_fixture::state().terminalExecutable,{"-d",native(directory)});
        QCOMPARE(platform_fixture::state().lookupCalls,1);
    }
    void terminalRootRoundtrip() {
        QVERIFY(glancelot::PlatformIntegration::openTerminal(QDir::rootPath()));
        verify(platform_fixture::state().terminalExecutable,{"-d",native(QDir::rootPath())});
    }
    void terminalLiteralFallbackAndControlledFailure_data() {
        QTest::addColumn<bool>("wtPresent");QTest::addColumn<bool>("psSucceeds");
        QTest::newRow("missing-wt")<<false<<true;QTest::newRow("failed-wt")<<true<<true;
        QTest::newRow("both-fail")<<true<<false;QTest::newRow("ps-fails")<<false<<false;
    }
    void terminalLiteralFallbackAndControlledFailure() {
        QFETCH(bool,wtPresent);QFETCH(bool,psSucceeds);auto& capture=platform_fixture::state();
        if(!wtPresent)capture.terminalExecutable.clear();else capture.launchResults.push_back(false);
        capture.launchResults.push_back(psSucceeds);
        QCOMPARE(glancelot::PlatformIntegration::openTerminal(photo),psSucceeds);
        QCOMPARE(capture.lookupCalls,1);QCOMPARE(capture.launches.size(),wtPresent?2:1);
        if(wtPresent){QCOMPARE(capture.launches.first().program,capture.terminalExecutable);QCOMPARE(capture.launches.first().arguments,QStringList({"-d",native(directory)}));}
        auto escaped=native(directory);escaped.replace("'","''");
        QCOMPARE(capture.launches.last().program,QString("powershell.exe"));
        QCOMPARE(capture.launches.last().arguments,QStringList({"-NoExit","-Command",QString("Set-Location -LiteralPath '%1'").arg(escaped)}));
        QCOMPARE(capture.nativeRecords.size(),psSucceeds?1:0);QVERIFY2(capture.error.isEmpty(),qPrintable(capture.error));
    }
    void nativeArgumentEdgeCases_data() {
        QTest::addColumn<QStringList>("arguments");
        QTest::newRow("no-payload")<<QStringList{};
        QTest::newRow("empty")<<QStringList{""};
        QTest::newRow("quotes-and-slashes")<<QStringList{"a\"b","space trailing\\","\\\\",""};
        QTest::newRow("literal-metacharacters")<<QStringList{QString::fromUtf8("☂ 🧪 🙂"),"' ; $() & | < > % !","--a=b"};
        QStringList maximum;for(int i=0;i<64;++i)maximum.append("x");
        QTest::newRow("maximum-argument-count")<<maximum;
        QTest::newRow("maximum-character-budget")<<QStringList{QString(20000,'x')};
    }
    void nativeArgumentEdgeCases() {
        QFETCH(QStringList,arguments);
        const auto result=platform_fixture::captureArguments(arguments,directory);
        QVERIFY2(result.has_value(),qPrintable(platform_fixture::state().error));
        QCOMPARE(result->value("arguments").toArray(),QJsonArray::fromStringList(arguments));
    }
    void helperRejectsInvalidControlsAndExistingOutput_data() {
        QTest::addColumn<QString>("kind");
        for(const auto* kind:{"missing","nonce","root","size","count","existing"})QTest::newRow(kind)<<QString(kind);
    }
    void helperRejectsInvalidControlsAndExistingOutput() {
        QFETCH(QString,kind);const auto nonce=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto destination=sandbox.filePath("capture-"+nonce+".json");
        auto env=QProcessEnvironment::systemEnvironment();env.insert("GLANCELOT_TEST_ARGV_ROOT",kind=="root"?directory:sandbox.path());
        QStringList arguments{kind=="nonce"?"../invalid":nonce};
        if(kind=="missing")arguments.clear();
        if(kind=="size")arguments.append(QString(20001,'x'));
        if(kind=="count")for(int i=0;i<65;++i)arguments.append("x");
        if(kind=="existing"){QFile file(destination);QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));QCOMPARE(file.write("preserve"),qint64(8));}
        QProcess child;child.setProcessEnvironment(env);child.setWorkingDirectory(directory);child.start(platform_fixture::helperProgram(),arguments);
        QVERIFY(child.waitForStarted(2000));
        if(!child.waitForFinished(2000)){child.kill();child.waitForFinished(1000);QFAIL("Helper control rejection timed out");}
        QCOMPARE(child.exitStatus(),QProcess::NormalExit);QVERIFY(child.exitCode()!=0);QCOMPARE(child.state(),QProcess::NotRunning);
        if(kind=="existing"){QFile file(destination);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("preserve"));}
        else QVERIFY(!QFileInfo::exists(destination));
    }
    void cleanupTestCase() {
        QCOMPARE(qgetenv("PATH"),originalPath);verifyApartmentUninitialized();QVERIFY(QDir::setCurrent(originalCwd));
    }
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
#ifdef Q_OS_WIN
    return QTest::qExec(&tests, app.arguments());
#else
    return QTest::qExec(&tests, argc, argv);
#endif
}
#include "platform_integration_tests.moc"
