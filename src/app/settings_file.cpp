#include "glancelot/settings_file.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

namespace glancelot {
namespace {
constexpr qint64 kMaxSettingsBytes = 1024 * 1024;

SettingsStatus parseExisting(const QString& path, QJsonObject& out) {
    out = {};
    QFile file(path);
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxSettingsBytes) return {SettingsStatus::Unreadable};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {SettingsStatus::Corrupt};
    const auto object = document.object();
    if (object.value("schema").toInt() != 1) return {SettingsStatus::Unsupported};
    out = object;
    return {};
}
}

QString settingsDirectory() {
    const QString override = qEnvironmentVariable("GLANCELOT_CONFIG_DIR");
    return override.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : override;
}
QString settingsFilePath() { return QDir(settingsDirectory()).filePath("settings.json"); }

SettingsStatus readSettings(QJsonObject& out) {
    // Writers replace the file atomically, but on Windows an open can fail for a moment while the replacement
    // commits. A short bounded retry keeps a startup read from reporting "unreadable" for a healthy file.
    SettingsStatus status;
    for (int attempt = 0; attempt < 10; ++attempt) {
        status = parseExisting(settingsFilePath(), out);
        if (status.code != SettingsStatus::Unreadable && status.code != SettingsStatus::Corrupt) break;
        QThread::msleep(10);
    }
    return status;
}

SettingsStatus updateSettings(const std::function<void(QJsonObject&)>& edit) {
    const QString path = settingsFilePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return {SettingsStatus::WriteFailed};
    QLockFile lock(path + ".lock");
    lock.setStaleLockTime(15000); // a crashed holder's lock is reclaimed by pid/age
    if (!lock.tryLock(4000)) return {SettingsStatus::Busy};
    QJsonObject root;
    const auto status = parseExisting(path, root);
    if (!status.ok()) return status;
    const bool existed = QFileInfo::exists(path);
    edit(root);
    root["schema"] = 1;
    if (existed) { QFile::remove(path + ".bak"); QFile::copy(path, path + ".bak"); }
    QSaveFile output(path);
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) return {SettingsStatus::WriteFailed};
    return {};
}
}
