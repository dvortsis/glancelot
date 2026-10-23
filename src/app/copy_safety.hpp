#pragma once
#include <QString>
#include <QtGlobal>
#include <functional>

// COPY-SAFETY-01: Overwrite never destroys the existing target.
// CopyMoveService copies an Overwrite item into a uniquely named temporary file beside the target (same
// directory, so same volume), flushes, syncs and size-verifies it, and only then replaces the target
// atomically. Implemented in copy_move_service.cpp; internal to the app, not a shared contract.
namespace glancelot::copy_safety {

// Temporary name for `targetPath`: ".<target file name>.glancelot-<16 hex>.part" in the target's directory.
QString temporaryPathFor(const QString& targetPath);

// True when `fileName` (a bare name, no directory) has the temporaryPathFor() shape.
bool isTemporaryName(const QString& fileName);

// Atomically replaces (or creates) `targetPath` with `replacementPath`, which must be on the same volume.
// POSIX: rename(2), then a best-effort fsync of the directory. Windows: MoveFileExW with
// MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH. On failure neither file is changed and `error` says why.
bool replaceFileAtomically(const QString& replacementPath, const QString& targetPath, QString* error = nullptr);

// Removes leftover temporaries (isTemporaryName) in `directory` that no copy in this process is writing,
// e.g. after a crash. Never touches any other file. Returns how many it removed.
int removeStaleTemporaries(const QString& directory);

// Test-only fault injection. A hook returning true makes the copy fail at that point as if the operating
// system had reported an error. Called on the copy worker thread; set it before enqueueing and clear it
// after the batch finishes. Unset in production.
enum class FaultPoint {
    Read,        // before each chunk read of the source (bytes = source bytes read so far)
    Write,       // before each chunk write (bytes = bytes written so far)
    AfterChunk,  // after each chunk was written; returning true is ignored (use it to cancel or inspect)
    Sync,        // flush/fsync of the written temporary (simulates a full volume at flush)
    Replace,     // the atomic replace of the target (simulates access denied or a sharing violation)
};
using FaultHook = std::function<bool(FaultPoint point, const QString& path, qint64 bytes)>;
void setFaultHookForTesting(FaultHook hook);

} // namespace glancelot::copy_safety
