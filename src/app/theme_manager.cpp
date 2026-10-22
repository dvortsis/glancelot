#include "glancelot/theme_manager.hpp"
#include <QCoreApplication>
#include "glancelot/settings_file.hpp"
#include "settings_retry.hpp"
#include <memory>
#include <QGuiApplication>
#include <QStyleHints>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>

namespace {
QString settingsPath() {
    QString directory=qEnvironmentVariable("GLANCELOT_CONFIG_DIR");
    if(directory.isEmpty())directory=QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(directory).filePath("settings.json");
}
}
ThemeManager::ThemeManager(QObject* parent):QObject(parent) {
    QFile catalog(":/themes/catalog.json");
    if(catalog.open(QIODevice::ReadOnly)) m_families=QJsonDocument::fromJson(catalog.readAll()).array().toVariantList();
    QJsonObject root;
    if(glancelot::readSettings(root).ok()) {
        if(root.value("schema").toInt()==1) {
            const auto appearance=root.value("appearance").toObject();
            const auto selected=appearance.value("family").toString();
            if(!preview(selected,true).isEmpty())m_family=selected;
            const auto mode=appearance.value("mode").toString();
            if(mode=="system"||mode=="dark"||mode=="light")m_mode=mode;
        }
    }
    if(qGuiApp)connect(qGuiApp->styleHints(),&QStyleHints::colorSchemeChanged,this,[this]{if(m_mode=="system")emit changed();});
}
bool ThemeManager::dark() const {
    return m_mode=="dark" || (m_mode=="system" && (!qGuiApp || qGuiApp->styleHints()->colorScheme()!=Qt::ColorScheme::Light));
}
QVariantMap ThemeManager::preview(const QString& family,bool isDark) const {
    for(const auto& row:m_families) {const auto value=row.toMap();if(value.value("id").toString()==family)return value.value(isDark?"dark":"light").toMap();}
    return {};
}
QVariantMap ThemeManager::colors() const {return preview(m_family,dark());}
void ThemeManager::setFamily(const QString& family) {if(family==m_family||preview(family,true).isEmpty())return;m_family=family;save("family");emit changed();}
void ThemeManager::setMode(const QString& mode) {if(mode==m_mode||(mode!="system"&&mode!="dark"&&mode!="light"))return;m_mode=mode;save("mode");emit changed();}
void ThemeManager::save(const char* key) {
    m_error.clear();
    // Only the appearance key is written, through the shared cross-process writer that preserves everything else.
    const auto field=QString::fromLatin1(key);
    const auto value=field=="family"?m_family:m_mode;
    const auto deferred=std::make_shared<bool>(false);
    glancelot::settings_detail::saveWithRetry(this,"appearance/"+field,[field,value](QJsonObject& root){
        auto appearance=root.value("appearance").toObject();
        // Only the changed key is written, so another window changing the other one is not reverted.
        appearance[field]=value;
        root["appearance"]=appearance;
    },[this,deferred](glancelot::SettingsStatus status){
        m_error.clear();
        if(status.code==glancelot::SettingsStatus::Unreadable)m_error=QCoreApplication::translate("Theme","Preferences could not be read; theme applies for this session.");
        else if(status.code==glancelot::SettingsStatus::Corrupt||status.code==glancelot::SettingsStatus::Unsupported)m_error=QCoreApplication::translate("Theme","Unsupported preferences; theme applies for this session.");
        else if(!status.ok())m_error=QCoreApplication::translate("Theme","Theme applies for this session; preferences could not be saved.");
        if(*deferred)emit changed();
    });
    *deferred=true;
}
