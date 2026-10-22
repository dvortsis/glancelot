#pragma once
#include "settings_recovery.hpp"

namespace glancelot::recovery {
struct ResumeState {
    bool hasPosition{}, priorDirty{};
    QString folder, image, checkpointRevision;
};
struct BeginResult {
    Status status;
    ResumeState previous; // captured before installing this claim's dirty marker
};
struct CheckpointInput {
    QString folder, image;
    quint64 sequence{}; // positive, monotonic within one claim; stale commits refuse
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
