# OPERATION-JOURNAL contract proposal (draft for the lead)

Status: **draft, not frozen**. Proposed header: `include/glancelot/operation_journal.hpp`. Base: `origin/main` at `3893c77`.

## 1. Purpose and consumers

A durable record of every user-initiated mutation. For each operation it records the **exact targets** (snapshotted identity), the **intent** before each mutation and the **outcome** after. It states **undo capability** per item honestly, and lets a restarted viewer **reconcile** an interrupted operation instead of guessing. Mutation services today report results only in memory (`FileOperationService::finished`, `CopyMoveService::itemFinished`, `MutationService::renamePath`). After a crash nothing is known.

Consumers: **B2** (journal, operation center replacing TransferHud, pause/cancel/retry, restart reconciliation; all mutations journaled), **B1** (records the asset-expanded list), **B4** batch rename (restart recovery), **B5** manifests, **B6** drag/drop, **C1** XMP sidecar writes (preimage), **D7** lossless JPEG transforms. LEAD-PLAN section 7: the journal (B2) lands before batch rename, drag/drop and sidecar writes.

## 2. Proposed header

```cpp
#pragma once
#include "file_operations.hpp"   // FileIdentity, TargetKind
#include <QString>
#include <QtGlobal>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace glancelot::journal {
// Lead-frozen OPERATION-JOURNAL. Worker-thread only; never UI/render code. One OperationJournal per
// process over a per-profile directory shared by all viewer processes. Durable before mutation.
inline constexpr std::size_t kMaxItemsPerOperation = 100000;
inline constexpr std::size_t kMaxIntentBatch = 64;        // items made durable per fsync
inline constexpr qint64 kMaxOperationFileBytes = 64LL << 20;
inline constexpr qint64 kMaxJournalBytes = 256LL << 20;   // completed operations pruned oldest first
inline constexpr int kMaxRetainedOperations = 200;
inline constexpr int kSchemaVersion = 1;

enum class Kind : std::uint8_t { Trash, Copy, Move, Rename, CreateFolder, SidecarWrite, LosslessTransform };
enum class Outcome : std::uint8_t {
    Pending, Done, Skipped, Missing, Changed, Denied, InUse, TrashUnavailable, Collision,
    Cancelled, Failed,
    Unknown            // intent durable, no outcome: interrupted; resolved only by reconcile()
};
enum class UndoCapability : std::uint32_t {
    None = 0,
    RestoreFromTrash = 1u << 0,   // trash location token recorded and still present at undo time
    ReverseRename = 1u << 1,
    ReverseMove = 1u << 2,        // same-volume rename or verified copy back
    TrashCopy = 1u << 3,          // undo a copy by trashing the created file (never permanent)
    RestorePreimage = 1u << 4,    // sidecar/transform: sealed preimage retained in the journal
};
enum class State : std::uint8_t { Open, Running, Paused, Interrupted, Completed, Cancelled, Failed };

struct OperationId { quint64 value{}; bool operator==(const OperationId&) const = default; };
struct TargetSnapshot {
    QString path;                   // absolute, as shown to the user
    TargetKind kind{TargetKind::File};
    bool link{};
    glancelot::FileIdentity identity;
    qint64 bytes{}, modified{};
};
struct ItemIntent {
    quint32 index{};                // 0-based, stable, < kMaxItemsPerOperation
    Kind kind{Kind::Trash};
    TargetSnapshot source;
    QString target;                 // destination path; empty for Trash
    QString partialPath;            // journal-owned temporary name for copies/writes, if any
    QString requestedBy;            // ASSET-GROUP: requested file that pulled this companion in
};
struct ItemOutcome {
    quint32 index{};
    Outcome outcome{Outcome::Failed};
    glancelot::FileIdentity resultIdentity;   // created/moved file, for undo revalidation
    QString trashToken;             // Linux: path in trash after QFile::moveToTrash; Windows: the Recycle
                                    // Bin item from IFileOperationProgressSink::PostDeleteItem; empty if none
    QString preimagePath;           // RestorePreimage only, inside the journal directory
    std::uint32_t undo{};           // UndoCapability bits valid for this item
    QString message;
};
struct OperationHeader {
    Kind kind{Kind::Trash};
    QString origin;                 // "grid" | "tree" | "canvas" | "command" | "drop" | "rename-batch"
    QString scope;                  // ASSET-GROUP "file" | "asset"
    QString destination;            // copy/move directory, if any
    std::optional<OperationId> undoOf;   // an undo is itself a journaled operation
    quint32 itemCount{};
};
struct OperationSummary {
    OperationId id; OperationHeader header; State state{State::Open};
    quint32 done{}, failed{}, unknown{}, notStarted{};
    std::uint32_t undoAvailable{};  // intersection over Done items; None if any Done item has none
    quint64 revision{};             // increments on every durable change; UI drops older revisions
    qint64 createdMs{}, updatedMs{};
};
enum class Code : std::uint8_t { Ok, Busy, Unwritable, Corrupt, LimitExceeded, Conflict, Cancelled };
struct Status { Code code{Code::Corrupt}; QString detail; };

class Operation;   // move-only handle; holds the per-operation cross-process FileLock
class OperationJournal {
public:
    static std::unique_ptr<OperationJournal> open(const QString& directory, Status* status);
    // Durable header (fsync) before any intent; Unwritable/LimitExceeded refuse the operation.
    std::unique_ptr<Operation> begin(const OperationHeader&, Status* status);
    std::vector<OperationSummary> list(std::size_t max = kMaxRetainedOperations) const;
    // Interrupted operations whose lock is free (owner process gone). Probes the filesystem; never
    // re-executes; resolves Unknown items to Done/NotStarted/Partial with evidence.
    struct Reconciled { OperationId id; quint32 resolvedDone{}, notStarted{}, partialsRemoved{}, stillUnknown{}; };
    std::vector<Reconciled> reconcile(std::stop_token cancel = {});
    Status dismiss(OperationId);    // completed/reconciled only; pruning never drops Interrupted
};
class Operation {
public:
    OperationId id() const;
    // Append up to kMaxIntentBatch intents and fsync. Mutate only items whose intent returned Ok.
    Status recordIntents(const std::vector<ItemIntent>&);
    // Append outcomes; fsync at the end of each batch and on state changes.
    Status recordOutcomes(const std::vector<ItemOutcome>&);
    Status setState(State);         // Paused/Running/Cancelled/Completed/Failed; terminal states are final
    ~Operation();                   // without a terminal state the record stays Interrupted
};
}
```

## 3. Durable format

- **Location:** `<settings directory>/journal/`, beside `settings.json` and `sessions.json`. That is the same explicit-namespace rule as `recovery::Context`, so redirected environment variables are never re-read.
- **One file per operation:** `op-<16 hex id>.gjl`, JSON Lines, append-only. Line 1 is the header record `{"v":1,"type":"header",...}`. Later lines are `intent`, `outcome` and `state` records. Each line ends with `"crc":"<first 16 hex of SHA-256 of the line without crc>"`. The same SHA-256 helper is used by `settings_recovery`.
- **Atomicity:** each append is a single `write` of a full line followed by flush and fsync (`FlushFileBuffers` / `fsync`) at batch boundaries. A torn or failed-checksum **last** line is ignored and the file is truncated back to the last good line on the next open. A bad checksum anywhere else marks the operation `Corrupt`: it is shown, never acted on.
- **Index:** `journal/index.json` is a cache only, rewritten with `QSaveFile` (the `settings_file.cpp` pattern). It is rebuilt from the `.gjl` files when it is missing or mismatched.
- **Locks:** `journal/journal.lock` (`FileLock`, `src/app/file_lock.hpp`) serializes create/prune/index writes across processes. Each running operation holds `op-<id>.lock` for its lifetime. The OS releases it on crash, so "lock free and state not terminal" reliably means **interrupted**. There is no pid guessing.
- **IDs:** 64-bit, random high 32 bits plus a monotonic counter. They are never reused while a file with that id exists.
- **Bounds:** the `k*` constants above. If a write would exceed them, the operation is refused before any mutation (`LimitExceeded`).
- **Privacy:** paths stay local in the user's profile and are never exported. The F058 diagnostics export (E2) redacts them.

## 4. Protocol every mutation service follows

1. Plan (existing snapshot step, ASSET-GROUP expansion) → `begin(header)`.
2. For each batch of up to 64 items: `recordIntents`, then mutate exactly those items, then `recordOutcomes`.
3. Copies and sidecar or transform writes go to `partialPath`, a journal-owned name in the destination directory: `.glancelot-partial-<id>-<index>`. A verified copy is then renamed to `target` (the `MutationService` temporary-rename pattern). A partial file is the only thing reconcile may delete.
4. `setState(Completed | Cancelled | Failed)`. Cancellation is honoured between items. An item already handed to the OS completes and gets its real outcome.

**Undo capability rules (set per item, at outcome time):**

| Item | Undo | Never |
| --- | --- | --- |
| Trash, with a trash token | `RestoreFromTrash` | When the token is missing at undo time (trash emptied or expired, or the platform gave no token): `Outcome::Missing`, reported, no fallback |
| Rename, same-volume move | `ReverseRename` / `ReverseMove` | When the moved file's identity no longer matches `resultIdentity` → `Changed` |
| Cross-volume move | `ReverseMove` (verified copy back, then the moved copy goes to trash, never to permanent delete) | When the source volume is gone |
| Copy | `TrashCopy` | Never a permanent delete of the copy |
| Copy/move with **Overwrite** | `None`, unless B2 first moves the existing target to trash (then `RestoreFromTrash` for it) | Today's in-place truncate (`copy_move_service.cpp`, `WriteOnly|Truncate`), or the `QFile::remove(destPath)` after a failed overwrite, destroys the user's original: no undo, and B2 must replace it |
| Permanent deletion | — | There is no permanent-delete path. If one is ever added, its undo is `None` by contract |
| SidecarWrite, LosslessTransform | `RestorePreimage` while the preimage is retained | After pruning |

An undo is a new operation with `undoOf` set. It revalidates every item, and items that cannot be undone are reported, never forced. The operation center advertises undo only when `undoAvailable != None` (FEATURE-OPPORTUNITIES §14: undo is advertised only where it is supported).

**Restart reconciliation** (worker, after window claim, never blocking the UI):

- An `Unknown` item is probed with exact stats only. The source is checked against the snapshot identity, the target and partial for existence and size.
- Source intact and no target: **NotStarted**.
- Target present with the snapshot size, and for a move the source gone: **Done**. Its result identity is recorded.
- Partial present: the partial is removed and the item is **NotStarted**.
- Anything else stays **Unknown**, with evidence in `message`.
- Reconcile never retries. The operation center shows something like "Interrupted: 37 done, 61 not started, 2 unknown", with **Retry remaining** (a new operation over the NotStarted items, re-planned and re-confirmed) and **Dismiss**.

**Generation fencing.** `OperationSummary::revision` increments on each durable change, and UI models drop older revisions. Snapshot identities fence execution: a target that changed after confirmation is `Changed`, never mutated. Late outcomes from a cancelled worker are still recorded (that is the truth), but they cannot reopen a terminal state.

**Failure policy.** Fail closed: if the journal cannot be opened or written, the mutation does not start, and the UI says why. That covers a read-only profile, a full disk and contention past the timeout.

## 5. Migration without behaviour change

- B2 introduces the journal behind the existing services. `FileOperationService` writes a header at `confirm`, intents and outcomes around `executeDeletion` items (a progress callback hook already exists per item), and records `trashToken` once `TrashBackend` returns it. That needs one added field on `DeleteResult`, which is a lead-approved diff in B2.
- Results, signals and wording stay identical. The journal adds records, not behaviour. The only intentional changes are fail-closed when the journal is unwritable and the Overwrite fix. Both are B2 acceptance items that need lead sign-off.
- `CopyMoveService` and `MutationService` adopt the protocol in B2. TransferHud is replaced by the operation center over `list()` in the same packet.
- Recovery precedent reused: the explicit directory namespace, `FileLock`, SHA-256 integrity, `QSaveFile` for the index, and "a sealed preimage precedes replacement" (`recovery::apply`) for sidecar and transform writes.

## 6. Test plan (`tests/operation_journal_tests.cpp`; fake backends, generated files, temp profile)

- **Format:** round-trip of each record type; torn last line ignored and truncated; checksum failure in the middle marks the operation Corrupt and leaves it untouched; unknown future `v` is read-only; oversized file and too many items are refused before mutation.
- **Durability ordering:** test hooks kill the worker between intent and mutation, between mutation and outcome, and mid-copy. Reconcile yields NotStarted, Done and NotStarted (partial removed) respectively.
- **Restart reconciliation:** a separate process holds the op lock, so the operation is not reconciled (owner alive); the lock is released by process exit, so it is reconciled; two processes reconcile at once and only one wins `journal.lock`; a source replaced after the crash stays Unknown with evidence.
- **Partial failure:** a 10-item move with item 4 denied and item 7 in use. Per-item outcomes are recorded, the summary counts are right, and undo is offered for the Done items only.
- **Undo:** restore from the trash with a fake token; token gone (expired trash) gives Missing and no fallback; renamed back; copy trashed, never removed; the moved file changed after the move gives Changed and is skipped; Overwrite without a trashed preimage advertises no undo.
- **Cancellation:** cancel between batches, so no intent is recorded beyond the batch; cancel during a copy, so the partial is removed and the original target is untouched.
- **Bounds and pruning:** the 201st completed operation prunes the oldest; Interrupted operations are never pruned; the `kMaxJournalBytes` cap holds.
- **Fail closed:** read-only journal directory and a full-disk simulation. The mutation is not attempted and the message is surfaced.

## 7. Open questions for the lead

1. **Fail closed versus proceed.** May the user choose "proceed without journal" for a single-file rename when the profile is read-only (portable mode on read-only media)? Proposal: no for batches; yes for a single item, behind explicit wording.
2. **Trash tokens.** Linux's `QFile::moveToTrash` yields the trashed path (plus a `.trashinfo`). On Windows the `RecycleOnlySink` in `file_operation_service.cpp` can capture the new Recycle Bin item in `PostDeleteItem`. Should Windows restore move that item back through `IFileOperation`, or stay unsupported (undo `None`) until it is qualified?
3. **Preimage retention.** Retain sidecar/transform preimages for the life of the operation record (proposed: bounded by `kMaxJournalBytes`), or for a fixed number of days?
4. **Overwrite.** Approve "move the existing target to trash first" as B2's mandatory fix, or remove the Overwrite policy from the UI until then?
5. **Intent batch size.** 64 items per fsync balances HDD and network latency against the replay window. Confirm, or make it storage-type aware through `IoScheduler`?

## 8. Non-goals

- No automatic retry or resume after restart. Every re-execution is re-planned and re-confirmed.
- No undo for permanent deletion, expired trash, overwrites without a preimage, or external changes.
- No journaling of view state, settings (recovery already covers those) or caches.
- No cross-machine sync, history UI beyond the operation center, or export of path lists.
- No transactional rollback across items: each item is atomic on its own, and the batch is not.
