// REL-01: build provenance. The generated values, the --version text and the real executable's output, the BuildInfo QML
// singleton, the About rows (ui/AboutBuildInfo.qml) and their pseudo-locale catalog entries. The configure-time rules
// (a release or rc build without a SHA fails) are tested by tests/provenance/run_configure_tests.cmake.
#include "build_info.hpp"
#include "glancelot/theme_manager.hpp"
#include <QDateTime>
#include <QFile>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>
#include <memory>

using glancelot::build::Info;

namespace {
const QRegularExpression fullSha(QStringLiteral("^([0-9a-f]{40}|[0-9a-f]{64})$"));
const QRegularExpression isoUtc(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z$"));
const QRegularExpression suffix(QStringLiteral("^(-(rc|dev)\\.(0|[1-9][0-9]*))?$"));

QString kindOfSuffix(const QString& value) {
    if (value.startsWith(QLatin1String("-rc."))) return QStringLiteral("rc");
    if (value.startsWith(QLatin1String("-dev."))) return QStringLiteral("dev");
    return {};
}
QString englishKind(const QString& kind) {
    if (kind == QLatin1String("release")) return QStringLiteral("Release");
    if (kind == QLatin1String("rc")) return QStringLiteral("Release candidate");
    if (kind == QLatin1String("dev")) return QStringLiteral("Development build");
    return QStringLiteral("Local build");
}
QObject* child(QObject* root, const char* name) { return root ? root->findChild<QObject*>(QLatin1String(name)) : nullptr; }
}

class BuildInfoUiTests final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> config;
private slots:
    void initTestCase() {
        config = std::make_unique<QTemporaryDir>();
        QVERIFY(config->isValid());
        qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8()); // ThemeManager reads preferences; never the user's
        QQuickStyle::setStyle("Basic");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")), "Glancelot", 1, 0, "Theme");
    }

    // The generated translation unit holds this configuration's values, in the documented forms.
    void generatedValuesAreWellFormed() {
        const auto& info = glancelot::build::current();
        QCOMPARE(info.version, QStringLiteral(GLANCELOT_TEST_VERSION_FULL));
        QVERIFY2(info.version.startsWith(QStringLiteral(GLANCELOT_TEST_PROJECT_VERSION)), qPrintable(info.version));
        const auto versionSuffix = info.version.mid(qsizetype(sizeof(GLANCELOT_TEST_PROJECT_VERSION) - 1));
        QVERIFY2(suffix.match(versionSuffix).hasMatch(), qPrintable(info.version));
        QCOMPARE(info.kind, QStringLiteral(GLANCELOT_TEST_KIND));
        QVERIFY(QStringList({"release", "rc", "dev", "local"}).contains(info.kind));
        if (info.kind == QLatin1String("rc") || info.kind == QLatin1String("dev")) QCOMPARE(kindOfSuffix(versionSuffix), info.kind);
        else QVERIFY2(versionSuffix.isEmpty(), qPrintable(info.version));
        QVERIFY2(fullSha.match(info.sha).hasMatch() || info.sha == QLatin1String("unknown"), qPrintable(info.sha));
        if (info.kind == QLatin1String("release") || info.kind == QLatin1String("rc")) QVERIFY(fullSha.match(info.sha).hasMatch());
        // A SHA known at configure time is still known at build time.
        if (QStringLiteral(GLANCELOT_TEST_CONFIGURED_SHA) != QLatin1String("unknown")) QVERIFY(fullSha.match(info.sha).hasMatch());
        QVERIFY2(isoUtc.match(info.builtAt).hasMatch(), qPrintable(info.builtAt));
        const auto built = QDateTime::fromString(info.builtAt, Qt::ISODate);
        QVERIFY(built.isValid());
        QCOMPARE(built.timeSpec(), Qt::UTC);
        QVERIFY(built <= QDateTime::currentDateTimeUtc().addSecs(60)); // taken when the build ran, not later
        QCOMPARE(info.shortSha(), info.sha == QLatin1String("unknown") ? info.sha : info.sha.left(7));
    }

    void versionTextFormat_data() {
        QTest::addColumn<QString>("version");
        QTest::addColumn<QString>("sha");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("expected");
        const QString sha1 = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
        const QString sha256 = sha1 + QStringLiteral("89abcdef0123456789abcdef");
        QTest::newRow("release") << "1.2.3" << sha1 << "release"
            << "Glancelot 1.2.3 (0123456)\ncommit: " + sha1 + "\nbuilt: 2026-11-12T08:09:10Z\nkind: release\n";
        QTest::newRow("rc") << "1.2.3-rc.4" << sha1 << "rc"
            << "Glancelot 1.2.3-rc.4 (0123456)\ncommit: " + sha1 + "\nbuilt: 2026-11-12T08:09:10Z\nkind: rc\n";
        QTest::newRow("dev-sha256") << "1.2.3-dev.0" << sha256 << "dev"
            << "Glancelot 1.2.3-dev.0 (0123456)\ncommit: " + sha256 + "\nbuilt: 2026-11-12T08:09:10Z\nkind: dev\n";
        QTest::newRow("local-unknown") << "1.2.3" << "unknown" << "local"
            << QStringLiteral("Glancelot 1.2.3 (unknown)\ncommit: unknown\nbuilt: 2026-11-12T08:09:10Z\nkind: local\n");
    }
    void versionTextFormat() {
        QFETCH(QString, version); QFETCH(QString, sha); QFETCH(QString, kind); QFETCH(QString, expected);
        const Info info{version, sha, QStringLiteral("2026-11-12T08:09:10Z"), kind};
        QCOMPARE(glancelot::build::versionText(info), expected);
    }

    // The shipped executable prints exactly this build's values for --version, and exits 0.
    void versionOptionPrintsBuildInfo() {
        QProcess process;
        process.start(QStringLiteral(GLANCELOT_TEST_APP), {QStringLiteral("--version")});
        QVERIFY2(process.waitForStarted(10000), qPrintable(process.errorString()));
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        const auto out = QString::fromUtf8(process.readAllStandardOutput());
        QCOMPARE(out, glancelot::build::versionText());
        const auto lines = out.split(u'\n');
        QCOMPARE(lines.size(), 5); // four lines and the final newline
        QVERIFY2(QRegularExpression(QStringLiteral("^Glancelot \\d+\\.\\d+\\.\\d+(-(rc|dev)\\.\\d+)? \\(([0-9a-f]{7}|unknown)\\)$"))
                     .match(lines[0]).hasMatch(), qPrintable(lines[0]));
        QVERIFY(lines[1].startsWith(QLatin1String("commit: ")));
        QVERIFY(isoUtc.match(lines[2].mid(7)).hasMatch() && lines[2].startsWith(QLatin1String("built: ")));
        QVERIFY(lines[3].startsWith(QLatin1String("kind: ")));
    }

    // The BuildInfo singleton is registered without any call site and carries the same values.
    void qmlSingletonExposesValues() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQml\nimport Glancelot\nQtObject { property string version: BuildInfo.version; "
                          "property string sha: BuildInfo.sha; property string shortSha: BuildInfo.shortSha; "
                          "property string builtAt: BuildInfo.builtAt; property string kind: BuildInfo.kind; "
                          "property string details: BuildInfo.details }", QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        const auto& info = glancelot::build::current();
        QCOMPARE(object->property("version").toString(), info.version);
        QCOMPARE(object->property("sha").toString(), info.sha);
        QCOMPARE(object->property("shortSha").toString(), info.shortSha());
        QCOMPARE(object->property("builtAt").toString(), info.builtAt);
        QCOMPARE(object->property("kind").toString(), info.kind);
        QCOMPARE(object->property("details").toString(), glancelot::build::versionText());
    }

    // About shows the short commit (full SHA in the description), build time and kind.
    void aboutShowsProvenance() {
        ThemeManager theme;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        QQuickWindow window;
        window.resize(480, 240);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/AboutBuildInfo.qml")));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* about = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(about);
        about->setParentItem(window.contentItem());
        window.show();
        QVERIFY(about->isVisible());
        const auto& info = glancelot::build::current();
        const bool known = info.sha != QLatin1String("unknown");
        QCOMPARE(child(about, "aboutCommit")->property("text").toString(), known ? info.shortSha() : QStringLiteral("Unknown"));
        QCOMPARE(child(about, "aboutBuilt")->property("text").toString(), info.builtAt);
        QCOMPARE(child(about, "aboutKind")->property("text").toString(), englishKind(info.kind));
        QCOMPARE(child(about, "aboutCommit")->property("readOnly").toBool(), true);
        QTRY_VERIFY_WITH_TIMEOUT(child(about, "aboutKind")->property("width").toDouble() > 0, 10000); // laid out
        // The full SHA is reachable without hovering: it is the commit value's accessible description.
        auto* commit = qobject_cast<QQuickItem*>(child(about, "aboutCommit"));
        QVERIFY(commit);
        QObject* accessible = nullptr;
        for (auto* attached : commit->children())
            if (attached->inherits("QQuickAccessibleAttached")) accessible = attached;
        QVERIFY(accessible);
        QCOMPARE(accessible->property("description").toString(),
                 known ? info.sha : QStringLiteral("This build does not record its commit."));
    }

    // Every About provenance string has a finished pseudo-locale translation, so translation_coverage_ui stays green
    // once the About page shows these rows in its harness.
    void aboutStringsAreInThePseudoLocaleCatalog() {
        QFile file(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/translations/glancelot_en_XA.ts"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(&file);
        QString context, source;
        QMap<QString, QString> finished; // source -> translation, context AboutBuildInfo
        bool unfinished = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) continue;
            if (xml.name() == QLatin1String("name")) context = xml.readElementText();
            else if (xml.name() == QLatin1String("source")) source = xml.readElementText();
            else if (xml.name() == QLatin1String("translation") && context == QLatin1String("AboutBuildInfo")) {
                unfinished = xml.attributes().value(QLatin1String("type")) == QLatin1String("unfinished");
                const auto text = xml.readElementText();
                if (!unfinished && !text.isEmpty()) finished.insert(source, text);
            }
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        for (const char* text : {"Commit", "Commit %1", "Unknown", "This build does not record its commit.",
                                 "Built", "Built %1", "Build", "Release", "Release candidate", "Development build", "Local build"}) {
            const auto key = QString::fromLatin1(text);
            QVERIFY2(finished.contains(key), qPrintable(QStringLiteral("AboutBuildInfo: no finished translation for \"%1\"").arg(key)));
            QVERIFY2(finished.value(key).contains(u'['), qPrintable(finished.value(key)));
            if (key.contains(QLatin1String("%1"))) QVERIFY(finished.value(key).contains(QLatin1String("%1")));
        }
    }
};

QTEST_MAIN(BuildInfoUiTests)
#include "build_info_ui_tests.moc"
