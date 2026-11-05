# PHASE2-CONTRACTS review, round 2

**Verdict: CHANGES REQUIRED**

- PR: https://github.com/dvortsis/glancelot/pull/5
- Reviewed head: `01fe089b2f762d67bbe6d6181968577f860ab721`. Diff read: `git diff 53dab6c 01fe089`. Round 1: `docs/verification/PHASE2-CONTRACTS-REVIEW.md` on `claude/review-phase2-contracts` (`979206a`).
- Reviewer: fresh context. I wrote neither the packet nor round 1.
- J1, J4 and the lead's B1, B2 and B3 decisions were not reopened. Every finding below is a gap in carrying them out.

## 1. Summary

The round closed most of what it set out to. Six of round 1's eight blocking items are fully closed, and the two that stay open (B1, B5) are each one table row. The rest of the safety design holds up: the probe before the prompt, explicit consent, stop and report on a failed trash, and the durable preimage.

Four new findings block. All four are short text fixes. No header has to change for them.

| # | Contract | Finding | Why it blocks |
| --- | --- | --- | --- |
| R2-B1 | OPERATION-JOURNAL | A backup is deleted when its identity "matches" the target, and a volume with no file index always matches | Data safety. A held original, including a `TrashFailed` one, is removed. This breaks the lead's "never falls through to removal" |
| R2-B2 | OPERATION-JOURNAL | The same trust in identity decides "not started" for in-place writes | Data safety. The preimage is deleted after a same-size write, and Retry can transform twice, which is what B5 was meant to stop |
| R2-B3 | OPERATION-JOURNAL | `enqueueTransfer` with Overwrite behaves as "no consent", but every COPY-SAFETY-01 test must pass with only the sweep change | B2 can't satisfy both. It has to break one frozen rule |
| R2-B4 | ASSET-GROUP | B1 must pass Overwrite and rename-reversal tests that the same contract forbids until B2 | B1 can't satisfy both. It has to break `confirmTransfer`'s frozen limit |

## 2. Round 1's blocking items

| Item | Status | Evidence |
| --- | --- | --- |
| B1 no usable trash | **Open, one row** (R2-B1) | Closed: the probe at the plan step (`OPERATION-JOURNAL.md:90`), B2 named as prompt owner (`:88`), permanent wording and explicit consent (`:95`), a failed trash that stops and reports whatever the consent (`:102`), the no-backup routes (`:105-106`), the concrete fake-backend tests (`:210-217`), J4 and J11 corrected. `TrashBackend` gains `probeTrash`. Windows already refuses anything it wouldn't recycle (`platform_trash.hpp:17`, `file_operation_service.cpp:79`), so a wrong `Available` still fails safe |
| B2 transfer class | **Closed**, pending the lead's re-check | No contract or decision row names the reaper, `TransferPtr` or `QSharedMemory`. The abstract rules agree across `RESOURCE-LEDGER.md:58-63`, `DECODE-TIER.md:130`, `:163` and `COMPARE-PANES.md:63`. One leftover API name, N14. **PR 3 round 5 isn't pushed** (`claude/stack-confirm-fix` is still `9c0c919`), so the wording has not been checked against it |
| B3 EN-11 refresh | **Closed** | I read `viewer_controller.cpp:428` at `b1d57cd`. The apply rule (`DECODE-TIER.md:137-142`) admits an equal-rank refresh and drops a non-refresh repeat. A refresh never starts below what's shown (`:144`), which fits the `DisplayManager` diff. The citations are now right, the site is in the table (`:174`), and tests are at `:199-205`. See N9 for the cost with the ladder on |
| B4 read items | **Closed** | `OperationJournal::items` and `ItemRecord`, bounded and paged, with a Corrupt prefix. Undo, retry and B4's recovery all read through it (`OPERATION-JOURNAL.md:58`) |
| B5 preimage order | **Open, one row** (R2-B2) | Closed: `ItemIntent::preimagePath`, `sealPreimage` before the mutation, the protocol order (`:65`), kill-hook tests (`:232`). The reconcile row at `:153` is not safe |
| B6 share key | **Closed** | `commit(actualBytes, shareKey = 0)`, the table row, unit test text and a pointer-to-member static assert that can fail |
| B7 key pass | **Closed** | `requestAllMetadata`, `metadataPending`, `metadataPartial`; A3, A4, A7 and C5 in the consumer table; M5 corrected; tests at `META-ROLES.md:211-216`. N7 and N8 are refinements |
| B8 colour source | **Closed** | `imageColour` in D1's diff, read on `imageChanged`. `D2 after D1` is stated, and the handoff asks the lead to add the edge. One stale line, N12 |

## 3. Blocking findings

### R2-B1. A backup can be deleted on a meaningless identity match

`OPERATION-JOURNAL.md:172`: "Occupied | The same file as the occupant, by identity ... | The extra name is unlinked". It is meant for the POSIX hard link, but the row isn't limited to it, and it comes before the `TrashFailed` and `TargetOccupied` rows, so it wins.

`FileIdentity` on Windows is the raw `{dwVolumeSerialNumber, nFileIndex}` (`src/core/file_operations.cpp:106`). The repository already knows that index can be zero: `copy_move_service.cpp:388` returns `Unknown` for it, with the comment "some redirectors report no index". `:169` also allows for "when the volume reports no identity", but this row doesn't.

On such a volume, a target and a `.~gb` backup that are different files compare equal. After a crash between the replace and the trash, or after a `TrashFailed` that left the old copy held, reconcile unlinks the only copy of the original, and nothing is reported. The user may have been told that file was going to the Recycle Bin.

**Fix.** Apply the row only when `copy_safety::compareFileIdentity(backup, target) == Same`, and on POSIX also require a link count of at least 2. `Unknown` falls through to the `TrashFailed` or `TargetOccupied` report. Add a test using an injected identity that reports no file index: the backup survives and is reported. Say at `:66` that this removal needs that proof.

### R2-B2. "Not started" for in-place writes rests on the same identity

`OPERATION-JOURNAL.md:153`: "Target identity and size match `source` | Not started; the preimage, if any, is removed".

On NTFS this is sound, because a replace changes the file ID. I checked: after `ReplaceFileW`, the target name carries the replacement's ID and the backup keeps the old one. It isn't sound with no file index, because the identity always matches, and a same-size write is ordinary here: an XMP rating going from 3 to 4 keeps its size, and so can an orientation-only transform.

So a crash after the replace on such a volume gives "not started", deletes the only copy of the prior bytes, and lets **Retry remaining** transform the file twice. The row also contradicts `:66`, "A partial is the only file reconcile may delete".

**Fix.** Decide by content, which the seal already makes possible, since `sealPreimage` records a checksum:

| Evidence | Result |
| --- | --- |
| Preimage sealed and good, and the target's checksum equals it | Not started; the preimage is removed |
| Preimage sealed and good, and the target differs | Done, undo `RestorePreimage` |
| Preimage path recorded but not sealed, and the target matches `source` on identity, size and modified | Not started, since the protocol seals before it mutates; the unsealed file is removed |
| Preimage missing, bad, or unsealed with a target that differs | Unknown, with evidence |

The new-sidecar rows stay. Name preimages at `:66` as a second file reconcile may remove, and remove a sidecar or transform partial here too. Add a same-size test with an injected identity that reports no file index.

### R2-B3. `enqueueTransfer` with Overwrite against the COPY-SAFETY-01 regression line

- `OPERATION-JOURNAL.md:199`: `enqueueTransfer` "called with Overwrite ... behaves as 'no consent'".
- `:106`: a volume that can't keep a backup, without consent, stops with the target unchanged and the item `Failed`.
- `:245`: "every `copy_safety` test passes, with only the sweep change above".

`tests/copy_move_tests.cpp:840` `testOverwriteFallbackKeepsAttributes` injects `FaultPoint::ReplaceUnsupported`, calls `enqueueTransfer` through `runTransfer` (default policy Overwrite, `:458`) and asserts `succeeded == 1`. Under `:106` it must fail, so this one is certain on Windows. There is more of the same kind:

- Both `runTransfer` helpers (`:458`, `tests/copy_safety_test_support.hpp:71`) default-construct `CopyMoveService`, so with the real backend every Overwrite test sends fixture files to the real Recycle Bin or Trash.
- Where the temp directory has no usable trash, the probe gives "no consent, stop" and those tests fail. That is plausible on some Linux setups; I didn't run it.
- `tests/copy_safety_tests.cpp:331` `overwriteLongPath` asserts nothing is left beside a target over 300 characters. Whether the shell trash takes such a path isn't established. If it doesn't, the old copy stays beside the target and the test fails.

B2's grant covers only the sweep test, so B2 must break `:199`, `:245` or its grant.

**Fix.** Keep `:199`. Reword `:245` and grant B2 the migration: both helpers take a fake `TrashBackend` reporting `Available`, and the fallback tests go through `planTransfer` and `confirmTransfer(..., permanentReplaceConsent = true)` with a fake reporting `Unavailable`. Every existing assertion on target bytes, attributes and permissions is kept. Add the row to the handoff's grant table.

### R2-B4. B1's acceptance list holds tests only B2 can pass

- `ASSET-GROUP.md:35`: "Until B2, `confirmTransfer` accepts only `Skip` and `RenameBoth`."
- `:48` and `:123`: asset-scope rename is refused until B2.
- `:125`: "Acceptance tests B1 must pass". These include `:136`, "an asset Copy ... asks once, and an injected replace fault on one member keeps that member's target byte-identical", and in `:135`, "a failure on the second member reverses the first and reports it; a fault that blocks the reversal reports the exact state".

`:135` also says "B1's asset-scope rename is refused until the journal lands". B1 can pass those tests only by accepting what the same contract refuses. B2's list in OPERATION-JOURNAL doesn't have them, so moving them isn't a duplicate: its "Asset rename" line covers kills, not a member failure, and nothing covers one decision for an asset's colliding members.

**Fix.** Move the Overwrite bullet and the two reversal clauses to OPERATION-JOURNAL's acceptance tests, for B2. In B1's list, assert that `confirmTransfer` with Overwrite returns false and nothing runs.

## 4. Non-blocking

These are safe to carry into packet briefs. N1 to N3 touch frozen text or a header comment, so they are cheapest in the same push.

**OPERATION-JOURNAL**

- **N1. `undoAvailable` is an intersection** (`operation_journal.hpp:95`, `OPERATION-JOURNAL.md:84`). B5's fix makes this visible: a rating write over five existing sidecars (`RestorePreimage`) and five new ones (`TrashCopy`) intersects to `None`, so undo is never offered. A paste with a mix of plain copies and Overwrites is the same unless B2 sets both bits. Fix: "`None` if any Done item has none; otherwise the union".
- **N2. `trashBackup` has no way to reach a fake backend** (`operation_journal.hpp`, `open` and `trashBackup`; test at `OPERATION-JOURNAL.md:221`). A test hook works around it. Fix: `open(directory, status, TrashBackend* trash = nullptr)`.
- **N3. `CopyMoveService`'s injected backend has no frozen diff** (`:47`), and B2's grant row omits `copy_move_service.hpp`. Fix: state the constructor overload, matching `file_operation_service.hpp:28`, and add it to the grant.
- **N4. A dead end.** `:106` says "Run this again and confirm permanent replacement", but a re-plan on a volume whose probe says `Available` offers `"trash"` again, so no consent can be given. This hits Linux on a vfat USB stick (a usable `.Trash-$uid`, no hard links). The same applies to a volume where the probe says `Available` but the trash always refuses, and `SHQueryRecycleBinW` may do that with "don't move files to the Recycle Bin" set (not verified). It fails safe, but Overwrite can never work there. Fix: at the plan step also test a hard link on POSIX, and have a re-plan after such a stop report `"permanent"`.
- **N5. Consent should count only when the plan said `"permanent"`.** As written, `confirmTransfer(id, Overwrite, true)` on a `"trash"` plan permanently removes files if the trash vanishes before execution (`:103`), though the user never saw that wording. Fix: ignore the flag on a `"trash"` plan, and add a test.
- **N6. Reconcile table gaps.** The Trash table has no last row, yet `:209` expects "a source replaced after the crash stays Unknown". `:146` says "for a move the source gone" and should include Rename. `CreateFolder` and `BatchRename` have no table. The rename to `replacedPath` (`:112`) must not overwrite, since `rename(2)` replaces a name that appeared after the choice: use the same no-replace move as `:169`. `dismiss`, "completed or reconciled only" (`operation_journal.hpp:149`), leaves a `Cancelled` or `Failed` operation with no backup undismissable forever: make it "terminal or reconciled".

**META-ROLES**

- **N7. Scrolling can fence the key pass.** `META-ROLES.md:151-153` applies a result only when `metaGeneration` matches and each `requestMetadata` increments it, while `:25` routes key-pass rows through the display batching. Scrolling could then discard the pass, and a second call "starts nothing". Fix: the key pass is fenced by listing generation only; add a test that scrolls during a pass.
- **N8. The sort pass caches ratings without the overlay.** `:126-129` skips the overlay on a sort pass, but the cache is shared (`:27`), so a later display read hits an entry with the embedded rating and shows it over the sidecar's. Fix: mark entries read without the overlay as misses for rating. Also, `:142` records the pass "in `MetadataKeyCache`", whose frozen API has no call for it; it is Track A owned, so say A2 adds one. The 4 s budget (`:158`) leaves most of a large folder on a rotational disk or network share unread for a chip or group, and a repeat call starts nothing, so say a new listing resumes from the cache.

**DECODE-TIER and RESOURCE-LEDGER**

- **N9. Baseline proof with the ladder on.** `DECODE-TIER.md:173` proves with `reuse({key, Full}, registered)`. With Fit shown there is no Full, so every late baseline starts a refresh and cancels the refinement, even when the pixels are current. Fix: prove at the shown tier and bucket.
- **N10. A refinement can't use the headroom.** `:147` runs refinement at `Visible`, and non-Foreground requests must leave the headroom free (`RESOURCE-LEDGER.md:34`). At M2 with a 1 GiB limit, headroom is 512 MiB, so a Full above about 128 MiB of pixels (32 MP) is refused and stays at Fit, while today's rule admits 256 MiB. "The largest validated decode fits" (`:36`) holds only at Foreground. Honest, but a regression. Fix: Full of the current image reserves as Foreground in the ledger, whatever its `IoScheduler` priority, or M2 measures and states this.
- **N11.** `DECODE-TIER.md:220`, "source dimensions differ between tiers: rejected", needs `:161`'s "of one non-empty token", or it contradicts `:204`.

**CAPABILITY**

- **N12.** `CAPABILITY.md:98` still has the populator check the selection generation, which the controller doesn't expose. B8's fix replaced that. Delete the line, since `:53` and `:149` are right.
- **N13.** `:43` says `viewer_controller` is not edited, but `:89` removes the suffix at `viewer_controller.cpp:1001` and `:1047`, and the handoff's D2 row doesn't grant them. Grant those two lines.

**Others**

- **N14.** `RESOURCE-LEDGER.md:102`, "between `create(bytes)` and `decoded`", still uses `QSharedMemory`'s API name. Write "between the buffer's allocation and `decoded`".
- **N15.** `ASSET-GROUP.md:93`, "one decision per asset", contradicts `:39`, "one collision policy per plan", and the single `collision` argument. Write "one decision per plan, never per member".
- **N16.** Handoff line 5 has a broken sentence ("needed it Not merged in") and names `f24f2e7`; `main` is now `3a14044`. The conclusion still holds: `main` has added no source, header or CMake change.

## 5. Consumers and contradictions

Checked against section 6 of the lead plan: A2 to A7, B1, B2, B4 to B6, C1 to C5, D1, D2, D4 to D7, E1 to E3, F1, F2. Apart from R2-B3, R2-B4, N3 and N13, each has the interface it needs. C3's constructor takes `resource::OwnerId`, which fits its plan dependency on E1. D2 now needs D1, which the plan still doesn't list: the lead's edge is outstanding. FS-01's "no silent permanent-delete fallback" holds once R2-B1 is fixed. Consent is explicit, and `file_operations.hpp` keeps its rule for `TrashBackend`.

## 6. Code claims checked

| Claim | Result |
| --- | --- |
| `DeleteFileW` of the backup at `copy_move_service.cpp:444`; fallback at `:453`; `RenameBoth` at `:708`; sweep at `:922`; direct write at `:928`; replace at `:990`; `QFile::remove` at `:1014` | Pass |
| Nothing in `ui/` calls `enqueueTransfer`; `pasteTo` is the only production caller | Pass |
| `.tmp_rename_` at `mutation_service.cpp:182` | Pass |
| `viewer_controller.cpp:428` keeps the generation; `:1702` takes a new one; `:232` and `:1097` are clears, as the diff now says | Pass |
| `:425`, `:946`, `:991`, `:995`, `:1001`, `:1020`, `:1021`, `:1043`, `:1047` | Pass |
| `Navigator` value-constructed at `:1197`, `:1341`, `:1838`, `:1953`, `:1966` | Pass (the list is partial, and the text says "several") |
| `setActive`, role enum, `sortedImageFiles` signature, `admissionAvailable`, `directory_entry_model.cpp:27`, `:64`, `:454`, `navigation.cpp:221`, `:241` | Pass |
| `settings_recovery.hpp:40` `Context`, `:115` `apply` (in `include/glancelot/`), `settings_file.cpp:61` `QSaveFile` | Pass |
| `Main.qml:755`, `SettingsDialog.qml:845`, `display_manager.cpp:350`, `QVariantMap` already in `viewer_controller.hpp` | Pass |
| `Stale` perf flag exists (`directory_entry_model.cpp:231`) | Pass |
| Identity rows assume a replace changes the file ID | **Pass on NTFS, by experiment on this PC** (`ReplaceFileW`: target took the replacement's ID, backup kept the old). **Fail with no file index**: R2-B1, R2-B2 |
| "Every `copy_safety` test passes with only the sweep change" | **Fail**: R2-B3 |

**Headers.** `git diff 53dab6c 01fe089 -- src ui CMakeLists.txt` is empty, so nothing reachable changed this round. The header changes are new types, fields, constants, declarations and comments. Every new `operation_journal.hpp`, `asset_group.hpp` and `resource_ledger.hpp` member is undefined until its packet, and only the compile test includes those headers. `fail-before-r2.txt` shows real failures for the assertions I sampled.

## 7. Build and test

See the PR comment for whether a local `contract_headers` run completed after this document was pushed. At the time of writing it was queued behind full gates held on this machine, so it **had not run**. Nothing here depends on it: the four findings come from reading the text and the existing tests.

On `01fe089`, `gh pr checks 5` showed `guard`, `core (windows-2022)` and `core (ubuntu-24.04)` passing, and `viewer` pending.

Not run by this review: the full suite, `docker build`, the existing tests under B2's future behaviour (R2-B3 is established by reading them; only `testOverwriteFallbackKeepsAttributes` is certain), and the behaviour of `SHQueryRecycleBinW` on network volumes or with the Recycle Bin disabled. PR 3 round 5 was not available.

## 8. What to do

Fix R2-B1 to R2-B4 on `claude/phase2-contracts`, with N1 to N3 in the same push, because they touch a header comment or a grant. The rest can be text in that push or go into the packets' briefs. A new fresh reviewer then checks only that diff.
