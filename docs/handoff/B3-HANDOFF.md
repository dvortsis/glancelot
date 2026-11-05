# B3 handoff: F070 export dialog over `selection_export`

Written for: the lead and the fresh-context reviewer. Implementer: a Claude Code (Opus 5.5) local track session, which did not review this packet.

| | |
| --- | --- |
| Branch | `claude/b3-export-ui` |
| Base (exact) | `f24f2e7` (`origin/main` when the packet started); not a stacked packet |
| Commits | `ea37573` (the feature), `5486ecf` (stronger checks, evidence), then the commit that adds this file, which is the tip |
| Review diff | `git diff f24f2e7 <tip>` |
| `main` since the base | `3a14044`: docs and queue only (`AGENTS.md`, `docs/WORK-QUEUE.yaml`, one decision), so not merged in |

**Full suite: not run locally, by lead decision** (November 4, 2026: implementers don't run it, because the gate lock is the machine's bottleneck). The hosted `viewer` job runs it on the PR, and the tester gates the exact head before merge. Targeted results are below.

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
