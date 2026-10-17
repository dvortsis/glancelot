#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>

// The single writer for settings.json (schema 1), shared by every window process and every subsystem.
// Contract (frozen by the lead; change only through the lead):
//  * updateSettings() holds a cross-process lock, re-reads the file, lets `edit` change only its own keys, then
//    replaces the file atomically (last-known-good copy kept in settings.json.bak). Keys it does not touch,
//    including unknown ones, are preserved byte-for-value; a concurrent window's changes are never lost.
//  * A file that cannot be read, is not valid JSON, or has another schema is never written (Unreadable /
//    Corrupt / Unsupported); callers keep running with in-memory state.
//  * readSettings() takes no lock: replacement is atomic, so a reader sees the old or the new file, never a mix.
namespace glancelot {
struct SettingsStatus {
    enum Code { Ok, Unreadable, Corrupt, Unsupported, Busy, WriteFailed };
    Code code{Ok};
    bool ok() const { return code == Ok; }
};
QString settingsDirectory();   // GLANCELOT_CONFIG_DIR, otherwise the OS application-config location
QString settingsFilePath();
// A missing file is Ok with an empty object.
SettingsStatus readSettings(QJsonObject& out);
SettingsStatus updateSettings(const std::function<void(QJsonObject&)>& edit);
}
