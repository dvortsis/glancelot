# B1-HANDOFF: ASSET-GROUP + F022

Implementer: local Claude subagent on the lead's Windows PC. Contract: [docs/contracts/ASSET-GROUP.md](../contracts/ASSET-GROUP.md), frozen. Written for the lead and the fresh reviewer.

## Branches, PRs and tips

Three stacked PRs, because the packet came to about 3,400 changed lines and the target is about 1,500 per PR.

| Part | Branch | PR | Base | Tip | Changed lines (excluding docs and catalog) |
| --- | --- | --- | --- | --- | --- |
| 1, grouping | `claude/b1-asset-group` | [#13](https://github.com/dvortsis/glancelot/pull/13) | `main` | `e0e76ca` | 1133 |
| 2, scoped plans | `claude/b1-asset-services` | [#14](https://github.com/dvortsis/glancelot/pull/14) | part 1 | `8650d78` | 1067 |
| 3, confirmations | `claude/b1-asset-ui` | [#16](https://github.com/dvortsis/glancelot/pull/16) | part 2 | the commit that adds this file | 1185 |

Part 1 needs no other part. Part 2 sits on part 1's exact tip, and part 3 on part 2's exact tip.

## What changed

- **Part 1, grouping.** `groupListing`, `expand` and `renameAsset` in `src/app/asset_group.cpp`, added to `glancelot_platform`. Nothing calls them yet. `src/app/asset_group_internal.hpp` is the private seam for the tests and shared name helpers.
- **Part 2, scoped plans.** `FileOperationService::planScoped`, `CopyMoveService::planTransfer`/`confirmTransfer` and `MutationService::planRename`/`confirmRename`, as the contract's three diffs. `plan()`, `enqueueTransfer` and `renamePath` behave as before.
- **Part 3, confirmations.** `ui/AssetCompanions.qml` is the one shared preview and choice, in `ui/DeleteFlow.qml`, `ui/RenameFlow.qml` and the new `ui/TransferFlow.qml`. `ui/UiRadioButton.qml` is a new primitive with full keyboard behaviour. Paste asks only when there is something to decide or show; plain files run straight through, as they do today. `translations/glancelot_en_XA.ts` is regenerated, and captures are in `docs/verification/B1/`.

## Lead grant, quoted

The brief said not to change `include/glancelot/*`, while the frozen contract says B1 applies three diffs there. I asked. The lead's answer, November 5, 2026:

> Lead: confirmed. My brief was too narrow, and the frozen contract wins. The grant covers exactly the three diffs in docs/contracts/ASSET-GROUP.md under "Applied by B1", to `file_operation_service.hpp`, `copy_move_service.hpp` and `mutation_service.hpp`, and nothing else in `include/glancelot/`. Quote this grant and the exact diff in the handoff. Same rule for any other "Applied by B1" item the contract lists. If you find one that goes beyond these, tell me.

The diff as applied (`git diff <part 1 tip> <part 2 tip> -- include/`). Beyond the contract's text it adds only the includes those signatures need (`<QVariantMap>`, `<QVariantList>`):

```diff
diff --git a/include/glancelot/copy_move_service.hpp b/include/glancelot/copy_move_service.hpp
--- a/include/glancelot/copy_move_service.hpp
+++ b/include/glancelot/copy_move_service.hpp
@@ -2,6 +2,7 @@
 #include <QObject>
 #include <QString>
 #include <QStringList>
+#include <QVariantMap>
 #include <memory>
 
 namespace glancelot {
@@ -65,6 +66,13 @@ public:
                                        glancelot::CopyMoveService::TransferMode mode,
                                        glancelot::CopyMoveService::CollisionPolicy collision = glancelot::CopyMoveService::CollisionPolicy::RenameBoth);
 
+    // scope: "file" | "asset". Summary as planScoped, plus collisions [{path, target, requestedBy}] and, from B2, replaceFate
+    // "trash" | "permanent" per the trash probe. confirmTransfer executes exactly the snapshotted list; one collision policy per plan.
+    Q_INVOKABLE quint64 planTransfer(const QStringList& sourcePaths, const QString& destinationDirectory,
+                                     glancelot::CopyMoveService::TransferMode mode, const QString& scope);
+    Q_INVOKABLE bool confirmTransfer(quint64 id, glancelot::CopyMoveService::CollisionPolicy collision,
+                                     bool permanentReplaceConsent = false);
+
     Q_INVOKABLE void pause();
     Q_INVOKABLE void resume();
     Q_INVOKABLE void cancel();
@@ -84,6 +92,7 @@ signals:
     void itemFinished(const QString& sourcePath, const QString& destPath, bool success, const QString& error);
     void batchFinished(quint64 batchId, int successfulCount, int failedCount);
     void pathsMutated(const QStringList& sourcePaths, const QStringList& destPaths, bool isMove);
+    void transferPlanReady(quint64 id, const QVariantMap& summary);
 
 private:
     struct Private;
diff --git a/include/glancelot/file_operation_service.hpp b/include/glancelot/file_operation_service.hpp
--- a/include/glancelot/file_operation_service.hpp
+++ b/include/glancelot/file_operation_service.hpp
@@ -29,7 +29,10 @@ public:
     ~FileOperationService() override;
     bool busy() const;
     // origin: "grid" | "tree" | "canvas" | "command" (diagnostics and wording only). Returns the plan id, 0 if busy.
-    Q_INVOKABLE quint64 plan(const QStringList& paths, const QString& origin);
+    Q_INVOKABLE quint64 plan(const QStringList& paths, const QString& origin);   // == scope "file"
+    // scope: "file" | "asset". The summary gains companions [{path, role, requestedBy, included,
+    // exclusion}] and scope; confirm() executes exactly the snapshotted included list.
+    Q_INVOKABLE quint64 planScoped(const QStringList& paths, const QString& origin, const QString& scope);
     Q_INVOKABLE bool confirm(quint64 id);   // false when id is stale, superseded, cancelled or empty
     Q_INVOKABLE void cancel(quint64 id);    // planning: discard; executing: stop before the next item
 signals:
diff --git a/include/glancelot/mutation_service.hpp b/include/glancelot/mutation_service.hpp
--- a/include/glancelot/mutation_service.hpp
+++ b/include/glancelot/mutation_service.hpp
@@ -1,6 +1,7 @@
 #pragma once
 #include <QObject>
 #include <QString>
+#include <QVariantList>
 #include <QVariantMap>
 #include <memory>
 
@@ -59,6 +60,10 @@ public:
     // Emits pathRenamed(oldPath, newPath) on success.
     Q_INVOKABLE QVariantMap renamePath(const QString& sourcePath, const QString& newName);
 
+    // scope: "file" | "asset". Summary: renames [{from, to}], collisions, excluded. Asset scope returns a refusal until B2 lands.
+    Q_INVOKABLE quint64 planRename(const QString& sourcePath, const QString& newName, const QString& scope);
+    Q_INVOKABLE bool confirmRename(quint64 id);
+
     // Creates a new folder named `folderName` inside `parentDirectory`.
     // Returns QVariantMap:
     //   "success": bool
@@ -70,6 +75,8 @@ public:
 signals:
     void pathRenamed(const QString& oldPath, const QString& newPath);
     void folderCreated(const QString& folderPath);
+    void renamePlanReady(quint64 id, const QVariantMap& summary);
+    void renameFinished(quint64 id, const QVariantList& results);
 
 private:
     struct Private;
```

The contract lists no other "Applied by B1" item.

## Pre-handoff checklist

### 1. Fail-before evidence

The test files are new, so against the base they only fail to compile. So each piece of evidence below is one mutation of the production code.

The full record, with each edit and each run's QTest totals, is [docs/verification/B1/fail-before.txt](../verification/B1/fail-before.txt): 115 mutations of the production code, each built and run alone with the targeted test. Every test function, leaving out the `_data` tables and the fixtures, failed under at least one. A selection:

| Mutation | Tests that caught it |
| --- | --- |
| `no-companions`: asset scope adds nothing | `assetAddsExactlyTheTablesMembers`, `requestedCompanionExpandsTheSameWay`, `withAHintOnlyItsMembersAreProbed` and others |
| `fileonly-takes-companions` | `fileOnlyReturnsExactlyTheRequest` (all 5 rows), `losslessIgnoresAsset` |
| `no-folding`: exact keys everywhere | `caseVariants`, `caseVariantsOnThisVolume`, `unicodeStemsMatchAfterCaseFolding` |
| `no-budget`, `no-cancel` | `aSlowVolumeEndsAtTheBudget`, `aReadThatEndsInTime`; `cancellationDuringLookupsLeavesNothing` |
| `hint-ignored`, `hint-trusted` | `withAHintOnlyItsMembersAreProbed`; `aStaleHintGivesMissingNotIncluded` |
| `overwrite-allowed`, `asset-rename-runs` | `overwriteIsRefusedUntilB2`; `planRenamePreviewsEveryMemberAndAssetIsRefused` |
| `no-revalidate`, `rename-no-revalidate` | `aReplacedCompanionIsReportedAndTheOthersProceed`; `aChangedSourceIsNotRenamed` |
| `rename-per-member`, `skip-member-only` | `keepBothKeepsAnAssetTogether`; `skipLeavesACollidingAssetWhole` |
| `ui-scope-remembered`, `ui-confirm-during-replan` | `choosingAssetTrashesWhatItShowsAndIsNotRemembered`; `confirmWaitsForTheNewPlan` |
| `ui-paste-runs-at-once`, `ui-rename-executes-asset` | `pasteAsksFirstAndRunsNothingUntilConfirmed`; `renameShowsWhatKeepsItsNameAndRefusesTheSetWithAReason` |
| `ui-paste-opens-at-once`, `ui-always-asks` | `aPlainPasteRunsWithNoDialog` (copy and move), `thePasteCommandRunsPlainFilesStraightThrough` |
| `ui-run-with-companions`, `-past-the-list`, `-with-collisions`, `-with-exclusions`, `-unchecked`, `-refused`, `-nothing` (one condition dropped from `nothingToDecide` each) | the matching row of `everythingToDecideIsSeen`, and of `aRealPlanWithSomethingToDecideShowsTheDialog` where a real plan can produce it |
| `ui-same-folder-asks`, `ui-partly-here-is-a-notice`, `ui-cancel-keeps-the-plan` | `movingWithinOneFolderIsANoticeNotADialog`; `aMoveWithOnlySomeItemsAlreadyHereStillAsks`; `aSlowPlanCanBeCancelled` |
| `ui-radio-no-arrows`, `-every-option-a-stop`, `-held-arrow-churns`, `-no-ring`, `-no-wrap`, `-group-order`, `-not-mirrored`, `-out-of-view`, `ui-list-not-a-stop` | `theKeyboardWalksTheChoices` (delete, copy, move, rename), `theKeyboardMakesTheCollisionChoice`, `leftAndRightFollowTheLayoutDirection`, `aFocusedChoiceStaysInViewInAShortWindow`, `arrowsScrollALongCompanionList` |

Five mutations changed no result, and the file says why for each: `plan-implicit-companions` and `video-alone-groups` are equivalent, `stale-transfer-plan` and `stale-rename-plan` are not observable alone (their paired `*-and-cancelled` mutations fail), and `ui-radio-binding-lost` is equivalent, which is why the radios now use plain bindings.

The first round's runs predate the two merges of `main`; the second round's (paste and keyboard) ran on `88b14dc`, and the last edits after them (plain radio bindings and the polish step before a test click) changed no assertion. One run, `ui-rename-scope-remembered`, also reported `pasteOfAWholeAsset`, which that mutation can't reach. The cause was probably a click on a control before a relayout, so `click` now forces the polish first. It has passed in every local run since, including one under the gate lock, but I never reproduced it, so treat it as watched rather than proven.

### 2. Grant check

Base to tip: `git diff --name-only 88b14dc..<part 3 tip>`. Every file the three parts change, against the brief and the lead's grant:

| Files | Grant |
| --- | --- |
| `src/app/asset_group.cpp`, `src/app/asset_group_internal.hpp`, `src/app/file_operation_service.cpp`, `src/app/copy_move_service.cpp`, `src/app/mutation_service.cpp` | Implementation under `src/app/` |
| `include/glancelot/file_operation_service.hpp`, `copy_move_service.hpp`, `mutation_service.hpp` | The lead's grant, quoted above. Nothing else in `include/glancelot/` |
| `ui/DeleteFlow.qml`, `ui/RenameFlow.qml`, `ui/TransferFlow.qml` (new), `ui/AssetCompanions.qml` (new), `ui/UiRadioButton.qml` (new) | Confirmation UI in its existing component files or new components |
| `ui/Main.qml` | Thin call sites: the `modalOpen` line, the cut, copy and paste commands, and one `TransferFlow` instance. None is in a slideshow or tab region |
| `tests/asset_group_tests.cpp`, `tests/asset_group_ui_tests.cpp` | New tests |
| `cmake/AssetGroupTests.cmake`, one include line in `CMakeLists.txt` | New cmake file plus one include line. **Worth a look:** that file also adds production items, so that the include stays one line: `asset_group.cpp` to `glancelot_platform`, `glancelot_platform` linking `glancelot_core`, the three QML files to the module, and `glancelot_platform` to `file_operation_service_tests` |
| `translations/glancelot_en_XA.ts` | Regenerated per translations/README.md. After the merges, lupdate reports 715 messages, 0 new, and no diff |
| `docs/handoff/B1-HANDOFF.md`, `docs/verification/B1/*` | Handoff and captures |

Nothing is outside the grants.

### 3. Exact base

`origin/main` at `88b14dc` (Merge pull request #9), merged into part 1 as `e0e76ca`, as the lead asked after PR 3 and again after PR 9. The earlier merge is `3e903af`. Part 2 sits on part 1's exact tip through `8650d78`, and part 3 on part 2's exact tip.

Merge conflicts, all resolved as the lead directed or by keeping both sides:
- `CMakeLists.txt`: every include line kept (`AssetGroupTests`, `ExportUiTests`, `StackConfirmTests`).
- `ui/Main.qml` in part 3: the `modalOpen` line keeps B3's `exportFlow` clause and adds `transferFlow.modal`.

`main` has since gained `20f1210` (#17), which is docs and evidence only, so it is not merged here.

The hosted `viewer` job now retries once and treats budget tests as advisory (`docs/decisions/2026-11-05-hosted-ci-retries.md`), so a green `viewer` is weaker evidence on those tests than it was. The gate still decides.

### 4. Targeted ctest

Windows, MSVC, Qt 6.8.3, part 3's code on `88b14dc` (before the commit that outlines the focused option), all under the gate lock so the budget-labelled `folder_grid` and `input_ui` count: `ctest --test-dir build-claude-b1 -R "^(asset_group|asset_group_ui|file_operation_service|copy_move|mutation|command_routing|translation_coverage_ui|contract_headers|input_ui|folder_grid)$"`:

```text
Test project C:/src/Github/Glancelot/.worktrees/claude-b1-asset-group/build-claude-b1
      Start 12: folder_grid
 1/10 Test #12: folder_grid ......................   Passed  260.19 sec
      Start 13: input_ui
 2/10 Test #13: input_ui .........................   Passed   87.98 sec
      Start 15: file_operation_service
 3/10 Test #15: file_operation_service ...........   Passed    1.35 sec
      Start 35: mutation
 4/10 Test #35: mutation .........................   Passed    0.24 sec
      Start 36: copy_move
 5/10 Test #36: copy_move ........................   Passed    1.66 sec
      Start 45: contract_headers
 6/10 Test #45: contract_headers .................   Passed    0.08 sec
      Start 48: translation_coverage_ui
 7/10 Test #48: translation_coverage_ui ..........   Passed   25.95 sec
      Start 50: command_routing
 8/10 Test #50: command_routing ..................   Passed   10.46 sec
      Start 53: asset_group
 9/10 Test #53: asset_group ......................   Passed    5.28 sec
      Start 54: asset_group_ui
10/10 Test #54: asset_group_ui ...................   Passed   73.94 sec
100% tests passed out of 10
budget       = 348.17 sec*proc (2 tests)
clipboard    = 348.17 sec*proc (2 tests)
fast         =   7.25 sec*proc (4 tests)
native       = 261.53 sec*proc (2 tests)
process      = 461.53 sec*proc (7 tests)
ui           = 458.52 sec*proc (5 tests)
Total Test time (real) = 467.52 sec
```

QTest totals: `asset_group_ui` 44 passed; `folder_grid` 74 passed, 4 skipped; `input_ui` 33 passed, 1 skipped. No baseline failures. The final commit only changes `UiRadioButton`'s focus outline and padding; `asset_group_ui` and `translation_coverage_ui` were run again on it and pass (44 passed).

No existing test file changed, so the `file_operation_service`, `copy_move` and `mutation` results are unchanged assertions on the new code.

Hosted `viewer`, full suite: green on all three PRs at the first-round tips (`e0e76ca`, `8650d78`, `f838a6d`). Results for the final tips are on the PRs.

### 5. Decisions taken and open questions

See below.

## Failure and cancellation cases covered

| Case | Test |
| --- | --- |
| Cancel during lookups: nothing confirmable | `cancellationDuringLookupsLeavesNothing`, `cancelDuringLookupsLeavesNoConfirmablePlan` |
| A newer plan supersedes an older one | `aScopeSwitchSupersedesThePlanBeingBuilt`, `aSupersededTransferPlanCannotBeConfirmed`, `anOlderRenamePreviewCannotReplaceANewerOne`, `confirmWaitsForTheNewPlan` |
| Companion deleted after grouping (stale hint): `Missing`, not included | `aStaleHintGivesMissingNotIncluded` |
| Budget exhausted: `companionsIncomplete`, unchecked companions stay out, the confirmation says so | `aSlowVolumeEndsAtTheBudget`, `aReadThatEndsInTime`, `theBudgetReachesTheSummary`, `companionsThatCouldNotBeCheckedAreSaid` |
| Unreadable parent, truncated listing | `anUnreadableParentIsReportedNotGuessed`, `aTruncatedHintStillFindsCompanionsInTheParent` |
| `kMaxExpandedItems`, `kMaxMembers`, `kMaxExcludedListed` | `requestsStopAtTheCapWithTheRestLimitReached`, `companionsStopAtTheCapAndExcludedAreCounted`, `seventeenthIsLimitReachedNeverDropped`, `fileOnlyCompanionsAreCountedPastTheListedLimit`, `companionSummaryIsBounded` |
| Replaced or vanished between confirmation and execution | `aCompanionReplacedBeforeConfirmIsChangedAtExecution`, `aReplacedCompanionIsReportedAndTheOthersProceed`, `aChangedSourceIsNotRenamed`, `aMemberChangedAfterConfirmationIsReported` |
| Partial failure per member | `partialFailureIsReportedPerMember`, `aReplacedCompanionIsReportedAndTheOthersProceed` |
| Overwrite and asset rename refused | `overwriteIsRefusedUntilB2`, `planRenamePreviewsEveryMemberAndAssetIsRefused`, `renameShowsWhatKeepsItsNameAndRefusesTheSetWithAReason` |
| Invalid stem, target collision | `renameAssetRefusesAnInvalidStem`, `planRenameFindsTargetCollisionsInAssetScope` |
| Missing destination, moving into the same folder | `transferToAMissingDestinationCannotRun`, `movingIntoTheSameFolderIsRejected`, `movingWithinOneFolderIsANoticeNotADialog`, `aMoveWithOnlySomeItemsAlreadyHereStillAsks` |
| A slow plan: progress after a delay, then run, or Cancel | `aSlowPlanShowsProgressThenRunsIfThereIsNothingToAsk`, `aSlowPlanCanBeCancelled` |
| Paste with something to decide never runs unasked | `everythingToDecideIsSeen`, `aRealPlanWithSomethingToDecideShowsTheDialog` |

## Decisions taken

| # | Decision | Alternatives rejected | Visible |
| --- | --- | --- | --- |
| 1 | Three stacked PRs: grouping, scoped plans, confirmations | Two, as the brief suggested: part 1 would have been about 2,200 lines | No |
| 2 | **Confirmed by the lead, November 5.** **Under asset scope, a member's collision moves its whole asset.** Keep both gives every member the same `stem (n)`, including `stem (n).CR2.xmp`. Skip leaves the whole asset behind. Otherwise a sidecar lands beside a different photo of the same name and describes the wrong picture, and `IMG.CR2.xmp` would become `IMG.CR2 (2).xmp`, which no tool reads | Per-member naming, which is what `resolveCollision` does. It stays for file scope, as today | Yes |
| 3 | The plan shows the name each item would take under keep both. A target that appears after the plan still can't be overwritten: the worker resolves the collision again | Name only at execution: the confirmation couldn't show it | Yes |
| 4 | The services pass no hint in B1. One directory read per parent costs less than stats on a share and finds mixed case, and the contract defers the listing's grouping proxy to "later through the lead". `hintIfCurrent` holds the fencing rule for that proxy, and `expand` honours a hint today (tested) | Wire the listing now: PR 7 and PR 12 are editing it, and nothing shows a badge yet | No |
| 5 | A partial directory read is thrown away (`companionsIncomplete`): an unseen same-stem file would hide a `Collision` | Group what was read: could add a wrong companion | Only on a slow share |
| 6 | A hint with no groups, a hint with any `Incomplete` group, and a hint with any group at `kMaxMembers` all read the directory. `GroupingSnapshot` has no truncated field, and a truncated listing with no groups looks like an empty one. Cost: one read, only for a folder with no groups at all | A marker group: breaks "only groups with >= 2 members" | No |
| 7 | Case sensitivity is a stat of the same name with one ASCII letter flipped, compared by identity. No letter, or a missing sample, falls back to the platform's usual answer | Volume flags: NTFS reports case-sensitive search and still matches insensitively | No |
| 8 | `EmbeddedMotion` is set from the name (`*.MP.jpg`, `MVIMG_*`), because `groupListing` does no I/O. It is only a flag and changes no behaviour. ADV-09 replaces it | Read the payload: forbidden here by the contract | No |
| 9 | A Derived file left out beside a RAW keeps its own FullName sidecar as a separate two-member asset. The RAW's asset lists both as `NotApplicable` | Treat the sidecar as a companion of the RAW: it describes the export | Yes |
| 10 | In a Collision group under Trash, Copy or Move, a FullName sidecar stays with its exact file, in both directions. Under Rename, it doesn't, since that scope falls back to file-only | Exclude it as well: separates a sidecar from the only file it can belong to | Yes |
| 11 | Beyond `companions` and `scope`, summaries gain `companionsNotListed` and `companionsIncomplete`, which the contract's own count and notice need, and a `name` on each companion. Included companions are listed first within the 256 | Bury them in the list | No |
| 12 | The contract has no cancel for transfer or rename plans, and `CopyMoveService::cancel` would stop a running batch. An empty request withdraws the plan and stops its lookups (`planTransfer({}, ...)`, `planRename("", ...)`) | Add a method: outside the frozen diff | No |
| 13 | `confirmRename` runs on a worker, revalidates the source and reports through `renameFinished`. `RenameFlow` still executes a file-only rename with `renamePath` and uses the plan for its preview, so typing and Enter stay as they were and a rename never waits for a preview | Move the flow to `confirmRename` now: Enter would have to wait for the worker, for no safety gain, since file-only touches one file. B4 and B2 use the pair | No |
| 14 | `TransferFlow` keeps its own copy of what was cut or copied: `clipboardTargets()` isn't invokable and isn't in the diff. All of Main's cut and copy commands go through it. `pasteTo` stays for its tests | Ask for a grant: unnecessary | No |
| 15 | A Move into the source's own folder is rejected in the plan, because nothing would change. When that is the whole paste, it is a non-modal notice ("It's already in this folder.") through the existing `deleteFlow.showNotice`, and what was cut stays cut. Mixed with other items, it is listed as an exclusion in the confirmation. A Copy there still makes a keep-both copy. **Lead's answer, November 5** | Today's behaviour, which renames the file to `name (2)`. A blocking dialog for something that changes nothing | Yes |
| 16 | The plan's asset choice uses one shared component. `UiRadioButton` is a new canonical primitive, because a dialog with two mutually exclusive choices had none | A switch: the choice isn't on/off. Buttons: no state shape | Yes |
| 17 | Copy and Move focus the action once the plan is ready, because they aren't destructive; the action ignores auto-repeat. Delete keeps Cancel focused | | Yes |
| 18 | The rename preview is debounced 150 ms | One directory read per key press | No |
| 19 | `cmake/AssetGroupTests.cmake` also adds production sources, so that one include line is the whole root change, and `glancelot_platform` now links `glancelot_core` | A second root edit, outside the grant | No |
| 20 | **Paste asks only when there is something to decide or show. Lead's answer, November 5:** it runs straight through when the plan has no companions (listed or `companionsNotListed`), no collisions, no exclusions, `companionsIncomplete` false, nothing refused and something to run. File-only stays the default, so nothing is included silently. This reads the contract's "copy and move get a confirmation" as applying when there is something to decide; the lead adds that line to the contract in bookkeeping | A confirmation on every paste (my first build): Ctrl+V on plain files would have changed | Yes |
| 21 | Nothing appears for the first 400 ms of a plan, so a plain paste has no flash. A slower plan opens the dialog with "Checking the selected items…" and Cancel, and closes it and runs if there is then nothing to ask. The planning state isn't modal until then, so a plan that needs a decision opens the dialog over whatever is on screen | Open at once: a flash on every paste. A non-modal progress line: no Cancel | Yes, on a slow share |
| 22 | `UiRadioButton` is one Tab stop per group, on the checked option, and the arrows move and select with wrap-around, in screen order (Left and Right follow layout direction). Keyboard focus outlines the whole option, and the options are inset to line up with the list text beside them. A held arrow moves once, so it can't churn through plans. A focused choice scrolls into view in a short window. Qt's `RadioButton` has no arrow handling, so this is in the primitive | Every option a Tab stop: more stops in a dialog with two groups, and against the platform convention | Yes |

## Open questions for the lead

None. The three from the first round are answered: decisions 20 and 21 (paste), 2 (whole-asset collisions) and 15 (a Move within one folder). The lead adds the first two to the contract and notes the second for B2 in lead bookkeeping.

## Unverified

- **Linux and Docker were not run by me.** The `caseVariantsOnThisVolume` branch for a case-sensitive volume, and the `glancelot_platform` to `glancelot_core` link order under GNU ld, run for the first time at the gate's `docker build`. The hosted `viewer` is Windows only.
- **The real Recycle Bin with an asset.** The trash is a fake in every asset test. `ShellTrash` is unchanged and sees one target at a time.
- **Keyboard:** Tab, Shift+Tab and the arrow keys through the choices in Delete, Copy, Move and Rename are now tested (`theKeyboardWalksTheChoices` and the four tests after it), with the focus outline in the `*-keyboard-*.png` captures. Closed.
- **A real slow share.** The budget runs on a virtual clock, and the 400 ms progress path on a gated lookup. A read that hangs inside the OS call isn't interrupted, because the budget is checked between entries. The dialog then stays on "Checking…" with Cancel available. `planDeletion` has the same limit today.
- **Offscreen software rendering only**: the captures prove layout, not a physical display.
- **Widths.** Captured at 560 and 1180 px as the brief asks. Dialogs narrow with the window, and names elide, but 320 px wasn't captured.
- ui-design skills were read before the QML was written (application-interface-design, layout-spacing, responsive-layout, typography-ui, component-geometry, iconography-ui, motion-ui, design-review). Self-review of the captures found nothing blocking. The dialogs add no motion. No separate design-review session was run.
