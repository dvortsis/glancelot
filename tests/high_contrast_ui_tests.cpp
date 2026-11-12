// E6 (F052/F053): the Theme singleton follows the desktop's high-contrast setting, the theme picker overrides it, and the
// derived contrast tokens reach the interface. Loads the shipped ui/Theme.qml and ui/ThemePicker.qml.
#include "glancelot/theme_manager.hpp"
#include "glancelot/viewer_controller.hpp"
#include "system_contrast.hpp"
#include "theme_contrast.hpp"
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using glancelot::SystemContrast;
using glancelot::theme::contrastRatio;

namespace {
struct FakeContrast {
    SystemContrast state;
    FakeContrast() { glancelot::setSystemContrastProbeForTesting([this] { return state; }); }
    ~FakeContrast() { glancelot::setSystemContrastProbeForTesting({}); }
    void set(SystemContrast next) { state=std::move(next); glancelot::setSystemContrastProbeForTesting([this] { return state; }); }
};

const char* const pickerWindow=R"(
import QtQuick
import QtQuick.Window
import Glancelot
Window {
    width: 640; height: 760; visible: true
    color: Theme.surface
    Flickable {
        anchors.fill: parent; anchors.margins: Theme.gapXl
        contentHeight: picker.implicitHeight
        ThemePicker { id: picker; objectName: "picker"; width: parent.width }
    }
})";
}

class HighContrastUiTests final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> config;
    struct Harness {
        ViewerController viewer;
        ThemeManager theme;
        QQmlApplicationEngine engine;
        std::unique_ptr<QQuickWindow> window;
        QObject* tokens=nullptr;
        Harness() {
            auto imports=engine.importPathList(); imports.removeAll(QCoreApplication::applicationDirPath()); engine.setImportPathList(imports);
            engine.rootContext()->setContextProperty("viewer", &viewer);
            engine.rootContext()->setContextProperty("theme", &theme);
            QQmlComponent component(&engine);
            component.setData(pickerWindow, QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/E6PickerHarness.qml")));
            window.reset(qobject_cast<QQuickWindow*>(component.create()));
            if(!window) qWarning().noquote()<<component.errorString();
            tokens=engine.singletonInstance<QObject*>("Glancelot", "Theme");
        }
        QColor token(const char* name) const { return tokens->property(name).value<QColor>(); }
        template<class T=QQuickItem> T* named(const char* name) const { return window->findChild<T*>(QString::fromLatin1(name)); }
    };
    static QColor colorOf(const QVariantMap& palette, const char* name) { return QColor(palette.value(QLatin1String(name)).toString()); }
    static QString evidence() { return qEnvironmentVariable("GLANCELOT_E6_UI_EVIDENCE"); }
    static void shot(Harness& h, const QString& name) {
        if(evidence().isEmpty()) return;
        QDir().mkpath(evidence());
        QCoreApplication::processEvents();
        QVERIFY(h.window->grabWindow().save(QDir(evidence()).filePath(name+".png")));
    }
    static void click(Harness& h, QQuickItem* item) {
        QTest::mouseClick(h.window.get(), Qt::LeftButton, {}, item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
    }
    static void checkHighContrastPairs(Harness& h, const QString& context) {
        const struct { const char* a; const char* b; double min; } pairs[]={
            {"text", "background", 7}, {"text", "surface", 7}, {"muted", "surface", 7}, {"border", "background", 7},
            {"controlBorder", "background", 7}, {"controlBorder", "surface", 7}, {"text", "placeholder", 7},
            {"muted", "placeholder", 4.5}, {"focusRing", "surface", 7}, {"textOnPrimary", "primary", 7}};
        for(const auto& pair:pairs) {
            const double ratio=contrastRatio(h.token(pair.a), h.token(pair.b));
            QVERIFY2(ratio>=pair.min, qPrintable(QString("%1 %2/%3 %4").arg(context, pair.a, pair.b).arg(ratio, 0, 'f', 2)));
        }
        QCOMPARE(h.token("controlBorder"), h.token("border"));
        QCOMPARE(h.token("placeholder"), h.token("hover"));
        QVERIFY(h.tokens->property("borderWidth").toInt()>=2);
    }
private slots:
    void initTestCase() {
        for(const auto& font:QDir(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/assets/fonts/geist")).entryList({"*.ttf"}, QDir::Files))
            QFontDatabase::addApplicationFont(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/assets/fonts/geist/")+font);
        QQuickStyle::setStyle("Basic");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")), "Glancelot", 1, 0, "Theme");
    }
    void init() { config=std::make_unique<QTemporaryDir>(); qputenv("GLANCELOT_CONFIG_DIR", config->path().toUtf8()); }
    void cleanup() { qunsetenv("GLANCELOT_CONFIG_DIR"); config.reset(); }

    // F052/F053: turning the desktop's high contrast on and off while the app runs switches the tokens both ways, says so
    // in the picker, and writes no preference.
    void systemHighContrastIsFollowedWhileRunning() {
        FakeContrast fake;
        Harness h; QVERIFY(h.window); QVERIFY(h.tokens);
        QTRY_VERIFY_WITH_TIMEOUT(h.window->isExposed(), 10000);
        auto* note=h.named("highContrastPaletteNote"); QVERIFY(note);
        QVERIFY(!h.tokens->property("highContrast").toBool());
        QVERIFY(!note->isVisible());
        for(const QString mode:{QString("dark"), QString("light")}) {
            h.theme.setMode(mode);
            const QColor standardText=h.token("text");
            fake.set({true, std::nullopt, "test"});
            QTRY_VERIFY_WITH_TIMEOUT(h.tokens->property("highContrast").toBool(), 10000);
            QVERIFY(h.tokens->property("systemHighContrast").toBool());
            QCOMPARE(h.token("text"), QColor(mode=="dark" ? "#ffffff" : "#000000"));
            QVERIFY(h.token("text")!=standardText);
            checkHighContrastPairs(h, "system " + mode);
            QTRY_VERIFY_WITH_TIMEOUT(note->isVisible(), 10000);
            shot(h, "theme-picker-system-high-contrast-"+mode);
            fake.set({});
            QTRY_VERIFY_WITH_TIMEOUT(!h.tokens->property("highContrast").toBool(), 10000);
            QCOMPARE(h.token("text"), standardText);
            QTRY_VERIFY_WITH_TIMEOUT(!note->isVisible(), 10000);
        }
        QVERIFY(!h.viewer.highContrast());
        QFile settings(QDir(config->path()).filePath("settings.json"));
        if(settings.open(QIODevice::ReadOnly)) QVERIFY(!QJsonDocument::fromJson(settings.readAll()).object().contains("highContrast"));
    }

    // Windows reports whether its high-contrast theme is black or white; "Follow system" mode takes that polarity.
    void systemHighContrastPolarityPicksTheTokenSet() {
        FakeContrast fake;
        Harness h; QVERIFY(h.window);
        QCOMPARE(h.theme.mode(), QString("system"));
        fake.set({true, true, "test"});
        QTRY_COMPARE_WITH_TIMEOUT(h.token("background"), QColor("#000000"), 10000);
        fake.set({true, false, "test"});
        QTRY_COMPARE_WITH_TIMEOUT(h.token("background"), QColor("#ffffff"), 10000);
        QVERIFY(!h.tokens->property("dark").toBool());
    }

    // The picker overrides the desktop: "Always on" keeps high contrast with the desktop off and persists it;
    // "Follow system" hands control back. Keyboard and pointer both work.
    void pickerAlwaysOnOverridesAndFollowSystemReturns() {
        FakeContrast fake;
        Harness h; QVERIFY(h.window);
        h.window->requestActivate(); QTRY_VERIFY_WITH_TIMEOUT(h.window->isExposed(), 10000);
        auto* follow=h.named("highContrastFollowSystem"); auto* always=h.named("highContrastAlwaysOn");
        QVERIFY(follow && always);
        QVERIFY(follow->property("selected").toBool()); QVERIFY(!always->property("selected").toBool());
        click(h, always);
        QTRY_VERIFY_WITH_TIMEOUT(h.viewer.highContrast(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(h.tokens->property("highContrast").toBool(), 10000);
        QVERIFY(always->property("selected").toBool()); QVERIFY(!follow->property("selected").toBool());
        QVERIFY(!h.tokens->property("systemHighContrast").toBool());
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            QFile file(QDir(config->path()).filePath("settings.json"));
            return file.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(file.readAll()).object().value("highContrast").toBool();
        }(), 10000);
        // A desktop change while "Always on" is chosen leaves high contrast on.
        fake.set({true, std::nullopt, "test"}); fake.set({});
        QVERIFY(h.tokens->property("highContrast").toBool());
        follow->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(h.window.get(), Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(!h.viewer.highContrast(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!h.tokens->property("highContrast").toBool(), 10000);
        QVERIFY(follow->property("selected").toBool());
        // With the desktop on, "Follow system" is high contrast again.
        fake.set({true, std::nullopt, "test"});
        QTRY_VERIFY_WITH_TIMEOUT(h.tokens->property("highContrast").toBool(), 10000);
        QVERIFY(follow->property("selected").toBool());
    }

    // The contrast tokens as the interface draws them, in every family and mode: brand separators unchanged,
    // control boundaries at 3:1, family-tinted hover and placeholder fills.
    void contrastTokensReachTheInterfaceInEveryFamily() {
        Harness h; QVERIFY(h.window);
        int variants=0;
        for(const auto& entry:h.theme.families()) {
            const auto id=entry.toMap().value("id").toString();
            for(bool dark:{false, true}) {
                h.theme.setFamily(id); h.theme.setMode(dark ? "dark" : "light"); QCoreApplication::processEvents();
                const auto palette=h.theme.preview(id, dark);
                const auto context=id+(dark ? " dark" : " light");
                QVERIFY2(h.token("border")==colorOf(palette, "border"), qPrintable(context+" separator changed"));
                QVERIFY2(h.token("controlBorder")==colorOf(palette, "controlBorder"), qPrintable(context));
                QVERIFY2(h.token("hover")==colorOf(palette, "familyHover"), qPrintable(context));
                QVERIFY2(h.token("placeholder")==colorOf(palette, "placeholder"), qPrintable(context));
                QVERIFY2(contrastRatio(h.token("controlBorder"), h.token("background"))>=3.0, qPrintable(context));
                QVERIFY2(contrastRatio(h.token("controlBorder"), h.token("surface"))>=3.0, qPrintable(context));
                QVERIFY2(contrastRatio(h.token("text"), h.token("pressed"))>=4.5, qPrintable(context+" text/pressed"));
                QVERIFY2(contrastRatio(h.token("muted"), h.token("placeholder"))>=4.5, qPrintable(context+" muted/placeholder"));
                ++variants;
            }
        }
        QCOMPARE(variants, 80);
    }

    // Palette cards and their swatches use the 3:1 boundary of the family they show.
    void paletteCardsUseTheFamilysControlBorder() {
        Harness h; QVERIFY(h.window);
        h.theme.setFamily("slate-ice"); h.theme.setMode("light");
        auto* grid=h.named("paletteGrid"); QVERIFY(grid);
        QTRY_VERIFY_WITH_TIMEOUT(grid->property("count").toInt()==40, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(h.window->isExposed(), 10000);
        QQuickItem* card=nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, card), Q_ARG(int, 1)) && card, 10000);
        const auto family=card->property("modelData").toMap().value("id").toString();
        QVERIFY(family!="slate-ice");
        auto* frame=card->childItems().value(0); QVERIFY(frame);
        auto* border=frame->property("border").value<QObject*>(); QVERIFY(border);
        const auto expected=colorOf(h.theme.preview(family, false), "controlBorder");
        QCOMPARE(border->property("color").value<QColor>(), expected);
        QVERIFY(contrastRatio(expected, colorOf(h.theme.preview(family, false), "background"))>=3.0);
        shot(h, "theme-picker-light");
        h.theme.setMode("dark");
        QTRY_COMPARE_WITH_TIMEOUT(border->property("color").value<QColor>(), colorOf(h.theme.preview(family, true), "controlBorder"), 10000);
        shot(h, "theme-picker-dark");
        h.viewer.setHighContrast(true);
        QTRY_VERIFY_WITH_TIMEOUT(h.tokens->property("highContrast").toBool(), 10000);
        shot(h, "theme-picker-always-high-contrast-dark");
        h.theme.setMode("light");
        shot(h, "theme-picker-always-high-contrast-light");
        h.viewer.setHighContrast(false);
    }
};

QTEST_MAIN(HighContrastUiTests)
#include "high_contrast_ui_tests.moc"
