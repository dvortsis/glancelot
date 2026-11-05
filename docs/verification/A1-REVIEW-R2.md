# A1 second review: incremental cancellable folder listing

**Verdict: CHANGES REQUIRED**

- **Reviewed head:** `ac490319e0fbe8e258b79de2acab326b7f62edec` (`claude/a1-perf-enum`, PR 7). Code tip `2810c5c`. Includes merge `2876dbf` of `main` at `7c0da34`. Packet base `3a14044`.
- **Reviewer:** fresh context; wrote neither this code nor the first review. Windows 11, MSVC, Qt 6.8.3, offscreen with the software backend. Build directory `build-review-a1-r2`.
- **UI skills:** `application-interface-design` read. The QML changes add no layout, geometry, icon or motion work, so the other skills did not apply.
- **`ci-ok`:** green on this head, with `guard`, both `core` jobs and the hosted `viewer` job.

All six first-review findings are fixed, and the menu decision holds for every selection command. The model layer stays sound. What blocks is three small gaps in `ui/PhotoGrid.qml`. Each reproduces against this head, and each has a fix of a few lines that I tried locally: `docs/verification/A1-REVIEW-R2-trial-fix.patch`. The tests are in `docs/verification/A1-REVIEW-R2-scratch-tests.patch` (names start with `r` or `x`; apply to `ac49031`).

## First-review findings

| Finding | Status | Evidence at this head |
| --- | --- | --- |
| B1. Keys and scrollbar overridden by placement | **Fixed** | `rKeyboard...`, `rSelectAll...`, `rScrollbar...` and `rRealWheelSettles` pass. Right-click is covered by the committed `rightClickDuringArrivalIsNotReplaced`. |
| B2. Restore read geometry not yet laid out | **Fixed** | `rFolderInsertsKeepTheAnchor`: anchor row 2518, top 2514, same line (was top 2664). Scrollbar: anchor 2383, top 2382 (was clamped at 1128). |
| B3. Try again on an arriving folder lost the position | **Fixed** | `rRetryArrivalRestoresViewport`: anchor 1488, top 1488 (was 0). The quick path still passes. |
| B4. Rename while hidden lost the cursor | **Fixed** | `rRenameWhileHiddenKeepsTheCursor` ends on `renamed.png`. |
| B5. Lost wakeup left `loading` stuck | **Fixed** | By reading, `dry` is now true whenever the definitive batch was taken. The committed test with its 7 ms seam fails with the fix reverted (see Fail-before). |
| C1. 10k numbers overstated | **Fixed** | The handoff, the PR body and the `trace10k` comment use the like-for-like figures. The 100k "before" is labelled as not like for like. |

The first review's N1, N2, N5 and N7 are also fixed, with the two small remainders of N2 below. Its N3, N4 and N6 stay open with reasons in the handoff, and I agree they can wait.

## Blocking findings

### R2-B1. The grid's own Clear and Invert buttons lose to the placement (medium, regression)

`ui/PhotoGrid.qml:532`, `:543`.

`gridInvertSelection` and `gridClearSelection` call the model directly, so they are the one selection path left that does not settle. This is B1's class. The lead's rule was written for the menu, and I read it as covering these buttons too, since they do the same thing. It needs a multi-selection kept across leaving the grid, a different photo viewed meanwhile, and a click while the listing arrives. By reading, this cannot happen on `main`: the buttons show only when two or more selected rows are showing, and no rows show until the first listing has landed.

```text
xToolbarSelectionDuringArrival(clear)   selected after the click 0     placing true
                                        after the listing landed 1 (p2.png)    FAIL
xToolbarSelectionDuringArrival(invert)  selected after the click 1021  placing true
                                        after the listing landed 1 (p2.png)    FAIL
```

**Fix.** `panel.settleByUser()` first in both `onClicked`. Tried: both tests pass, keeping 0 and 1021. Commit a test like this one.

### R2-B2. The first frame after the definitive order shows the wrong page (medium)

`ui/PhotoGrid.qml:317`.

`onLayoutChanged` defers the restore with `Qt.callLater`, and a frame gets in first. When the view was scrolled during arrival and the order moves rows, the frame after the definitive order is drawn at the old `contentY` over the new rows, hundreds of lines from the anchor. The next frame is right. The final position is correct, which is why every committed test passes: they all look at the end state. The brief says the visible row must not jump, and this is a one-frame flash of an unrelated page. Any sort that differs from enumeration order, such as by date, moves rows this way.

Drift of the anchor from the top of the view, in lines, sampled at `QQuickWindow::frameSwapped`:

```text
xVisibleRowAtEveryFrame, under the gate lock, three rounds
files only           per frame after definitive QList(297)  QList(297)  QList(297, 0)
every 10th a folder  per frame after definitive QList(287)  QList(287)  QList(287, 0)
every 7th a folder   per frame after definitive QList(273)  QList(273)  QList(273, 0)
```

An earlier run in a build slot read worst drifts of 297, 0 and 273, so 11 of 12 runs showed the wrong frame. While rows were arriving the drift was 0 at every sampled frame, so arrival itself holds still. This is offscreen with the basic render loop. How often it shows on a real display with the threaded loop is not verified.

**Fix.** Call it directly in `onLayoutChanged`: `panel.restoreViewport(saved)`. `restoreViewport` already forces the layout. Tried: the drift read 0 at every frame in six runs, and all 25 committed `perf_enum_ui` cases pass with this and R2-B1's fix applied. Frames are few here, and two of those six runs drew none after the definitive order, so a frame sampler alone can pass without testing anything. A firmer test, which I did not try: connect to `arrivingChanged` and assert the anchor is on the top line inside that slot, since `publishDefinitive` clears `arriving` right after the layout change.

### R2-B3. Escape from type-ahead returns to the wrong file (low to medium)

`ui/PhotoGrid.qml:659`, `:708`.

`preSearchRow` is a row number. Type-ahead started during arrival and cancelled with Escape after the definitive order selects whatever now sits at that row:

```text
xEscapeFromSearchAfterTheOrderLands
before search ".../f02939.txt"   after escape QList(".../f00003.txt")   FAIL
```

The window is the search timer's 3 s. The row-number logic is also on `main`, where only a refresh can move rows. A1 makes it certain, because the definitive order moves every row. Selection must survive arrival, and the next delete or copy would act on this file, though it is highlighted and scrolled into view.

**Fix.** Keep the path beside the row and look it up on Escape. Tried: the test passes on `f02939.txt`. Commit a test.

## Non-blocking

- **N1. Two small stale `cursorRow` cases remain, from reading, not run.** The folder insert emits `selectionChanged` before the same tick's file rows go in. If the cursor's own file is in that tick, `cursorRow` is past `count` and `selectedPath` reads "" until the next emission. A files-only tick that brings the cursor's row emits nothing, so a re-entered folder can read "No selection" until the definitive order. Both end at the definitive order, and `activate()` reads live values. Moving the emit below the file insert, and emitting when a batch contains `cursorKey`, would close them.
- **N2. Nothing commits the "does not settle" half of the menu rule.** A mutation that settles on every command should survive the committed tests. `xNonSelectionCommandsKeepThePlacement` passes today for `fit`, `toggle-metadata`, `grid-zoom-in` and `filter`, and is worth committing. `xInvokeSelectionDuringArrivalSettles` runs the real `window.invoke` path that menu items use, where the committed test calls `viewer.trigger`.
- **N3. A key settles even when it chooses nothing.** Any non-modifier key does, such as Tab, while the same zoom from a menu does not. The only result is that the view stays where it is. It follows the first review's suggested fix, so I would leave it.
- **N4. Weak wait.** `aFinishedScanIsNotLostAtTheDefinitiveBatch` waits a fixed 200 ms (`sleep-ok`) for the worker's last steps. On a very loaded machine it can pass without exercising the race. It cannot fail falsely, and it failed correctly here with the fix reverted. A wait on the `beforeProbes` seam would be firmer.

## Questions for the lead

Both are product behaviour, so I did not treat them as findings.

1. **Select all during arrival selects only the rows that have arrived.** The committed tests pin this (1024 of 3000). It is safe, since it selects too little, and the chip shows the honest count, but after the definitive order those rows are scattered through the grid. Should it stay, or should it extend to rows still arriving?
2. **The footer reads "3 of 1,024" during arrival,** with no "so far" (`ui/Main.qml:2152`, from reading). The header label is honest. `ui/Main.qml` is outside A1's grant, so a change needs a grant, here or in a later packet.

## What the brief asked to check

**Menu selections (`2810c5c`).** Sound. `select-all`, `grid-select-all`, `clear-selection` and `invert-selection` are every command that changes the selection: `handleViewCommand` in `ui/Main.qml` has no others, and `openGridMenu` selects only after a right click or `contextAtCursor`, which both settle first. Menu items reach it through `window.invoke` then `viewer.trigger`, and I ran that path for all four. It does not settle on `fit`, `toggle-metadata`, `grid-zoom-in` or `filter`. The buttons in R2-B1 are not commands, which is how they were missed.

**The merge (`2876dbf`).** `git show --remerge-diff` shows only the two `ui/PhotoGrid.qml` hunks, and both match what the lead specified: `main`'s `Accessible.description` with A1's scrollbar handler, and A1's modifier check with `main`'s Ctrl+F condition that excludes Shift. `en_XA` drops the old description string and has the new one. `input_ui`, `stack_confirm_ui` and `translation_coverage_ui` pass.

**The contract as a whole.** No findings beyond those above.

| Item | Result |
| --- | --- |
| Generation fence | `abandonScan` bumps the generation and drops the queue and finished result. A stale watcher result is dropped. |
| Bounded queue | `Arrivals` caps queued rows, and a batch over the cap enters only an empty queue. |
| Cancelling a blocked worker | `push` waits on the stop token and returns false, and the scan returns cancelled. |
| GUI thread | Enumeration, sort, keys, index and probes are all on the worker. |
| Right file | Selection and cursor are keys. `gridMenu.targets` are paths fixed at menu open, and `gridMenu.row` only decides whether "Open" shows. R2-B3 is the one row-number path I found that picks a file. |
| Original media | No write added: a search of the source diff finds none. |
| Focus | `xFocusSurvivesArrival` passes: the view keeps active focus and the next arrow moves on from the cursor. |
| Count wording | "N items so far" until the definitive order, with singulars. See question 2 for the footer. |
| Numbers | 10k is like for like. 100k is labelled as not. My traces agree, allowing for load. |

**Cost of the first review's N2 fix.** I checked this because every `selectionChanged` re-runs `selectedCount` and `selectedSummary`, each over all rows, and going up a folder leaves a selection set. No cost shows. These are a real window, 100k rows, under the gate lock, in two rounds. The selected row came from `select(10)`, not from going up a folder.

```text
                                       worst GUI gap ms   selectionChanged
files only, no selection               130, 63            2, 3
files only, a selected row             68, 55             2, 3
every 10th a folder, no selection      106, 88            16, 15
every 10th a folder, a selected row    98, 73             17, 14
```

Gaps here are 55 to 130 ms whatever the selection, against 12 to 32 ms in the model-only trace, so the window's own work with software rendering dominates. I did not compare this with `main`.

## Fail-before

Reverted locally, rebuilt, run, then restored. `git status` was clean afterwards.

```text
menu fix reverted -> menuSelectionDuringArrivalIsNotReplaced
FAIL!  : (select all) '!f.placing()' returned FALSE.
FAIL!  : (grid menu select all) '!f.placing()' returned FALSE.
FAIL!  : (invert) '!f.placing()' returned FALSE.
FAIL!  : (clear) '!f.placing()' returned FALSE.
Totals: 2 passed, 4 failed     exit 4

B5 fix reverted -> aFinishedScanIsNotLostAtTheDefinitiveBatch
FAIL!  : '!model.loading()' returned FALSE.
Totals: 2 passed, 1 failed     exit 1
```

The packet's own mutation logs are in `docs/verification/A1/` on the branch. I did not rerun the others.

## ctest output

`ac49031`, unmodified. In a build slot:

```text
1/7 Test  #4: directory_listing ................   Passed   35.58 sec
2/7 Test  #7: viewer_acceptance ................   Passed   15.02 sec
3/7 Test #16: directory_model ..................   Passed   62.95 sec
4/7 Test #48: translation_coverage_ui ..........   Passed   35.81 sec
5/7 Test #51: perf_enum ........................   Passed  115.37 sec
6/7 Test #52: perf_enum_ui .....................   Passed   87.87 sec
7/7 Test #55: stack_confirm_ui .................   Passed  343.31 sec
100% tests passed out of 7
Total Test time (real) = 696.43 sec
```

Under the gate lock, passing on the first run, so no rerun was needed:

```text
1/2 Test #12: folder_grid ......................   Passed  282.34 sec
2/2 Test #13: input_ui .........................   Passed   99.55 sec
100% tests passed out of 2
folder_grid Totals: 74 passed, 0 failed, 4 skipped
input_ui Totals: 33 passed, 0 failed, 1 skipped
```

Traces from that `perf_enum` run, in a build slot on a shared machine:

```text
10k-cold  first_row_ms=86  definitive_ms=87  settled_ms=1177 worst_gui_gap_ms=12
10k-warm  first_row_ms=65  definitive_ms=66  settled_ms=1075 worst_gui_gap_ms=13
100k-cold first_row_ms=142 definitive_ms=794 settled_ms=821  worst_gui_gap_ms=25
100k-warm first_row_ms=140 definitive_ms=816 settled_ms=824  worst_gui_gap_ms=32
```

Scratch tests against this head, before any trial fix. The 3 failures are R2-B1 (two) and R2-B3:

```text
Totals: 22 passed, 3 failed, 0 skipped, 0 blacklisted, 186879ms
```

`pr_guard` against `7c0da34`: `code=True, 0 error(s)`, with the notice `2310 changed lines`. Added fixed sleeps all carry `sleep-ok`, and all explicit deadlines are at least 10 s.

## Not verified

- A real slow or dead network share. Everything here uses the seam.
- A real display, a GPU or the threaded render loop, which matters most for R2-B2.
- The full suite and `docker build`: the hosted `viewer` job and the tester's gate cover them.
- The first review's `rFinishedScanIsNotLostAtTheDefinitiveBatch` (no injected delay) was added to the scratch patch but not run. The committed test covers B5.
- N2's mutation was not run; "should survive" is from reading the committed tests.
- The trial fixes beyond the tests named here. The Escape fix ran only its own test. None of this is a reviewed change.

## For the merge

`main` has moved to `20f1210`, which holds code this head lacks, so a merge and a new gate are needed before landing. `git merge-tree` reports no conflict.
