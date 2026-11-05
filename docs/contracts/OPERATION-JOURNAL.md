# OPERATION-JOURNAL contract

Status: **frozen** by PHASE2-CONTRACTS. Header: [`include/glancelot/operation_journal.hpp`](../../include/glancelot/operation_journal.hpp), the authoritative text of the interface. Code references are at `b1d57cd`. Owner: Track B. Implementing packet: B2 (F017, F062). Decisions and their alternatives: [2026-11-05-phase2-contracts.md](../decisions/2026-11-05-phase2-contracts.md).

## Purpose

A durable record of every user-initiated mutation. For each operation it records the **exact targets** (snapshotted identity), the **intent** before each mutation and the **outcome** after. It states **undo capability** per item honestly, and lets a restarted viewer **reconcile** an interrupted operation instead of guessing. Mutation services today report results only in memory (`FileOperationService::finished`, `CopyMoveService::itemFinished`, `MutationService::renamePath`). After a crash nothing is known.

## Consumers

**B2** (journal, operation center replacing TransferHud, pause, cancel, retry and restart reconciliation, backup recovery; all mutations journaled), **B1** (records the asset-expanded list), **B4** batch rename (restart recovery), **B5** manifests, **B6** drag/drop, **C1** XMP sidecar writes (preimage), **D7** lossless JPEG transforms. LEAD-PLAN section 7: the journal (B2) lands before batch rename, drag/drop and sidecar writes.

## Frozen interface

The header is frozen as committed: the `k*` bounds, `Kind`, `requiresJournal`, `Outcome`, `UndoCapability`, `State`, `OperationId`, `TargetSnapshot`, `ItemIntent`, `ItemOutcome`, `OperationHeader`, `OperationSummary`, `Code`, `Status`, `BackupState`, `BackupFinding`, `OperationJournal` and `Operation`.

**Applied by B2 (B2's brief grants these exact diffs).**

`include/glancelot/file_operations.hpp`, so a backend can hand back its trash location. Positional `DeleteResult{path, outcome, message}` stays valid:

```diff
-struct DeleteResult { std::filesystem::path path; DeleteOutcome outcome{DeleteOutcome::Failed}; std::string message; };
+struct DeleteResult { std::filesystem::path path; DeleteOutcome outcome{DeleteOutcome::Failed}; std::string message;
+                      std::string trashToken; };  // POSIX: path in the trash; Windows: Recycle Bin item parsing name; empty if none
```

`src/app/copy_safety.hpp` (Track B owned, not shared), so the backup name is chosen, and journaled, before the replace starts:

```diff
-bool replaceFileAtomically(const QString& replacementPath, const QString& targetPath, QString* error = nullptr,
-                           bool* originalDisplaced = nullptr);
+QString backupPathFor(const QString& targetPath);    // first free ".~gb<8 hex>.tmp" beside the target, or empty
+bool replaceFileAtomically(const QString& replacementPath, const QString& targetPath, QString* error = nullptr,
+                           bool* originalDisplaced = nullptr, const QString& backupPath = {});
```

## Durable format

- **Location:** `<settings directory>/journal/`, beside `settings.json` and `sessions.json`. The directory comes from an explicit context, like `recovery::Context` (`settings_recovery.hpp:40`), so redirected environment variables are never re-read.
- **One file per operation:** `op-<16 hex id>.gjl`, JSON Lines, append-only. Line 1 is the header record `{"v":1,"type":"header",...}`. Later lines are `intent`, `outcome` and `state` records. Each line ends with `"crc":"<first 16 hex of SHA-256 of the line without crc>"`, using `QCryptographicHash::Sha256`.
- **Atomicity:** each append is a single `write` of a full line followed by flush and fsync (`FlushFileBuffers` / `fsync`) at batch boundaries. A torn or failed-checksum **last** line is ignored and the file is truncated back to the last good line on the next open. A bad checksum anywhere else marks the operation `Corrupt`: it is shown, never acted on.
- **Index:** `journal/index.json` is a cache only, rewritten with `QSaveFile` (the `settings_file.cpp:61` pattern). It is rebuilt from the `.gjl` files when it is missing or mismatched.
- **Locks:** `journal/journal.lock` (`FileLock`, `src/app/file_lock.hpp`) serializes create, prune and index writes across processes. Each running operation holds `op-<id>.lock` for its lifetime. The OS releases it on crash, so "lock free and state not terminal" reliably means **interrupted**. There is no pid guessing.
- **IDs:** 64-bit, random high 32 bits plus a monotonic counter. They are never reused while a file with that id exists.
- **Bounds:** the `k*` constants. If a write would exceed them, the operation is refused before any mutation (`LimitExceeded`). `kMaxJournalBytes` includes preimages. A preimage that does not fit after pruning refuses that operation.
- **Privacy:** paths stay local in the user's profile and are never exported. The F058 diagnostics export (E2) redacts them.

## Protocol every mutation service follows

1. Plan (existing snapshot step, ASSET-GROUP expansion), then `begin(header)`.
2. For each batch of up to `kMaxIntentBatch` (64) items: `recordIntents`, then mutate exactly those items, then `recordOutcomes`. The batch size is fixed; a storage-aware size would need measurements and the lead.
3. Copies and sidecar or transform writes go to `partialPath`, the existing COPY-SAFETY-01 temporary `.~gl<8 hex>.tmp` beside the target (`copy_safety::temporaryPathFor`). A verified copy is then renamed or replaced into place. B2 moves non-Overwrite copies onto this path too, replacing the direct write at `copy_move_service.cpp:928` and the `QFile::remove(destPath)` at `:1014`. **A partial is the only file reconcile may delete, and only after the operation's lock is free.** The one other removal is an extra hard link to a file that stays in place, in the table below.
4. An item that holds an original under another name records that name in `backupPath` **before the step starts**: the Overwrite backup, and the `.tmp_rename_<id>` intermediate of a case-only rename (`mutation_service.cpp:182`). An item with a `backupPath` must not put an original in `partialPath`.
5. `setState(Completed | Cancelled | Failed)`.

**Undo capability rules (set per item, at outcome time):**

| Item | Undo | Never |
| --- | --- | --- |
| Trash, with a trash token | `RestoreFromTrash`. POSIX token: `QFile::fileName()` after `moveToTrash` (`file_operation_service.cpp:173`). Windows token: the Recycle Bin item from `PostDeleteItem` (`:83`). Advertised on a platform only if B2's round-trip test passes there: trash a generated file, restore by token, compare bytes and identity. Otherwise that platform records the token and advertises `None` | When the token is missing at undo time (trash emptied or expired, or the platform gave no token): `Outcome::Missing`, reported, no fallback |
| Rename, same-volume move | `ReverseRename` / `ReverseMove` | When the moved file's identity no longer matches `resultIdentity`: `Changed` |
| Cross-volume move | `ReverseMove` (verified copy back, then the moved copy goes to trash, never to permanent delete) | When the source volume is gone |
| Copy | `TrashCopy` | Never a permanent delete of the copy |
| Copy or move with **Overwrite** | `RestoreFromTrash` when the replaced original reached the trash (below): `overwrittenTrashToken`. Otherwise `None` | The undo first trashes the current target, then restores the old one. It never overwrites |
| Permanent deletion | — | There is no permanent-delete path. If one is ever added, its undo is `None` by contract |
| SidecarWrite, LosslessTransform | `RestorePreimage` while the preimage is retained, for the life of the operation record | After pruning |

An undo is a new operation with `undoOf` set. It revalidates every item, and items that cannot be undone are reported, never forced. The operation center advertises undo only when `undoAvailable != None` (FEATURE-OPPORTUNITIES section 13, Mutations: "Undo is advertised only when supported").

**Overwrite and the replaced original.** COPY-SAFETY-01 already keeps the original until a complete, verified copy replaces it. B2 adds:

- Windows: after `ReplaceFileW` succeeds, B2 moves the `.~gb` backup to the trash instead of calling `DeleteFileW` (`copy_move_service.cpp:444`). It first renames the backup to `<stem> (replaced)<ext>` on the first free name, so the Recycle Bin shows something recognisable; if that rename fails it trashes the file as it is.
- POSIX: `rename(2)` leaves no backup, so B2 first hard-links the target to a `.~gb<8 hex>.tmp` name, recorded as `backupPath`, then replaces, then trashes the link. A volume without hard links gets undo `None`, the same result as today.
- If the volume has no usable trash, the old copy is removed as it is today and the item's undo is `None`. The user asked to replace the file, and the confirmation already says the original goes away.

**Failure policy.** When the journal cannot be opened or written (read-only profile, full disk, contention past the timeout):

- `requiresJournal(kind)` is true (sidecar writes, lossless transforms, batch rename): the operation is refused and the UI says why. These exist only with the journal's protection.
- Otherwise (trash, copy, move, rename, create folder, including Overwrite under COPY-SAFETY-01's protection): the operation works as it does today. The confirmation adds "Glancelot can't record this change, so it can't be undone here and won't appear in history." This keeps read-only profiles and portable media working.
- Once an operation has begun, any failed `recordIntents` stops it before that batch mutates. Items already done stay recorded; the rest report `Cancelled` with the reason. It never continues unrecorded.

**Generation fencing.** `OperationSummary::revision` increments on each durable change, and UI models drop older revisions. Snapshot identities fence execution: a target that changed after confirmation is `Changed`, never mutated. Late outcomes from a cancelled worker are still recorded (that is the truth), but they cannot reopen a terminal state.

## Restart reconciliation (`reconcile`)

Runs on a worker after window claim, never blocking the UI, once per process start. It takes each operation that is interrupted (lock free, state not terminal) or that holds an `OriginalDisplaced` item.

**Unknown items**, probed with exact stats only:

- Source intact and no target: **not started**.
- Target present with the snapshot size, and for a move the source gone: **done**. Its result identity is recorded.
- Partial present: the partial is removed and the item is **not started**.
- Anything else stays **Unknown**, with evidence in `message`.
- Reconcile never retries. The operation center shows something like "Interrupted: 37 done, 61 not started, 2 unknown", with **Retry remaining** (a new operation over the not-started items, re-planned and re-confirmed) and **Dismiss**.

**Backups left by a crash or error 1177 inside `ReplaceFileW` (COPY-SAFETY-01 review N1, lead decision).** `ReplaceFileW` renames the target to the backup, then moves the replacement into place. A crash between the two, or a 1177 whose move back failed (`originalDisplaced`, `copy_move_service.cpp:990`), leaves the target name empty and the original in `.~gb<8 hex>.tmp`. That name is hidden from the grid, and the random name can only be found through the journal, which is why step 4 above is durable first. A non-crash 1177 records `Outcome::OriginalDisplaced` and is handled here on the next start, and in the operation center at once.

For each item with a `backupPath` that still exists, with the recorded original being `overwritten` when set and `source` otherwise:

| Target name | The file at `backupPath` | Action | Reported as |
| --- | --- | --- | --- |
| Empty | Identity and size match the recorded original (size and modified when the volume reports no identity) | Moved back with `MoveFileExW` and no replace flag, or `link` then `unlink` on POSIX, so it can never overwrite | `Restored`, once: "<name> was put back after an interrupted replace." The item is then not started, or `OriginalDisplaced` resolved |
| Empty | Does not match | Nothing moves | `IdentityMismatch` |
| Empty | Matches, but the move fails | Nothing else changes | `RestoreFailed`, with the reason and the full backup path |
| Occupied | The same file as the target, by identity (POSIX: a crash after the hard link and before the rename) | The extra name is unlinked; no data moves, and the target still holds the file. The item is not started | Nothing to report |
| Occupied | A different file | The target is never touched | `TargetOccupied`: "An older copy of <name> is still in this folder as <backup>." |

The operation center lists each non-restored finding with **Show in folder**, **Put back** (`restoreBackup`, only when the target name is free; otherwise `Code::Conflict`) and **Move to Trash** (`trashBackup`). Neither can delete permanently. `dismiss` returns `Code::Conflict` while a finding's backup still exists, and pruning never drops such an operation, because its record is the only pointer to the original.

**Backups the journal can't know.** A `.~gb` file from before B2 ships, or from an Overwrite run while the journal was unavailable, has no record. Reconcile does not scan disks for them: that would wake sleeping drives and stall on network shares, and the random name does not say which file it belonged to. They stay safe where they are, because nothing sweeps them, and an unrecoverable 1177 already named the file in its message. A7's hidden-files toggle makes them visible.

## Failure and cancellation

| Case | Result |
| --- | --- |
| Cancel | Honoured between items, so no intent is recorded beyond the current batch. An item already handed to the OS completes and gets its real outcome. A cancelled copy removes its partial and leaves the target untouched |
| Item failure | Per-item outcome; the operation continues. Summary counts are right and undo covers Done items only |
| Process death | The record stays Interrupted. The lock is released by the OS; the next start reconciles |
| Another process still running the operation | Its lock is held, so it is not reconciled |
| Two processes reconcile at once | `journal.lock` lets one win; the other finds nothing left |
| Corrupt operation file | Marked `Corrupt`, shown, never acted on. Its backups are reported as unattributed text if the intent lines before the bad line name them |
| Unknown future `v` | Read-only |
| Limits exceeded | `LimitExceeded` before any mutation |
| Reconcile cancelled at shutdown | Unprocessed operations stay Interrupted and are reconciled next start |

## Migration without behaviour change

- B2 introduces the journal behind the existing services. `FileOperationService` writes a header at `confirm`, intents and outcomes around `executeDeletion` items (a progress callback hook already exists per item), and records `trashToken` once `TrashBackend` returns it.
- Results, signals and wording stay identical, with these deliberate, listed changes: the unjournaled notice, the replaced original going to the trash, and the reports above. The journal adds records, not behaviour.
- `CopyMoveService` and `MutationService` adopt the protocol in B2. `TransferHud` is replaced by the operation center over `list()` in the same packet.
- Recovery precedent reused: the explicit directory namespace, `FileLock`, SHA-256 integrity, `QSaveFile` for the index, and "a sealed preimage precedes replacement" (`recovery::apply`, `settings_recovery.hpp:115`) for sidecar and transform writes.

## Acceptance tests B2 must pass

`tests/operation_journal_tests.cpp`; fake backends, generated files, temp profile.

- **Format:** round-trip of each record type; torn last line ignored and truncated; checksum failure in the middle marks the operation Corrupt and leaves it untouched; unknown future `v` is read-only; oversized file and too many items are refused before mutation.
- **Durability ordering:** test hooks kill the worker between intent and mutation, between mutation and outcome, and mid-copy. Reconcile yields not started, done and not started (partial removed) respectively.
- **Restart reconciliation:** a separate process holds the op lock, so the operation is not reconciled (owner alive); the lock is released by process exit, so it is reconciled; two processes reconcile at once and only one wins `journal.lock`; a source replaced after the crash stays Unknown with evidence.
- **Backup recovery (Windows, plus POSIX with hand-made files for the portable logic):** using `copy_safety::setFaultHookForTesting`:
  - `FaultPoint::ReplacePartial` followed by a process kill: the next start restores the original, its bytes and identity match the snapshot, and one `Restored` report appears;
  - `ReplacePartial` plus a failing `RestoreBackup` (a real `OriginalDisplaced`, no crash): the outcome is recorded, and the next start restores it;
  - a kill after the replacement took the target name: the target is untouched and `TargetOccupied` is reported; **Move to Trash** reaches the fake backend, and `restoreBackup` returns `Conflict`;
  - a file at the backup path with another identity: `IdentityMismatch`, nothing moves;
  - POSIX, a kill after the hard link and before the rename: the extra name is removed, the target's bytes and identity are unchanged and nothing is reported;
  - the backup vanished before start: no finding and no error;
  - the move back denied: `RestoreFailed` with the path, and the original still at the backup path;
  - `dismiss` is refused and pruning skips the operation while the backup exists, and both proceed once it is resolved;
  - a backup with no journal record is never touched or reported.
- **Intent ordering:** the backup path is durable before `ReplaceFileW` starts, proven by a kill hook at the replace.
- **Case-only rename:** a kill between the two steps restores the original name through the same table, and the file is never deleted as a partial.
- **Partial failure:** a 10-item move with item 4 denied and item 7 in use. Per-item outcomes are recorded, the summary counts are right, and undo is offered for the Done items only.
- **Undo:** restore from the trash with a fake token; token gone (expired trash) gives Missing and no fallback; renamed back; copy trashed, never removed; the moved file changed after the move gives Changed and is skipped; an Overwrite whose old copy reached the trash restores it after trashing the new one; an Overwrite on a volume with no trash advertises no undo.
- **Trash round trip (native):** on each platform, a real trash and restore by token, or a recorded `None` with the reason.
- **Cancellation:** cancel between batches, so no intent is recorded beyond the batch; cancel during a copy, so the partial is removed and the original target is untouched.
- **Bounds and pruning:** the 201st completed operation prunes the oldest; Interrupted operations are never pruned; the `kMaxJournalBytes` cap holds, including preimages.
- **Journal unavailable:** a read-only journal directory and a full-disk simulation. Trash, copy, move, rename and create folder run with the notice and no undo; sidecar write, lossless transform and batch rename are refused with the reason and attempt no mutation. A write failure mid-operation stops before the next batch mutates.
- **COPY-SAFETY-01 regression:** every `copy_safety` test passes unchanged, and `.~gb` files are still never swept by `removeStaleTemporaries`.

## Not covered

- No automatic retry or resume after restart. Every re-execution is re-planned and re-confirmed.
- No undo for permanent deletion, expired trash, overwrites without a trashed original, or external changes.
- No journaling of view state, settings (recovery already covers those) or caches.
- No cross-machine sync, history UI beyond the operation center, or export of path lists.
- No transactional rollback across items: each item is atomic on its own, and the batch is not.
- No disk scan for backups without a journal record.
