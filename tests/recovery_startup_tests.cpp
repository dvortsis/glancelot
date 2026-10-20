#include "glancelot/command_registry.hpp"
#include "startup_intent.hpp"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <QTemporaryDir>
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
    void cleanupTestCase() {
        for(const auto* name:{"settings.json","settings.json.bak","settings.json.damaged","sessions.json"})
            QVERIFY(!QFile::exists(config.filePath(QLatin1String(name))));
        QVERIFY(!qobject_cast<QGuiApplication*>(QCoreApplication::instance()));
    }
};
QTEST_GUILESS_MAIN(RecoveryStartupTests)
#include "recovery_startup_tests.moc"
