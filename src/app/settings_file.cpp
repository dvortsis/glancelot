#include "glancelot/settings_file.hpp"
#include "file_lock.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRandomGenerator>
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

// The last-known-good copy, only when it is itself a real schema-1 file.
bool validBackup(const QString& path, QJsonObject& out) {
    QFile file(path + ".bak");
    if (!file.exists()) return false;
    return parseExisting(path + ".bak", out).ok() && !out.isEmpty();
}

// One atomic replacement. On Windows the rename can fail for a moment while another process has the file open,
// so a few jittered retries are part of a write, not an error.
bool replaceFile(const QString& path, const QByteArray& bytes) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        QSaveFile output(path);
        if (output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit()) return true;
        // Jittered: timer granularity can otherwise phase-lock a retry with a periodic foreign reader.
        QThread::msleep(quint32(10 + 15 * attempt + QRandomGenerator::global()->bounded(20)));
    }
    return false;
}
}

QString settingsDirectory() {
    const QString override = qEnvironmentVariable("GLANCELOT_CONFIG_DIR");
    return override.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : override;
}
QString settingsFilePath() { return QDir(settingsDirectory()).filePath("settings.json"); }

SettingsStatus readSettings(QJsonObject& out) {
    const QString path = settingsFilePath();
    // Deliberately lock-free: readers must never queue behind (or starve) writers; a replacement in flight is
    // handled by the bounded retry below and by the writer's own retry.
    SettingsStatus status;
    // Writers replace the file atomically, but on Windows an open can fail for a moment while the replacement
    // commits; retry only that case (a damaged file is not transient and must not stall startup).
    for (int attempt = 0; attempt < 8; ++attempt) {
        status = parseExisting(path, out);
        if (status.code != SettingsStatus::Unreadable) break;
        QThread::msleep(10);
    }
    if (status.code == SettingsStatus::Corrupt) {
        QJsonObject backup;
        if (validBackup(path, backup)) { out = backup; return {SettingsStatus::Ok, true}; }
    }
    return status;
}

SettingsStatus updateSettings(const std::function<void(QJsonObject&)>& edit) {
    const QString path = settingsFilePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return {SettingsStatus::WriteFailed};
    FileLock lock(path + ".lock");
    if (!lock.acquire(4000)) return {lock.contended() ? SettingsStatus::Busy : SettingsStatus::WriteFailed};
    QJsonObject root;
    auto status = parseExisting(path, root);
    bool recovered = false, primaryGood = status.ok();
    if (status.code == SettingsStatus::Corrupt && validBackup(path, root)) {
        // Keep the damaged bytes for inspection, then continue from the last known good settings.
        QFile::remove(path + ".damaged");
        QFile::copy(path, path + ".damaged");
        recovered = true; primaryGood = false; status = {};
    }
    if (!status.ok()) return status;
    const bool existed = QFileInfo::exists(path);
    edit(root);
    root["schema"] = 1;
    // Only a verified-good file becomes the last-known-good copy; a damaged one never overwrites it.
    if (existed && primaryGood) { QFile::remove(path + ".bak"); QFile::copy(path, path + ".bak"); }
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!replaceFile(path, bytes)) return {SettingsStatus::WriteFailed};
    return {SettingsStatus::Ok, recovered};
}

bool resetSettings(QString* movedTo) {
    const QString path = settingsFilePath();
    if (!QFileInfo::exists(path)) return true;
    FileLock lock(path + ".lock");
    if (!lock.acquire(4000)) return false;
    const QString target = path + ".reset-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    if (movedTo) *movedTo = target;
    return QFile::rename(path, target);
}
}
