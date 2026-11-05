# PHASE2-CONTRACTS review

**Verdict: CHANGES REQUIRED**

- PR: https://github.com/dvortsis/glancelot/pull/5
- Reviewed head: `53dab6ce1a5a89e80a6d887dd3a7c1989fdf613e`. Base `b1d57cd`; `git diff --stat b1d57cd origin/main -- src include ui tests CMakeLists.txt cmake` is empty, so every code reference still describes current `main` (`3a14044`).
- Reviewer: fresh context, wrote none of this. Read section 6 of the lead plan, the handoff, the decision record, all seven contracts and five new headers, then read the code the contracts cite. Line numbers in `viewer_controller.cpp` are at the reviewed head (base + 2 after line 100).
- Build and test output: section 11.

## 1. Summary

Most of this is careful work. The header rule held: nothing reachable changed, and all but three of the code claims I spot-checked are right (section 10). J1 is written as the lead confirmed it, and J4 is too, apart from the no-trash case in B1. Eight findings block. Most are small additions to frozen text or a frozen header, which are cheap now and costly after six tracks start.

| # | Contract | Blocking finding | Why it blocks |
| --- | --- | --- | --- |
| B1 | OPERATION-JOURNAL | A volume with no usable trash: the replaced file's fate is not stated before confirmation, and the contract's reason is false | The lead's J4 condition; FS-01; B2 has no frozen way to ask |
| B2 | RESOURCE-LEDGER, DECODE-TIER, COMPARE-PANES | Built on `shared_memory_reaper.hpp`, `TransferPtr` and QSharedMemory, which PR 3 round 5 removes | Frozen E1 acceptance test names removed symbols; freezes an unbounded leak |
| B3 | DECODE-TIER | The apply rule drops EN-11's same-generation refresh, so a stale image stays on screen | D1 must break the frozen rule or fail an existing test |
| B4 | OPERATION-JOURNAL | No way to read an operation's items, so undo and Retry remaining cannot be built | Missing operation in a frozen header |
| B5 | OPERATION-JOURNAL | A preimage is recorded only after the write, and reconcile has no row for in-place writes | Data safety for C1 and D7 |
| B6 | RESOURCE-LEDGER | `commit` takes no share key, but the M1 table requires one at commit | Missing parameter in a frozen header |
| B7 | META-ROLES | No whole-folder key pass without a sort, so rating groups, chips, jump to date and bursts cannot work | Missing operation for A3, A4, A7, C5 |
| B8 | CAPABILITY, DECODE-TIER | D2 has no way to read the applied image's colour source | The frozen diffs give it none, and the contract says the controller is not edited |

## 2. Blocking findings

### B1. OPERATION-JOURNAL: a volume with no usable trash

Lead's PR comment: "B2 must also cover a volume with no usable trash, where the replaced file's fate is stated before the user confirms, never silently lost."

`docs/contracts/OPERATION-JOURNAL.md:74` says: "If the volume has no usable trash, the old copy is removed as it is today and the item's undo is `None`. The user asked to replace the file, and the confirmation already says the original goes away." Decision J4 repeats it. That does not meet the comment:

1. **The stated reason is false.** No Overwrite confirmation exists on `main`. Nothing in `ui/` mentions Overwrite or a collision choice, and the only caller, `CopyMoveService::pasteTo`, hardcodes `CollisionPolicy::RenameBoth` (`copy_move_service.cpp:707`). The first Overwrite prompt arrives with B1 or B2, and no contract says who owns it or what it says.
2. **There is nothing to ask.** `TrashBackend` (`file_operations.hpp`) has only `moveToTrash`, and B2's granted diff to that header only adds `trashToken`. The order of J4 means `TrashUnavailable` is learned after the replace, so after confirmation. B2 would need an interface the contract does not grant.
3. **A failed trash deletes the original.** The sentence covers "no usable trash" only. A transient failure (`InUse`, `Denied`, a full bin) is undefined, and a backend that reports `TrashUnavailable` at run time leads straight to removal of a file the user was told would land in the trash.
4. **Two more routes lose the old copy, with no statement either.** The Windows fallback `replaceWithoutMerge` (`copy_move_service.cpp:453`, `MoveFileExW` with replace, for volumes without `ReplaceFileW`) leaves no backup at all, and POSIX "a volume without hard links" (`:73`) has none either.
5. **No test can fail for this.** The only acceptance line is "an Overwrite on a volume with no trash advertises no undo" (`:153`).

FS-01 says "No silent permanent-delete fallback", META-07 says "trash fallback confirmation", and FEATURE-OPPORTUNITIES section 13.5 says overwrites "retain explicit conflict/recovery semantics".

**Fix** (contract text, plus one frozen diff):

- Add a B2 diff to `file_operations.hpp`: `enum class TrashAvailability { Unknown, Available, Unavailable };` and `virtual TrashAvailability probeTrash(const std::filesystem::path& directory) { return TrashAvailability::Unknown; }` on `TrashBackend`, worker only. Windows: `SHQueryRecycleBinW` on the volume root. POSIX: home trash on the same device, or a usable `.Trash-$uid`. Tests use the fake backend.
- Name the owner of the collision prompt (B2) and make the plan step run the probe per destination volume. The prompt then names one of: "replaced files go to the <trashName>", or "this location has no <trashName>: replaced files are permanently removed and can't be undone". `Unknown`, the `replaceWithoutMerge` route and POSIX without hard links count as the second wording, unless they can be told apart at plan time.
- If trashing fails after the replace, despite the prompt: **never delete**. Keep the file (`<stem> (replaced)<ext>` or the `.~gb` name), record the item `Done` with `undo = None` and a message saying where the old copy is, and raise a `BackupFinding`. Removal happens only where the prompt said so and the probe still says `Unavailable`. Reconcile must also visit such items (today it takes only interrupted or `OriginalDisplaced` operations, `:86`), and `dismiss` and pruning already hold them.
- Add acceptance tests: the fake backend reports `Unavailable` and the summary carries the permanent wording before `confirm`; the fake backend reports `Available` at plan and fails at trash, and the old copy still exists with a finding; `Unknown` uses the permanent wording; the `ReplaceUnsupported` fault path has the same wording.
- Correct `:74` and decision J4.

### B2. Three contracts assume PR 3's reaper and QSharedMemory

The brief states that PR 3 round 5 replaces the Windows QSharedMemory transfers with a lock-free file-mapping class and removes `shared_memory_reaper.hpp`. Round 5 is not pushed yet (`claude/stack-confirm-fix` is still `9c0c919`), so this reads the contracts against that stated direction. The contracts name the code it removes:

| Where | Text |
| --- | --- |
| `RESOURCE-LEDGER.md:56-60` | `TransferPtr`, the reaper closure, `MaxPendingSharedMemoryReaps`, `src/app/shared_memory_reaper.hpp`, "a reap blocked on a dead worker's semaphore", "a leaked segment stays charged for the life of the process", `leakedSharedMemorySegments()` |
| `RESOURCE-LEDGER.md:102` | Acceptance test "Reaper": a held reap, the cap, and `leakedSharedMemorySegments()` |
| `RESOURCE-LEDGER.md:87`, decision R6 | "the segment lifetime above"; the reaper rule |
| `DECODE-TIER.md:126` | "D1 keeps PR 3's rules in `src/app/shared_memory_reaper.hpp`: the GUI thread never calls `lock()` or `unlock()`"; "goes through `TransferPtr`" |
| `DECODE-TIER.md:153` | "A worker dies holding the transfer segment's semaphore: PR 3's reaper handles it" |
| `COMPARE-PANES.md:63` | "`TransferPtr` teardown from PR 3" |

E1 cannot pass a frozen acceptance test that names removed symbols. Separately, "stays charged for the life of the process" freezes what `STACK-CONFIRM-REVIEW-R4.md` R4-B2 rejected as breaking "Bound allocation".

The `resource_ledger.hpp` header does not assume any of this. The `Transfer` ledger and `Reservation` are transport-neutral, so only the text changes.

**Fix.** Replace those passages with a transport-neutral rule: "The Transfer reservation lives with the transfer buffer. It is released after the buffer is really unmapped and closed, wherever that happens, and never earlier. The GUI thread never takes a cross-process lock or blocks on a transfer buffer. Each tier and each pane has its own buffer. The class is whatever PR 3 lands; E1, D1 and C3 start after PR 3 merges." Replace the Reaper test with "a transfer buffer whose teardown is held open by a test hook stays charged; releasing it returns to baseline". Add "worker killed mid-transfer: the buffer is released and the GUI does not wait" to DECODE-TIER, and remove the leak clause. Amend decision R6. Note that D1 and C3 now also start after PR 3, or re-read the transfer class when rebasing.

### B3. DECODE-TIER: the apply rule drops the EN-11 refresh

`DECODE-TIER.md:132-137` says a result applies only when the generation matches, tokens agree when both are non-empty, and `tierRank(R.tier) > tierRank(shownTier)`. Line 137 says a revision change "already starts a new generation (the `++generation` at `viewer_controller.cpp:230` and `:1095`), so it is the only way to reset the floor."

Those citations are wrong: `:232` is `showUnavailable` and `:1097` is an empty-folder visit. Ordinary hot reload does take a new generation through `q->open` (`ViewerController::open`, `:1704`). But there is a path that deliberately does not. In `viewer_controller.cpp:425-430`, EN-11's late baseline runs `cancel();cacheReuseAllowed=false;quietDecode=background;decode(path,generation);`, with the comment "It keeps the generation". It exists for exactly this case: the shown pixels are not proven current.

The result of that decode has the same generation, equal rank and a different token, so the frozen rule drops it twice. `tests/fs_watch_tests.cpp:756` `controllerLateCoalescedBaselineDecodesOnce/fresh-pixels` opens a red file, rewrites it blue while the baseline is delayed, and expects blue on screen. Under the frozen rule the viewer stays red, which breaks META-04. D1 must either fail that test or break the rule. The fencing test "an equal-tier decode result arriving twice is dropped by the controller" (`:183`) freezes the same behaviour, and the table of cache readers and writers (`:157-168`) misses this site.

Related: a token mismatch has no defined next state. If Fit shows under T1 and Full arrives under T2, the result is dropped, nothing restarts it, `imageFinal` stays false and `scheduleLookahead` waits for it forever (`:138`).

**Fix.** Rewrite the rule around the revision, not only the generation. The controller marks the decode started at `:430` as a refresh, and within one generation:

- a result from a refresh request replaces whatever is shown, at any rank, and resets the floor to its own tier;
- otherwise rank must rise, as written;
- a non-refresh result whose non-empty token differs from the shown non-empty token is not simply dropped: it starts one fresh ladder in the same generation, marked as a refresh, under the existing one-in-flight rule. Bound it to one restart per observation, so a file being rewritten cannot loop.

Alternatively `:430` takes a new generation, but the EN-11 comment records why it does not (it must not disturb the open's own listing or a newer request). Correct the citation in `:137`, add the site to the table, and add tests: the late-baseline refresh replaces an equal tier; the same refresh with a Fit ladder returns to Fit then Full; Fit under T1 and Full under T2 ends with `imageFinal` true. `DisplayManager` needs no change, since it accepts an equal tier.

### B4. OPERATION-JOURNAL: no way to read items

`operation_journal.hpp` offers `list()` (`OperationSummary`, counts only), `reconcile()` (counts and `BackupFinding`s) and write-only `Operation`. Nothing returns an operation's intents and outcomes. The contract requires them:

- Undo "revalidates every item" using `resultIdentity`, `trashToken`, `overwrittenTrashToken` and `preimagePath` (`OPERATION-JOURNAL.md:68`, table at `:58-66`).
- **Retry remaining** is "a new operation over the not-started items" (`:94`).
- B4's restart recovery, the operation center's detail view, and acceptance "undo is offered for the Done items only" (`:152`).

The only route left is parsing `.gjl` files outside the class, which is the interface broken.

**Fix.** Add to `OperationJournal`, bounded and worker only:

```cpp
struct ItemRecord { ItemIntent intent; std::optional<ItemOutcome> outcome; };
// Paged; at most kMaxIntentBatch records per call. Empty past the end. Conflict for an unknown id; Corrupt reads the good prefix.
std::vector<ItemRecord> items(OperationId operation, quint32 first, std::size_t max, Status* status) const;
```

Also add `bool reconciled{}` to `OperationSummary`; see OJ-N4.

### B5. OPERATION-JOURNAL: preimages are recorded too late

`preimagePath` exists only on `ItemOutcome` (`operation_journal.hpp:73`), which is written after the mutation, and undo capability is "set per item, at outcome time" (`OPERATION-JOURNAL.md:56`). The contract's own precedent is "a sealed preimage precedes replacement" (`:131`). For C1 and especially D7, which rewrites an original JPEG, a crash between the replace and the outcome leaves:

- the new bytes in place;
- the only copy of the original in the journal directory with no record pointing at it, charged to no operation and open to pruning;
- a reconcile with no row for it. Lines 88-93 are written for source and target pairs. A transform has `source == target`, whose identity or size has changed, so it lands on "Unknown" and **Retry remaining** would transform the file a second time.

This is the case the journal exists for: `requiresJournal` refuses these kinds precisely because they rely on it. The acceptance list has no such test. Using `backupPath` instead does not work: reconcile would report `TargetOccupied` for a file that is not beside the target.

**Fix.**

- Add `QString preimagePath;` to `ItemIntent`, durable before the replace (intent, then seal the preimage and record it). Keep `ItemOutcome::preimagePath` or remove it.
- Add a reconcile row for `SidecarWrite` and `LosslessTransform`: target identity and size match `source` means not started and the preimage is removed; target differs and the preimage is sealed and intact means Done with `RestorePreimage`; preimage missing or bad checksum stays Unknown with evidence; a sidecar that did not exist before means the intent records no preimage, and undo moves the created file to the trash.
- Add kill-hook tests between preimage seal, replace and outcome, for both kinds, and "retry never transforms a Done item twice".

### B6. RESOURCE-LEDGER: `commit` has no share key

`resource_ledger.hpp:10` says every allocation "is reserved before it is made". The M1 table (`RESOURCE-LEDGER.md:87`) charges "CpuPixels `bytes` (parent copy, committed at `decoded` with `shareKey = image.cacheKey()`)". But `Request::shareKey` is set in `tryReserve`, at `probe`, when no `QImage` exists, and `bool commit(qint64 actualBytes)` (`:81`) takes no key. A share hit needs an "owned entry with the same key" (`:31`), so `DisplayManager`, the decode cache and compare panes can never find this buffer. Every holder double counts, which breaks "Why nothing is double counted" and the "Two windows, same source" and parity tests.

The workarounds fail: reserving after the copy breaks reserve-first, and reserving twice can spuriously refuse at the limit.

**Fix.** `bool commit(qint64 actualBytes, quint64 shareKey = 0);` with "a nonzero key publishes the buffer for share hits; if an owned entry with that key already exists, the handle becomes a share and its charge returns at once". Add a unit test and a line in the table. Add a compile check to `contract_headers_tests.cpp` if useful.

### B7. META-ROLES: no whole-folder key pass without a sort

Only two paths read keys: `requestMetadata`, clamped to 512 visible rows (`META-ROLES.md:22`, `:69-74`), and the order path, which runs only for `captured` and `dimensions` sorts (`:23`, `:132`). Its own consumers need more:

| Consumer | Needs | Under the frozen interface |
| --- | --- | --- |
| A3 F013, group by rating or date | Every row's value, for membership and counts. Sorting by rating does not exist | Off-screen rows all look "unknown", and rows jump between groups on scroll, against FEATURE-OPPORTUNITIES 13.3 "must not continuously shift the keyboard target" |
| A4 F042 rating, orientation and date chips | Value per row, to filter and count (`docs/F013-F042-UI-PLAN.md`: "pending items stay visible... until proven not to match") | Pending can never resolve for off-screen rows, so a chip filter never settles |
| A7 F008 jump to date | Dates when the sort is by name | Only the visible window |
| C5 F028 bursts | Capture times across the folder | Decision M5 says "C5 reads the `captureTime` role, which covers everything it groups". That holds only under a `captured` sort |

Each would have to add a `Q_INVOKABLE` to `DirectoryEntryModel` and invent its own budget and fencing.

**Fix.** Add to the frozen `directory_entry_model.hpp` diff:

```cpp
// Whole-folder key pass with no reorder: the order path's worker, budget, cancellation and fencing. Rows become ready through
// the same dataChanged batching. Idempotent per listing generation; cleared by setDirectory, setActive(false) and a new listing.
Q_INVOKABLE void requestAllMetadata();
Q_PROPERTY(bool metadataPending READ metadataPending NOTIFY sortStateChanged)
Q_PROPERTY(bool metadataPartial READ metadataPartial NOTIFY sortStateChanged)  // budget ended; "Counts say when they are incomplete"
```

Add A3, A4, A7 and C5 to the consumer table with "calls `requestAllMetadata` only while its feature is on", correct decision M5, and add tests: the pass leaves order unchanged, is cancelled by `setDirectory`, and reports partial at the budget.

### B8. CAPABILITY: no path for the applied image's colour source

`CAPABILITY.md:53` has D2 read `colour.source` from the worker's `decoded.color` "structured by D1", fenced by selection generation. D2's granted files (the handoff's table of diffs; `CAPABILITY.md:43`, "`viewer_controller` is not edited") are `viewer_context.hpp` and two regions of `Main.qml`. DECODE-TIER's frozen `viewer_controller.hpp` diff adds only `imageTier` and `imageFinal`. Today the value reaches only the `status` string (`viewer_controller.cpp:1049`), the controller exposes no generation (`viewer_controller.hpp`), and D2 itself removes the status suffix. So D2 can get neither the fact nor the fence, and parsing a translated status string is not an option.

**Fix.** Add to DECODE-TIER's `viewer_controller.hpp` diff:

```cpp
// {"tagged":bool, "description":string} for the applied image; empty when none. Set with imageTier under the apply rule.
Q_PROPERTY(QVariantMap imageColour READ imageColour NOTIFY imageChanged)
```

The populator reads it on `imageChanged`, so the apply rule already fences it, and "generation N arriving after N+1" (`:149`) becomes a controller test. Add the plan edge **D1 -> D2**: the HUD also reads `imageTier`, and the plan lists D2 as depending on CAPABILITY only. If D2 must land first, D2's brief grants this one property, with `tagged` parsed from the existing two strings.

## 3. OPERATION-JOURNAL, non-blocking

- **OJ-N1. The header contradicts confirmed J1.** `operation_journal.hpp:113`, `:116-117`: "Unwritable or LimitExceeded refuse the operation". The contract (`:76-80`) says non-`requiresJournal` kinds proceed. Also, the notice belongs in the confirmation, but `begin` runs after it (`:50`, `:128`), so a journal that fails at `begin` gets no notice. Fix: reword the comment ("the caller applies `requiresJournal`"), and have the plan step probe with `open` plus a test append. A `begin` failure after confirmation returns to the prompt once, with the notice, without mutating anything.
- **OJ-N2. The limits disagree, and a limit can refuse a delete that works today.** `kMaxOperationFileBytes` / `kMaxItemsPerOperation` is 671 bytes per item, and one copy needs an intent (three paths, identities, JSON keys, CRC) plus an outcome, so a large copy reaches 64 MiB well before 100,000 items. Mid-operation the only response is stopping (`:80`). Also, `LimitExceeded` is not in the failure policy, and Interrupted and displaced operations are never pruned, so a full journal refuses every Trash. Fix: raise the file cap to 256 MiB or the item cap to what fits; `begin` estimates from `itemCount` and path lengths; and for non-`requiresJournal` kinds `LimitExceeded` takes the same notice route as an unwritable journal.
- **OJ-N3. Reconcile can report Done wrongly.** `:91` "Target present with the snapshot size" is true of an Overwrite that never replaced a same-size target, so its partial is orphaned and the file is reported as replaced. Fix: when `overwritten` is set, compare the target's identity with it first; a match means not started. Add the order of the rows, because partial and target can both exist, and a row for `Kind::Trash` (source gone, no token: Done with undo `None`).
- **OJ-N4. Reconcile has no durable result.** `State` has no reconciled value and "lock free and state not terminal" stays true after a pass, so `:120` "the other finds nothing left", `dismiss` "reconciled only" and "`Restored`, once" have nothing to rest on. Fix: reconcile appends outcomes and a `{"type":"state","reconciled":true}` record; expose `OperationSummary::reconciled`; the selection skips reconciled operations unless they hold a backup.
- **OJ-N5. Asset rename rollback against "no transactional rollback".** `ASSET-GROUP.md:87` and decision A8 reverse members on failure; `OPERATION-JOURNAL.md:166` says none. A reversal is a mutation, and no `Outcome` or rule covers it. A crash mid-rename, or mid-reversal, is undefined, and under J1 an asset rename could run unjournaled and split an asset with no record. Fix: an asset rename uses `Kind::BatchRename`, so it requires the journal; a reversal is a new operation with `undoOf`; reconcile reports "<stem> is split: renamed A, not B" with **Retry remaining**; add a kill test; narrow `:166` to "except asset rename".
- **OJ-N6. The sweep crosses processes.** `removeStaleTemporaries` spares only `inFlightTemporaries()`, a per-process set (`copy_move_service.cpp:153-158`, `:515-528`). B2 moves every copy onto `.~gl` (`:52`), so window B can remove window A's partial, against "only after the operation's lock is free" (`:52`). POSIX unlinks silently. The size check then fails, so the source survives, but the copy fails. Fix: sweep only names older than a bound and not recorded by a running operation, or drop the sweep once reconcile owns partials, with the COPY-SAFETY-01 test updated by grant.
- **OJ-N7. Case-only rename's restore name.** The table (`:100-106`) says "target name" and the test (`:151`) says "the original name". For a case-only rename those differ. Fix: restore to `source.path` when `overwritten` is empty, and say so.
- **OJ-N8. POSIX name in the trash.** `:72` renames to `<stem> (replaced)<ext>` on Windows only, so POSIX trash shows `.~gb1a2b3c4d.tmp`. Fix: same rule, by making the hard link under that name. Record the rename in the outcome, since a crash between rename and trash leaves a visible file that reconcile treats as "backup vanished".
- **OJ-N9. Power loss.** `:41` syncs the file but not the directory. On POSIX a new `op-*.gjl`, and so the only pointer to a `.~gb`, can vanish on power loss. Fix: fsync the directory after create and after the backup link.
- **OJ-N10. D7's cap.** 256 MiB of records and preimages allows about 25 rotations of 10 MB JPEGs per operation, then `LimitExceeded`. That is safe but visible; say so in D7's brief, with its wording.

## 4. ASSET-GROUP, non-blocking

- **AG-N1. No aggregate bound.** A request of 100,000 with 16 members each gives 1.6 million `Planned` entries plus a QML summary, and only then a `LimitExceeded` from the journal. Fix: `kMaxExpandedItems = journal::kMaxItemsPerOperation`, past which entries are `LimitReached`, and a cap on summary rows with a count, like `kMaxBackupFindings`.
- **AG-N2. Probing is too small and too slow.** The tables hold 25 extensions (12 RAW, 6 rendered, 4 derived, xmp, mov, mp4). Lower and upper case on a case-sensitive volume is 50, above `kMaxProbesPerTarget` (48), and mixed case can never be found by name. And `plan()` becomes `planScoped("file")`, which also lists companions, so a 10,000-file delete on SMB costs 250,000 stats before the confirmation. Fix: with a hint, stat only the hint's members. Without one, one bounded directory read per parent replaces the name guesses (still worker only, and it catches mixed case). Add a per-plan time budget, after which the confirmation says "Companions couldn't all be checked" and companions stay excluded.
- **AG-N3. `IMG.jpg` + `IMG.jpeg`.** `:52` and `asset_group.hpp:26` say "more than one Rendered family", which this is not; the test (`:102`) says "Collision by case or duplicate family rule, asserted". Fix: add "or two files of one family" to both, or an asset delete takes a file the user may not expect.
- **AG-N4. `Incomplete` cannot be set.** `groupListing` is pure and has no truncated input (`asset_group.hpp:55`). Fix: add `bool truncated`.
- **AG-N5. Scoped entry points have no diff.** `:35` promises scoped `enqueueTransfer` and `renamePath` "in B1 and B4", but the shared headers are lead-owned and the grant table lists only `file_operation_service.hpp`. Fix: freeze the two diffs, including the Overwrite prompt's summary from B1 in section 2.
- **AG-N6.** `expand(..., SidecarWrite, ...)` is "not an expansion" and its return is undefined. Fix: returns the request unchanged; C1 resolves the name.

## 5. RESOURCE-LEDGER, non-blocking

- **RL-N1. Headroom is one decode short.** `kMaxForegroundHeadroomBytes` is 768 MiB, "256 MiB of pixels plus 2x scratch". The M1 table charges scratch 2x, Transfer 1x and CpuPixels 1x at once (the parent copy reads the mapping), which is 1 GiB at the validated maximum. And the 1 GiB fallback minus the 64 MiB reserve gives a limit of 960 MiB, so that image is refused where today's `bytes*4 <= 1 GiB` passes. Fix: set the constant to 1 GiB (and update `contract_headers_tests.cpp:42`), and raise the fallback or say in M2 that this is accepted.
- **RL-N2. The IoScheduler row does not reproduce today at M1.** `tryAdmit` counts every job's estimate against one 256 MiB, but the table moves thumbnail estimates to WorkerScratch and calls `admittedBytes()` a view over Transfer, so thumbnails stop throttling decodes. "The sole foreground job may exceed" stays in `tryAdmit`, but `tryReserve` has no matching exception, so a lowered limit (`setMemoryLimit`, E3 low memory) refuses it with `LedgerCeiling`. Fix: at M1, keep bytes for every job in Transfer and move thumbnails at M2, and add `Request::soleForegroundMayExceed` or keep the byte check in the scheduler until M2.
- **RL-N3. Owner and setter rules.** `unregisterOwner` requires no live reservations, but retirement closures and render-thread renderers release late, and "closing each window in any order" is a test. Fix: unregister marks the owner retired, and it goes when its last handle releases. State the thread rule for `registerOwner` and the setters (any thread).
- **RL-N4. Test isolation.** The constructor is private and `shutdown()` is irreversible, so one test process gets one ledger. Fix: a public constructor for tests, with `instance()` for production, or say that those tests run in a child process.
- **RL-N5. Charge migration.** It can push the inheriting ledger over its ceiling (say so: allowed, no refusal, reclaim runs), and `GpuTextures` in the order at `:38` never shares a physical buffer with CPU pixels. Remove it from the list.
- **RL-N6. `Quality` reclaim conflicts with the decode fence.** `:48` and `docs/PERFORMANCE-STRATEGY.md:126` lower a fit preview's resolution under pressure. DECODE-TIER says nothing lower replaces what is shown within a generation, and `:48` also says the current image is never reclaimed. Fix: `Quality` means "the next request takes a lower bucket, and a pending Full does not start", never a swap of shown pixels. Say so in both.
- **RL-N7. No tier-to-priority mapping.** Neither contract says what priority Fit and Full take, and it decides who hits the headroom. Fix: first shown tier Foreground, refinement Visible, in DECODE-TIER.

## 6. DECODE-TIER, non-blocking

- **DT-N1. Fit has no allocation bound.** "Per-tier probe limits as today" (`:141`) is ambiguous. The worker rejects on *source* capacity above 256 MiB (`src/worker/main.cpp`, after `:232`), which would keep the >64 MP fix from working if it stays, while for formats whose reader does not scale during decode (PNG, TIFF, WebP) lifting it means a full decode of an arbitrary size in the worker, and RESOURCE-LEDGER's "WorkerScratch 2*bytes" counts only the bucket. Fix: `kFitMaxSourceBytes` per format class: JPEG may exceed 256 MiB because it scales in the codec (with a hard cap, set by D1's measurement); the others keep today's cap and go straight to the refusal text. WorkerScratch for Fit charges from source size for non-scaling formats. Add a test with a generated PNG above the cap.
- **DT-N2. Re-publishes lose the tier.** `viewer_controller.cpp:481` and the timed paths `display_manager.cpp:138`, `:654` use the defaults. With the ladder on, Fit pixels are labelled Full and lose `sourceSize`, so presentation geometry is wrong. "Default Full: identical" (`:168`) is true only before the ladder. Fix: D1 passes the shown tier and size at `:481`; `DisplayManager` keeps them with the pending frame.
- **DT-N3. Multiple Fit buckets.** "At most 3 whole-image tiers per source" (`:141`) cannot hold, since `encoded()` includes the bucket and a resize adds another. Fix: a Fit insert also removes other Fit buckets of that source and token.
- **DT-N4.** The table row "source dimensions differ between tiers" (`:151`) drops the "of one token" condition in `:120`. A file replaced between tiers with new dimensions is a revision, handled by B3's restart, not an invalid response. An empty token on either side should restart rather than fail.

## 7. COMPARE-PANES, non-blocking

- **CP-N1. Nothing connects the session to the surface.** `CompareSession` takes a generation source, and its only outputs are `changed`, `decodeRequested`, `cancelRequested` and value lists. `ICompareSurface` has no attach point, and `deliver` receives pixels with no way out. C3 can work around it by wiring images beside the session, which duplicates swap and removal state. Fix: `void setSurface(ICompareSurface* surface);`, plus the ledger owner as a constructor argument or `setOwner`.
- **CP-N2. `begin` and folders.** `:94` returns false for a folder, while `:122` forbids file access in the session and it is GUI-thread only. A stat on a UNC path can stall. Fix: callers pass files known from the model; a folder that slips through becomes `Failed` from the decode path.
- **CP-N3.** `compare_panes.hpp:14`, "through the ordinary path", contradicts P7. Reword as "through the pane decode path".
- **CP-N4. Inactive is not hidden.** `:75` and the test (`:111`) reclaim textures "when the window is inactive", but an unfocused window stays visible, and RESOURCE-LEDGER `:48` never reclaims the last valid frame of a visible pane. Fix: "not exposed" (`QWindow::isExposed` false or minimized).
- **CP-N5. 4-up before D1.** 64 MiB per pane is 16 MP, and 2-up is 32 MP, so typical cameras get `TooLarge` in 4-up until D1 lands. P2 and P3 accept that honestly, but it would make F026 look broken for typical use. Fix: the lead orders C3 after D1, or the text suggests 2-up with the limit named.
- **CP-N6.** Settle `TooLarge` at probe, before any transfer is allocated. Give `compare:<slot>` a stated CpuPixels quota. `deliver` applies the same rank rule as B3 per pane.

## 8. META-ROLES, non-blocking

- **MR-N1. "Calls `setSortKeyReader(...)` once" cannot work.** `Navigator` is value-constructed fresh at `viewer_controller.cpp:235`, `:1199`, `:1343`, `:1358`, `:1840`, `:1955` and `:1968`, and any missed site silently gives natural order, so traversal disagrees with the grid (NAV-06). Fix: a process-wide `setDefaultSortKeyReader` in `navigation.hpp`, set once and read by `list()`, or a helper every site must use, plus a test crossing a folder boundary under `captured`.
- **MR-N2. Order-path admission is undefined.** `admitIoJob` returns empty on refusal, and `tryAdmit` refuses non-foreground work whenever the background slots (one on HDD or network, two on SSD) are busy, so thumbnails and the pass compete. The failure row covers rows only, through a GUI-thread signal. Fix: wait with cancellation for a bounded time, counted in the budget; state fairness (one thumbnail slot stays free while the grid is visible, on storage with two slots).
- **MR-N3. Time budget only on network.** 20,000 reads on a rotational disk take minutes. A folder crossing under `captured` has no "previous order" to show, so it waits. Fix: the time budget also applies to `Rotational` and `Unknown`, and the contract states the landing rule: wait for the pass or budget, cancellable, with `navigating` shown.
- **MR-N4. Partial orders can differ.** The grid and navigation workers each spend their own budget, so their unread sets differ and `sortPartial` breaks parity. The controller exposes no pending state. Fix: one pass per folder and revision, recorded in `MetadataKeyCache`, with the second reader using cache only, plus a test.
- **MR-N5.** The overlay does sidecar I/O for every file in a pass, outside the 64 KiB bound. Fix: apply it only when ratings are wanted (`requestMetadata`, `requestAllMetadata`).

## 9. CAPABILITY, non-blocking

- **CA-N1. `serverPaths` flickers.** `setSourceScope` clears the Source section (`capability_model.hpp:79`), which also holds the startup fact `source.serverPath`, so F2's indicator drops on each folder change or probe timeout. Fix: `serverPaths()` reads a process fact set once, not the section.
- **CA-N2. Colour has three cadences on one key.** Build facts, output status and per-image colour all replace (Colour, scope) atomically with one revision. It can be done, by resending everything. Fix: say that one builder in D2 owns each (section, scope) and resends the section; add a test that a per-image update keeps `colour.output.*`. Also state that a runtime `Unsupported` for the reference window survives a `displaysChanged` rebuild.
- **CA-N3. Owners.** Bit depth waits for "a later parser packet" (`:167`), which the plan does not have. Name it. F1 sets the variable and F1 depends on E1, so name what D2's Docker text says until then.
- **CA-N4.** `:69` says GuiPrivate is linked by the app and UI tests. `glancelot_platform` does not link it (`CMakeLists.txt:51`), so the populator belongs in the viewer sources, or `cmake/CapabilityTests.cmake` adds it.

## 10. Code claims and headers

**Spot-checks** (read at the reviewed head):

| Claim | Result |
| --- | --- |
| `FileLock` is OS-released, with no stale locks (`file_lock.hpp`) | Pass |
| `moveToTrash` at `file_operation_service.cpp:173`, `PostDeleteItem` at `:83` | Pass |
| `DeleteFileW` of the backup at `copy_move_service.cpp:444`, direct write at `:928`, `QFile::remove` at `:1014` | Pass |
| `.tmp_rename_` at `mutation_service.cpp:182` | Pass |
| `.~gb` never swept; `.~gl` swept (`copy_safety.hpp`, `:515-528`) | Pass |
| `updateImage` rejects only `generation < current` (`display_manager.cpp:602`) | Pass |
| Timed publishes `:138`, `:654`; `canPosition` `:350`; budget `:27`; `setReferenceImage` `:727` | Pass |
| Allowance `viewer_controller.cpp:727`, `:1020`; 40 % Windows only; `cache.clear()` `:1021` | Pass |
| `currentImageRequested` `:479-481`; status strings `:1001`, `:1047` | Pass |
| `tryAdmit` sole-foreground exception (`io_scheduler`) | Pass; the row's mapping is RL-N2 |
| `retireIoProcess` releases at NotRunning (`io_job_lease.hpp`) | Pass |
| Worker allowlist duplicated at `src/worker/main.cpp:103`, `:232`; `color` at `:216`, `:265` | Pass |
| `modeValid` and every `pane_layout.cpp` site; `Main.qml:76` holds for compare modes | Pass |
| `bitDepth` and `colorSpace` defaults and overwrites in `metadata_parser.cpp` | Pass |
| `settings_policy.cpp:140`; `file_operations.cpp:121`, `:167`; `Dockerfile:35`; `docker/start.py:58` | Pass |
| `Main.qml:755`, `:2115`; `SettingsDialog.qml:845`; `app/main.cpp:269`; `ui_test_support.hpp:82` | Pass |
| `presentation_window.cpp:128-132` (in `src/render/`); `image_canvas.hpp:50`, `:56` | Pass |
| `navigation.cpp:241`; no string parses to the new keys (`:591-595`, `:1987-1991`, `directory_entry_model.cpp:64`); no integer cast of `SortKey` anywhere | Pass |
| `sort` command cycle four keys `:1717` | Pass |
| `FileMeta` 24 to 48 bytes | Pass |
| `QPlatformIntegration::MultipleWindows` in Qt 6.8.3's header | Pass, existence only, as the handoff says |
| `Qt6::GuiPrivate` linked by app and UI tests | Pass; CA-N4 |
| "A revision change starts a new generation at `:230`, `:1095`" | **Fail**, B3 |
| "The confirmation already says the original goes away" | **Fail**, B1 |
| "The app calls `setSortKeyReader(...)` once" | **Fail**, MR-N1 |
| `DECODE-TIER` claims about reaper rules | True of PR 3 at `9c0c919`; going away, B2 |

**Reachable behaviour.** None changed. `sortBefore`'s new line (`navigation.cpp:224`) is reachable only with `SortKey::Captured` or `Dimensions`, and nothing can set those: all parse sites fall back to Name or return, and there are no casts. `sortKeyName` gains two cases behind the same condition. The removed `DisplayCapabilities` and `DecoderCapability` have no users. `FileMeta{a,b,c}` sites compile unchanged, and new headers are included only by the compile test.

**Adoption.** Frozen diffs keep callers valid: `updateImage` has defaulted trailing parameters and no pointer-to-member use (grep), `plan()` stays, `replaceFileAtomically` gains a defaulted argument. `display_manager.hpp` gaining `contracts.hpp` is harmless.

**`tests/contract_headers_tests.cpp`.** Meaningful for what it says. The per-header units and the moc object library are good, and the runtime block exercises the one behaviour line through three entry points with a fixture whose sizes disagree with name order. I re-ran one mutation (section 11). The `cmake` comment claims the keys "stay unreachable", which nothing asserts. Non-blocking fix: a `directory_entry_model` or viewer check that `setSortKey("captured")` changes nothing, deleted by A2, or drop the word. Some asserts only pin constants; they are change detectors, which is fine for frozen values. If B4, B6 or RL-N1 land, extend them.

**Requirements.** With the fixes above, viewing never writes media, no decode happens on UI threads (Fit scales in the worker, and compare and metadata stay on workers), and there is no cloud or telemetry. Contradictions found are B1 (FS-01, META-07), B2 ("Bound allocation") and B3 (META-04). DT-N1 is a bound to settle.

## 11. Build and test

Worktree `.worktrees/review-phase2-contracts`, build `build-review-phase2-contracts`, at `53dab6c`.

The whole tree built through `scripts/build-slot.ps1` (`[737/737]`, exit 0, MSVC 2022, Qt 6.8.3), including `contract_headers_moc`. Tests through `scripts/gate-lock.ps1`, offscreen:

```text
Test project C:/src/Github/Glancelot/.worktrees/review-phase2-contracts/build-review-phase2-contracts
    Start  7: viewer_acceptance
1/2 Test  #7: viewer_acceptance ................   Passed   21.86 sec
    Start 45: contract_headers
2/2 Test #45: contract_headers .................   Passed    0.24 sec

100% tests passed, 0 tests failed out of 2
```

Independent mutation: the natural-order line in `sortBefore` removed, `contract_headers_tests` rebuilt and run, the file then restored (`git status` clean):

```text
FAIL captured ascending: natural name wins over a smaller size
FAIL captured ascending: the comparison is antisymmetric
FAIL captured descending: natural, not lexical
FAIL dimensions ascending: natural name wins over a smaller size
FAIL dimensions ascending: the comparison is antisymmetric
FAIL dimensions descending: natural, not lexical
FAIL listDirectory keeps natural order
FAIL listDirectory keeps natural order
8 failed
exit: 1
```

Not run by this review: the full suite, `docker build`, a Linux build. The full suite is in the handoff, and the gate decides. B3 was established by reading the code and `tests/fs_watch_tests.cpp:756`, not by building the ladder, which does not exist yet. Round 5 of PR 3 was not available to read; B2 rests on the brief's description of it.

## 12. What to do

Fix B1 to B8 on `claude/phase2-contracts`, with OJ-N1, OJ-N2, OJ-N4, RL-N1, AG-N1, AG-N2, CP-N1 and MR-N1 in the same push, because they change frozen headers or constants. The rest can be text in the same push, or go into the implementing packets' briefs. Then a new fresh reviewer re-reviews the diff. J1 and J4 were not reopened; B1 adds only what the lead's comment asks.
