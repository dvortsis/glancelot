#pragma once
#include "glancelot/settings_file.hpp"
class QObject;

namespace glancelot::settings_detail {
// Interactive, value-owned edits only: never capture a stack reference or an
// owner in edit. Immediate writes retain ordinary writer semantics. Busy writes
// retry without waiting on the GUI thread, for at most four seconds; terminal
// completion runs on the owner's thread. One latest value per channel, at most
// 128 channels per owner. Destruction makes a bounded final batch attempt using
// only the owned edits, without calling a dying owner's completion. A true return means
// committed OR accepted for retry, not a durability acknowledgement.
// Superseding a queued channel intentionally discards its obsolete completion;
// consumers must not await a callback for every submission. Teardown also never
// invokes completions. Read overlays are for value-owned preference edits only.
bool saveWithRetry(QObject* owner,QString channel,std::function<void(QJsonObject&)> edit,
                   std::function<void(SettingsStatus)> completed = {});
void overlayPendingSettings(QObject* owner,QJsonObject& settings);
bool hasPendingSettings(QObject* owner,const QString& channelPrefix = {});
// Explicit successful import/reset supersedes pending edits in its owned scope.
void cancelPendingSettings(QObject* owner,const QString& channelPrefix = {});
}
