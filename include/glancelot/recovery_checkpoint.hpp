#pragma once
#include "settings_recovery.hpp"
#include <QList>

namespace glancelot::recovery {
// F015 tabs: one saved folder/image location per tab, owned by value.
struct TabLocation {
    QString folder, image;
    bool operator==(const TabLocation&) const = default;
};
inline constexpr int MaxTabs = 8;
struct ResumeState {
    bool hasPosition{}, priorDirty{};
    QString folder, image, checkpointRevision;
    // Appended, defaulted: empty for a single location (an entry without tabs, or tab state that is malformed,
    // oversized or disagrees with folder/image). Otherwise 2..MaxTabs records and tabs[activeTab] == {folder, image}.
    QList<TabLocation> tabs;
    int activeTab{};
};
struct BeginResult {
    Status status;
    ResumeState previous; // captured before installing this claim's dirty marker
};
struct CheckpointInput {
    QString folder, image;
    quint64 sequence{}; // positive, monotonic within one claim; stale commits refuse
    // Appended, defaulted: fewer than two records save one location and remove stored tabs. Otherwise at most MaxTabs
    // records with tabs[activeTab] == {folder, image}, written in the same transaction as folder/image and the revision.
    QList<TabLocation> tabs;
    int activeTab{};
};
struct CheckpointResult {
    Status status;
    QString checkpointRevision;
    quint64 sequence{};
};
// One slot only; no saved-media scan. Sessions guard precedes settings lock.
// Legacy/unverifiable/stale identities cannot authorize any recovery mutation.
BeginResult beginWindow(const WindowClaim&, Options = {});
// Called only for accepted successful decode or directory navigation. Atomically
// merge this slot's known position fields AND a new dirty checkpoint reference.
CheckpointResult checkpoint(const WindowClaim&, const CheckpointInput&, Options = {});
// Clean only if the newest accepted checkpoint was persisted and still matches.
// Caller serializes one active/one latest pending operation with value ownership.
Status finishWindow(const WindowClaim&, const QString& expectedCheckpointRevision,
                    quint64 expectedSequence, Options = {});
}
