#include "glancelot/theme_manager.hpp"
#include "glancelot/viewer_controller.hpp"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cmath>

namespace {
double luminance(const QColor& color) {
    const auto channel=[](double value) { return value<=0.04045 ? value/12.92 : std::pow((value+0.055)/1.055,2.4); };
    return 0.2126*channel(color.redF())+0.7152*channel(color.greenF())+0.0722*channel(color.blueF());
}
double contrast(const QColor& a,const QColor& b) {
    const auto x=luminance(a),y=luminance(b);
    return (std::max(x,y)+0.05)/(std::min(x,y)+0.05);
}
QByteArray read(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool write(const QString& path,const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size();
}
}

class ThemeTests final : public QObject {
    Q_OBJECT
private slots:
    void init() { originalConfig=qgetenv("GLANCELOT_CONFIG_DIR"); hadConfig=qEnvironmentVariableIsSet("GLANCELOT_CONFIG_DIR"); }
    void cleanup() { if(hadConfig)qputenv("GLANCELOT_CONFIG_DIR",originalConfig);else qunsetenv("GLANCELOT_CONFIG_DIR"); }
    void allCatalogVariantsAreValidAndLegible();
    void slateIceMatchesUserPalette();
    void invalidSelectionsDoNotChangePreferences();
    void themeAndControlsPreserveEachOther();
    void futureSchemaRemainsByteIdentical();
    void corruptPreferencesRemainByteIdentical();
    void unavailableConfigIsSessionOnly();
private:
    QByteArray originalConfig;
    bool hadConfig{};
};

void ThemeTests::allCatalogVariantsAreValidAndLegible() {
    QTemporaryDir dir; QVERIFY(dir.isValid()); qputenv("GLANCELOT_CONFIG_DIR",dir.path().toUtf8());
    ThemeManager manager;
    QCOMPARE(manager.families().size(),40);
    QSet<QString> ids,names;
    const QStringList roles{"background","surface","text","primary","accent","border","muted","hover","onPrimary","danger"};
    for(const auto& entry:manager.families()) {
        const auto family=entry.toMap();
        const auto id=family.value("id").toString(),name=family.value("name").toString();
        QVERIFY(!id.isEmpty()); QVERIFY(!name.isEmpty()); QVERIFY(!ids.contains(id)); QVERIFY(!names.contains(name));
        ids.insert(id); names.insert(name);
        for(bool dark:{false,true}) {
            const auto palette=manager.preview(id,dark);
            for(const auto& role:roles) {
                const QColor value(palette.value(role).toString());
                QVERIFY2(value.isValid()&&value.alpha()==255,qPrintable(id+" invalid/transparent "+role));
            }
            const auto check=[&](const QString& a,const QString& b) {
                const auto ratio=contrast(QColor(palette.value(a).toString()),QColor(palette.value(b).toString()));
                return ratio>=4.5;
            };
            const auto context=id+(dark?" dark ":" light ");
            QVERIFY2(check("text","background"),qPrintable(context+"text/background below 4.5:1"));
            QVERIFY2(check("text","surface"),qPrintable(context+"text/surface below 4.5:1"));
            QVERIFY2(check("muted","surface"),qPrintable(context+"muted/surface below 4.5:1"));
            QVERIFY2(check("onPrimary","primary"),qPrintable(context+"onPrimary/primary below 4.5:1"));
        }
    }
}

void ThemeTests::slateIceMatchesUserPalette() {
    QTemporaryDir dir; QVERIFY(dir.isValid()); qputenv("GLANCELOT_CONFIG_DIR",dir.path().toUtf8());
    const auto bytes=read(QString::fromUtf8(GLANCELOT_TEST_SOURCE_DIR)+"/assets/brand/palette.json");
    QVERIFY(!bytes.isEmpty());
    const auto source=QJsonDocument::fromJson(bytes).object(); QVERIFY(!source.isEmpty());
    ThemeManager manager;
    const QStringList roles{"background","surface","text","primary","accent","border"};
    for(bool dark:{false,true})for(const auto& role:roles) {
        const auto palette=manager.preview("slate-ice",dark);
        const auto expected=source.value((dark?"dark_":"light_")+role).toString(); QVERIFY(!expected.isEmpty());
        QCOMPARE(QColor(palette.value(role).toString()),QColor(expected));
    }
    QCOMPARE(manager.family(),QString("slate-ice")); QCOMPARE(manager.mode(),QString("system"));
}

void ThemeTests::invalidSelectionsDoNotChangePreferences() {
    QTemporaryDir dir; QVERIFY(dir.isValid()); qputenv("GLANCELOT_CONFIG_DIR",dir.path().toUtf8());
    ThemeManager manager; QSignalSpy changed(&manager,&ThemeManager::changed);
    manager.setFamily("unknown-theme"); manager.setMode("sepia");
    QCOMPARE(manager.family(),QString("slate-ice")); QCOMPARE(manager.mode(),QString("system"));
    QVERIFY(manager.preview("unknown-theme",false).isEmpty()); QCOMPARE(changed.count(),0);
    QVERIFY(!QFile::exists(dir.filePath("settings.json")));
}

void ThemeTests::themeAndControlsPreserveEachOther() {
    QTemporaryDir dir; QVERIFY(dir.isValid()); qputenv("GLANCELOT_CONFIG_DIR",dir.path().toUtf8());
    const auto path=dir.filePath("settings.json");
    QVERIFY(write(path,R"({"schema":1,"futurePreference":{"retained":true},"appearance":{"futureColorSetting":"retained"},"bindings":{"open":"Ctrl+Alt+O"},"wheel":"pan","boundary":"loop"})"));
    ThemeManager theme; ViewerController controls;
    QCOMPARE(controls.bindingFor("open"),QString("Ctrl+Alt+O"));
    theme.setFamily("graphite"); theme.setMode("light"); QVERIFY(theme.error().isEmpty());
    auto saved=QJsonDocument::fromJson(read(path)).object();
    QCOMPARE(saved.value("bindings").toObject().value("open").toString(),QString("Ctrl+Alt+O"));
    QVERIFY(saved.value("futurePreference").toObject().value("retained").toBool());
    QCOMPARE(saved.value("appearance").toObject().value("futureColorSetting").toString(),QString("retained"));
    QVERIFY(controls.setBinding("next","Alt+Right")); controls.setWheelMode("zoom");
    theme.setMode("dark");
    ViewerController reloadedControls; ThemeManager reloadedTheme;
    QCOMPARE(reloadedControls.bindingFor("next"),QString("Alt+Right"));
    QCOMPARE(reloadedControls.wheelMode(),QString("zoom")); QCOMPARE(reloadedControls.boundaryMode(),QString("loop"));
    QCOMPARE(reloadedTheme.family(),QString("graphite")); QCOMPARE(reloadedTheme.mode(),QString("dark"));
    QVERIFY(QJsonDocument::fromJson(read(path)).object().value("futurePreference").toObject().value("retained").toBool());
    QCOMPARE(QJsonDocument::fromJson(read(path)).object().value("appearance").toObject().value("futureColorSetting").toString(),QString("retained"));
}

void ThemeTests::futureSchemaRemainsByteIdentical() {
    QTemporaryDir dir; QVERIFY(dir.isValid()); qputenv("GLANCELOT_CONFIG_DIR",dir.path().toUtf8());
    const auto path=dir.filePath("settings.json");
    const QByteArray bytes=R"({"schema":99,"appearance":{"family":"graphite","mode":"light"},"bindings":{"open":"Ctrl+Alt+O"},"future":[1,2,3]})";
    QVERIFY(write(path,bytes));
    ThemeManager theme; QCOMPARE(theme.family(),QString("slate-ice"));
    theme.setFamily("graphite"); theme.setMode("light");
    QCOMPARE(theme.family(),QString("graphite")); QVERIFY(!theme.error().isEmpty()); QCOMPARE(read(path),bytes);
    ViewerController controls; QVERIFY(controls.setBinding("next","Alt+Right")); QCOMPARE(read(path),bytes);
}

void ThemeTests::corruptPreferencesRemainByteIdentical() {
    QTemporaryDir dir; QVERIFY(dir.isValid()); qputenv("GLANCELOT_CONFIG_DIR",dir.path().toUtf8());
    const auto path=dir.filePath("settings.json"); const QByteArray bytes="{broken preferences";
    QVERIFY(write(path,bytes)); ThemeManager theme; theme.setMode("light");
    QCOMPARE(theme.mode(),QString("light")); QVERIFY(!theme.error().isEmpty()); QCOMPARE(read(path),bytes);
}

void ThemeTests::unavailableConfigIsSessionOnly() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path=dir.filePath("occupied"); const QByteArray bytes="keep this file"; QVERIFY(write(path,bytes));
    qputenv("GLANCELOT_CONFIG_DIR",path.toUtf8());
    ThemeManager theme; theme.setFamily("graphite");
    QCOMPARE(theme.family(),QString("graphite")); QVERIFY(!theme.error().isEmpty()); QCOMPARE(read(path),bytes);
}

QTEST_MAIN(ThemeTests)
#include "theme_tests.moc"
