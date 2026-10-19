#include "glancelot/settings_file.hpp"
#include "file_lock.hpp"
#include <QDateTime>
#include <QCoreApplication>
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
// Interactive writes never sleep behind another process or retry a replacement.
bool interactiveThread() {
    const auto* app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}

SettingsStatus parseExisting(const QString& path, QJsonObject& out, QByteArray* original = nullptr) {
    out = {};
    QFile file(path);
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxSettingsBytes) return {SettingsStatus::Unreadable};
    QJsonParseError error;
    const auto bytes=file.read(kMaxSettingsBytes+1);
    if(file.error()!=QFileDevice::NoError || bytes.size()>kMaxSettingsBytes) return {SettingsStatus::Unreadable};
    if(original)*original=bytes;
    const auto document = QJsonDocument::fromJson(bytes, &error);
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
bool replaceFile(const QString& path, const QByteArray& bytes, bool retry) {
    for (int attempt = 0; attempt < (retry ? 10 : 1); ++attempt) {
        QSaveFile output(path);
        if (output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit()) return true;
        // Jittered: timer granularity can otherwise phase-lock a retry with a periodic foreign reader.
        if (retry) QThread::msleep(quint32(10 + 15 * attempt + QRandomGenerator::global()->bounded(20)));
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
        // A schema-1 file is never empty, so ok-and-empty means "absent". Once a writer has used this directory
        // (its lock file exists) absence can be the instant of a concurrent replacement on Linux; look again.
        const bool vanished = status.ok() && out.isEmpty() && QFileInfo::exists(path + ".lock");
        // A genuinely absent file (e.g. after a reset) must not cost the UI thread the full unreadable budget.
        if (status.code != SettingsStatus::Unreadable && !(vanished && attempt < 2)) break;
        QThread::msleep(10);
    }
    if (status.code == SettingsStatus::Corrupt) {
        QJsonObject backup;
        if (validBackup(path, backup)) { out = backup; return {SettingsStatus::Ok, true}; }
    }
    return status;
}

SettingsStatus updateSettings(const std::function<void(QJsonObject&)>& edit, bool waitOnExit) {
    const bool retry=waitOnExit || !interactiveThread();
    const QString path = settingsFilePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return {SettingsStatus::WriteFailed};
    FileLock lock(path + ".lock");
    if (!lock.acquire(retry ? 4000 : 0)) return {lock.contended() ? SettingsStatus::Busy : SettingsStatus::WriteFailed};
    QJsonObject root;
    QByteArray primaryBytes;
    auto status = parseExisting(path, root, &primaryBytes);
    bool recovered = false, primaryGood = status.ok();
    if (status.code == SettingsStatus::Corrupt && validBackup(path, root)) {
        // Preserve the damaged bytes atomically before replacing the primary.
        recovered = true; primaryGood = false; status = {};
    }
    if (!status.ok()) return status;
    const bool existed = QFileInfo::exists(path);
    edit(root);
    root["schema"] = 1;
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    // Never write a settings file that our own bounded reader cannot reopen.
    if(bytes.size()>kMaxSettingsBytes) return {SettingsStatus::WriteFailed};
    // Backup and recovery evidence have the same atomic replacement guarantee as the primary.
    // A failed backup must not silently discard the only recoverable copy.
    if(existed && primaryGood && !replaceFile(path+".bak",primaryBytes,retry)) return {SettingsStatus::WriteFailed};
    if(recovered && !replaceFile(path+".damaged",primaryBytes,retry)) return {SettingsStatus::WriteFailed};
    if (!replaceFile(path, bytes, retry)) return {SettingsStatus::WriteFailed};
    return {SettingsStatus::Ok, recovered};
}

bool resetSettings(QString* movedTo) {
    const QString path = settingsFilePath();
    if (!QFileInfo::exists(path)) return true;
    FileLock lock(path + ".lock");
    if (!lock.acquire(interactiveThread() ? 0 : 4000)) return false;
    const QString target = path + ".reset-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    if (movedTo) *movedTo = target;
    return QFile::rename(path, target);
}

bool exportSettings(const QString& targetFilePath, QString* error) {
    if (targetFilePath.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Target file path cannot be empty.");
        return false;
    }
    QJsonObject root;
    const auto status = readSettings(root);
    if (!status.ok()) {
        if (error) *error = QStringLiteral("Could not read settings for export.");
        return false;
    }
    QSaveFile saveFile(targetFilePath);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not create target export file.");
        return false;
    }
    root["schema"] = 1;
    saveFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!saveFile.commit()) {
        if (error) *error = QStringLiteral("Could not write export file atomically.");
        return false;
    }
    return true;
}

bool importSettings(const QString& sourceFilePath, QString* error) {
    QFile file(sourceFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Could not open settings file for import.");
        return false;
    }
    const auto bytes = file.readAll();
    if (bytes.size() > kMaxSettingsBytes) {
        if (error) *error = QStringLiteral("Import file exceeds maximum settings size.");
        return false;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(bytes, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        if (error) *error = QStringLiteral("Import file contains invalid JSON.");
        return false;
    }
    const auto imported = doc.object();
    const int schema = imported.value(QStringLiteral("schema")).toInt(1);
    if (schema > 1) {
        if (error) *error = QStringLiteral("Import file has an unsupported newer schema.");
        return false;
    }

    const auto status = updateSettings([&](QJsonObject& root) {
        for (auto it = imported.begin(); it != imported.end(); ++it) {
            if (it.key() == QStringLiteral("schema")) continue;
            root[it.key()] = it.value();
        }
    });

    if (!status.ok()) {
        if (error) *error = QStringLiteral("Could not save imported settings.");
        return false;
    }
    return true;
}

bool resetSection(const QString& sectionName, QString* error) {
    const QString sec = sectionName.trimmed().toLower();
    const auto status = updateSettings([&](QJsonObject& root) {
        if (sec == QStringLiteral("appearance")) {
            root.remove(QStringLiteral("appearance"));
            root.remove(QStringLiteral("textScale"));
            root.remove(QStringLiteral("reducedMotion"));
        } else if (sec == QStringLiteral("viewing")) {
            root.remove(QStringLiteral("boundary"));
            root.remove(QStringLiteral("wheel"));
            root.remove(QStringLiteral("nearest"));
            root.remove(QStringLiteral("backdrop"));
        } else if (sec == QStringLiteral("browsing")) {
            root.remove(QStringLiteral("filmstrip"));
            root.remove(QStringLiteral("tree"));
            root.remove(QStringLiteral("treePinned"));
            root.remove(QStringLiteral("sort"));
        } else if (sec == QStringLiteral("panes")) {
            root.remove(QStringLiteral("layouts"));
        } else if (sec == QStringLiteral("bindings")) {
            root.remove(QStringLiteral("bindings"));
        } else if (sec == QStringLiteral("all")) {
            root.remove(QStringLiteral("appearance"));
            root.remove(QStringLiteral("textScale"));
            root.remove(QStringLiteral("reducedMotion"));
            root.remove(QStringLiteral("boundary"));
            root.remove(QStringLiteral("wheel"));
            root.remove(QStringLiteral("nearest"));
            root.remove(QStringLiteral("backdrop"));
            root.remove(QStringLiteral("filmstrip"));
            root.remove(QStringLiteral("tree"));
            root.remove(QStringLiteral("treePinned"));
            root.remove(QStringLiteral("sort"));
            root.remove(QStringLiteral("layouts"));
            root.remove(QStringLiteral("bindings"));
        }
    });

    if (!status.ok()) {
        if (error) *error = QStringLiteral("Could not reset settings section.");
        return false;
    }
    return true;
}
}

