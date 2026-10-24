#pragma once
#include <QString>
#include <QtGlobal>
#include <functional>

// COPY-SAFETY-01: Overwrite never destroys the existing target.
// CopyMoveService copies an Overwrite item into a uniquely named temporary file beside the target (same
// directory, so same volume), flushes, syncs and size-verifies it, and only then replaces the target
// atomically. Implemented in copy_move_service.cpp; internal to the app, not a shared contract.
namespace glancelot::copy_safety {

// Temporary name for `targetPath`: ".~gl<8 hex>.tmp" in the target's directory. Short and independent of the
// target's name, so a target with a long name or a long path can still be replaced.
QString temporaryPathFor(const QString& targetPath);

// True when `fileName` (a bare name, no directory) has the temporaryPathFor() shape. Only these names are
// swept as crash leftovers; the replace backups (".~gb<8 hex>.tmp") never are.
bool isTemporaryName(const QString& fileName);

// Whether two paths name the same file, by file identity rather than by path text, so aliases (subst drive
// letters, junctions, symbolic links, folder-mounted volumes, bind mounts) and hard links are recognised.
// Windows: volume serial number plus file ID (FILE_ID_INFO, else BY_HANDLE_FILE_INFORMATION) from handles
// opened with backup semantics. POSIX: st_dev plus st_ino. Symbolic links are followed.
// Unknown: either file cannot be opened, or both are on one volume that reports no usable file ID.
enum class FileIdentityMatch { Different, Same, Unknown };
FileIdentityMatch compareFileIdentity(const QString& pathA, const QString& pathB);

// Replaces (or creates) `targetPath` with `replacementPath`, which must be on the same volume. The result keeps
// the replaced target's protection:
// - Windows: ReplaceFileW with a backup name, which keeps the target's attributes (hidden, system,
//   not-indexed), its DACL, alternate data streams, creation time and short name. A partial failure that
//   left the target under the backup name is undone. Only when the volume does not support ReplaceFileW
//   (ERROR_INVALID_FUNCTION, ERROR_NOT_SUPPORTED, ERROR_INVALID_PARAMETER, ERROR_CALL_NOT_IMPLEMENTED)
//   does it fall back to MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) after copying the
//   hidden, system and not-indexed attributes; that fallback refuses a target with explicit or protected
//   permissions rather than loosen them, and loses streams, creation time, short name and object ID.
// - POSIX: the replacement takes the target's permission bits (rwx only, never set-id or sticky), then
//   rename(2) and a best-effort fsync of the directory.
// Paths longer than MAX_PATH are passed to Windows with the \\?\ prefix.
// On failure the target is unchanged and `error` says why. `originalDisplaced` is set only if Windows left the
// original under the backup name and it could not be moved back; `error` then names that file.
bool replaceFileAtomically(const QString& replacementPath, const QString& targetPath, QString* error = nullptr,
                           bool* originalDisplaced = nullptr);

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
    // Windows only, inside replaceFileAtomically:
    ReplaceUnsupported,  // ReplaceFileW reports ERROR_NOT_SUPPORTED, so the MoveFileExW fallback runs
    ReplacePartial,      // ReplaceFileW renamed the target to its backup name, then failed (error 1177)
    RestoreBackup,       // moving that backup back to the target name fails too
};
using FaultHook = std::function<bool(FaultPoint point, const QString& path, qint64 bytes)>;
void setFaultHookForTesting(FaultHook hook);

} // namespace glancelot::copy_safety
