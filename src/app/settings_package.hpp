#pragma once
#include "glancelot/settings_recovery.hpp"
#include <functional>

namespace glancelot::recovery::package {
struct Data {
    RawMember primary,lastGood;
    QByteArray manifest;
    QString revision;
};
struct ReadResult {Status status;std::optional<Data> data;};
ReadResult read(const QString& directory,Options = {});
CaptureResult write(const RawSnapshot&,const QString& destination,Options = {});
enum class Stage { DirectoryCreated, PrimaryWritten, BackupWritten, BeforeSeal, AfterSeal };
#ifdef GLANCELOT_RECOVERY_TESTING
struct Hooks {std::function<void(Stage)> observe;std::function<bool(Stage)> fail;};
inline thread_local const Hooks* hooks{};
#endif
}
