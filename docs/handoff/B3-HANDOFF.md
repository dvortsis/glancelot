# B3 handoff: F070 export dialog over `selection_export`

Written for: the lead and the fresh-context reviewer. Implementer: a Claude Code (Opus 5.5) local track session, which did not review this packet.

| | |
| --- | --- |
| Branch | `claude/b3-export-ui` |
| Base (exact) | `f24f2e7` (`origin/main` when the packet started); not a stacked packet |
| Commits | `ea37573` (the feature), `5486ecf` (stronger checks, evidence), `7d0c8fd` and `1a8be76` (handoff; `1a8be76` is the approved head), then one commit for the review delta below, which is the tip |
| Review diff | Whole packet: `git diff f24f2e7 <tip>`. Delta only: `git diff 1a8be76 <tip>` |
| `main` since the base | Moved, including code (PR 5), but `git merge-tree` shows no conflict, so not merged in, as the lead asked |

**Full suite: not run locally, by lead decision** (November 4, 2026: implementers don't run it, because the gate lock is the machine's bottleneck). The hosted `viewer` job runs it on the PR, and the tester gates the exact head before merge. Targeted results are below.

## Review delta (after APPROVED at `1a8be76`)

The lead decided three open points from `docs/verification/B3-REVIEW.md` (`origin/claude/review-b3`, `02b29f7`). Findings 4 to 10 in that review were not part of these decisions and are unchanged.

| # | Decision | What changed | Tests |
| --- | --- | --- | --- |
| 1 | A confirmed Replace must not swap an earlier export for a header-only file | With the new engine option, an export with no rows commits nothing. The dialog says "Nothing could be exported, so “list.csv” was not changed." and still lists what was left out | `nothingToExportLeavesTheTargetAlone` (5 rows, including all vanished and all outside the root), `confirmedReplaceWithNothingToExportKeepsTheEarlierFile`, `nothingToExportCreatesNoNewFile`, `nothingToExportSaysNoFileWasCreated`, `oneSurvivingItemStillReplaces` |
| 2 | A locked destination gets the right advice, not the raw error | A replace refused with Windows error 5 or 32 becomes `destinationLocked`: "“list.csv” couldn't be replaced. It may be open in another app, so close it and try again. The existing file hasn't changed." The raw error is not shown | `lockedDestinationSaysToCloseItAndKeepsTheFile` (a real open handle, then a successful retry after closing), `aBlockedReplaceIsToldApartFromOtherFailures` (4 rows) |
| 3 | Really exercise the `changed` reason | `itemChangedAfterSnapshotIsSkipped` now rewrites the file and asserts `changed`. The old delete case is kept as `itemDeletedAfterSnapshotIsReportedMissing` | `itemChangedAfterSnapshotIsSkipped`, `changedItemIsNamedWithItsReason` (the dialog's text) |

**The exact header diff** (lead grant, the only change to `include/glancelot/selection_export.hpp`):

```diff
     bool overwrite{false};           // replace an existing destination that is not an exported item
+    bool keepDestinationWhenEmpty{false};  // no row to write -> commit nothing, destination untouched; Written, written == 0
     std::size_t maxItems{1000000};   // more items -> ExportStatus::TooMany, nothing written
```

The default is off, so the engine behaves as before for any other caller. The engine change that goes with it is 7 lines in `src/core/selection_export.cpp`, placed after the last revalidation and the final cancel check and before the commit, so there's no window between the check and the decision.

### Delta: targeted `ctest` (actual output)

```
1/6 Test  #1: selection_export .................   Passed    0.41 sec
2/6 Test  #7: viewer_acceptance ................   Passed   11.07 sec
3/6 Test #29: shortcuts_help ...................   Passed    0.65 sec
4/6 Test #47: translation_coverage_ui ..........   Passed   24.73 sec
5/6 Test #49: command_routing ..................   Passed    9.14 sec
6/6 Test #52: export_ui ........................   Passed   38.47 sec

100% tests passed out of 6
```

`export_ui` totals: `55 passed, 0 failed, 0 skipped`. These are the same six suites the review ran. The full suite wasn't run locally.

### Delta: fail-before evidence

16 mutations, 16 caught. The exact edits are in `docs/verification/B3/fail-before/delta-mutations.json` and the results in `delta-results.json`.

| Mutation | What it breaks | Failing tests |
| --- | --- | --- |
| `engine-ignores-the-option` | **The fix reverted in the engine** | All 5 rows of `nothingToExportLeavesTheTargetAlone`, `nothingToExportCreatesNoNewFile`, `confirmedReplaceWithNothingToExportKeepsTheEarlierFile`, `nothingToExportSaysNoFileWasCreated` |
| `service-does-not-ask` | **The fix reverted in the service** | The same 8 |
| `zero-rows-reported-as-written` | Service reports zero rows as written | The same 8 |
| `empty-guard-too-wide` | One surviving item is also refused | `oneSurvivingItemStillReplaces` |
| `blocked-replace-not-detected` | **Fix 2 reverted** | `aBlockedReplaceIsToldApartFromOtherFailures` (error 5, error 32), `lockedDestinationSaysToCloseItAndKeepsTheFile` |
| `any-failure-called-locked` | Every failure gets the "open in another app" advice | `aBlockedReplaceIsToldApartFromOtherFailures` (another error, not a replace) |
| `engine-text-drifts` | Engine's error wording changes | `lockedDestinationSaysToCloseItAndKeepsTheFile` |
| `locked-case-removed` | Dialog has no message for the locked case | `lockedDestinationSaysToCloseItAndKeepsTheFile` |
| `raw-error-shown-as-detail` | Raw Windows error shown again | `lockedDestinationSaysToCloseItAndKeepsTheFile` |
| `changed-reported-as-missing` | Wrong reason reported | `itemChangedAfterSnapshotIsSkipped`, `changedItemIsNamedWithItsReason`, `nothingToExportLeavesTheTargetAlone(all changed)` |
| `no-revalidation` | Engine doesn't re-check items | `itemChangedAfterSnapshotIsSkipped`, `changedItemIsNamedWithItsReason`, and 3 rows of `nothingToExportLeavesTheTargetAlone` |
| `changed-reason-text-wrong` | Dialog shows the wrong reason | `changedItemIsNamedWithItsReason` |
| `nothing-case-removed` | Dialog has no case for nothing exported | `confirmedReplaceWithNothingToExportKeepsTheEarlierFile`, `nothingToExportSaysNoFileWasCreated` |
| `always-says-not-changed` | A new file reported as "not changed" | `nothingToExportSaysNoFileWasCreated` |
| `reveal-offered-for-nothing` | "Show in folder" offered when nothing was written | `confirmedReplaceWithNothingToExportKeepsTheEarlierFile` |
| `path-shown-for-nothing` | Path shown when nothing was written | `confirmedReplaceWithNothingToExportKeepsTheEarlierFile` |

### Delta: grant check

`git diff --name-only 1a8be76..<tip>`:

| File | Status |
| --- | --- |
| `include/glancelot/selection_export.hpp` | Lead grant: the one option, shown above |
| `src/core/selection_export.cpp` | **Not named in the grant, but the option needs it:** 7 lines |
| `src/app/selection_export_service.cpp`, `ui/ExportDialog.qml`, `tests/export_ui_tests.cpp` | Granted |
| `translations/glancelot_en_XA.ts` | Regenerated as the lead asked: 3 added, 1 removed, 654 in total, nothing unrelated |
| `translations/README.md` | The accepted count line, 652 to 654 |
| `docs/verification/B3/fail-before/delta-*.json`, `docs/handoff/B3-HANDOFF.md` | Evidence |

### Delta: decisions taken

- **No new engine status.** The grant is one option, so an untouched destination is reported as `Written` with `written == 0`, which the option's comment documents. The service always sets the option, so for it that pair can only mean "nothing committed". Alternatives: a new `ExportStatus` value, which is cleaner but needs a wider grant; or `Failed` with a message, which can't be told apart from a real write failure.
- **The rule also covers a new file, not only Replace.** The lead's decision was about Replace. I applied it to every export, because a header-only file nobody asked for is no use, and one rule is simpler than two. The dialog says "was not created" in that case. This removes the old "was saved, but it lists no items" message. Alternative: set the option only when replacing, which keeps the old message for new files. It's one line to change if you prefer it.
- **Errors 5 and 32 both count as locked**, because 32 is literally a sharing violation. Only on a replace: a new file failing with error 5 keeps the permission message, since no existing file can be held open.
- **Detection matches the engine's exact text**, since the grant rules out a new status. It's a string match by necessity, and `engine-text-drifts` shows the real-lock test fails if the wording changes.
- **"Show in folder" and the path are hidden when nothing was written.** A new file doesn't exist to show, and the skip list is what matters.

### Delta: unverified

- **The hosted suite on the delta:** pending on this push.
- **Linux:** `lockedDestinationSaysToCloseItAndKeepsTheFile` is Windows-only, since POSIX rename replaces an open file. The zero-rows tests are portable, but I ran them only on Windows.
- **Which Windows error the real lock gave:** the test accepts 5 or 32 and doesn't record which.
- **A real spreadsheet app:** the test holds the file with a plain `QFile` handle, not Excel. The review reproduced the same error 5 with a locked file.

## What it does

The new `export-selection` command (`Ctrl+Shift+E`) opens a dialog that exports the grid selection, or the viewed photo if nothing is selected, to a CSV or path list. The engine runs on a worker, so the GUI never blocks and any engine failure becomes a message in the dialog. Viewing and exporting never write to the original media.

- **`src/app/selection_export_service.{hpp,cpp}`:** `SelectionExportService`, one `std::jthread` slot with generation ids, so a stale result is dropped. Progress is coalesced, and the destructor stops and joins the worker. Engine exceptions are contained (review O1), and an item whose name can't be converted to UTF-8 is left out with a reason instead of failing the export.
- **`ui/ExportDialog.qml`:** the flow, with phases `checking`, `ready`, `running`, `replace` and `done`.
- **`ui/Main.qml`:** a `Loader` by URL, so nothing loads until first use, and other executables that load `Main.qml` are unaffected.

## Behaviour covered

| Case | Behaviour | Test |
| --- | --- | --- |
| Progress and live GUI | Worker thread; coalesced updates; the GUI responds during a held export | `exportsSnapshotOnWorkerAndLeavesOriginals`, `progressUpdatesAreCoalesced`, `guiStaysLiveAndEscapeCancelsARunningExport` |
| Cancel | Stop or Escape; nothing left behind; closing mid-run stops the worker | `cancelMidExportLeavesNothing`, `guiStaysLiveAndEscapeCancelsARunningExport`, `destroyingTheServiceStopsTheWorker` |
| Destination choice | Field and save dialog; the file type follows through to the path and options | `aFileChosenInTheSaveDialogFillsTheField`, `changingTheFileTypeFollowsThroughToThePathAndOptions` |
| Overwrite | Confirmation required; never for an exported item or a non-`.csv`/`.txt` file | `existingFileIsReplacedOnlyAfterConfirmation`, `anExportedItemIsNeverOfferedForReplacing`, `refusalsWriteNothing` (8 rows) |
| Unwritable destination | A real OS refusal becomes a clear message | `unwritableDestinationBecomesAClearMessage` (`icacls` deny or `chmod`) |
| Engine exception | Becomes a result and a message, not a crash | `engineExceptionsBecomeResults`, `engineExceptionBecomesAClearMessage` |
| Empty selection | A notice, and no dialog | `emptySelectionExplainsAndOpensNothing` |
| Selection changes behind the dialog | The captured items are exported; a note offers "Use the current selection" | `selectionChangeBehindTheDialogNeverChangesTheExport`, `selectionEmptiedBehindTheDialogStillExportsTheCapture` |
| Items that can't be exported | Missing, changed or unrepresentable items are named with a reason; the rest still export | `missingItemIsReportedWithoutStoppingTheRest`, `itemChangedAfterSnapshotIsSkipped`, `loneSurrogateNameIsLeftOutNotFatal` (Windows), `itemsLeftOutAreNamedWithTheirReason` |
| Stale results and limits | A newer prepare drops the older result; too many items are refused before any work | `newerPrepareDropsTheOlderResult`, `tooManyItemsAreRefusedBeforeAnyWork` |
| Keyboard, names, sizes | Initial focus, Escape, accessible names; fits at 560x640, 560x420 and 1180x780 | `keyboardOperableWithAccessibleNames`, `escapeClosesAnIdleDialog`, `fitsAtNarrowAndWideWindows` |

All test media is generated. Captures of the form, error, replace, running and done states at all three sizes are in `docs/verification/B3/` (15 files).

## Targeted `ctest` (actual output, on `5486ecf`)

```
Test project C:/src/Github/Glancelot/.claude/worktrees/elastic-kare-4edd77/build-claude-b3-export-ui
    Start 29: shortcuts_help
1/3 Test #29: shortcuts_help ...................   Passed    0.95 sec
    Start 49: command_routing
2/3 Test #49: command_routing ..................   Passed   14.75 sec
    Start 52: export_ui
3/3 Test #52: export_ui ........................   Passed   56.34 sec

100% tests passed out of 3
```

`export_ui` totals: `39 passed, 0 failed, 0 skipped`. `command_routing` and `shortcuts_help` are included because the packet adds a command and a default shortcut.

The same three also passed on the previous run, when `export_ui` took 132 s with the machine under load. That is why its limit went from 180 s to 300 s.

**Full offscreen suite:** not run (see above), so no baseline failures are named here. The hosted `viewer` job decides.

## Fail-before evidence

Each mutation breaks one behaviour in the source, and the named tests are then run against it. **28 of 29 were caught; the one that survived is explained below.** The exact edits are in `docs/verification/B3/fail-before/mutations.json` and the results in `results.json`, including the failure text. Exit code 1 or higher is the number of failing test functions and rows reported by QTest.

| Mutation | What it breaks | Result | Failing test |
| --- | --- | --- | --- |
| `no-exception-containment` | Engine exceptions escape the worker | Caught: process crash, exit -1073740791 | none named (see note 1) |
| `no-unrepresentable-precheck` | Lone-surrogate name reaches the engine | Caught | `loneSurrogateNameIsLeftOutNotFatal` |
| `cancel-does-nothing` | Cancel doesn't stop the worker | Caught | `cancelMidExportLeavesNothing` |
| `stale-result-delivered` | Older results still delivered | Caught | `newerPrepareDropsTheOlderResult` |
| `progress-not-coalesced` | One queued event per item | Caught | `progressUpdatesAreCoalesced` |
| `no-destination-refusals` | All pre-checks off | Caught | `refusalsWriteNothing` (6 rows) |
| `max-items-ignored` | Item limit off | Caught | `tooManyItemsAreRefusedBeforeAnyWork` |
| `runs-on-the-gui-thread` | Export runs on the GUI thread | Caught | `exportsSnapshotOnWorkerAndLeavesOriginals` |
| `loaded-at-startup` | Dialog loads at startup | Caught | `mainLoadsTheDialogOnlyOnFirstUse` |
| `command-unregistered` | Command missing from the registry | Caught | `mainLoadsTheDialogOnlyOnFirstUse` |
| `no-empty-selection-notice` | Empty selection is silent | Caught | `emptySelectionExplainsAndOpensNothing` |
| `not-modal-for-shortcuts` | App shortcuts fire behind the dialog | Caught | `exportFromTheDialogShowsTheResult` |
| `escape-does-not-cancel` | Escape ignored while running or asking | Caught | `guiStaysLiveAndEscapeCancelsARunningExport`, `existingFileIsReplacedOnlyAfterConfirmation` |
| `overwrites-without-asking` | Export always overwrites | Caught | `existingFileIsReplacedOnlyAfterConfirmation` |
| `replace-in-exports-slot` | Double-click on Export could confirm Replace | Caught | `existingFileIsReplacedOnlyAfterConfirmation` |
| `replace-ignores-held-key` | A held Enter confirms Replace | Caught | `existingFileIsReplacedOnlyAfterConfirmation` |
| `offers-replace-for-an-item` | Service's item refusal cleared | **Survived** (see note 2) | none |
| `item-reason-not-reported` | Item reported as merely protected | Caught | `refusalsWriteNothing(an exported item)`, `anExportedItemIsNeverOfferedForReplacing` |
| `unselected-photo-replaceable` | Suffix guard removed | Caught | `refusalsWriteNothing(a photo not in the selection)` |
| `selection-move-unnoticed` | No note when the selection changes | Caught | `selectionChangeBehindTheDialogNeverChangesTheExport`, `selectionEmptiedBehindTheDialogStillExportsTheCapture` |
| `export-follows-the-selection` | Export silently follows the new selection | Caught | `selectionChangeBehindTheDialogNeverChangesTheExport` |
| `failure-closes-silently` | Unexpected failure closes the dialog | Caught | `engineExceptionBecomesAClearMessage` |
| `unwritable-unexplained` | OS write failure shows nothing | Caught | `unwritableDestinationBecomesAClearMessage` |
| `skips-not-shown` | Left-out items not listed | Caught | `itemsLeftOutAreNamedWithTheirReason` |
| `format-change-ignored` | File type choice ignored | Caught | `changingTheFileTypeFollowsThroughToThePathAndOptions` |
| `no-initial-focus` | No focus on open | Caught | `keyboardOperableWithAccessibleNames` |
| `export-not-named` | Control has no accessible name | Caught | `keyboardOperableWithAccessibleNames` |
| `idle-escape-ignored` | Escape can't close an idle dialog | Caught | `escapeClosesAnIdleDialog` |
| `too-wide-for-560` | Dialog is 640 px wide | Caught | `fitsAtNarrowAndWideWindows` (2 sizes) |

Notes:

1. `no-exception-containment` terminates the process (0xC0000409) when the exception leaves the worker thread, which is what that mutation should do. QTest writes no failure line, so the evidence is the exit code. The mutated test was `engineExceptionsBecomeResults`.
2. `offers-replace-for-an-item` is an equivalent mutant. It clears the service's refusal, but the engine then refuses the same destination itself (`src/core/selection_export.cpp:292`), so behaviour doesn't change. Looking into it did show a real weakness: `anExportedItemIsNeverOfferedForReplacing` asserted text that also appears in the protected-file message. The test now asserts the item-specific message, and `item-reason-not-reported` and `unselected-photo-replaceable` cover this code properly.
3. The first 27 mutations ran on `ea37573`, and the last two on the tightened test in `5486ecf`. Only one assertion and the `export_ui` limit differ between the two.
4. An earlier mutation run was discarded and is not in this evidence: its builds timed out behind other sessions' gates, and it recorded the wrong exit code.

**Not mutation-checked.** This is a representative set, as the lead allowed, so checklist item 1 isn't fully met. 23 of the 28 test functions are named by a mutation. These 5 pass on the clean build, but **I never saw them fail**:

| Test | What it would take to check | Why it matters |
| --- | --- | --- |
| `destroyingTheServiceStopsTheWorker` | Drop `request_stop` from the service destructor | **Highest priority.** Closing mid-export must stop the worker and leave no file behind |
| `missingItemIsReportedWithoutStoppingTheRest` | Stop recording the Missing skip in `prepare` | The rest of the export must still complete |
| `itemChangedAfterSnapshotIsSkipped` | Hard to mutate from the service, because the engine does this check | Confirms the engine's check is reachable through the service |
| `pathListAndRelativeRootOptionsReachTheEngine` | Ignore the options when building the request | Path-list and relative-root options |
| `aFileChosenInTheSaveDialogFillsTheField` | Empty `applyChosenFile` in the QML | The save dialog's result reaches the field |

I left these out because another gate request was queued and a build slot could have meant another long wait. A reviewer who wants more certainty should start with the destructor.

**Update after review:** the fresh reviewer mutated all five, and each failed (`docs/verification/B3-REVIEW.md`). That also corrects two things in the table above: a detached worker is the right destructor mutation, because dropping only `request_stop` hangs; and `request.revalidate = false` does catch the changed-item test, so "hard to mutate" was wrong. That test has since been rewritten (see the review delta).

## Grant check

`git diff --name-only f24f2e7..<tip>`, with the 15 `docs/verification/B3/export-*.png` captures collapsed:

| File | Status |
| --- | --- |
| `ui/ExportDialog.qml`, `src/app/selection_export_service.{hpp,cpp}`, `tests/export_ui_tests.cpp`, `cmake/ExportUiTests.cmake` | Granted (new files) |
| `CMakeLists.txt` | Granted: one include line |
| `src/app/command_registry.cpp` | Granted: per-task block |
| `translations/glancelot_en_XA.ts` | Required by the brief: 63 new messages, purely additive (652 in total), generated with lupdate 6.8.3 and `pseudolocalize.py` |
| `docs/verification/B3/*` | Required by the brief, plus the fail-before evidence |
| `docs/handoff/B3-HANDOFF.md` | Required by `AGENTS.md` |
| `ui/Main.qml` | **Slightly beyond the literal grant, for the lead to accept.** The grant names one instantiation and invoke site; there are three touches (see below) |
| `translations/README.md` | **Outside the grant.** One line updating the message count from 589 to 652, because the file states it |

`ui/Main.qml` touches: the `modalOpen` line (437, so app shortcuts don't fire behind the dialog), the invoke branch (849) and the `Loader` block (2194-2215). None overlaps PR 3's hunks on `claude/stack-confirm-fix`, which fall at base lines 645-657, 1677 and 1838-1876. No `include/glancelot/*` file changed.

**Size:** `pr_guard` reports `1774 changed lines; aim for about 1500`, with 0 errors. The catalog and captures aren't counted. That is 983 product lines and 790 test lines, so it's over the "well under 1,500" the brief expected. `tests/export_ui_tests.cpp` is the reason, and it splits cleanly into worker and dialog tests if the lead prefers.

## UI skills

Read and applied, all available: application-interface-design, layout-spacing, responsive-layout, typography-ui, design-review. Copy is sentence case, theme tokens are used throughout, and numerals are tabular. In a short window the form scrolls, while status, progress and the replace question stay in a fixed footer above the buttons.

## Decisions taken

- **Load by URL on first use**, rather than a by-name type. Startup pays nothing, and about 8 other test executables that load `Main.qml` don't need this source. Alternative: instantiate at startup.
- **The service registers its own QML type** with a startup function, so the app and tests share one path and no shared module file changes. Alternative: `QML_ELEMENT`, which would need build changes outside the grant.
- **Items are captured when the dialog opens**, with an explicit "Use the current selection". The export can't silently change under the user. Alternative: follow the live selection.
- **The grid selection, or else the viewed photo**, matching `copy-selection`.
- **Empty selection shows a notice and no dialog**, because there's nothing to configure.
- **Default destination `Documents/selection.csv`**, with the folder remembered for the session only, because there's no settings grant.
- **Replacing is limited to `.csv` and `.txt`** (`destinationProtected`). Otherwise Replace could overwrite a photo that just wasn't selected. Alternative: rely on the engine, which only protects exported items.
- **An item with an unrepresentable name is left out and named**, rather than failing the whole export.
- **CSV options:** the UTF-8 BOM is always on, formula protection is on by default, and there are no metadata columns.
- **`Ctrl+Shift+E` as the default shortcut**, which is unused. `resolveCommandBindings` drops a new default that collides with a stored choice, so this is safe for existing users. Alternative: no default, which hides the feature from the shortcut list.
- **Replace sits to the left of "Keep the file"**, so a double-click on Export lands on the safe answer.
- **Status, progress and the replace question are in a fixed footer**, because at 560x420 the question was below the fold.

## Open questions for the lead

None block this packet.

1. **Grid context menu:** the command is reachable by shortcut and command list only. A context-menu item needs another `Main.qml` site, so I didn't add one. Do you want it in a later cycle?
2. **Grant:** please accept the extra `modalOpen` touch and the `translations/README.md` line, or tell me to change them.
3. **`translation_coverage_ui`** doesn't list the dialog as a surface yet. Adding it means editing another track's test file, so it's yours to assign.
4. **Command label:** it's translated at run time, so lupdate doesn't extract it, like the other registry labels. Engine `detail` text is English only.

## Unverified

- **Full suite and `docker build`:** not run locally, by lead decision. The hosted `viewer` job and the tester's gate cover them.
- **Linux:** nothing was run on Linux. `loneSurrogateNameIsLeftOutNotFatal` is Windows-only, and the `chmod` branch of `unwritableDestinationBecomesAClearMessage` first runs on the hosted job.
- **The native save dialog on Windows:** tests use `applyChosenFile` and the text field, not the native dialog.
- **"Show in folder":** launching Explorer wasn't exercised.
- **The dialog in the real app:** `Glancelot/qmldir` lists `ExportDialog 1.0 ui/ExportDialog.qml` and `glancelot` builds, but tests load the QML from source, and I didn't open the dialog in the shipped executable.
- **`en_XA` layout:** the catalog is generated, but I didn't look at the dialog in it for clipping.
- **Physical display:** captures come from the offscreen software backend. Physical qualification is the user's, from a RUNBOOK packet.
