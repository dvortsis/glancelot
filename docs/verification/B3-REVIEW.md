# B3 review: F070 export dialog over `selection_export`

**Verdict: APPROVED**

Written for: the lead. Reviewer: a fresh-context Claude Opus 5.5 subagent that wrote none of this packet.

| | |
| --- | --- |
| Reviewed head | `1a8be7674dbbdbf18d66170ee004f49f2591349d` (PR 9, `claude/b3-export-ui`) |
| Base | `f24f2e7` |
| Build | `.worktrees/review-b3/build-review-b3`, under a build slot, as `scripts/build.ps1` does it |
| PR checks at review | `guard`, `core` (both), `viewer` and `ci-ok` all pass (`viewer` took 35m56s) |
| Merge with current `origin/main` | no conflict (`git merge-tree` on the same day) |

Nothing blocks. Finding 1 is the one to decide on: it is below the blocking bar, because the user confirmed the replace, the result is reported accurately and originals are untouched, while the brief's data-safety cases are unconfirmed, partial or failed writes. The rest are polish and test gaps.

## Targeted tests (actual output, clean head)

```
Test project C:/src/Github/Glancelot/.worktrees/review-b3/build-review-b3
    Start  1: selection_export
1/6 Test  #1: selection_export .................   Passed    0.83 sec
    Start  7: viewer_acceptance
2/6 Test  #7: viewer_acceptance ................   Passed   19.61 sec
    Start 29: shortcuts_help
3/6 Test #29: shortcuts_help ...................   Passed    1.14 sec
    Start 47: translation_coverage_ui
4/6 Test #47: translation_coverage_ui ..........   Passed   44.15 sec
    Start 49: command_routing
5/6 Test #49: command_routing ..................   Passed   20.65 sec
    Start 52: export_ui
6/6 Test #52: export_ui ........................   Passed   84.12 sec
100% tests passed out of 6
Total Test time (real) = 171.05 sec
```

The full suite was not run, as briefed.

## Findings

No finding is blocking.

| # | Severity | Where | Finding | Concrete fix |
| --- | --- | --- | --- | --- |
| 1 | Medium, confirmed-replace outcome, lead decision | `src/core/selection_export.cpp:309,593-601`; `ui/ExportDialog.qml:411` | **A confirmed Replace can swap an earlier export for an empty one.** If every item vanished or changed after the dialog opened, or every item is outside the chosen root, the engine still commits a header-only file over the old one. Reproduced: `earlier export` became `path,name,kind,bytes,modified`, status `written`, 0 written, 2 skipped. The user did confirm, the dialog says the file "lists no items", and original media is untouched, so this is a narrow case and not an unconfirmed loss. | Two options for the lead. **In the service, no header change:** when `overwrite` is set, re-snapshot the items on the worker before calling the engine, and refuse with "Nothing could be exported. The file was left as it was." if none survive. This fixes the case I reproduced but leaves a tiny race, and it does not cover every item being outside the root unless it also checks that. **In the engine:** a contract option such as `ExportRequest::refuseEmpty` that removes the temp file and returns a new status when `written == 0`. This closes every case but changes a frozen header. |
| 2 | Low to medium, copy | `ui/ExportDialog.qml:145` | **The most common re-export failure gets the wrong hint.** With the destination locked, as a CSV open in a spreadsheet would be, the message says to check permission and space, with the detail `Atomic replacement failed with Windows error 5`. The old file stays intact and nothing is left behind (reproduced), so this is advice, not safety. | Add "or close it if it's open in another app" to the message, and don't show raw engine text as the visible detail. |
| 3 | Low, test | `tests/export_ui_tests.cpp:212` | `itemChangedAfterSnapshotIsSkipped` deletes the file, so it repeats the Missing path. Nothing proves the `changed` reason or its dialog text through the service. | Rewrite the file with different content and assert `reason == "changed"`. |
| 4 | Low, unreached state | `ui/ExportDialog.qml:248` | If `prepare` catches an exception, the summary has no `files` or `folders`, so the label reads `nan items to export` above the error (shown by injecting that summary). I found no ordinary way to trigger the exception, and no test covers this branch. | Return `""` when `s.status === "error"`, and add a test seam. |
| 5 | Low, layout | `ui/ExportDialog.qml:365` | At the app's minimum window, 540x360 (outside the brief's sizes), an error plus the selection note pushes the error row below the fold, and its alert icon draws over the footer outside the scroll clip. This looks like the known CurveRenderer clip problem. It takes a selection change behind a modal dialog to get there. The form alone at 540x360 also cuts off the formula switch, with no scrollbar in the capture. | Move the error to the fixed footer with the other status text, which also solves the below-the-fold case. |
| 6 | Low, memory | `src/app/selection_export_service.cpp:144`; `ui/ExportDialog.qml:203-208` | The snapshot, `capturedPaths` and `currentPaths` stay in memory after the dialog closes, until the next `prepare`, because the Loader is never deactivated. All are bounded by `maxItems` (1,000,000). | Clear them in `onClosed`, through a small `release()` on the service. |
| 7 | Low, polish | `src/app/selection_export_service.cpp:124-127` | The default destination shows forward slashes on Windows (`C:/Users/...`), while Browse gives backslashes. | Apply `QDir::toNativeSeparators` in `defaultFolder()`. |
| 8 | Low, polish | `ui/ExportDialog.qml:352-356` | Browse opens the save dialog without the current file name. | Set `saveDialog.selectedFile` from the field. |
| 9 | Low, cost | `ui/ExportDialog.qml:92-96,172-175` | Each `viewer.stateChanged` while the dialog is open rebuilds `selectedPaths()`, which is O(rows), and compares it in JS. `Qt.callLater` coalesces this, and it runs only while the dialog is open. | Compare only on `fileGrid.selectionChanged`, plus `filePath` changes when nothing is selected. |
| 10 | Low, note | `src/core/selection_export.cpp:291`; `src/app/selection_export_service.cpp:257-266` | The engine's destination pre-check has no stop check, so cancel or exit waits for it on a very large selection. The skip-report code sits outside the `try`, so only an allocation failure there could escape. `progress(i, total)` fires before item `i`, so the bar never shows N of N. | No change needed now. Add a stop check when the engine is next touched. |

## Data safety, as checked

- **Existing file before confirmation:** untouched. The engine refuses at `selection_export.cpp:309`, and the final commit can't overwrite (`MoveFileExW` without `REPLACE_EXISTING`, or `link()` elsewhere).
- **Cancel or failure:** the export writes to a temp file and replaces the destination only at the final rename. Reproduced through the real dialog: Replace, then Escape while running, leaves `earlier export` intact, no leftovers and the message "Export cancelled. Nothing was written."
- **Killed process:** a `.glancelot_export_*.tmp` file can be left in the destination folder. A leading dot does not hide it on Windows. The only unhandled case, and not worth more than this note.
- **Destination in the source folder:** works (probe). The CSV appears in the grid as a fourth item, the selection stays at 3, no note appears and the originals are unchanged.
- **Destination is a photo:** an item is refused even with `overwrite`, and a photo outside the selection is refused by the suffix guard. A `.csv` or `.txt` that isn't a previous export can be replaced, after confirmation.
- **NTFS alternate data stream** (`img2.png:evil.csv`): refused by the OS with error 87 and reported as a failure. No stream was created and the photo is unchanged (probe). The result's `written` field is 2 in this case, but the dialog doesn't show it.
- **Worker lifetime:** the destructor stops and joins before `d` goes away, and pending queued calls die with the `QObject`. A stale id is dropped on the GUI thread. `lastId`, `running` and `pending` are touched only on the GUI thread, and progress uses atomics. At exit, the wait is bounded by the check between items and finding 10.
- **Large selection:** 20,000 real files took 1.4 s to prepare and 4.1 s to export on this machine, all on the worker. An item count over the limit is refused before any work, and reported skips are capped at 200.

## Fail-before spot-check

I ran these on the reviewed head, with each edit restored afterwards (`git status` clean, HEAD unchanged). They cover all 5 test functions the handoff lists as unchecked, and each failed.

| Mutation | Test | Result |
| --- | --- | --- |
| Destructor detaches the worker instead of stopping and joining | `destroyingTheServiceStopsTheWorker` | **Fails:** `leftovers(out.path()).isEmpty()` returned FALSE |
| `request.revalidate = false` | `itemChangedAfterSnapshotIsSkipped` | **Fails** (so the handoff's "hard to mutate from the service" doesn't hold, though see finding 3) |
| The Missing skip isn't recorded in `prepare` | `missingItemIsReportedWithoutStoppingTheRest` | **Fails** |
| `request.format` is always CSV | `pathListAndRelativeRootOptionsReachTheEngine` | **Fails** |
| `applyChosenFile` ignores the URL | `aFileChosenInTheSaveDialogFillsTheField` | **Fails** |

The handoff's suggested destructor mutation, dropping `request_stop` only, would hang rather than fail, so I used a detach.

**The surviving mutant is equivalent.** `offers-replace-for-an-item` clears the service's refusal, but the engine checks for an item at `selection_export.cpp:291-306`, before the exists check and the temp file and regardless of `overwrite`. Behaviour doesn't change. Accepted.

**Sleeps:** no plain `QTRY_*` and no `QSignalSpy::wait()`. The only marked waits are the stated ones: a capture-only `qWait(250)` that runs only with `GLANCELOT_B3_EVIDENCE` set, and two `yield` loops, of which one is the held engine and the other has a deadline.

## Shipped executable (the handoff listed this as unverified)

With a local patch to `app/main.cpp` (not committed), `glancelot.exe` ran offscreen and went through the real `export-selection` command on a generated PNG:

```
loader status=Ready source=ExportDialog.qml
flow=loaded
phase=ready modal=true
field=C:/Users/dvort/OneDrive/Documents/selection.csv
phase after export=done
csv: path,name,kind,bytes,modified
```

There were no QML errors on stderr. So the runtime `qmlRegisterType` into the `Glancelot` module and the load by URL from the module's resources both work in the real app. The CSV has no `folder` column, because the frozen `ExportRequest` default leaves it out, unlike the column list in `docs/EXPORT-CSV-01-PLAN.md`. That predates this packet.

## Grants, catalog, call site

- **Both grant items accepted.** The `modalOpen` touch is needed: the handoff's `not-modal-for-shortcuts` mutation shows app shortcuts fire behind the dialog without it. The `translations/README.md` line is accurate (589 + 63 = 652) and harmless.
- **For the lead's awareness:** `cmake/ExportUiTests.cmake` adds product sources to `glancelot` outside `BUILD_TESTING`. This is needed, and the header comment says so.
- **Catalog: 63 messages is right.** The diff is 255 added lines (63 x 4, plus 3 for the context) and 0 removed, in one new `ExportDialog` context, with nothing unrelated changed. The count comes from one dialog with 5 phases, 9 error messages, 6 skip reasons and hand-built singular and plural pairs. The pairs are the repo's convention: `ui/` has no `%n`.
- **`ui/Main.qml` stays thin:** one `modalOpen` clause, one invoke branch and a short `Loader`.

## UI (skills read: application-interface-design, layout-spacing, responsive-layout, typography-ui)

The captures in `docs/verification/B3/` at 560x420, 560x640 and 1180x780 are sound.

- There is one primary action in each state. On the replace question the danger button is not the focused one and sits away from where Export was.
- Labels stay visible. Errors sit next to the field with an icon and text, and the entered path survives an error.
- The form stays in place, disabled, while running, so nothing jumps. Progress and the replace question stay in the footer in a short window.
- Spacing and type come from theme tokens, numerals are tabular, and line height is 1.4.
- Keyboard: focus starts on the file name, Tab reaches every stop, and Escape cancels or backs out. A held Enter can't confirm Replace. Names and alert roles are set.

## Not verified

- **`en_XA` layout.** `lrelease` isn't in this checkout's Qt, so no `.qm` is built and the run stayed English ("no translation catalog for GLANCELOT_LANGUAGE=en_XA"). This is also unverified in the handoff. `translation_coverage_ui` reads the `.ts` directly and passes, but it doesn't list this dialog as a surface.
- **The native save dialog and "Show in folder"** were not exercised.
- **Linux:** the `chmod` branch of `unwritableDestinationBecomesAClearMessage` always takes its `QSKIP` in Docker, because `Dockerfile:23` runs `ctest` as root before `USER viewer` at line 39. That branch is unverified, not covered.
- **Physical display:** all captures come from the offscreen software backend.
