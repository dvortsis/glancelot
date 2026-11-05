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

`include/glancelot/copy_move_service.hpp`: `planTransfer` and `confirmTransfer` are in ASSET-GROUP's diff (B1 adds them; B2 adds Overwrite, `replaceFate` and consent). B2 also adds an injected backend, matching `file_operation_service.hpp:28`:

```diff
     explicit CopyMoveService(QObject* parent = nullptr);
+    // Tests inject a backend; the default is the platform trash (never permanent deletion).
+    explicit CopyMoveService(std::unique_ptr<glancelot::TrashBackend> backend, QObject* parent = nullptr);
```

**Test migration (B2's brief grants this).** Under the rules below, `enqueueTransfer` with Overwrite behaves as "no consent", so existing tests can't keep the real trash or the fallback route as they are:

- both `runTransfer` helpers (`tests/copy_move_tests.cpp:458`, `tests/copy_safety_test_support.hpp:71`) construct the service with a fake `TrashBackend` reporting `Available`, so no fixture reaches the real trash;
- the fallback tests, such as `testOverwriteFallbackKeepsAttributes` (`tests/copy_move_tests.cpp:840`), go through `planTransfer` and `confirmTransfer(..., permanentReplaceConsent = true)` with a fake reporting `Unavailable`.

The tests move and none is weakened: every existing assertion on target bytes, attributes and permissions stays.

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
4. Copies and sidecar or transform writes go to `partialPath`, the existing COPY-SAFETY-01 temporary `.~gl<8 hex>.tmp` beside the target (`copy_safety::temporaryPathFor`). A verified copy is then renamed or replaced into place. B2 moves non-Overwrite copies onto this path too, replacing the direct write at `copy_move_service.cpp:928` and the `QFile::remove(destPath)` at `:1014`. **Reconcile deletes only three things, and only after the operation's lock is free:** a partial; a preimage, where the in-place table says so; and an extra hard link to a file that stays in place, with the proof the backup table requires.
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

Consent counts only when the plan said `"permanent"`. On a `"trash"` plan the flag is ignored, because the user never saw that wording.

On POSIX the plan step also tests a hard link in the destination. A volume that can't make one reports `"permanent"`, even with a usable trash. A re-plan after a stop on that volume, or after a trash move refused on a volume whose probe says `Available`, also reports `"permanent"` for that volume for the rest of the process, so the user can always reach the consent the stop message asks for.

**At execution.** Each operation re-probes once per volume before its first replace, because a trash can change after the prompt.

| Situation | Result |
| --- | --- |
| Probe `Available` | Replace, keeping a backup (below), then move the replaced original to the trash. Undo `RestoreFromTrash` |
| Probe `Available`, and the trash move fails for any reason (`TrashUnavailable`, `InUse`, `Denied`, a full bin), whatever the consent | **Never removed.** The new file stays in place, and the old file stays where it is, under `replacedPath` or `backupPath`. The item is `Done` with undo `None` and a message naming that path. A `BackupFinding` with `TrashFailed` is raised, the operation stops, its remaining items are `Cancelled` with the reason, and its state is `Failed` |
| Probe not `Available`, consent counted | Replace, keeping a backup where the volume can, then remove the backup. Undo `None` |
| Probe not `Available`, no counted consent (the trash vanished after the prompt) | Stop before replacing. The target is unchanged, the item is `Failed` with "This location no longer has a <trashName>. Run this again to choose what happens to replaced files", and the remaining items are `Cancelled` |
| The volume can't keep a backup (`ReplaceFileW` unsupported, so the `replaceWithoutMerge` fallback at `copy_move_service.cpp:453`; POSIX without hard links), consent counted | The replace takes the fallback. Undo `None`. The consent wording already covers this |
| The volume can't keep a backup, no counted consent (`allowNoBackup` false; `link` fails) | Stop before replacing. The target is unchanged, the item is `Failed` with "This location can't keep the replaced file. Run this again and confirm permanent replacement", and the remaining items are `Cancelled` |

**Keeping the backup.**

- Windows: `ReplaceFileW` leaves the original at the `.~gb<8 hex>.tmp` backup name.
- POSIX: `rename(2)` leaves no backup, so B2 first hard-links the target to `backupPath`, then replaces.
- Both, on the trash route: B2 renames the backup to `replacedPath` (`<stem> (replaced)<ext>` on the first free name, chosen before the intent is written, so the trash shows something recognisable), using a move that can't replace a name that appeared meanwhile, and moves it to the trash, instead of the `DeleteFileW` at `copy_move_service.cpp:444`. If the rename fails, it trashes the file under its backup name.

## Failure policy

When the journal cannot be opened or written (read-only profile, full disk, contention past the timeout), or an operation would exceed its limits (`LimitExceeded`):

- `requiresJournal(kind)` is true (sidecar writes, lossless transforms, batch rename, and asset-scope rename, which is `Kind::BatchRename`): the operation is refused and the UI says why. These exist only with the journal's protection.
- Otherwise (trash, copy, move, single-file rename, create folder, including Overwrite under COPY-SAFETY-01's protection): the operation works as it does today. The confirmation adds "Glancelot can't record this change, so it can't be undone here and won't appear in history." This keeps read-only profiles and portable media working.
- The confirmation learns this from `probe()` at the plan step, and a `begin` failure afterwards returns to the prompt once, with the notice.
- Once an operation has begun, any failed `recordIntents` stops it before that batch mutates. Items already done stay recorded; the rest report `Cancelled` with the reason. It never continues unrecorded.

**Generation fencing.** `OperationSummary::revision` increments on each durable change, and UI models drop older revisions. Snapshot identities fence execution: a target that changed after confirmation is `Changed`, never mutated. Late outcomes from a cancelled worker are still recorded (that is the truth), but they cannot reopen a terminal state.

## Restart reconciliation (`reconcile`)

Runs on a worker after window claim, never blocking the UI, once per process start. It takes each operation that is interrupted (lock free, state not terminal) and not yet reconciled, and each operation with `displaced` or `heldBackups` items. A pass appends its outcomes and a `{"type":"state","reconciled":true}` record, which sets `OperationSummary::reconciled`, so the next start or another process finds nothing left to resolve. A `Restored` report is therefore made once. Operations that still hold a backup are revisited each start, only to report it.

**Identity.** Throughout this contract, an identity with no file index is no identity: `FileIdentity` is the raw volume and file index (`src/core/file_operations.cpp:106`), some redirectors report no index (`copy_move_service.cpp:388`), and two different files on such a volume would compare equal. It never counts as a match. A row that needs a match then uses size and modified where it says so, and otherwise falls through. The same rule applies to the revalidation at execution and at undo.

**Unknown items**, by kind, with exact stats only, except that the in-place table also reads checksums. The first matching row wins.

*Trash:*

| Evidence | Result |
| --- | --- |
| Source intact with the snapshot identity | Not started |
| Source gone, trash token recorded | Done, with the token's undo |
| Source gone, no token | Done, undo `None` |
| Anything else, such as a source replaced after the crash | Unknown, with evidence |

*Copy, Move, Rename, BatchRename, Overwrite:*

| Evidence | Result |
| --- | --- |
| `overwritten` set, and the target still has its identity | Not started. A same-size target that was never replaced is not "done". A partial is removed |
| Partial present, target not holding the new file | The partial is removed; not started |
| Source intact and no target | Not started |
| Target present with the snapshot size, and for a move or rename the source gone | Done. Its result identity is recorded |
| Anything else | Unknown, with evidence in `message` |

*CreateFolder:*

| Evidence | Result |
| --- | --- |
| Folder absent | Not started |
| Folder present | Done, undo `None`; a folder's identity can't prove who made it |

*SidecarWrite, LosslessTransform (`source == target`).* This is decided by content, because the seal records a checksum and a same-size write is ordinary here: a rating going from 3 to 4 keeps its size. Sealed means a `preimage` record exists and the file's checksum matches it. Any partial is removed in the first five rows.

| Evidence | Result |
| --- | --- |
| Preimage sealed, and the target's checksum equals it | Not started; the preimage is removed |
| Preimage sealed, and the target differs | Done, undo `RestorePreimage` |
| Preimage path recorded but not sealed, and the target matches `source` on size and modified, and on identity where the volume has one | Not started, since the protocol seals before it mutates; the unsealed file is removed |
| No preimage recorded (a new sidecar), target absent | Not started |
| No preimage recorded (a new sidecar), target present | Done, undo `TrashCopy` |
| Preimage missing or bad, or unsealed with a target that differs | Unknown, with evidence |

Reconcile never retries. The operation center shows something like "Interrupted: 37 done, 61 not started, 2 unknown", with **Retry remaining** (a new operation over the not-started items, read through `items`, re-planned and re-confirmed, so a Done transform is never applied twice) and **Dismiss**.

**Split assets.** An asset rename interrupted partway is reported as "<stem> is split: renamed A, not B", with **Retry remaining**. Reversing members after a failure is a new operation with `undoOf`, so a crash during the reversal reconciles the same way.

**Backups left by a crash or error 1177 inside `ReplaceFileW` (COPY-SAFETY-01 review N1, lead decision).** `ReplaceFileW` renames the target to the backup, then moves the replacement into place. A crash between the two, or a 1177 whose move back failed (`originalDisplaced`, `copy_move_service.cpp:990`), leaves the target name empty and the original in `.~gb<8 hex>.tmp`. That name is hidden from the grid, and the random name can only be found through the journal, which is why step 5 above is durable first. A non-crash 1177 records `Outcome::OriginalDisplaced` and is handled here on the next start, and in the operation center at once.

For each item whose `backupPath` or `replacedPath` still exists, the recorded original is `overwritten` when set and `source` otherwise. Its **original name** is that snapshot's path: the Overwrite target, or for a case-only rename the source spelling, not the new case.

| Original name | The file at the backup | Action | Reported as |
| --- | --- | --- | --- |
| Empty | Identity and size match the recorded original (size and modified when the volume reports no identity) | Moved back with `MoveFileExW` and no replace flag, or `link` then `unlink` on POSIX, so it can never overwrite | `Restored`, once: "<name> was put back after an interrupted replace." The item is then not started, or `OriginalDisplaced` resolved |
| Empty | Does not match | Nothing moves | `IdentityMismatch` |
| Empty | Matches, but the move fails | Nothing else changes | `RestoreFailed`, with the reason and the full backup path |
| Occupied | Proven the same file: `copy_safety::compareFileIdentity(backup, target) == Same`, and on POSIX a link count of at least 2 (a crash after the hard link and before the rename). `Unknown` is not proof and falls through to the rows below | The extra name is unlinked; no data moves, and the target still holds the file. The item is not started | Nothing to report |
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
| Volume with no file index | Identities never match. A held backup is reported, not removed, and an in-place write is decided by checksum |
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
  - `confirmTransfer` on a `"trash"` plan with the consent flag set, and the trash vanishing before execution: the flag is ignored, the target is unchanged and the operation stops;
  - on a volume that refused its trash, and POSIX without hard links, a re-plan reports `"permanent"`, so the consent can be given;
  - with consent on `Unavailable`, the target is replaced, the old copy is gone, undo is `None` and the header records the consent;
  - `trashBackup` reaches the fake passed to `open`;
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
  - an injected identity that reports no file index, with a different file at the target, including after a `TrashFailed`: the backup survives and is reported;
  - the backup vanished before start: no finding and no error;
  - the move back denied: `RestoreFailed` with the path, and the original still at the backup path;
  - `dismiss` is refused and pruning skips the operation while the backup exists, and both proceed once it is resolved;
  - a backup with no journal record is never touched or reported.
- **Intent ordering:** the backup path is durable before `ReplaceFileW` starts, proven by a kill hook at the replace.
- **Case-only rename:** a kill between the two steps restores the original spelling through the same table, and the file is never deleted as a partial.
- **Preimages (SidecarWrite and LosslessTransform):**
  - kill hooks between the intent and the seal, between the seal and the replace, and between the replace and the outcome give not started, not started with the preimage removed, and Done with `RestorePreimage`;
  - a bad preimage checksum after a replace stays Unknown, and **Retry remaining** does not offer the item;
  - with an injected identity that reports no file index, a same-size write killed after the replace is Done with `RestorePreimage`: the preimage survives and retry doesn't transform twice;
  - the same write killed before the replace is not started, and the preimage is removed;
  - retry never transforms a Done item twice;
  - a new sidecar has no preimage, and its undo moves it to the trash;
  - `sealPreimage` over the journal cap returns `LimitExceeded` and the item doesn't run.
- **Partial failure:** a 10-item move with item 4 denied and item 7 in use. Per-item outcomes are recorded, the summary counts are right, and undo is offered for the Done items only.
- **Undo availability:** five existing sidecars (`RestorePreimage`) and five new ones (`TrashCopy`) offer undo, as the union; one Done item with `None` offers none.
- **Undo:** restore from the trash with a fake token; token gone (expired trash) gives Missing and no fallback; renamed back; copy trashed, never removed; the moved file changed after the move gives Changed and is skipped; an Overwrite whose old copy reached the trash restores it after trashing the new one; an Overwrite with permanent consent advertises no undo.
- **Trash round trip (native):** on each platform, a real trash and restore by token, or a recorded `None` with the reason. `probeTrash` against a real volume returns `Available` on the gate machine's system drive.
- **Asset rename:** a kill partway reports the split; a kill during a reversal does too; with the journal unavailable an asset rename is refused; a failure on the second member reverses the first and reports it; a fault that blocks the reversal reports the exact state; a target collision blocks the whole rename before any mutation.
- **Asset Overwrite:** an asset Copy onto a folder where two members collide asks once, and an injected replace fault on one member keeps that member's target byte-identical while the report names it.
- **Cancellation:** cancel between batches, so no intent is recorded beyond the batch; cancel during a copy, so the partial is removed and the original target is untouched.
- **Sweep:** with the journal available, a second process's partial survives; with it unavailable, only names older than 24 hours go.
- **Bounds and pruning:** the 201st completed operation prunes the oldest; Interrupted operations and those holding a backup are never pruned; the `kMaxJournalBytes` cap holds, including preimages.
- **Journal unavailable:** a read-only journal directory, a full-disk simulation and a `LimitExceeded`. `probe()` makes the notice appear in the confirmation; trash, copy, move, single-file rename and create folder run with the notice and no undo; sidecar write, lossless transform, batch rename and asset rename are refused with the reason and attempt no mutation. A write failure mid-operation stops before the next batch mutates. A `begin` failure after the confirmation returns to the prompt once.
- **COPY-SAFETY-01 regression:** every `copy_safety` and `copy_move` test passes, changed only by the sweep and the granted test migration, with every assertion on target bytes, attributes and permissions kept. `.~gb` files are still never swept by `removeStaleTemporaries`.

## Not covered

- No automatic retry or resume after restart. Every re-execution is re-planned and re-confirmed.
- No undo for permanent deletion, expired trash, overwrites without a trashed original, or external changes.
- No journaling of view state, settings (recovery already covers those) or caches.
- No cross-machine sync, history UI beyond the operation center, or export of path lists.
- No transactional rollback across items: each item is atomic on its own, and the batch is not. The one exception is asset rename, which reverses its members (see ASSET-GROUP).
- No disk scan for backups without a journal record.
