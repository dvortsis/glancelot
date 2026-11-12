// E6 (F052/F053, UX audit minors): derived theme roles, native high-contrast following and the --window-size clamp.
#include "glancelot/theme_manager.hpp"
#include "system_contrast.hpp"
#include "theme_contrast.hpp"
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using glancelot::SystemContrast;
using glancelot::theme::contrastRatio;

namespace {
QColor role(const QVariantMap& palette, const char* name) { return QColor(palette.value(QLatin1String(name)).toString()); }

// Restores the desktop reading even when a check fails part-way.
struct FakeContrast {
    SystemContrast state;
    FakeContrast() { glancelot::setSystemContrastProbeForTesting([this] { return state; }); }
    ~FakeContrast() { glancelot::setSystemContrastProbeForTesting({}); }
    void set(SystemContrast next) { state=std::move(next); glancelot::setSystemContrastProbeForTesting([this] { return state; }); }
};

struct Launch {
    int exitCode=-1;
    bool finished=false;
    QString errors, output;
    QJsonObject report;
    qint64 elapsedMs=0;
};
}

class ThemeContrastTests final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> config;
    Launch launch(const QStringList& arguments) {
        Launch result;
        QTemporaryDir work; if(!work.isValid()) return result;
        QProcess process;
        auto environment=QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("GLANCELOT_CONFIG_DIR", work.filePath("config"));
        process.setProcessEnvironment(environment);
        const auto reportPath=work.filePath("report.json");
        QElapsedTimer clock; clock.start();
        process.start(QStringLiteral(GLANCELOT_APP_PATH), QStringList{"--no-resume", "--report", reportPath, "--quit-after", "800"}+arguments);
        result.finished=process.waitForFinished(60000);
        if(!result.finished) { process.kill(); process.waitForFinished(10000); }
        result.elapsedMs=clock.elapsed();
        result.exitCode=process.exitCode();
        result.errors=QString::fromUtf8(process.readAllStandardError());
        result.output=QString::fromUtf8(process.readAllStandardOutput());
        QFile report(reportPath);
        if(report.open(QIODevice::ReadOnly)) result.report=QJsonDocument::fromJson(report.readAll()).object();
        return result;
    }
private slots:
    void init() { config=std::make_unique<QTemporaryDir>(); qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8()); }
    void cleanup() { qunsetenv("GLANCELOT_CONFIG_DIR"); config.reset(); }

    // Every family keeps its supplied catalog roles exactly and gains a 3:1 control boundary and family-tinted fills
    // that keep text, muted text and the primary marker legible.
    void derivedRolesMeetContrastInEveryVariant() {
        ThemeManager manager;
        QFile catalogFile(":/themes/catalog.json"); QVERIFY(catalogFile.open(QIODevice::ReadOnly));
        const auto catalog=QJsonDocument::fromJson(catalogFile.readAll()).array();
        QCOMPARE(catalog.size(), 40);
        int variants=0;
        double borderBefore[2]={100, 100}, borderAfterMin[2]={100, 100}, borderAfterMax[2]={0, 0};
        double hoverBefore[2]={100, 100}, hoverAfter[2]={100, 100}, placeholderAfter[2]={100, 100}, mutedOnPlaceholder[2]={100, 100};
        for(const auto& entry:catalog) {
            const auto family=entry.toObject();
            const auto id=family.value("id").toString();
            for(bool dark:{false, true}) {
                const int mode=dark ? 1 : 0;
                const auto supplied=family.value(dark ? "dark" : "light").toObject().toVariantMap();
                const auto palette=manager.preview(id, dark);
                const auto context=id+(dark ? " dark" : " light");
                for(auto it=supplied.cbegin(); it!=supplied.cend(); ++it)
                    QVERIFY2(palette.value(it.key())==it.value(), qPrintable(context+" changed catalog role "+it.key()));
                const auto background=role(palette, "background"), surface=role(palette, "surface"), text=role(palette, "text"),
                           muted=role(palette, "muted"), primary=role(palette, "primary");
                const auto border=role(palette, "controlBorder"), hover=role(palette, "familyHover"), placeholder=role(palette, "placeholder");
                QVERIFY2(border.isValid() && hover.isValid() && placeholder.isValid(), qPrintable(context+" missing derived role"));
                const auto require=[&](const QColor& a, const QColor& b, double minimum, const char* what) {
                    const double ratio=contrastRatio(a, b);
                    QVERIFY2(ratio>=minimum, qPrintable(QString("%1 %2 %3 < %4").arg(context, what).arg(ratio, 0, 'f', 2).arg(minimum)));
                };
                require(border, background, 3.0, "controlBorder/background");
                require(border, surface, 3.0, "controlBorder/surface");
                require(text, hover, 4.5, "text/familyHover");
                require(muted, hover, 4.5, "muted/familyHover");
                require(primary, hover, 3.0, "primary/familyHover");
                require(text, placeholder, 4.5, "text/placeholder");
                require(muted, placeholder, 4.5, "muted/placeholder (Loading preview label)");
                // The placeholder tile is visible on the surface, and never fainter than the hover fill.
                QVERIFY2(contrastRatio(placeholder, surface)>=contrastRatio(hover, surface), qPrintable(context+" placeholder fainter than hover"));
                QVERIFY2(contrastRatio(placeholder, surface)>=1.2, qPrintable(context+" placeholder indistinguishable from surface"));
                borderBefore[mode]=std::min(borderBefore[mode], std::min(contrastRatio(role(palette, "border"), background), contrastRatio(role(palette, "border"), surface)));
                const double after=std::min(contrastRatio(border, background), contrastRatio(border, surface));
                borderAfterMin[mode]=std::min(borderAfterMin[mode], after); borderAfterMax[mode]=std::max(borderAfterMax[mode], after);
                hoverBefore[mode]=std::min(hoverBefore[mode], contrastRatio(role(palette, "hover"), surface));
                hoverAfter[mode]=std::min(hoverAfter[mode], contrastRatio(hover, surface));
                placeholderAfter[mode]=std::min(placeholderAfter[mode], contrastRatio(placeholder, surface));
                mutedOnPlaceholder[mode]=std::min(mutedOnPlaceholder[mode], contrastRatio(muted, placeholder));
                ++variants;
            }
        }
        QCOMPARE(variants, 80);
        for(int mode:{0, 1})
            qInfo("%s: border min %.2f -> controlBorder %.2f..%.2f; hover/surface min %.2f -> %.2f; placeholder/surface min %.2f; muted/placeholder min %.2f",
                  mode ? "dark" : "light", borderBefore[mode], borderAfterMin[mode], borderAfterMax[mode], hoverBefore[mode], hoverAfter[mode],
                  placeholderAfter[mode], mutedOnPlaceholder[mode]);
    }

    // The supplied catalog hover is one fixed blue-grey for all 40 families (UX audit finding 19); the derived fill
    // follows each family, so warm and neutral families get their own.
    void hoverFollowsTheFamily() {
        ThemeManager manager;
        for(bool dark:{false, true}) {
            const auto slate=manager.preview("slate-ice", dark), graphite=manager.preview("graphite", dark), parchment=manager.preview("parchment", dark);
            QCOMPARE(slate.value("hover"), graphite.value("hover")); // the supplied role really is shared
            QVERIFY(slate.value("familyHover")!=graphite.value("familyHover"));
            QVERIFY(graphite.value("familyHover")!=parchment.value("familyHover"));
            const QColor neutral=role(graphite, "familyHover");
            QCOMPARE(neutral.red(), neutral.blue()); // graphite is grey, so its fill carries no blue cast
        }
    }

    void raiseToContrastStopsAtTheFirstPassingMix() {
        using namespace glancelot::theme;
        const QColor white(255, 255, 255), black(0, 0, 0);
        const auto raised=raiseToContrast(QColor(0xDF, 0xE5, 0xED), black, {white}, 3.0);
        QVERIFY(contrastRatio(raised, white)>=3.0);
        // One step less would not pass, so the boundary is no darker than it needs to be.
        bool earlierPasses=false;
        for(int step=0; step<mixSteps; ++step) {
            const auto candidate=mix(QColor(0xDF, 0xE5, 0xED), black, double(step)/mixSteps);
            if(candidate==raised) break;
            if(contrastRatio(candidate, white)>=3.0) earlierPasses=true;
        }
        QVERIFY(!earlierPasses);
        QCOMPARE(raiseToContrast(black, white, {black}, 30.0), white); // unreachable: ends at the target colour
        QVERIFY(withDerivedRoles({{"background", "#FFFFFF"}}, false).value("controlBorder").isNull()); // incomplete palette unchanged
    }

    // The desktop's high-contrast state reaches colors() and is followed while the app runs.
    void systemHighContrastIsFollowedLive() {
        FakeContrast fake;
        ThemeManager manager;
        QSignalSpy changed(&manager, &ThemeManager::changed);
        QCOMPARE(manager.colors().value("systemHighContrast").toBool(), false);
        fake.set({true, std::nullopt, "test"});
        QCOMPARE(changed.count(), 1);
        QCOMPARE(manager.colors().value("systemHighContrast").toBool(), true);
        fake.set({true, std::nullopt, "test"}); // same reading: no notification
        QCOMPARE(changed.count(), 1);
        fake.set({});
        QCOMPARE(changed.count(), 2);
        QCOMPARE(manager.colors().value("systemHighContrast").toBool(), false);
        QVERIFY(!QFile::exists(QDir(config->path()).filePath("settings.json"))); // following the desktop writes nothing
    }

    // A Windows high-contrast theme states its polarity; "Follow system" mode uses it, an explicit mode wins.
    void systemHighContrastPolarityAppliesInFollowSystemModeOnly() {
        FakeContrast fake;
        ThemeManager manager;
        QCOMPARE(manager.mode(), QString("system"));
        fake.set({true, false, "test"});
        QVERIFY(!manager.dark());
        QCOMPARE(manager.colors().value("background"), manager.preview(manager.family(), false).value("background"));
        fake.set({true, true, "test"});
        QVERIFY(manager.dark());
        manager.setMode("light");
        QVERIFY(!manager.dark());
        manager.setMode("system");
        fake.set({true, std::nullopt, "test"}); // polarity unknown (GNOME): the colour scheme decides, as before
        QCOMPARE(manager.dark(), qGuiApp->styleHints()->colorScheme()!=Qt::ColorScheme::Light);
    }

    // Every manager has its own watch, and a manager that is gone is never called.
    void watchesEndWithTheirManager() {
        FakeContrast fake;
        auto first=std::make_unique<ThemeManager>();
        ThemeManager second;
        QSignalSpy secondChanged(&second, &ThemeManager::changed);
        first.reset();
        fake.set({true, std::nullopt, "test"});
        QCOMPARE(secondChanged.count(), 1);
        QVERIFY(glancelot::systemContrastOf(&second).high);
        QVERIFY(!glancelot::systemContrastOf(nullptr).high);
    }

    void gtkThemeNamesAreRecognised() {
        using glancelot::contrast_detail::fromGtkThemeName;
        QVERIFY(!fromGtkThemeName("Adwaita", "gtk-theme"));
        QVERIFY(!fromGtkThemeName("", "GTK_THEME"));
        const auto plain=fromGtkThemeName("HighContrast", "gtk-theme");
        QVERIFY(plain && plain->high && plain->dark==false);
        const auto inverse=fromGtkThemeName("HighContrastInverse", "gtk-theme");
        QVERIFY(inverse && inverse->dark==true);
        const auto variant=fromGtkThemeName("highcontrast:dark", "GTK_THEME");
        QVERIFY(variant && variant->dark==true && variant->source=="GTK_THEME");
    }

    // The native reading runs on this desktop without crashing; offscreen platforms report no desktop.
    void nativeReadingIsSafeHere() {
        QVERIFY(!glancelot::SystemContrastWatch::nativeAllowed()); // QT_QPA_PLATFORM=offscreen
        glancelot::SystemContrastWatch watch(nullptr, {});
        QVERIFY(!watch.state().high);
        watch.refresh();
#ifdef Q_OS_WIN
        const auto reading=glancelot::contrast_detail::probeWindows();
        QVERIFY(!reading.high || reading.dark.has_value());
#endif
    }

    // --window-size is clamped to the screen's available area and the 540x360 minimum, and says so on stderr.
    void windowSizeIsClampedToTheScreen() {
        const auto huge=launch({"--window-size", "99999x99999"});
        QVERIFY2(huge.finished, qPrintable(huge.errors));
        QCOMPARE(huge.exitCode, 0);
        const auto window=huge.report.value("window").toObject();
        // QT_QPA_PLATFORM=offscreen has one screen; the report records its available area.
        const auto available=window.value("screen").toObject().value("available").toObject();
        QVERIFY(available.value("width").toInt()>=540 && available.value("height").toInt()>=360);
        QVERIFY2(window.value("width").toInt()==available.value("width").toInt() && window.value("height").toInt()==available.value("height").toInt(),
                 qPrintable(QJsonDocument(window).toJson(QJsonDocument::Compact)));
        QVERIFY(window.value("width").toInt()>=540 && window.value("height").toInt()>=360);
        QVERIFY2(huge.errors.contains("--window-size 99999x99999 is outside"), qPrintable(huge.errors));
        qInfo("99999x99999 -> %dx%d in %lld ms", window.value("width").toInt(), window.value("height").toInt(), huge.elapsedMs);

        const auto small=launch({"--window-size", "100x50"});
        QVERIFY(small.finished); QCOMPARE(small.exitCode, 0);
        QCOMPARE(small.report.value("window").toObject().value("width").toInt(), 540);
        QCOMPARE(small.report.value("window").toObject().value("height").toInt(), 360);
        QVERIFY2(small.errors.contains("using 540x360"), qPrintable(small.errors));

        const auto fits=launch({"--window-size", "640x480"});
        QVERIFY(fits.finished); QCOMPARE(fits.exitCode, 0);
        QCOMPARE(fits.report.value("window").toObject().value("width").toInt(), 640);
        QCOMPARE(fits.report.value("window").toObject().value("height").toInt(), 480);
        QVERIFY2(!fits.errors.contains("--window-size"), qPrintable(fits.errors));
    }

    // Malformed sizes never become 0: the early parser (app/startup_intent.cpp) refuses them before any window exists,
    // with exit code 2 and its ArgumentError record on standard output.
    void malformedWindowSizeIsRefused() {
        for(const auto* value:{"0x0", "-5x400", "wide", "640x", "640x480x2"}) {
            const auto run=launch({"--window-size", QString::fromLatin1(value)});
            QVERIFY2(run.finished, value);
            QCOMPARE(run.exitCode, 2);
            QVERIFY2(run.output.contains("ArgumentError") && run.output.contains("Invalid window size."),
                     qPrintable(QString::fromLatin1(value)+": "+run.output));
            QVERIFY(run.report.isEmpty());
        }
    }
};

QTEST_MAIN(ThemeContrastTests)
#include "theme_contrast_tests.moc"
