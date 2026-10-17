#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>

// The single writer for settings.json (schema 1), shared by every window process and every subsystem.
// Contract (frozen by the lead; change only through the lead):
//  * updateSettings() holds a cross-process lock, re-reads the file, lets `edit` change only its own keys, then
//    replaces the file atomically (last-known-good copy kept in settings.json.bak). Keys it does not touch,
//    including unknown ones, are preserved; a concurrent window's changes are never lost.
//  * A file that cannot be read or has another schema is never written (Unreadable / Unsupported). A damaged
//    file (Corrupt) is recovered from the last-known-good copy when that copy is valid: the damaged bytes are kept
//    as settings.json.damaged, `recovered` is set, and the edit applies on top of the restored settings.
//    Without a valid copy it is also left untouched until resetSettings() sets it aside (--reset-settings).
//  * readSettings() takes no lock: replacement is atomic, so a reader sees the old or the new file, never a mix.
//  * Interactive writes never wait for a lock or retry replacements; transient failures can be retried later.
//  * Busy means the lock stayed contended past the timeout; WriteFailed covers every other write failure.
namespace glancelot {
struct SettingsStatus {
    enum Code { Ok, Unreadable, Corrupt, Unsupported, Busy, WriteFailed };
    Code code{Ok};
    bool recovered{false};
    bool ok() const { return code == Ok; }
};
QString settingsDirectory();   // GLANCELOT_CONFIG_DIR, otherwise the OS application-config location
QString settingsFilePath();
// A missing file is Ok with an empty object.
SettingsStatus readSettings(QJsonObject& out);
// waitOnExit permits bounded lock/replacement retries during final teardown only.
SettingsStatus updateSettings(const std::function<void(QJsonObject&)>& edit, bool waitOnExit = false);
// Safe-mode reset: renames settings.json to settings.json.reset-<timestamp> (never deletes it). True when there
// was nothing to set aside or the rename succeeded.
bool resetSettings(QString* movedTo = nullptr);
}
