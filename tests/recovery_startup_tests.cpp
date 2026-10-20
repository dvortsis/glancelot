#include "glancelot/command_registry.hpp"
#include "startup_intent.hpp"
#include "startup_environment.hpp"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <QTemporaryDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QJsonDocument>
#include <cstdio>
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
    void earlyParserRejectsAmbiguousOrIncompatibleCommands_data() {
        QTest::addColumn<QStringList>("arguments");
        const auto row=[](const char* name,QStringList args){QTest::newRow(name)<<args;};
        row("unknown",{"--unknown"});row("missing-operand",{"--settings-backup"});
        row("multiple-verbs",{"--settings-backup","one","--settings-diff","two"});
        row("repeated-verb",{"--settings-diff","one","--settings-diff","two"});
        row("ambiguous-operand",{"--settings-backup","--settings-diff"});
        row("image-maintenance",{"--settings-diff","one","image.png"});
        row("reset-safe",{"--safe-mode","--reset-settings"});
        row("orphan-scope",{"--settings-scope","viewing"});row("orphan-source",{"--settings-source","primary"});
        row("orphan-slot",{"--settings-slot","1"});row("orphan-preview",{"--settings-preview","sf1:"+QString(64,'a')});
        row("scope-backup",{"--settings-backup","one","--settings-scope","global"});
        row("source-backup",{"--settings-backup","one","--settings-source","last-good"});
        row("preview-diff",{"--settings-diff","one","--settings-preview","sf1:"+QString(64,'a')});
        row("no-restore-preview",{"--settings-restore","one"});
        row("invalid-preview",{"--settings-restore","one","--settings-preview","sf1:"+QString(64,'A')});
        row("unknown-scope",{"--settings-diff","one","--settings-scope","everything"});
        row("missing-slot",{"--settings-diff","one","--settings-scope","position"});
        row("global-slot",{"--settings-diff","one","--settings-slot","1"});
        row("noncanonical-slot",{"--settings-diff","one","--settings-scope","layout","--settings-slot","01"});
        row("huge-slot",{"--settings-diff","one","--settings-scope","layout","--settings-slot","1000001"});
        row("unknown-source",{"--settings-diff","one","--settings-source","backup"});
        row("repeated-scope",{"--settings-diff","one","--settings-scope","viewing","--settings-scope","appearance"});
        row("two-images",{"one.png","two.png"});row("bad-timer",{"--quit-after","99"});
        row("bad-window-size",{"--window-size","540xbig"});row("empty-output",{"--report="});
        row("help-verb",{"--help","--settings-backup","one"});row("version-reset",{"--version","--reset-settings"});
        row("alias-repeat",{"-h","--help"});
        row("qt-maintenance",{"--settings-diff","one","-platform","offscreen"});
        row("qt-inline-operand",{"--platform=offscreen"});
        row("qt-alias-repeat",{"-qwindowtitle","one","-title","two"});
        for(const auto* name:{"safe-mode","no-resume","reset-settings","show-grid","show-settings","show-themes","frameless"})
            row(name,{"--settings-diff","one","--"+QString::fromLatin1(name)});
        for(const auto* name:{"quit-after","screenshot","perf-report","report","theme","theme-mode","window-size"})
            row(name,{"--settings-diff","one","--"+QString::fromLatin1(name),"value"});
    }
    void earlyParserRejectsAmbiguousOrIncompatibleCommands() {
        QFETCH(QStringList,arguments);arguments.prepend("glancelot");QCommandLineParser parser;glancelot::startup::configure(parser);
        const auto intent=glancelot::startup::parse(arguments,parser);QCOMPARE(intent.mode,glancelot::startup::Mode::Error);QVERIFY(!intent.error.isEmpty());
    }
    void earlyParserPreservesUnicodeAndExplicitDashOperand() {
        using namespace glancelot::startup;const QString image=QString::fromUtf8("-雪景-😀.png");
        QCommandLineParser parser;configure(parser);
        const auto intent=parse({"glancelot","--safe-mode","--no-resume","--theme","graphite","--theme-mode","dark","--report","evidence.json","--quit-after","100","--",image},parser);
        QCOMPARE(intent.mode,Mode::Viewer);QVERIFY(intent.safeMode&&intent.noResume);QCOMPARE(parser.positionalArguments(),QStringList{image});
        QVERIFY(!qobject_cast<QGuiApplication*>(QCoreApplication::instance()));
        const auto native=originalArguments(0,nullptr);
#ifdef Q_OS_WIN
        QVERIFY(!native.isEmpty());QVERIFY(native.first().endsWith("recovery_registry_component.exe")||native.first().endsWith("recovery_startup_tests.exe"));
#else
        QVERIFY(native.isEmpty());
#endif
    }
    void earlyParserKeepsQtGuiOptionsBeforeConstruction() {
        QCommandLineParser parser;glancelot::startup::configure(parser);
        const auto intent=glancelot::startup::parse({"glancelot","-platform","offscreen","--qwindowtitle","Generated title","-reverse","--safe-mode"},parser);
        QCOMPARE(intent.mode,glancelot::startup::Mode::Viewer);QVERIFY(intent.safeMode);
        QCOMPARE(parser.value("platform"),QString("offscreen"));QVERIFY(parser.isSet("reverse"));
        QVERIFY(!qobject_cast<QGuiApplication*>(QCoreApplication::instance()));
    }
    void earlyParserSelectsExactMaintenanceScopeAndFingerprint() {
        using namespace glancelot::startup;
        const QString directory=QString::fromUtf8("generated-配置-😀"),fingerprint="sf1:"+QString(64,'a');
        QCommandLineParser parser;configure(parser);
        const auto intent=parse({"glancelot","--settings-restore",directory,"--settings-scope","position","--settings-slot","1000000","--settings-source","last-good","--settings-preview",fingerprint},parser);
        QCOMPARE(intent.mode,Mode::Restore);QVERIFY(intent.maintenance());QVERIFY(intent.slotSection);QCOMPARE(*intent.slotSection,glancelot::recovery::SlotSection::Position);
        QCOMPARE(intent.slot,1000000);QCOMPARE(intent.source,glancelot::recovery::SourceMember::LastGood);QCOMPARE(intent.expectedFingerprint,fingerprint);
        QVERIFY(intent.packageDirectory.endsWith(directory));QVERIFY(QDir::isAbsolutePath(intent.packageDirectory));
        for(const auto& scope:QStringList{"global","viewing","browsing","appearance","bindings","performance"}) {
            QCommandLineParser other;configure(other);const auto diff=parse({"glancelot","--settings-diff",directory,"--settings-scope",scope},other);
            QCOMPARE(diff.mode,Mode::Diff);QVERIFY(!diff.slotSection);QCOMPARE(int(diff.global),QStringList({"global","viewing","browsing","appearance","bindings","performance"}).indexOf(scope));
        }
        for(const auto& command:QStringList{"--help","--version","--settings-backup"}) {
            QCommandLineParser other;configure(other);QStringList args{"glancelot",command};if(command=="--settings-backup")args.append(directory);
            const auto result=parse(args,other);QVERIFY(result.mode!=Mode::Error);
        }
    }
    void safeEnvironmentRunsBeforeApplicationAndRetainsUnicodePrivateRoot() {
        QTemporaryDir generated;QVERIFY(generated.isValid());
        const auto parent=generated.filePath(QString::fromUtf8("private-配置-😀"));const auto original=generated.filePath(QString::fromUtf8("original-雪景-😀"));
        QVERIFY(QDir().mkpath(parent));QVERIFY(QDir().mkpath(original));
        for(const auto* name:{"settings.json","settings.json.bak","settings.json.damaged","sessions.json"}) {
            QFile file(QDir(original).filePath(QLatin1String(name)));QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("original corrupt diagnostic bytes"),qint64(33));
        }
        QProcess process;auto environment=QProcessEnvironment::systemEnvironment();environment.insert("GLANCELOT_CONFIG_DIR",original);
        environment.insert("QT_QUICK_BACKEND","software");environment.insert("QMLSCENE_DEVICE","softwarecontext");environment.insert("QSG_RHI_BACKEND","null");environment.insert("QT_D3D_ADAPTER_INDEX","9999");
        process.setProcessEnvironment(environment);process.start(QCoreApplication::applicationFilePath(),{"--safe-env-helper",parent});
        QVERIFY(process.waitForStarted(3000));QVERIFY(process.waitForFinished(5000));QCOMPARE(process.exitStatus(),QProcess::NormalExit);QCOMPARE(process.exitCode(),0);
        const auto bytes=process.readAllStandardOutput();const auto report=QJsonDocument::fromJson(bytes).object();QVERIFY2(!report.isEmpty(),bytes.constData());
        QCOMPARE(report.value("status").toInt(),int(glancelot::recovery::Code::Ok));QCOMPARE(report.value("original_config").toString(),original);
        QVERIFY(!report.value("original_default").toBool());QVERIFY(report.value("no_gui_app").toBool());QVERIFY(report.value("redirect_exact").toBool());
        QVERIFY(report.value("root_exists_before").toBool());QVERIFY(report.value("root_exists_held").toBool());QVERIFY(!report.value("root_exists_after").toBool());
        QVERIFY(report.value("private_write").toBool());QVERIFY(report.value("root_path").toString().startsWith(parent+"/Glancelot-safe-"));
        QCOMPARE(report.value("late_status").toInt(),int(glancelot::recovery::Code::Conflict));QVERIFY(report.value("quick_backend").toString().isEmpty());QVERIFY(report.value("old_device").toString().isEmpty());
        QCOMPARE(report.value("prefer_software").toString(),QString("1"));QCOMPARE(report.value("opengl").toString(),QString("software"));
#ifdef Q_OS_WIN
        QCOMPARE(report.value("requested_backend").toString(),QString("d3d11-warp"));QCOMPARE(report.value("rhi_backend").toString(),QString("d3d11"));
#else
        QCOMPARE(report.value("requested_backend").toString(),QString("opengl-software"));QCOMPARE(report.value("rhi_backend").toString(),QString("opengl"));
#endif
        for(const auto* name:{"settings.json","settings.json.bak","settings.json.damaged","sessions.json"}) {
            QFile file(QDir(original).filePath(QLatin1String(name)));QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("original corrupt diagnostic bytes"));
        }
    }
    void safeEnvironmentDefaultDescriptorAndInvalidParent() {
        QTemporaryDir generated;QVERIFY(generated.isValid());
        for(const bool invalid:{false,true}) {
            QProcess process;auto environment=QProcessEnvironment::systemEnvironment();environment.remove("GLANCELOT_CONFIG_DIR");process.setProcessEnvironment(environment);
            process.start(QCoreApplication::applicationFilePath(),{"--safe-env-helper",invalid?generated.filePath("missing-parent"):generated.path()});
            QVERIFY(process.waitForStarted(3000));QVERIFY(process.waitForFinished(5000));QCOMPARE(process.exitCode(),0);
            const auto bytes=process.readAllStandardOutput();const auto report=QJsonDocument::fromJson(bytes).object();QVERIFY2(!report.isEmpty(),bytes.constData());
            QCOMPARE(report.value("status").toInt(),int(invalid?glancelot::recovery::Code::WriteFailed:glancelot::recovery::Code::Ok));
            QVERIFY(report.value("original_default").toBool());QCOMPARE(report.value("original_config"),report.value("default_namespace"));
            if(invalid)QVERIFY(report.value("root_path").toString().isEmpty());
        }
    }
    void cleanupTestCase() {
        for(const auto* name:{"settings.json","settings.json.bak","settings.json.damaged","sessions.json"})
            QVERIFY(!QFile::exists(config.filePath(QLatin1String(name))));
        QVERIFY(!qobject_cast<QGuiApplication*>(QCoreApplication::instance()));
    }
};
int main(int argc,char** argv) {
    const auto arguments=glancelot::startup::originalArguments(argc,argv);
    if(arguments.value(1)=="--safe-env-helper") {
        const auto original=glancelot::startup::captureOriginalConfig();
        auto prepared=glancelot::startup::prepareSafeEnvironment(arguments.value(2));
        const auto path=prepared.context.directory;QJsonObject report{{"status",int(prepared.status.code)},{"root_path",path},{"requested_backend",prepared.requestedBackend},
            {"original_default",original.useDefault},{"root_exists_before",!path.isEmpty()&&QDir(path).exists()},{"redirect_exact",!path.isEmpty()&&qEnvironmentVariable("GLANCELOT_CONFIG_DIR")==path}};
        bool written=false;
        if(!path.isEmpty()){QFile file(QDir(path).filePath("settings.json"));written=file.open(QIODevice::WriteOnly)&&file.write("private settings")==16;}
        report["private_write"]=written;auto held=prepared.context;prepared.context={};report["root_exists_held"]=!path.isEmpty()&&QDir(path).exists();held={};
        report["root_exists_after"]=!path.isEmpty()&&QDir(path).exists();
        QCoreApplication app(argc,argv);QCoreApplication::setApplicationName("Glancelot");QCoreApplication::setOrganizationName("Glancelot");
        report["original_config"]=glancelot::startup::resolveOriginalConfig(original);report["default_namespace"]=QDir::cleanPath(QFileInfo(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).absoluteFilePath());
        report["no_gui_app"]=!qobject_cast<QGuiApplication*>(QCoreApplication::instance());report["late_status"]=int(glancelot::startup::prepareSafeEnvironment(arguments.value(2)).status.code);
        for(const auto& pair:std::initializer_list<std::pair<const char*,const char*>>{{"quick_backend","QT_QUICK_BACKEND"},{"old_device","QMLSCENE_DEVICE"},{"rhi_backend","QSG_RHI_BACKEND"},{"prefer_software","QSG_RHI_PREFER_SOFTWARE_RENDERER"},{"opengl","QT_OPENGL"}})
            report[QLatin1String(pair.first)]=qEnvironmentVariable(pair.second);
        const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Compact);std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);return 0;
    }
    QCoreApplication app(argc,argv);RecoveryStartupTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "recovery_startup_tests.moc"
