# PHASE2-CONTRACTS review, round 3

**Verdict: APPROVED**

- PR: https://github.com/dvortsis/glancelot/pull/5
- Reviewed head: `3288407745a54af6f41bbaa7dddc02b20f4498d8`. Diff read: `git diff 01fe089 3288407`. Round 2: `docs/verification/PHASE2-CONTRACTS-REVIEW-R2.md` on `claude/review-phase2-contracts-r2` (`0e19e58`).
- Scope: this delta, all of `OPERATION-JOURNAL.md` and `ASSET-GROUP.md`, and META-ROLES's A1 scan text against `origin/claude/a1-perf-enum` (`e425a90`).
- Reviewer: fresh context. I wrote neither the packet nor an earlier review.
- Not reopened: what rounds 1 and 2 accepted, J1, J4 and the abstract transfer wording, which the lead re-checks against PR 3.

## 1. Summary

Nothing blocks. All four of round 2's blocking findings are fixed as the lead decided, and round 1's B1 and B5 are now closed. I walked every path in the contract that removes a file, and each one needs explicit consent or stated proof.

Five non-blocking findings are worth taking before the freeze (section 4). Each is a sentence or a table row, and none changes a header's interface. The first four are gaps around a volume with no file index, a refused trash, or an offline volume. They fail safe, or report wrongly without losing data. The fifth corrects the test-migration grant. The rest (section 5) can go into packet briefs.

## 2. Round 2's findings and round 1's B1 and B5

| Item | Status | Evidence |
| --- | --- | --- |
| R2-B1 backup deleted on a meaningless match | **Closed** | `OPERATION-JOURNAL.md:200` needs `compareFileIdentity == Same`, plus a link count of at least 2 on POSIX, and `Unknown` falls through to a report. I read the function (`copy_move_service.cpp:351`): an all-zero 128-bit ID doesn't count as having one (`:370`), and a zero file index returns `Unknown` (`:388`), so it can't return `Same` on an index-less Windows volume. POSIX has no zero check, but `stat` with a link count of 2 is a real hard link. Test at `:257` |
| R2-B2 "not started" rests on identity | **Closed** | `:180-185`. A sealed preimage is compared by checksum, an unsealed one needs size and modified plus identity where there is one, and anything else is Unknown. The protocol seals before it mutates, so an unsealed preimage can't follow a mutation. Removing a preimage is named at `:79`. Same-size tests at `:267-268` |
| R2-B3 Overwrite against the regression line | **Closed** | `enqueueTransfer` stays "no consent" (`:228`). The migration is granted (`:55-60`, handoff line 32) and the regression line is reworded (`:282`). `testOverwriteFallbackKeepsAttributes` still holds under `confirmTransfer` with consent and a fake reporting `Unavailable`. See R3-N5 for the wording |
| R2-B4 B1's list held B2's tests | **Closed** | `ASSET-GROUP.md:135-136` asserts the refusals, and `OPERATION-JOURNAL.md:276-277` takes the moved tests. `ASSET-GROUP.md:35`, `:51` and `:123` agree |
| R2-N1 `undoAvailable` | **Closed** | Header comment and test at `:273` |
| R2-N2 `open` takes a backend | **Closed** | Header, static assert, test at `:245` |
| R2-N3 injected backend | **Closed** | Diff at `:49-53`, matching `file_operation_service.hpp:28`, and in the grant table |
| Round 1 B1 (no usable trash) | **Closed** | See the removal paths below |
| Round 1 B5 (preimage order) | **Closed** | R2-B2 was the last row |

R2-N4 to N16 are applied. N4 has a gap (R3-N3). The others read as round 2 proposed.

**Every removal path.**

| Removal | Needs |
| --- | --- |
| Live: the backup after a replace (`:120`) | A probe that isn't `Available`, and consent on a plan that said `"permanent"` (`:110`) |
| Live: the fallback with no backup (`:122`) | The same consent |
| Live: a trash move that fails (`:119`) | Nothing is removed, whatever the consent |
| Reconcile: an extra hard link (`:200`) | `Same`, and on POSIX a link count of at least 2 |
| Reconcile: a preimage (`:180`, `:182`) | A checksum equal to the target, or a preimage that was never sealed |
| Reconcile: a partial (`:79`) | The lock is free, and a partial never holds an original (`:80`) |

## 3. PR 7 and META-ROLES

The added text matches `origin/claude/a1-perf-enum`.

- `ScanOrder` sends other keys through `fileSortBefore` (`directory_scan.hpp:204`), and `sortBefore` gives natural order for both keys until A2 (`navigation.cpp`), as `META-ROLES.md:55` says.
- `arriving` clears when the definitive order is published, and the scan computes that order after enumeration, so A2 can read keys in between. That is what `:143` asks for. A refresh or a re-sort with rows on screen doesn't arrive (`directory_entry_model.cpp:145`), which fits "the previous order stays".
- `setDirectory`, `setSort`, `setIncludeHidden`, `setActive(false)` and a refresh all pass through `abandonScan`, which bumps `generation`. So fencing the key pass by listing generation alone (`:154`) cancels it on all of these and not on a scroll.
- `git merge-tree` of the two branches is clean, as the handoff says.

Two small points for A2's brief are at R3-N9.

## 4. Non-blocking, worth taking before the freeze

### R3-N1. Revalidation and undo on a volume with no file index

`OPERATION-JOURNAL.md:146` says an index-less identity never matches, a row "uses size and modified where it says so, and otherwise falls through", and "the same rule applies to the revalidation at execution and at undo". Neither place says what it falls through to.

- **Execution.** `file_operations.cpp:302` reports `Changed` on an identity mismatch. Read literally, every item on such a volume is then `Changed`, so copy, move and trash stop working there. That contradicts `:136` ("works as it does today") and `:227` ("results stay identical"). A consumer can avoid it by moving on to the size and modified check at `:304`, but the text doesn't say so.
- **Undo.** The undo table (`:90`) reports `Changed` when the identity doesn't match `resultIdentity`, so an undo that is advertised can never succeed there. `:97` says undo is advertised only when supported.

**Fix.** "At execution and at undo, an identity with no file index is skipped and size and modified decide; a folder target is checked by kind and link." Add a test: on an injected index-less volume, a file is trashed and moved, and a file changed after the plan is `Changed`.

Seen in passing, in existing code: `file_operations.cpp:268` compares raw identities for duplicates, so on such a volume every selected file after the first is rejected as `Duplicate`. It fails safe, and B2's brief can name it.

### R3-N2. A same-size Overwrite on such a volume can read as Done

The handoff says the new rule gives the Overwrite rows the same protection. In the transfer table it doesn't quite. Row 1 (`:163`) falls through when the identity is missing. For a **copy**, row 4 (`:166`) then needs only "target present with the snapshot size". So a same-size Overwrite killed before the replace is reported Done, while the target still holds the old bytes. Retry isn't offered for it, and the user may delete the source because the summary says the copy finished. No file is removed by the viewer. A move isn't affected, because row 4 needs the source gone.

**Fix.** Add a row after row 2: "`overwritten` set, and the target's identity can't be told: Unknown, with evidence." Extend the test at `:238` with an injected index-less identity.

### R3-N3. Consent after a refused trash does nothing

`:112` says a re-plan after a refused trash reports `"permanent"`, "so the user can always reach the consent". Execution re-probes (`:114`). If the probe says `Available`, row 2 applies: "Never removed", "whatever the consent" (`:119`). So on a volume where the probe says `Available` and the trash always refuses, the user reads "permanently deleted", confirms, and gets another `TrashFailed` and another held copy on the first item. It fails safe, but R2-N4's dead end stays, and the prompt's wording is wrong.

Two related points in `:112`:

- `InUse` or `Denied` on one file says nothing about the volume, yet it flips the whole volume to `"permanent"` for the rest of the process. Limit this to `TrashUnavailable`.
- "That volume" reads as POSIX only. A Windows volume where `ReplaceFileW` is unsupported and the probe says `Available` needs the re-plan too, because its stop message asks for the consent (`:123`).

**Fix.** "For the rest of the process, plan and execution both treat that volume's probe as `Unavailable`, after a stop because the volume can't keep a backup, or a `TrashUnavailable` refusal." Extend the test at `:243` through to the result: with consent, the replace finishes and the old copy is gone.

### R3-N4. An offline volume at restart reads as "vanished"

`:258` expects "the backup vanished before start: no finding and no error". Nothing separates vanished from unreachable, and a photo library on a USB drive or a NAS is ordinary. If an Overwrite was interrupted there, or holds a `TrashFailed` backup, and the viewer next starts without that volume:

- the backup isn't found, so `heldBackups` falls to 0, the `reconciled` record is written, and the operation isn't revisited (`:144`);
- `dismiss` and pruning are then allowed, though `:204` calls that record "the only pointer to the original";
- with the volume back, nothing is reported. After a crash inside the replace, the original sits under a hidden random name and its own name is empty;
- an in-place item reads "target differs" and becomes Done, so **Retry remaining** doesn't offer an item that never ran (`:181`).

The viewer deletes nothing, and it takes two failures in a row, so this doesn't block. It is one row, though, and the contract's own rule is that this record must survive.

**Fix.** Add to the failure table: "An item's target or backup folder can't be read (volume offline): the item stays Unknown and held, and the operation gets no `reconciled` record, so the next start looks again." Add a test: make the folder unreadable through a test hook for one start, then restore it, and the finding appears on the next start.

### R3-N5. The test-migration wording

`:57` says the two `runTransfer` helpers "construct the service". They don't: both take `CopyMoveService&` (`tests/copy_move_tests.cpp:456`, `tests/copy_safety_test_support.hpp:69`), which round 2 also misread. The service is built at each test, and `CopyMoveService service` appears 26 times in `copy_move_tests.cpp` and 12 times in `copy_safety_tests.cpp`. `copy_move_tests.cpp:191` also calls `enqueueTransfer` with Overwrite directly. All are inside the grant, so B2 can do this, but a site it misses still sends fixtures to the real Recycle Bin.

The fake also has to take the file away, as a real trash does. `cstest::entriesIn` assertions such as `{"target.bin"}` otherwise find `target (replaced).bin` beside the target.

**Fix.** "Every test that can reach Overwrite constructs the service with a fake `TrashBackend` that reports `Available` and moves the file out of the folder."

## 5. Non-blocking, for the packet briefs

**OPERATION-JOURNAL, B2**

- **R3-N6. In-place writes and the backup table.** Step 5 (`:80`) means an in-place replace records its `.~gb` in `backupPath`. A kill inside it leaves the target name empty, so both `:181` ("target differs": Done) and `:197` (moved back: not started) apply. Say the backup table runs first. The checksum row then correctly gives not started. Add a kill-inside-the-replace case for an in-place item.
- **R3-N7. Syncing the seal.** `sealPreimage` (`operation_journal.hpp:165`) syncs the file, and "records its checksum". `:66` syncs records "at batch boundaries". Say the `preimage` record is synced before the mutation. Otherwise a power loss can leave a replaced target with no seal. That gives Unknown, unless a transform that keeps size and modified on an index-less volume matches `:182`, where the preimage would be removed.
- **R3-N8. Test seams and rows without tests.**
  - The "injected identity" tests need a seam that `copy_safety.hpp` doesn't have yet. It is Track B owned, so B2 can add it, and the brief should say so.
  - B2 flips B1's two refusal tests in `tests/asset_group_tests.cpp`, so the brief should name them.
  - The `CreateFolder` rows (`:173-174`) and the rename half of `:166` have no acceptance test.
  - `reconcile`'s header comment still says "exact stats" (`operation_journal.hpp:135`), while the in-place table reads checksums.

**META-ROLES, A2**

- **R3-N9.**
  - The scan opens arrival only while enumerating (`directory_scan.hpp:386`, `:398`). A small folder that finishes inside the patience window has shown no rows when a key pass starts, so under a captured sort on slow storage the grid could stay empty for up to the 4 s budget. A2 should flush provisional rows before reading keys. That also keeps `:143` true.
  - `:142` says "one pass per folder and revision" and that a new listing's *key pass* resumes. It doesn't say whether the *order path* resumes. If it doesn't, a partial captured sort stays partial until the folder changes. Say which, and the sentence starting "So their unread sets" should move up, next to the shared pass.

## 6. Tests

The changed tests are concrete, and each can fail against the bug it covers:

- **Injected identity (`:257`, `:267`).** A reconcile that trusts raw identity would unlink the backup, or remove the preimage, and fail these.
- **Consent flag ignored (`:242`).** A consumer that honours the flag on a `"trash"` plan replaces the target and fails.
- **Undo union (`:273`).** The old intersection gives none and fails.
- **Refusals (`ASSET-GROUP.md:135-136`).** Accepting Overwrite in B1 fails.
- **`trashBackup` reaches the fake (`:245`).** Using the platform trash leaves the fake uncalled.
- **Static assert on `open`.** I re-ran the mutation. With the third parameter removed, the build fails at `tests/contract_headers_tests.cpp(67)`, as `fail-before-r3.txt` records.

## 7. Build and test

The header changed, so I built on Windows (MSVC 2022, Qt 6.8.3, Release) in `.worktrees/review-phase2-contracts-r3`, under a build slot, at `3288407`: targets `contract_headers_tests` and `contract_headers_moc`.

```text
1/1 Test #45: contract_headers .................   Passed    0.11 sec
100% tests passed out of 1
```

Mutation of the new assertion:

```text
FAILED: [code=2] CMakeFiles/contract_headers_tests.dir/tests/contract_headers_tests.cpp.obj
...\tests\contract_headers_tests.cpp(67): error C2607: static assertion failed
build exit code: 2
```

`git diff --name-only eeb4562 3288407` lists only documents, as the handoff says.

On `3288407`, `gh pr checks 5` showed `guard`, `core (windows-2022)` and `core (ubuntu-24.04)` passing, and `viewer` still pending when this was written. This approval doesn't cover the `viewer` result.

Not run: the full suite and `docker build`, which belong to the gate on the merge head. Also not run: the existing tests under B2's future behaviour, and `SHQueryRecycleBinW` on a network volume. PR 3 round 5 was not checked, as instructed. No product code changed on this branch.

## 8. What to do

The lead decides. Either take R3-N1 to R3-N5 in one more push, or carry them into B2's brief, since all five are text and tests in `OPERATION-JOURNAL.md` and B2 is the only packet that implements them. A new push needs `fresh-review` again, from a fresh reviewer of that diff alone.
