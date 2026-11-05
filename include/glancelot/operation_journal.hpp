#pragma once
#include "file_operations.hpp" // FileIdentity, TargetKind
#include <QString>
#include <QtGlobal>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

// Lead-frozen OPERATION-JOURNAL (docs/contracts/OPERATION-JOURNAL.md). Implemented by packet B2. Worker-thread only; never UI
// or render code. One OperationJournal per process over a per-profile directory shared by all viewer processes. An intent is
// durable before its mutation starts.
namespace glancelot::journal {
inline constexpr std::size_t kMaxItemsPerOperation = 100000;
inline constexpr std::size_t kMaxIntentBatch = 64;        // items made durable per fsync
inline constexpr qint64 kMaxOperationFileBytes = 256LL << 20;  // room for kMaxItemsPerOperation at ordinary path lengths
inline constexpr qint64 kMaxJournalBytes = 1LL << 30;     // records and preimages; completed operations pruned oldest first
inline constexpr int kMaxRetainedOperations = 200;
inline constexpr int kSchemaVersion = 1;
inline constexpr int kMaxBackupFindings = 256;            // per reconcile pass; more are reported as a count

enum class Kind : std::uint8_t { Trash, Copy, Move, Rename, CreateFolder, SidecarWrite, LosslessTransform, BatchRename };
// Whether an operation may run when the journal cannot be written. Operations that work without a journal today keep working,
// with an explicit notice and no undo. Features that exist only because of the journal refuse.
constexpr bool requiresJournal(Kind kind) {
    return kind == Kind::SidecarWrite || kind == Kind::LosslessTransform || kind == Kind::BatchRename;
}
enum class Outcome : std::uint8_t {
    Pending, Done, Skipped, Missing, Changed, Denied, InUse, TrashUnavailable, Collision,
    Cancelled, Failed,
    Unknown,           // intent durable, no outcome: interrupted; resolved only by reconcile()
    OriginalDisplaced  // Windows Overwrite: the target name is empty and the original is at ItemIntent::backupPath
};
enum class UndoCapability : std::uint32_t {
    None = 0,
    RestoreFromTrash = 1u << 0,   // trash token recorded and still present at undo time
    ReverseRename = 1u << 1,
    ReverseMove = 1u << 2,        // same-volume rename or verified copy back
    TrashCopy = 1u << 3,          // undo a copy by trashing the created file (never permanent)
    RestorePreimage = 1u << 4,    // sidecar or transform: sealed preimage retained in the journal
};
enum class State : std::uint8_t { Open, Running, Paused, Interrupted, Completed, Cancelled, Failed };

struct OperationId { quint64 value{}; bool operator==(const OperationId&) const = default; };
struct TargetSnapshot {
    QString path;                   // absolute, as shown to the user
    glancelot::TargetKind kind{glancelot::TargetKind::File};
    bool link{};
    glancelot::FileIdentity identity;
    qint64 bytes{}, modified{};
};
struct ItemIntent {
    quint32 index{};                // 0-based, stable, < kMaxItemsPerOperation
    Kind kind{Kind::Trash};
    TargetSnapshot source;
    QString target;                 // destination path; empty for Trash
    QString partialPath;            // temporary copy being written (".~gl<8 hex>.tmp" beside the target); never holds an original
    // Where the original sits while an item replaces or renames it, durable before the step starts, so reconcile can find it.
    // Overwrite: the existing target is `overwritten` and this is ".~gb<8 hex>.tmp" beside it. Case-only rename: `source` is the
    // original and this is the intermediate ".tmp_rename_<id>". Empty when the item makes no such file.
    std::optional<TargetSnapshot> overwritten;
    QString backupPath;
    QString replacedPath;           // Overwrite: "<stem> (replaced)<ext>", the name the original takes on its way to the trash
    // SidecarWrite, LosslessTransform: Operation::preimagePathFor(index), inside the journal directory. Empty when there is no
    // prior file (a new sidecar). Operation::sealPreimage fills it before the item may touch the original.
    QString preimagePath;
    QString requestedBy;            // ASSET-GROUP: the requested file that pulled this companion in
};
struct ItemOutcome {
    quint32 index{};
    Outcome outcome{Outcome::Failed};
    glancelot::FileIdentity resultIdentity;   // the created or moved file, for undo revalidation
    QString trashToken;             // POSIX: path in the trash; Windows: Recycle Bin item parsing name; empty if none
    QString overwrittenTrashToken;  // Overwrite: where the replaced original went; empty when it was not trashed
    std::uint32_t undo{};           // UndoCapability bits valid for this item
    QString message;
};
struct OperationHeader {
    Kind kind{Kind::Trash};
    QString origin;                 // "grid" | "tree" | "canvas" | "command" | "drop"
    QString scope;                  // ASSET-GROUP "file" | "asset"
    QString destination;            // copy or move directory, if any
    bool permanentReplaceConsented{};    // the user confirmed that replaced files are permanently deleted here
    quint64 totalPathBytes{};       // UTF-8 length of every path in the plan, so begin can refuse an operation that can't fit
    std::optional<OperationId> undoOf;   // an undo is itself a journaled operation
    quint32 itemCount{};
};
struct OperationSummary {
    OperationId id; OperationHeader header; State state{State::Open};
    quint32 done{}, failed{}, unknown{}, notStarted{}, displaced{};
    quint32 heldBackups{};          // items whose original still sits at backupPath or replacedPath
    bool reconciled{};              // a reconcile pass has resolved this operation; durable
    std::uint32_t undoAvailable{};  // intersection over Done items; None if any Done item has none
    quint64 revision{};             // increments on every durable change; UI drops older revisions
    qint64 createdMs{}, updatedMs{};
};
enum class Code : std::uint8_t { Ok, Busy, Unwritable, Corrupt, LimitExceeded, Conflict, Cancelled };
struct Status { Code code{Code::Corrupt}; QString detail; };

// One backup found at startup that reconcile did not restore. Reported in the operation center; only explicit user actions
// touch it, and neither action can delete it permanently.
enum class BackupState : std::uint8_t {
    Restored,        // moved back to the empty target name; reported once for the record
    TargetOccupied,  // the target name holds a different file: the backup is an extra copy of the old original
    IdentityMismatch,// the file at backupPath is not the recorded original: never moved automatically
    RestoreFailed,   // the target name is free and the identity matched, but the move back failed; message says why
    TrashFailed      // an Overwrite's replaced original couldn't reach the trash; it stays where the finding says
};
struct BackupFinding {
    OperationId operation; quint32 index{};
    QString backupPath, targetPath;
    BackupState state{BackupState::TargetOccupied};
    QString message;
};

class Operation;   // move-only handle; holds the per-operation cross-process FileLock
class OperationJournal {
public:
    // Null with Status when the directory cannot be used. Callers then apply requiresJournal(): see the contract.
    static std::unique_ptr<OperationJournal> open(const QString& directory, Status* status);
    ~OperationJournal();
    // Plan step, before the confirmation: a synced scratch append, so a confirmation can say the journal is unavailable.
    Status probe();
    // Durable header (fsync) before any intent. Null with Status on failure; the caller applies requiresJournal().
    std::unique_ptr<Operation> begin(const OperationHeader& header, Status* status);
    std::vector<OperationSummary> list(std::size_t max = kMaxRetainedOperations) const;
    struct ItemRecord { ItemIntent intent; std::optional<ItemOutcome> outcome; };
    // One page, at most min(max, kMaxIntentBatch) records from `first`; empty past the end. Conflict for an unknown id. A Corrupt
    // operation returns its good prefix with Status Corrupt.
    std::vector<ItemRecord> items(OperationId operation, quint32 first, std::size_t max, Status* status) const;
    // Interrupted operations not yet reconciled whose lock is free (owner process gone), and any operation with heldBackups or
    // displaced items. Probes the filesystem with exact stats; never re-executes; resolves Unknown items to done or not started
    // with evidence and records the pass durably.
    struct Reconciled {
        OperationId id;
        quint32 resolvedDone{}, notStarted{}, partialsRemoved{}, backupsRestored{}, stillUnknown{};
    };
    struct ReconcileReport {
        std::vector<Reconciled> operations;
        std::vector<BackupFinding> backups;   // at most kMaxBackupFindings
        quint32 backupsNotListed{};
    };
    ReconcileReport reconcile(std::stop_token cancel = {});
    // Explicit user actions on a finding. Restore never replaces an existing file; trash goes through TrashBackend.
    Status restoreBackup(OperationId operation, quint32 index);
    Status trashBackup(OperationId operation, quint32 index);
    Status dismiss(OperationId operation);    // completed or reconciled only; Conflict while a backup is held; pruning skips those too
private:
    OperationJournal();
    struct Private;
    std::unique_ptr<Private> d;
};
class Operation {
public:
    ~Operation();                   // without a terminal state the record stays Interrupted
    Operation(Operation&&) noexcept;
    Operation& operator=(Operation&&) noexcept;
    OperationId id() const;
    // Append up to kMaxIntentBatch intents and fsync. Mutate only items whose intent returned Ok.
    Status recordIntents(const std::vector<ItemIntent>& intents);
    QString preimagePathFor(quint32 index) const;
    // After recordIntents, before the item touches its original: copies the source to the intent's preimagePath, syncs it and
    // records its checksum. LimitExceeded when it can't fit after pruning; the item then must not run.
    Status sealPreimage(quint32 index);
    // Append outcomes; fsync at the end of each batch and on state changes.
    Status recordOutcomes(const std::vector<ItemOutcome>& outcomes);
    Status setState(State state);   // Paused, Running, Cancelled, Completed or Failed; terminal states are final
private:
    friend class OperationJournal;
    Operation();
    struct Private;
    std::unique_ptr<Private> d;
};
}
