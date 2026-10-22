#pragma once
#include "glancelot/settings_recovery.hpp"

namespace glancelot::recovery::raw {
Status resolve(const Context&, Options, QString& directory, QString& namespaceId);
RawMember classify(QByteArray bytes);
QString revision(const RawMember&, const RawMember&);
// Caller holds settings.json.lock for this resolved namespace. No writes and
// no last-good substitution. Both members must be readable within their bounds.
SnapshotResult readLocked(const QString& directory, const QString& namespaceId, Options);
}
