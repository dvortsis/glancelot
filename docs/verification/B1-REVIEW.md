# B1-REVIEW: ASSET-GROUP + F022 (PRs 13, 14 and 16)

Reviewer: a fresh-context local Opus subagent on the lead's Windows PC. I implemented none of B1 and wrote no earlier review of it.
Read: [ASSET-GROUP contract](../contracts/ASSET-GROUP.md) (frozen), [B1-HANDOFF](../handoff/B1-HANDOFF.md) at the top tip, AGENTS.md.
Environment: detached worktree `.worktrees/review-b1` at the top tip; build `build-review-b1`; Windows 11, MSVC 2022, Qt 6.8.3, offscreen.

## Verdicts

| PR | Branch | Layer reviewed (`git diff <parent> <tip>`) | Verdict |
| --- | --- | --- | --- |
| 13 | `claude/b1-asset-group` | `88b14dcb4aa7db30e6ffc7a3d758f36c179a6afb..e0e76cab81bf8e7207997e4e730806af1a0ef15d` (merge-base with `main`, which is `88b14dc`) | **APPROVED** |
| 14 | `claude/b1-asset-services` | `e0e76cab81bf8e7207997e4e730806af1a0ef15d..8650d7856c0085a25cd24349ea851b002f811de4` | **APPROVED** |
| 16 | `claude/b1-asset-ui` | `8650d7856c0085a25cd24349ea851b002f811de4..0324e943ba37cca642ea4ac902d55af3914b0c6a` | **APPROVED** |

There are no blocking findings. I-1 to I-4 are follow-ups, best done in one small round before B2 builds on this code. L-1 is a grant question only the lead can answer, and it needs that answer before merge. It is not a code defect.

## What I checked, and what holds

**Data safety (the first priority).** I found no path by which a delete, copy, move or rename touches a file the confirmation didn't list.

- **File-only is the default, every time.**
  - `DeleteFlow.request`, `TransferFlow.paste` and `RenameFlow.renameFile` each set `scope` to `"file"` per operation.
  - `scopeFromName` maps anything but `"asset"` to FileOnly.
  - `FileOperationService` file scope still calls `planDeletion(requested)` on exactly the named items.
  - `choosingAssetTrashesWhatItShowsAndIsNotRemembered` checks that the next operation starts at file scope.
- **Confirm executes the snapshot.**
  - `FileOperationService::confirm` runs the ready `DeletionPlan`, built from `expansion.included`.
  - `confirmTransfer` copies `d->ready->items` and clears the ready plan. That is one confirmation per plan.
  - `confirmRename` runs only a file-scope, `executable` plan.
  - Every flow refuses confirm while it is replanning or while `summary.id !== planId`. `confirmWaitsForTheNewPlan` covers that, including a direct `confirmPlan` call.
- **Revalidation runs before each item.**
  - Delete: the existing `executeDeletion` identity checks.
  - Transfer: `compareToSnapshot` runs before each top-level item. A Changed or Missing item is reported and never substituted.
  - Rename: `compareToSnapshot` runs before `performRename`.
- **Exclusions carry reasons.**
  - Collision members: `Collision`.
  - Stem sidecars of a Collision group: `Ambiguous`.
  - A Derived file beside a Raw or Rendered member, and its own FullName sidecar: `NotApplicable`.
  - Members past 16: `LimitReached`.
  - Every exclusion is listed with its reason and never mutated.
- **A partial directory read is thrown away.** See `asset_group.cpp:458`. That holds, but no test can fail if it doesn't (I-1).
- **Refusals hold.**
  - `confirmTransfer` with Overwrite returns false and leaves the plan confirmable.
  - No Overwrite control exists in the UI.
  - Asset-scope rename is previewed with every member, then refused with the reason. `renameAllowed` and `executeRename` both refuse it.
- **No stale grouping snapshot in B1.** The services pass no hint (decision 4), so no snapshot can add or hide a member. `expand` re-probes a hint's members when one is given. See P-9 for what the future hint wiring must keep.
- **Viewing never writes.** `expand` and the plan builders only read. `RealLookup::caseSensitive` stats two names.
- **All plan I/O runs on workers.** That covers `FileOperationService::pool`, `CopyMoveService::planPool` and `MutationService::pool`. The GUI thread does no new blocking I/O in this packet.

**The three header diffs.** `git diff e0e76ca 8650d78 -- include/` is the contract's "Applied by B1" text for `file_operation_service.hpp`, `copy_move_service.hpp` and `mutation_service.hpp`. It also adds `#include <QVariantMap>` (copy_move) and `#include <QVariantList>` (mutation), which the handoff discloses (L-2). Nothing else in `include/glancelot/` changes.

**The lead's round-2 decisions.**
- **(a) Paste-through.**
  - `nothingToDecide` requires all of: targets present; no `rejected`, `collisions` or `companions`; `companionsNotListed` zero; `companionsIncomplete` false; no `error`.
  - In file scope every companion appears as an excluded row in `companions`, so any companion at all opens the dialog.
  - The rule runs only on the first plan of a paste (`first`). A later scope switch back to file never auto-runs.
  - No dialog flash: the 400 ms `progressDelayTimer` opens the dialog only while the flow is still `planning`, and a plan that needs nothing closes it and runs.
  - I re-ran the paste-through mutation myself (below).
- **(b) Whole-asset collisions.**
  - Under asset scope, members share one `(n)`, found by trying `n` until no member's renamed target is occupied.
  - `skippedBySkip` marks the whole asset.
  - Tests: `keepBothKeepsAnAssetTogether` and `skipLeavesACollidingAssetWhole`. I re-ran that mutation too.
  - Two edges are in I-3.
- **(c) Move into the source's own folder.** Each such item is rejected `sameFolder`.
  - All of them: the non-modal notice "It's already in this folder.", and the cut stays on the clipboard (`movingWithinOneFolderIsANoticeNotADialog`).
  - Some of them: the dialog, with "It's already in this folder." beside them (`aMoveWithOnlySomeItemsAlreadyHereStillAsks`).
- **(d) Keyboard.**
  - One Tab stop per group, on the checked option (`focusPolicy: checked ? StrongFocus : ClickFocus`).
  - Up/Down, and Left/Right swapped when `mirrored`, move with wrap-around in screen order.
  - A held arrow moves once.
  - Focus is visible: a row outline in `focusRing`.
  - The focused choice scrolls into view.
  - All of this is tested in delete, copy, move and rename. Polish items are in P-4.
- **(e) `UiRadioButton`.** It is a sound canonical primitive. It is built on Qt's `RadioButton`, keeps the state as a shape as well as a colour, and uses only `Theme` tokens: `primary`, `muted`, `background`, `hover`, `focusRing`, `focusWidth`, `borderWidth`, `radiusControl`, `iconSize`, `controlHeight`, `disabledOpacity`, `gapMd`, `gapSm`, `textBody`. It has no web dependencies.

**Performance and bounds.**
- `expand` reads each parent directory once (`withoutAHintOneReadPerParentFindsMixedCase`: two reads for two parents, no stats).
- `kExpandBudgetMs` bounds the reads and stats per plan.
- `kMaxExpandedItems` caps `included`, `kMaxExcludedListed` caps `excluded`, and `kMaxMembers` caps members, each with the counts carried through.
- `addCompanionSummary` caps the summary's `companions` at 256 and counts the rest.
- Grouping is O(n log n).
- The transfer plan's destination stats are the gap (I-2).

**UI quality.**
- I read the ui-design skills: application-interface-design, design-review, component-geometry, layout-spacing, typography-ui and iconography-ui. I reviewed the 24 captures in `docs/verification/B1/` against them, opening the collision, refusal and keyboard captures at 560 and 1180 px.
- The structure holds up:
  - one primary action per dialog, with the destructive treatment only on Move to Recycle Bin;
  - Delete keeps Cancel focused, and Copy/Move focus the non-destructive action (decision 17);
  - the radios are inset to line up with the list text;
  - each state is a word ("Included", "Not included", "Keeps its name"), never colour alone;
  - each reason sits under its row;
  - the long list scrolls inside the dialog, so the actions never move off screen;
  - all values are theme tokens, and the icons come from the existing `UiIcon` family.
- Wording is plain and truthful, apart from P-2, P-6 and P-7.
- Not checked: 320 px, and physical displays (offscreen software rendering only).

## Findings, by severity

### Blocking

None.

### Important

**I-1 (PR 13, test gap on a data-safety rule). Nothing tests that a read cut short by the budget is thrown away.**
- *What:* `expand` discards a directory read that the budget or a cancel stopped partway (`src/app/asset_group.cpp:458`, `if (stopped || !readable) { out.companionsIncomplete = true; continue; }`). That is decision 5, and it is right: a same-stem file the read never reached would hide a `Collision`. But the tests of the stopped branch, `aSlowVolumeEndsAtTheBudget` and `aReadThatEndsInTime`, stop the read *before its first entry*, so the partial listing is empty and grouping it changes nothing. The recorded `partial-read-groups` mutation was caught only by `anUnreadableParentIsReportedNotGuessed`, which tests the other branch.
- *Evidence:* my mutation `reviewer-partial-read-grouped` groups a stopped read while still setting `companionsIncomplete`. It survives the whole `asset_group` suite: **81 passed, 0 failed** (output below).
- *Fix:* add a test where the read stops after some entries. For example, `a.CR2` and `a.jpg` are read in time and `a.NEF` falls past the budget, in asset scope. Expect `included == {a.CR2}`, `companionsIncomplete`, and `a.jpg` not included.

**I-2 (PR 14, performance and cancel). Part of a transfer plan can't be cancelled, and plans share one thread.**
- *What:* `buildTransferPlan` (`src/app/copy_move_service.cpp`) checks `cancel` only in its first loop, the source snapshots. The second loop calls `occupied()` two or three times per member. Each call is a fresh `QFileInfo` stat on the destination, and a colliding asset adds up to 10,000 `(n)` probes. The loop never checks `cancel`, and no budget covers it. `planPool` has one thread. So on a slow share, Cancel closes the dialog but the abandoned plan keeps the thread busy, and the next paste waits behind it on "Checking the selected items…". 10,000 files at about 2 ms per remote stat is about a minute.
- *Why it matters:* the contract's "a `plan()` for 10,000 files on a network share must stay usable" is written for `plan()`. `planTransfer`'s summary is "as planScoped", on the same path.
- *Fix:*
  - check `cancel.stop_requested()` in the second loop and in the `(n)` search;
  - read the destination folder once into a set of names, case-folded on Windows;
  - make `occupied()` a lookup in that set plus `taken`, which also gives the collision answer one consistent snapshot.

**I-3 (PR 14, decision (b) edges). Under asset scope, unrelated requested files that share a stem are treated as one asset, and a target that appears after the plan splits an asset.**
- *Unrelated files as one asset:* under asset scope, `buildTransferPlan` keys every requested *file* by `parent/stem`, whatever its role. So `notes.txt` and `notes.pdf`, both selected, become one "set".
  - If `notes.txt` collides, keep both renames `notes.pdf` too.
  - Skip leaves `notes.pdf` behind, although nothing collides with it.
  - The confirmation lists `notes.pdf` with no sign that skip drops it.
- *A late target splits an asset:* at execution, a target that appeared after the plan goes through the per-member collision path in `processItem`. So keep both can give members different `(n)`, and skip can drop one member alone.
- *Safety:* neither case touches a file the confirmation didn't list, and neither overwrites. Both only leave files out or rename them.
- *Fix:* key the asset by group membership. Requests with role `Other`, and folders, keep their own path as their key. For the late target, re-resolve per asset at execution, or record it for B2, whose per-plan collision decision is where Overwrite lands.

**I-4 (PR 16, test). The slow-paste cancel test waits a fixed second where a condition exists.**
- *What:* `aSlowPlanCanBeCancelled` ends with `QTest::qWait(1000); // sleep-ok: asserting that nothing fires`. AGENTS.md allows that category. But a deterministic alternative exists: `planPool` is single-threaded and first-in, first-out.
- *Better:* after `gate.release()`, plan a sentinel (a plain file, through `service->planTransfer` directly) and wait with `QTRY_COMPARE_WITH_TIMEOUT(planned.count(), 1, 10000)` on its `transferPlanReady`. By then the cancelled plan's `finished` has been delivered and dropped. Then assert that the one signal carries the sentinel's id, and that `started.isEmpty()`.
- *Why it matters:* on a loaded gate machine, the fixed second can end before the worker returns, and the test then passes without testing anything.
- *Severity:* not blocking on its own. The mutation `ui-cancel-keeps-the-plan` is caught today (see "Fail-before"). Fix it in the next round.

### Polish

- **P-1 (PR 16). A newer cut can be cleared by an older paste.** In the invisible 400 ms planning window the main window isn't modal. A Ctrl+X on other files there replaces `clipboardPaths` and the service's clipboard. If the earlier paste was a move with nothing to decide, `confirmPlan` then runs it and calls `clearClipboard()`, erasing the newer mark. No file is touched, only the mark. *Fix:* clear only when the clipboard still holds the request's paths.
- **P-2 (PR 16). Wording for a clash within one paste.** Two sources with the same name (`a/x.jpg` and `b/x.jpg`) collide through `taken`, and the dialog says "“x.jpg” is already in this folder", which isn't true yet. *Fix:* give `collisions` a `withinPlan` flag, and word it "Two of these have the same name."
- **P-3 (PR 13). One unreadable folder stops every later lookup.** An unreadable parent sets `companionsIncomplete`. The loop's `if (out.companionsIncomplete || outOfBudget()) break;` then skips every later directory's companions, even with budget left. That is conservative, and the notice is shown. *Fix:* break on the budget only, and `continue` past an unreadable folder.
- **P-4 (PR 16, `UiRadioButton`).**
  - `step()` orders options by `y` only, so a horizontal group, or two options on one row, walks in the wrong order. *Fix:* sort by `y`, then by `x`, reversed when `mirrored`.
  - A group with no checked option has no Tab stop, because every option is `ClickFocus`. B1's groups always have one checked, but a future consumer of a canonical primitive might not. *Fix:* let the first visible option take `StrongFocus` when none is checked.
  - Keyboard focus is drawn twice: once as the row outline and once as the indicator's ring, both in `focusRing`. *Fix:* keep the row outline, and give the indicator its normal border.
- **P-5 (PR 16). A failed paste-through leaves state behind.** If `confirmTransfer` returns false in the path with no dialog (for example, a batch started in between), the notice shows. But `phase` stays "confirm" and the service keeps a ready plan. *Fix:* call `discardPlan()` there. The next paste resets it anyway.
- **P-6 (PR 16). The "Checking…" state is titled as a question.** During planning the title is "Copy these items here?", and the dialog can then close and run without an answer. *Fix:* use a neutral title, for example "Checking the items…", while `phase === "planning"`.
- **P-7 (PR 16). The refused asset rename says "Included".** Each member of the refused preview reads "Included" and "Would become …", while Rename is disabled. *Fix:* say "Would be renamed", or show no state word while `renameRefused`.
- **P-8 (PR 14). The transfer summary lists every item.** `targets`, `collisions` and `rejected` are unbounded. `namedTargets` and `renamed` rebuild in JavaScript on the GUI thread for each summary. That matches today's delete summary, and it is fine at 10,000. Note it for B5 and B6 (basket, drag and drop).
- **P-9 (PR 13, for whoever wires the listing hint).** The hint path has no `leftOut` or overflow data. With a hint, a Derived file beside a RAW isn't listed as `NotApplicable`. The hint also can't see a member added after the snapshot, such as a new `IMG.NEF` that would make `IMG.xmp` ambiguous. B1 passes no hint, so none of this happens yet. The proxy that adds hints must keep `hintIfCurrent` fencing on every listing change.
- **P-10 (PR 13 and 16). Production build lines sit in a tests file.** `cmake/AssetGroupTests.cmake` holds these three lines:
  - `target_sources(glancelot_platform …)`;
  - `target_link_libraries(glancelot_platform PUBLIC glancelot_core)`;
  - `qt_target_qml_sources(glancelot …)`.
  
  They sit outside `if(BUILD_TESTING)`, so the product builds correctly. But a reader of the root `CMakeLists.txt` won't find them. I checked for a link cycle and there is none: `glancelot_core` links nothing of the platform's.

### For the lead (decisions, not code defects)

- **L-1. Lead-owned files outside the quoted header grant: confirm before merge.**
  - *The files:* PR 13 adds one `include(cmake/AssetGroupTests.cmake)` line to the root `CMakeLists.txt` and creates `cmake/AssetGroupTests.cmake`. PR 16 edits `ui/Main.qml`: the `modalOpen` line, the cut, copy and paste commands, and one `TransferFlow` instance.
  - *What the handoff says:* the brief allowed "new cmake file plus one include line" and these call sites.
  - *What I found:* no record of that grant in the repository. `docs/WORK-QUEUE.yaml` has no B1 row, and no cycle or decision document mentions it. The lead's quoted grant covers the three headers only.
  - *Precedent:* the shape matches STACK-CONFIRM-FIX, which was "granted CMakeLists.txt (one include) and cmake/StackConfirmTests.cmake".
  - *Assessment:* the edits are minimal and correct.
- **L-2. Ratify the header diffs.** The contract says "this exact diff". The diffs are that text plus the two `#include` lines the new signatures need.
- **L-3. Visible change: Copy then Paste into the same folder now asks.** Before B1, Ctrl+C then Ctrl+V in a file's own folder silently made `name (2)`. Now the file collides with itself, so the confirmation opens, with keep both "as name (2)". That follows decision (a) exactly. If you want the old one-key duplicate back, the rule needs a "collides only with itself" exception. A Move there gets the decision (c) notice, as decided.
- **L-4. Contract text the handoff leaves to bookkeeping:**
  - decision 20, the paste-through rule;
  - decision 11, the extra summary keys `companionsNotListed`, `companionsIncomplete` and `name`;
  - decision 2, the whole-asset collision rule, for B2's Overwrite.

## Fail-before: my spot-checks

The handoff's record is [docs/verification/B1/fail-before.txt](B1/fail-before.txt). I re-ran four of its mutations, including the paste-through rule, and added one of my own. Each was applied alone in the review worktree, built where needed (`cmake --build --target asset_group_tests`), and run from the test binary with `QT_QPA_PLATFORM=offscreen` and `QT_QUICK_BACKEND=software`. Each file was restored from a backup copy afterwards. The SHA-256 hashes matched the originals, and `git status --porcelain` was empty. The restored files were then touched so that ninja recompiled them, before the final clean run.

| Mutation | Edit | Result | Matches record |
| --- | --- | --- | --- |
| `ui-run-with-companions` (paste-through) | `TransferFlow.qml`: `&& plan.companions.length === 0 && !plan.companionsNotListed` → `&& !plan.companionsNotListed` | `FAIL! everythingToDecideIsSeen(listed companion)`, `FAIL! aRealPlanWithSomethingToDecideShowsTheDialog(companion)`; `Totals: 15 passed, 2 failed` (I also ran `aPlainPasteRunsWithNoDialog`, which passed) | Yes, the same two |
| `ui-cancel-keeps-the-plan` | `TransferFlow.qml`: `onClosed: { progressDelayTimer.stop(); flow.discardPlan(); if` → `onClosed: { if` | `FAIL! aSlowPlanCanBeCancelled() Compared values are not the same`; `Totals: 2 passed, 1 failed` | Yes |
| `skip-member-only` | `copy_move_service.cpp`: `plan.items.push_back({{source, chosen, member.snapshot}, collides});` → `…, occupied(member.name)});` | `FAIL! skipLeavesACollidingAssetWhole() Compared values are not the same`; `Totals: 80 passed, 1 failed` | Yes |
| `reviewer-partial-read-grouped` (mine) | `asset_group.cpp:458`: `if (stopped \|\| !readable) { out.companionsIncomplete = true; continue; }` → `if (!readable) { out.companionsIncomplete = true; continue; } if (stopped) out.companionsIncomplete = true;` | `Totals: 81 passed, 0 failed`: **survives** | Not in the record; that is finding I-1 |

## Test output on the clean tip

Targeted, under a build slot, after the first build of `0324e94` and before any mutation:

```text
1/8 Test #15: file_operation_service ...........   Passed    1.55 sec
2/8 Test #35: mutation .........................   Passed    0.35 sec
3/8 Test #36: copy_move ........................   Passed    2.47 sec
4/8 Test #45: contract_headers .................   Passed    0.10 sec
5/8 Test #48: translation_coverage_ui ..........   Passed   36.40 sec
6/8 Test #50: command_routing ..................   Passed   12.88 sec
7/8 Test #53: asset_group ......................   Passed    7.13 sec
8/8 Test #54: asset_group_ui ...................   Passed   91.11 sec
100% tests passed out of 8
Total Test time (real) = 152.60 sec
```

QTest totals: `asset_group` 81 passed; `asset_group_ui` 44 passed; `copy_move` 27 passed, 1 skipped; `file_operation_service` 9 passed, 3 skipped; `mutation` 11 passed; `command_routing` 7 passed; `translation_coverage_ui` 10 passed.

After the mutations, the final clean run under the gate lock (`scripts/gate-lock.ps1`), budget tests included, ran on the restored tip, rebuilt:

```text
 1/10 Test #12: folder_grid ......................***Failed  323.90 sec
 2/10 Test #13: input_ui .........................   Passed   96.97 sec
 3/10 Test #15: file_operation_service ...........   Passed    1.36 sec
 4/10 Test #35: mutation .........................   Passed    0.30 sec
 5/10 Test #36: copy_move ........................   Passed    1.73 sec
 6/10 Test #45: contract_headers .................   Passed    0.04 sec
 7/10 Test #48: translation_coverage_ui ..........   Passed   23.89 sec
 8/10 Test #50: command_routing ..................   Passed    8.94 sec
 9/10 Test #53: asset_group ......................   Passed    5.26 sec
10/10 Test #54: asset_group_ui ...................   Passed   64.64 sec
90% tests passed, 1 tests failed out of 10
```

- **`folder_grid`**, budget-labelled: this run failed with `FAIL! GridUiTests::gridThumbnailEpochFollowsFilmstrip() 'h.model.epoch() > before' returned FALSE.` (73 passed, 1 failed, 4 skipped). B1 doesn't touch that grid thumbnail-epoch code. I reran it once alone under the gate lock, as AGENTS.md says: `1/1 Test #12: folder_grid ... Passed 275.74 sec`, `Totals: 74 passed, 0 failed, 4 skipped`. An earlier run under the lock also passed (241.53 sec, see the next point). The authoritative gate decides.
- **That earlier run** passed `folder_grid` and `input_ui`. Its `asset_group` and `asset_group_ui` failures came from my own build cache: restoring a mutated file from its backup also restored the backup's old timestamp, so ninja kept the mutated `copy_move_service.cpp.obj`. Both failures are the `skip-member-only` assertions. After the restored files were touched and recompiled, both pass, as shown above. No tracked file changed: the hashes match and `git status` is clean.
- **QTest totals** on the clean run: `asset_group` 81 passed; `asset_group_ui` 44 passed; `input_ui` 33 passed, 1 skipped; `folder_grid` 74 passed, 4 skipped on the rerun.

## Missing cases

- I-1: a read that the budget stops partway.
- I-3: two unrelated requested files that share a stem, under asset scope with a collision.
- The 400 ms default itself: the no-flash tests set `progressDelay` to an hour, and the slow-path tests set it to 50 ms. That is deliberate, for determinism. A test that a real plan of one local file arrives within 400 ms would be timing-sensitive, so I don't ask for one.
- A Cancel on the progress dialog while the second loop of a transfer plan runs (I-2). It can't be tested until the loop checks cancel.
- P-1: Ctrl+X during the invisible planning window.

## Unverified

- **Linux and Docker.** The case-sensitive branch of `caseVariantsOnThisVolume`, and linking `glancelot_platform` with `glancelot_core` under GNU ld, first run at the gate's `docker build`. The hosted Ubuntu `core` job builds no viewer targets.
- **The real Recycle Bin.** Every asset test uses a fake trash.
- **A real slow share.** The budget runs on a virtual clock and the slow path on a gated lookup. I-2 is from reading the code, with no measurement.
- **Displays.** Offscreen software rendering only, so the captures prove layout, not a physical display. I didn't check 320 px.
- **Captures.** I reviewed the handoff's captures and didn't regenerate them.
