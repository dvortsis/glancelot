# OPERATION-JOURNAL contract

Status: **frozen** by PHASE2-CONTRACTS. Header: [`include/glancelot/operation_journal.hpp`](../../include/glancelot/operation_journal.hpp), the authoritative text of the interface. Code references are at `b1d57cd`. Owner: Track B. Implementing packet: B2 (F017, F062). Decisions and their alternatives: [2026-11-05-phase2-contracts.md](../decisions/2026-11-05-phase2-contracts.md).

## Purpose

A durable record of every user-initiated mutation. For each operation it records the **exact targets** (snapshotted identity), the **intent** before each mutation and the **outcome** after. It states **undo capability** per item honestly, and lets a restarted viewer **reconcile** an interrupted operation instead of guessing. Mutation services today report results only in memory (`FileOperationService::finished`, `CopyMoveService::itemFinished`, `MutationService::renamePath`). After a crash nothing is known.

## Consumers

**B2** (journal, operation center replacing TransferHud, the Overwrite prompt, pause, cancel, retry and restart reconciliation, backup recovery; all mutations journaled), **B1** (records the asset-expanded list), **B4** batch rename (restart recovery), **B5** manifests, **B6** drag/drop, **C1** XMP sidecar writes (preimage), **D7** lossless JPEG transforms. LEAD-PLAN section 7: the journal (B2) lands before batch rename, drag/drop and sidecar writes.

## Frozen interface

The header is frozen as committed: the `k*` bounds, `Kind`, `requiresJournal`, `Outcome`, `UndoCapability`, `State`, `OperationId`, `TargetSnapshot`, `ItemIntent`, `ItemOutcome`, `OperationHeader`, `OperationSummary`, `Code`, `Status`, `BackupState`, `BackupFinding`, `OperationJournal` (including `probe` and `items`) and `Operation` (including `preimagePathFor` and `sealPreimage`).

**Applied by B2 (B2's brief grants these exact diffs).**

`include/glancelot/file_operations.hpp`, so a backend can hand back its trash location and say before confirmation whether a trash exists. Positional `DeleteResult{path, outcome, message}` stays valid:

```diff
-struct DeleteResult { std::filesystem::path path; DeleteOutcome outcome{DeleteOutcome::Failed}; std::string message; };
+struct DeleteResult { std::filesystem::path path; DeleteOutcome outcome{DeleteOutcome::Failed}; std::string message;
+                      std::string trashToken; };  // POSIX: path in the trash; Windows: Recycle Bin item parsing name; empty if none
+enum class TrashAvailability { Unknown, Available, Unavailable };
 struct TrashBackend {
     virtual ~TrashBackend() = default;
     virtual DeleteResult moveToTrash(const MutationTarget& target) = 0;
+    // Worker only. Whether `directory`'s volume has a usable trash. Windows: SHQueryRecycleBinW on the volume root.
+    // POSIX: the home trash on the same device, or a usable .Trash-$uid. Unknown when it can't be told.
+    virtual TrashAvailability probeTrash(const std::filesystem::path& directory) { (void)directory; return TrashAvailability::Unknown; }
 };
```

`src/app/copy_safety.hpp` (Track B owned, not shared), so the backup name is chosen, and journaled, before the replace starts, and so a replace that can't keep a backup needs consent:

```diff
-bool replaceFileAtomically(const QString& replacementPath, const QString& targetPath, QString* error = nullptr,
-                           bool* originalDisplaced = nullptr);
+QString backupPathFor(const QString& targetPath);    // first free ".~gb<8 hex>.tmp" beside the target, or empty
+// allowNoBackup false: a volume where ReplaceFileW doesn't work fails here, with the target unchanged, instead of taking the
+// fallback that keeps no backup (ERROR_NOT_SUPPORTED and its peers).
+bool replaceFileAtomically(const QString& replacementPath, const QString& targetPath, QString* error = nullptr,
+                           bool* originalDisplaced = nullptr, const QString& backupPath = {}, bool allowNoBackup = true);
```

`include/glancelot/copy_move_service.hpp`: `planTransfer` and `confirmTransfer` are in ASSET-GROUP's diff (B1 adds them; B2 adds Overwrite, `replaceFate` and consent). `CopyMoveService` gains an injected `TrashBackend`, as `FileOperationService` already has.

## Durable format

- **Location:** `<settings directory>/journal/`, beside `settings.json` and `sessions.json`. The directory comes from an explicit context, like `recovery::Context` (`settings_recovery.hpp:40`), so redirected environment variables are never re-read.
- **One file per operation:** `op-<16 hex id>.gjl`, JSON Lines, append-only. Line 1 is the header record `{"v":1,"type":"header",...}`. Later lines are `intent`, `preimage`, `outcome` and `state` records. Each line ends with `"crc":"<first 16 hex of SHA-256 of the line without crc>"`, using `QCryptographicHash::Sha256`.
- **Atomicity:** each append is a single `write` of a full line followed by flush and fsync (`FlushFileBuffers` / `fsync`) at batch boundaries. On POSIX the directory is also fsynced after an `op-*.gjl` is created and after a backup hard link is made, so power loss can't lose the only pointer to a backup. A torn or failed-checksum **last** line is ignored and the file is truncated back to the last good line on the next open. A bad checksum anywhere else marks the operation `Corrupt`: it is shown, never acted on.
- **Index:** `journal/index.json` is a cache only, rewritten with `QSaveFile` (the `settings_file.cpp:61` pattern). It is rebuilt from the `.gjl` files when it is missing or mismatched.
- **Locks:** `journal/journal.lock` (`FileLock`, `src/app/file_lock.hpp`) serializes create, prune and index writes across processes. Each running operation holds `op-<id>.lock` for its lifetime. The OS releases it on crash, so "lock free and state not terminal" reliably means **interrupted**. There is no pid guessing.
- **IDs:** 64-bit, random high 32 bits plus a monotonic counter. They are never reused while a file with that id exists.
- **Bounds:** the `k*` constants. `begin` estimates the file from `itemCount` and `totalPathBytes`, and refuses before any mutation (`LimitExceeded`) when it can't fit. `kMaxJournalBytes` includes preimages. A preimage that does not fit after pruning refuses that item through `sealPreimage`. At 1 GiB, D7 can hold about 100 rotations of 10 MB JPEGs in one operation before it refuses; D7's brief carries the wording.
- **Reading:** `list` gives summaries; `items` pages an operation's intents and outcomes. Undo, **Retry remaining**, B4's restart recovery and the operation center's detail view all read through `items`. Nothing outside the class parses a `.gjl` file.
- **Privacy:** paths stay local in the user's profile and are never exported. The F058 diagnostics export (E2) redacts them.

## Protocol every mutation service follows

1. Plan (existing snapshot step, ASSET-GROUP expansion), including `probe()` and, for an Overwrite, the trash probe below. The plan's summary decides what the confirmation says.
2. `begin(header)`. If it fails after the confirmation, nothing has mutated: return to the prompt once, with the journal notice, and go on only if the kind allows it.
3. For each batch of up to `kMaxIntentBatch` (64) items: `recordIntents`, then `sealPreimage` for items that have one, then mutate exactly those items, then `recordOutcomes`. The batch size is fixed; a storage-aware size would need measurements and the lead.
4. Copies and sidecar or transform writes go to `partialPath`, the existing COPY-SAFETY-01 temporary `.~gl<8 hex>.tmp` beside the target (`copy_safety::temporaryPathFor`). A verified copy is then renamed or replaced into place. B2 moves non-Overwrite copies onto this path too, replacing the direct write at `copy_move_service.cpp:928` and the `QFile::remove(destPath)` at `:1014`. **A partial is the only file reconcile may delete, and only after the operation's lock is free.** The one other removal is an extra hard link to a file that stays in place, in the table below.
5. An item that holds an original under another name records that name in `backupPath` **before the step starts**: the Overwrite backup, and the `.tmp_rename_<id>` intermediate of a case-only rename (`mutation_service.cpp:182`). An item with a `backupPath` must not put an original in `partialPath`.
6. `setState(Completed | Cancelled | Failed)`.

**The directory sweep.** `copy_safety::removeStaleTemporaries` spares only the current process's in-flight names (`copy_move_service.cpp:515`), so one window could remove another window's partial once every copy uses `.~gl`. With the journal available, reconcile owns partials and B2 stops calling the sweep at `:922`. With the journal unavailable, the sweep stays and removes only names older than 24 hours. B2's brief grants the matching change to the COPY-SAFETY-01 test.

**Undo capability rules (set per item, at outcome time):**

| Item | Undo | Never |
| --- | --- | --- |
| Trash, with a trash token | `RestoreFromTrash`. POSIX token: `QFile::fileName()` after `moveToTrash` (`file_operation_service.cpp:173`). Windows token: the Recycle Bin item from `PostDeleteItem` (`:83`). Advertised on a platform only if B2's round-trip test passes there: trash a generated file, restore by token, compare bytes and identity. Otherwise that platform records the token and advertises `None` | When the token is missing at undo time (trash emptied or expired, or the platform gave no token): `Outcome::Missing`, reported, no fallback |
| Rename, same-volume move | `ReverseRename` / `ReverseMove` | When the moved file's identity no longer matches `resultIdentity`: `Changed` |
| Cross-volume move | `ReverseMove` (verified copy back, then the moved copy goes to trash, never to permanent delete) | When the source volume is gone |
| Copy | `TrashCopy` | Never a permanent delete of the copy |
| Copy or move with **Overwrite** | `RestoreFromTrash` when the replaced original reached the trash (below): `overwrittenTrashToken`. Otherwise `None` | The undo first trashes the current target, then restores the old one. It never overwrites |
| Permanent deletion | — | There is no permanent-delete path. If one is ever added, its undo is `None` by contract |
| SidecarWrite, LosslessTransform | `RestorePreimage` while the preimage is retained, for the life of the operation record. A sidecar that did not exist before has no preimage, and its undo is `TrashCopy` | After pruning |

An undo is a new operation with `undoOf` set. It reads the original through `items`, revalidates every item, and reports items that cannot be undone, never forcing them. The operation center advertises undo only when `undoAvailable != None` (FEATURE-OPPORTUNITIES section 13, Mutations: "Undo is advertised only when supported").

## Overwrite: the prompt and the replaced original

COPY-SAFETY-01 already keeps the original until a complete, verified copy replaces it. **No Overwrite prompt exists today:** nothing in `ui/` offers Overwrite, and the only caller, `CopyMoveService::pasteTo`, hardcodes `RenameBoth` (`copy_move_service.cpp:708`). **B2 builds the collision prompt** and is the first packet to offer Overwrite to the user. It applies the ui-design-skills, per AGENTS.md.

**Before the prompt.** The plan step runs `TrashBackend::probeTrash` on a worker, once per destination volume, and the plan's summary carries `replaceFate`:

| Probe | `replaceFate` | The prompt says | Consent |
| --- | --- | --- | --- |
| `Available` | `"trash"` | "Replaced files go to the <trashName>. You can undo this." | The ordinary confirmation |
| `Unavailable` or `Unknown` | `"permanent"` | "This location has no <trashName> that Glancelot can use. Replaced files are permanently deleted and can't be undone." | Explicit: a separate, unchecked confirmation that enables a button named "Permanently replace". `confirmTransfer(id, Overwrite, permanentReplaceConsent = true)`, and the header's `permanentReplaceConsented` records it. Without it, `confirmTransfer` returns false and nothing runs |

**At execution.** Each operation re-probes once per volume before its first replace, because a trash can change after the prompt.

| Situation | Result |
| --- | --- |
| Probe `Available` | Replace, keeping a backup (below), then move the replaced original to the trash. Undo `RestoreFromTrash` |
| Probe `Available`, and the trash move fails for any reason (`TrashUnavailable`, `InUse`, `Denied`, a full bin), whatever the consent | **Never removed.** The new file stays in place, and the old file stays where it is, under `replacedPath` or `backupPath`. The item is `Done` with undo `None` and a message naming that path. A `BackupFinding` with `TrashFailed` is raised, the operation stops, its remaining items are `Cancelled` with the reason, and its state is `Failed` |
| Probe not `Available`, consent given | Replace, keeping a backup where the volume can, then remove the backup. Undo `None` |
| Probe not `Available`, no consent (the trash vanished after the prompt) | Stop before replacing. The target is unchanged, the item is `Failed` with "This location no longer has a <trashName>. Run this again to choose what happens to replaced files", and the remaining items are `Cancelled` |
| The volume can't keep a backup (`ReplaceFileW` unsupported, so the `replaceWithoutMerge` fallback at `copy_move_service.cpp:453`; POSIX without hard links), consent given | The replace takes the fallback. Undo `None`. The consent wording already covers this |
| The volume can't keep a backup, no consent (`allowNoBackup` false; `link` fails) | Stop before replacing. The target is unchanged, the item is `Failed` with "This location can't keep the replaced file. Run this again and confirm permanent replacement", and the remaining items are `Cancelled` |

**Keeping the backup.**

- Windows: `ReplaceFileW` leaves the original at the `.~gb<8 hex>.tmp` backup name.
- POSIX: `rename(2)` leaves no backup, so B2 first hard-links the target to `backupPath`, then replaces.
- Both, on the trash route: B2 renames the backup to `replacedPath` (`<stem> (replaced)<ext>` on the first free name, chosen before the intent is written, so the trash shows something recognisable) and moves it to the trash, instead of the `DeleteFileW` at `copy_move_service.cpp:444`. If the rename fails, it trashes the file under its backup name.

## Failure policy

When the journal cannot be opened or written (read-only profile, full disk, contention past the timeout), or an operation would exceed its limits (`LimitExceeded`):

- `requiresJournal(kind)` is true (sidecar writes, lossless transforms, batch rename, and asset-scope rename, which is `Kind::BatchRename`): the operation is refused and the UI says why. These exist only with the journal's protection.
- Otherwise (trash, copy, move, single-file rename, create folder, including Overwrite under COPY-SAFETY-01's protection): the operation works as it does today. The confirmation adds "Glancelot can't record this change, so it can't be undone here and won't appear in history." This keeps read-only profiles and portable media working.
- The confirmation learns this from `probe()` at the plan step, and a `begin` failure afterwards returns to the prompt once, with the notice.
- Once an operation has begun, any failed `recordIntents` stops it before that batch mutates. Items already done stay recorded; the rest report `Cancelled` with the reason. It never continues unrecorded.

**Generation fencing.** `OperationSummary::revision` increments on each durable change, and UI models drop older revisions. Snapshot identities fence execution: a target that changed after confirmation is `Changed`, never mutated. Late outcomes from a cancelled worker are still recorded (that is the truth), but they cannot reopen a terminal state.

## Restart reconciliation (`reconcile`)

Runs on a worker after window claim, never blocking the UI, once per process start. It takes each operation that is interrupted (lock free, state not terminal) and not yet reconciled, and each operation with `displaced` or `heldBackups` items. A pass appends its outcomes and a `{"type":"state","reconciled":true}` record, which sets `OperationSummary::reconciled`, so the next start or another process finds nothing left to resolve. A `Restored` report is therefore made once. Operations that still hold a backup are revisited each start, only to report it.

**Unknown items**, by kind, with exact stats only. The first matching row wins.

*Trash:*

| Evidence | Result |
| --- | --- |
| Source intact with the snapshot identity | Not started |
| Source gone, trash token recorded | Done, with the token's undo |
| Source gone, no token | Done, undo `None` |

*Copy, Move, Rename, Overwrite:*

| Evidence | Result |
| --- | --- |
| `overwritten` set, and the target still has its identity | Not started. A same-size target that was never replaced is not "done". A partial is removed |
| Partial present, target not holding the new file | The partial is removed; not started |
| Source intact and no target | Not started |
| Target present with the snapshot size, and for a move the source gone | Done. Its result identity is recorded |
| Anything else | Unknown, with evidence in `message` |

*SidecarWrite, LosslessTransform (`source == target`):*

| Evidence | Result |
| --- | --- |
| Target identity and size match `source` | Not started; the preimage, if any, is removed |
| No preimage recorded (a new sidecar), target absent | Not started |
| No preimage recorded (a new sidecar), target present | Done, undo `TrashCopy` |
| Target differs, and the preimage is sealed (a `preimage` record exists) with a good checksum | Done, undo `RestorePreimage` |
| Preimage missing, unsealed or with a bad checksum | Unknown, with evidence |

Reconcile never retries. The operation center shows something like "Interrupted: 37 done, 61 not started, 2 unknown", with **Retry remaining** (a new operation over the not-started items, read through `items`, re-planned and re-confirmed, so a Done transform is never applied twice) and **Dismiss**.

**Split assets.** An asset rename interrupted partway is reported as "<stem> is split: renamed A, not B", with **Retry remaining**. Reversing members after a failure is a new operation with `undoOf`, so a crash during the reversal reconciles the same way.

**Backups left by a crash or error 1177 inside `ReplaceFileW` (COPY-SAFETY-01 review N1, lead decision).** `ReplaceFileW` renames the target to the backup, then moves the replacement into place. A crash between the two, or a 1177 whose move back failed (`originalDisplaced`, `copy_move_service.cpp:990`), leaves the target name empty and the original in `.~gb<8 hex>.tmp`. That name is hidden from the grid, and the random name can only be found through the journal, which is why step 5 above is durable first. A non-crash 1177 records `Outcome::OriginalDisplaced` and is handled here on the next start, and in the operation center at once.

For each item whose `backupPath` or `replacedPath` still exists, the recorded original is `overwritten` when set and `source` otherwise. Its **original name** is that snapshot's path: the Overwrite target, or for a case-only rename the source spelling, not the new case.

| Original name | The file at the backup | Action | Reported as |
| --- | --- | --- | --- |
| Empty | Identity and size match the recorded original (size and modified when the volume reports no identity) | Moved back with `MoveFileExW` and no replace flag, or `link` then `unlink` on POSIX, so it can never overwrite | `Restored`, once: "<name> was put back after an interrupted replace." The item is then not started, or `OriginalDisplaced` resolved |
| Empty | Does not match | Nothing moves | `IdentityMismatch` |
| Empty | Matches, but the move fails | Nothing else changes | `RestoreFailed`, with the reason and the full backup path |
| Occupied | The same file as the occupant, by identity (POSIX: a crash after the hard link and before the rename) | The extra name is unlinked; no data moves, and the target still holds the file. The item is not started | Nothing to report |
| Occupied | A different file, and the operation recorded a `TrashFailed` | The target is never touched | `TrashFailed`: "The older copy of <name> couldn't go to the <trashName>. It is still in this folder as <file>." |
| Occupied | A different file | The target is never touched | `TargetOccupied`: "An older copy of <name> is still in this folder as <backup>." |

The operation center lists each non-restored finding with **Show in folder**, **Put back** (`restoreBackup`, only when the original name is free; otherwise `Code::Conflict`) and **Move to Trash** (`trashBackup`). Neither can delete permanently. `dismiss` returns `Code::Conflict` while a finding's backup still exists, and pruning never drops such an operation, because its record is the only pointer to the original.

**Backups the journal can't know.** A `.~gb` file from before B2 ships, or from an Overwrite run while the journal was unavailable, has no record. Reconcile does not scan disks for them: that would wake sleeping drives and stall on network shares, and the random name does not say which file it belonged to. They stay safe where they are, because nothing sweeps them, and an unrecoverable 1177 already named the file in its message. A7's hidden-files toggle makes them visible.

## Failure and cancellation

| Case | Result |
| --- | --- |
| Cancel | Honoured between items, so no intent is recorded beyond the current batch. An item already handed to the OS completes and gets its real outcome. A cancelled copy removes its partial and leaves the target untouched |
| Item failure | Per-item outcome; the operation continues, except for the Overwrite stops above. Summary counts are right and undo covers Done items only |
| Replaced original can't reach the trash | Never removed; the operation stops and reports (see "At execution") |
| Process death | The record stays Interrupted. The lock is released by the OS; the next start reconciles |
| Another process still running the operation | Its lock is held, so it is not reconciled |
| Two processes reconcile at once | `journal.lock` lets one win; the other finds the `reconciled` record and nothing left |
| Corrupt operation file | Marked `Corrupt`, shown, never acted on. `items` returns the good prefix, so its backups can be named in the report |
| Unknown future `v` | Read-only |
| Limits exceeded | `LimitExceeded` before any mutation, then the failure policy above |
| Reconcile cancelled at shutdown | Unprocessed operations stay Interrupted and are reconciled next start |

## Migration without behaviour change

- B2 introduces the journal behind the existing services. `FileOperationService` writes a header at `confirm`, intents and outcomes around `executeDeletion` items (a progress callback hook already exists per item), and records `trashToken` once `TrashBackend` returns it.
- Results, signals and wording stay identical, with these deliberate, listed changes: the unjournaled notice, the new Overwrite prompt, the replaced original going to the trash, and the reports above. The journal adds records, not behaviour.
- `CopyMoveService` and `MutationService` adopt the protocol in B2. `TransferHud` is replaced by the operation center over `list()` in the same packet. `enqueueTransfer` stays for existing callers; called with Overwrite it behaves as "no consent".
- Recovery precedent reused: the explicit directory namespace, `FileLock`, SHA-256 integrity, `QSaveFile` for the index, and "a sealed preimage precedes replacement" (`recovery::apply`, `settings_recovery.hpp:115`) for sidecar and transform writes.

## Acceptance tests B2 must pass

`tests/operation_journal_tests.cpp`; fake backends, generated files, temp profile.

- **Format:** round-trip of each record type; torn last line ignored and truncated; checksum failure in the middle marks the operation Corrupt and leaves it untouched; unknown future `v` is read-only; an operation too large for `kMaxOperationFileBytes` (from `totalPathBytes`) and too many items are refused before mutation.
- **Reading items:** paging across a batch boundary; empty past the end; `Conflict` for an unknown id; a Corrupt file returns its good prefix; undo and **Retry remaining** work from a fresh process using only `items`.
- **Durability ordering:** test hooks kill the worker between intent and mutation, between mutation and outcome, and mid-copy. Reconcile yields not started, done and not started (partial removed) respectively.
- **Restart reconciliation:** a separate process holds the op lock, so the operation is not reconciled (owner alive); the lock is released by process exit, so it is reconciled; two processes reconcile at once and only one wins `journal.lock`; a second start finds `reconciled` and repeats nothing; a source replaced after the crash stays Unknown with evidence; a same-size Overwrite killed before the replace is not started, not done; a killed Trash with no token is Done with undo `None`.
- **Overwrite prompt and fate (fake backend):**
  - `Available` gives `replaceFate` `"trash"` before `confirmTransfer`; `Unavailable` and `Unknown` give `"permanent"`, and the UI test finds the permanent wording in the prompt;
  - `confirmTransfer` with `"permanent"` and no consent returns false, and nothing runs;
  - with consent on `Unavailable`, the target is replaced, the old copy is gone, undo is `None` and the header records the consent;
  - `Available` at the plan with the trash move failing, for each of `TrashUnavailable`, `InUse` and `Denied`: the old copy still exists, the item is `Done` with undo `None`, a `TrashFailed` finding names the path, the remaining items are `Cancelled`, and the finding survives a restart;
  - `Available` at the plan and `Unavailable` at execution, no consent: the target is unchanged and the operation stops;
  - `FaultPoint::ReplaceUnsupported`: without consent the target is unchanged and the item fails with the wording; with consent the fallback runs;
  - POSIX with `link` failing: the same two results.
- **Backup recovery (Windows, plus POSIX with hand-made files for the portable logic):** using `copy_safety::setFaultHookForTesting`:
  - `FaultPoint::ReplacePartial` followed by a process kill: the next start restores the original, its bytes and identity match the snapshot, and one `Restored` report appears;
  - `ReplacePartial` plus a failing `RestoreBackup` (a real `OriginalDisplaced`, no crash): the outcome is recorded, and the next start restores it;
  - a kill after the replacement took the target name: the target is untouched and `TargetOccupied` is reported; **Move to Trash** reaches the fake backend, and `restoreBackup` returns `Conflict`;
  - a kill between the rename to `replacedPath` and the trash move: the file is found under that name and reported;
  - a file at the backup path with another identity: `IdentityMismatch`, nothing moves;
  - POSIX, a kill after the hard link and before the rename: the extra name is removed, the target's bytes and identity are unchanged and nothing is reported;
  - the backup vanished before start: no finding and no error;
  - the move back denied: `RestoreFailed` with the path, and the original still at the backup path;
  - `dismiss` is refused and pruning skips the operation while the backup exists, and both proceed once it is resolved;
  - a backup with no journal record is never touched or reported.
- **Intent ordering:** the backup path is durable before `ReplaceFileW` starts, proven by a kill hook at the replace.
- **Case-only rename:** a kill between the two steps restores the original spelling through the same table, and the file is never deleted as a partial.
- **Preimages (SidecarWrite and LosslessTransform):**
  - kill hooks between the intent and the seal, between the seal and the replace, and between the replace and the outcome give not started, not started with the preimage removed, and Done with `RestorePreimage`;
  - a bad preimage checksum after a replace stays Unknown, and **Retry remaining** does not offer the item;
  - retry never transforms a Done item twice;
  - a new sidecar has no preimage, and its undo moves it to the trash;
  - `sealPreimage` over the journal cap returns `LimitExceeded` and the item doesn't run.
- **Partial failure:** a 10-item move with item 4 denied and item 7 in use. Per-item outcomes are recorded, the summary counts are right, and undo is offered for the Done items only.
- **Undo:** restore from the trash with a fake token; token gone (expired trash) gives Missing and no fallback; renamed back; copy trashed, never removed; the moved file changed after the move gives Changed and is skipped; an Overwrite whose old copy reached the trash restores it after trashing the new one; an Overwrite with permanent consent advertises no undo.
- **Trash round trip (native):** on each platform, a real trash and restore by token, or a recorded `None` with the reason. `probeTrash` against a real volume returns `Available` on the gate machine's system drive.
- **Asset rename:** a kill partway reports the split; a kill during a reversal does too; with the journal unavailable an asset rename is refused.
- **Cancellation:** cancel between batches, so no intent is recorded beyond the batch; cancel during a copy, so the partial is removed and the original target is untouched.
- **Sweep:** with the journal available, a second process's partial survives; with it unavailable, only names older than 24 hours go.
- **Bounds and pruning:** the 201st completed operation prunes the oldest; Interrupted operations and those holding a backup are never pruned; the `kMaxJournalBytes` cap holds, including preimages.
- **Journal unavailable:** a read-only journal directory, a full-disk simulation and a `LimitExceeded`. `probe()` makes the notice appear in the confirmation; trash, copy, move, single-file rename and create folder run with the notice and no undo; sidecar write, lossless transform, batch rename and asset rename are refused with the reason and attempt no mutation. A write failure mid-operation stops before the next batch mutates. A `begin` failure after the confirmation returns to the prompt once.
- **COPY-SAFETY-01 regression:** every `copy_safety` test passes, with only the sweep change above, and `.~gb` files are still never swept by `removeStaleTemporaries`.

## Not covered

- No automatic retry or resume after restart. Every re-execution is re-planned and re-confirmed.
- No undo for permanent deletion, expired trash, overwrites without a trashed original, or external changes.
- No journaling of view state, settings (recovery already covers those) or caches.
- No cross-machine sync, history UI beyond the operation center, or export of path lists.
- No transactional rollback across items: each item is atomic on its own, and the batch is not. The one exception is asset rename, which reverses its members (see ASSET-GROUP).
- No disk scan for backups without a journal record.
