# A1 third review: incremental cancellable folder listing

**Verdict: CHANGES REQUIRED**

- **Reviewed head:** `5018b8fb86979b6003f688b0d81fd0ecff1db105` (`claude/a1-perf-enum`, PR 7).
  - Code tip `bc7b33e`.
  - Includes merge `c353d26` of `main` at `20f1210`.
  - Packet base `3a14044`. Round 2 reviewed `ac49031`.
- **Reviewer:** fresh context. I wrote neither this code nor the earlier reviews.
  - Windows 11, MSVC, Qt 6.8.3, offscreen with the software backend.
  - Worktree `.worktrees/review-a1-r3`, rebuilt from an empty `build-review-a1-r3`, so no object from an earlier mutated build was reused.
- **UI skills:** `application-interface-design` loaded. The QML changes add no layout, geometry, icon or motion work, so the other skills did not apply.
- **Time limit:** the user paused all work at 5 pm. Some planned checks did not run; "Not verified" lists them. The verdict does not depend on them.

All three round-2 blockers are fixed on the path round 2 tested. One of them, R2-B2 (the wrong page on the first frame after the definitive order), is still open on a second path: **with a filter on, the first frame after the definitive order is 397 lines from the anchor.** That blocks.

The committed test for R2-B2 also failed once on this head. The selection-totals cache held in every case I checked.

## Round-2 findings

| Finding | Status | Evidence at this head |
| --- | --- | --- |
| R2-B1, the Clear and Invert buttons | **Fixed** | `panel.settleByUser()` is first in both `onClicked`. The accessible press is fixed the same way. `gridButtonsDuringArrivalAreNotReplaced` and `accessiblePressDuringArrivalIsNotReplaced` pass. |
| R2-B2, the first frame after the definitive order | **Fixed for an unfiltered listing only** | `onLayoutChanged` restores directly, and the unfiltered row of my test reads 0 lines off at every frame. With a filter, the definitive order is a model reset, which still restores a frame late: **R3-B1**. The committed test also failed once: **R3-I1**. |
| R2-B3, Escape from type-ahead | **Fixed** | `preSearchPath` is looked up with `rowOf`. `escapeFromTypeAheadReturnsToTheSameFile` passes. |
| R2-N1, stale `cursorRow` | **Fixed** | One `selectionChanged` after both of a tick's inserts, when the cursor's row changed. Side effect: **R3-I2**. |
| R2-N2, the "does not settle" half | **Covered** | `otherCommandsDuringArrivalKeepThePlacement`, and the menu test drives `window.invoke`. |
| R2-N3, R2-N4 | **Left open with reasons** | I accept both reasons. |
| Question 2, the footer | **Done under the lead's grant** | Two branches in the one expression at `ui/Main.qml:2152`, strings in `en_XA`. `theFooterSaysSoFarWhileRowsArrive` passes. |

**The merge `c353d26`.** `git show --remerge-diff c353d26 -- src ui include tests cmake CMakeLists.txt translations` prints nothing, so the merge resolved no conflict in code.

## Blocking

### R3-B1. With a filter on, the first frame after the definitive order shows the wrong page (medium; R2-B2 left open on a second path)

`ui/PhotoGrid.qml:310`:

```qml
function onModelReset() { Qt.callLater(panel.restoreViewport, panel.resetViewport) }
```

**What happens.** With a text or kind filter active, `publishRows` publishes the definitive order through `applyFilter`, and that is a model reset, not `layoutChanged`. The reset handler still defers the restore, so a frame is drawn first, over the new rows. This is exactly R2-B2's failure on the reset path.

**Why it's A1's.** The reset handler is older than A1, and on `main` a filtered first listing publishes once, with nothing showing before it. A1 shows filtered rows during arrival and lets the user scroll them, so the deferred restore now lands after the user has scrolled. The row of the test with no filter is the control: it goes through `layoutChanged`, which round 3 fixed.

`zFilteredDefinitiveOrderFirstFrame` (in `A1-REVIEW-R3-scratch-tests.patch`) reads the view inside the `arrivingChanged` slot, which comes before any frame. It also samples every `frameSwapped` after that slot. Against this head, unmodified:

```text
FILTERED ""     resets 0 layouts 1 rowsMoved 1783 linesOff at definitive 0   frames after QList(0, 0, 0)   anchored at end true
FILTERED "kind" resets 1 layouts 0 rowsMoved 1783 linesOff at definitive 397 frames after QList(397, 0, 0) anchored at end true
FAIL!  : zFilteredDefinitiveOrderFirstFrame(kind image) 'linesOff < 1' returned FALSE. (397 lines from the anchor at the definitive order)
FILTERED "text" resets 1 layouts 0 rowsMoved 1783 linesOff at definitive 397 frames after QList(397, 0, 0) anchored at end true
FAIL!  : zFilteredDefinitiveOrderFirstFrame(text f, pinned) 'linesOff < 1' returned FALSE. (397 lines from the anchor at the definitive order)
Totals: 3 passed, 3 failed   (the third failure is the probe in R3-I2, a setup failure)
```

The first frame after the definitive order is 397 lines from the anchor, and the frames after it are right. Both filter types reproduce it, and so does a persistent text filter, which is how a user keeps one across folders.

**Fix.** Restore at once in `onModelReset` when it is the definitive order of the folder on screen, as `onLayoutChanged` now does. `restoreViewport` already calls `grid.forceLayout()`. Making it immediate in every case may be right too, but a refresh reset on `main` takes the same path, so the implementer should check that `folder_grid` still passes. Commit a test like the scratch one, with both filter types.

## Important

### R3-I1. The committed R2-B2 test failed once on this head: one line off

```text
perf-enum-ui-results.txt, targeted ctest, unmodified head:
FAIL!  : PerfEnumUiTests::theViewIsOnTheAnchorBeforeAnyFrameAfterTheDefinitiveOrder(every 7th a folder) 'linesOff < 1' returned FALSE. (1 lines from the anchor at the definitive order)
tests\perf_enum_ui_tests.cpp(407) : failure location
Totals: 38 passed, 1 failed, 1 skipped, 0 blacklisted, 57637ms

The same function, three reruns straight after, each of its three rows:
Totals: 5 passed, 0 failed, 0 skipped, 0 blacklisted, 4861ms
Totals: 5 passed, 0 failed, 0 skipped, 0 blacklisted, 5204ms
Totals: 5 passed, 0 failed, 0 skipped, 0 blacklisted, 4886ms
```

**Result.** One run in four. The first ran as part of the whole suite, with another session's build in a second slot. The view was exactly one line off at the definitive order, so the user would see the anchor row move by one line.

**My reading, not proven.** After `release()`, folder ticks keep inserting rows above the view. If the definitive order lands before the view has compensated for the last insert, `rememberViewport` in `onLayoutAboutToBeChanged` saves a position one line out, and the restore faithfully lands on it. If so, this is a narrow race in the product, not only in the test.

**What to do.** At minimum, the gate will run this test, and a red gate needs it settled. Take the cause into account when fixing R3-B1. If the race is real, `rememberViewport` could anchor on the row the user last saw rather than on the current `contentY`.

### R3-I2. The selection count lags while a kept selection arrives, and catches up when the folder settles

**What changed.** Since `bc7b33e`, `publishBatches` emits `selectionChanged` only when the cursor's row moves. A kept multi-selection whose other rows arrive in later ticks changes `selectedCount`, `selectedBytes` and `selectedSummary` with no notify. QML reads all three through that notify: the chip at `PhotoGrid.qml:503`, the Clear and Invert buttons, the footer and the status summary.

At `ac49031` a files-only listing announced nothing during arrival either, so this is narrower than a regression. It makes no difference for folders.

`zSelectedCountNotifiedAsSelectedRowsArrive` (model level):

```text
LAG before release: notified 2 actual 2
LAG ticks where the notified count was behind: 1 worst 1 final notified 3 3
FAIL!  : zSelectedCountNotifiedAsSelectedRowsArrive() Compared values are not the same   (lagging 1, expected 0)
```

**Classification: Important, not blocking.** Under the lead's rule it is a visible stale count that corrects itself, and it misleads no action:

- **It corrects itself.** The definitive order emits `selectionChanged` whenever the cursor is set, and `apply()` always emits it at the end.
- **No action reads the stale value.** Delete, trash, copy and export take `deletionTargets()` and `selectedPaths()`, which read the live selection. `DeleteFlow.qml` counts its own targets in the confirmation ("Move %1 items to the …?"), so it shows the true number.
- **Escape is live too.** It calls `fileGrid.selectedCount` imperatively.

So the user can see "2 selected", or no chip, while three are selected, until the folder settles. Anything they then do acts on three and says so. On a slow share that window can be long.

**Fix.** In `admit`, flag a tick that brings in a key from `selected`, and emit when that flag is set or the cursor's row moved. The cost is bounded by the kept selection, since select-all during arrival does not extend to rows still arriving.

The UI-level probe (`zKeptSelectionCountDuringArrival`) failed in its own setup, waiting for 2900 held rows. So the chip itself was not observed, only the model's notify.

## Non-blocking

- **N1. Footer number format.**
  - The arriving branches format with `toLocaleString` ("1,024 selected of 1,024 so far").
  - The settled branches do not ("1024 selected", "3 of 3000").
  - So the same count changes format when the listing lands.
  - The chip uses `toLocaleString`. Using it in all four branches would be consistent.
- **N2. Selection cost at a million rows (carry-forward to PR 2).**
  - PR 1 caps at `kMaxEntries = 100000`.
  - A cache miss costs one hash lookup per selected key. The implementer measured about 18 ms with all 100k selected, so about 180 ms at a million.
  - No QML binding reads `fileGrid.selectedCount` or `selectedSummary` while depending on `count`, so arrival ticks do not force misses. `Main.qml:2163` also depends on `viewer.status`.
  - With select-all during arrival and a cursor below the folder segment, every folder tick emits, so the GUI thread pays one miss per tick: up to about 9 ms per tick for 50k selected at 100k. That is better than `ac49031`, where each tick walked every row several times.
- **N3. `totals-never-cached` has no correctness oracle.** It is a speed change, measured by `traceSelectionChangedCost`, which is opt-in. That is acceptable for a cache, but nothing fails if caching is silently turned off.

## Selection totals cache

**By reading:**
- **When totals are cleared.** They are cleared on `selectionChanged`, `countChanged`, `directoryChanged`, `rowsInserted`, `rowsRemoved`, `modelReset`, `layoutChanged` and `dataChanged`. Those connections are made in `Private`'s constructor, before anything outside the model can connect.
- **The selection commands.** `select`, `selectAll`, `clearSelection` and `invertSelection` emit `selectionChanged` after every write. `applyFilter` and `release` reset the model.
- **`setDirectory`.** It emits `directoryChanged` and `selectionChanged` after clearing the selection.
- **Inside `publishBatches` and `merge`.** `admit` writes the index ahead of the rows, but no signal fires until the inserts. The inserts and `merge` run inside `ChangingRows`, where readers walk the rows and nothing is kept. `merge` swaps its index after its last row signal, and `countChanged` (sizes differ) or `apply()`'s final `selectionChanged` follows.
- **`apply()`.** It emits `arrivingChanged` and `loadingChanged` before its final `selectionChanged`, and neither of those clears the cache. A reader there gets a value computed after the row changes. The rename mapping runs before `publishRows`, so by reading that value is current.
- **`selectedPaths()` and `rowOfKey`.** `selectedPaths()` now sorts the found rows, which keeps listing order. Every lookup checks the row's key, so a lagging index cannot count the wrong row.

**By test:**
- `selectionTotalsFollowEveryChange` (committed) passes on this head.
- The implementer's mutation log covers forgetting past `selectionChanged`, `countChanged` and `rowsInserted`.
- My wider scratch tests did not reach their checks, because of setup errors in my tests, not findings:
  - In `zSelectionTotalsMoreChanges`, the sort step waits for an order change that `setSort("name", true)` does not produce here.
  - In `zSelectionTotalsAcrossARename`, the third `select` found no row, so the count read 2 before any rename.
- `zSelectionTotalsMoreChanges` did read totals at all eleven signals up to that step, `loadingChanged` and `arrivingChanged` included. It read `TOTALS three selected 3 2550 rows say 3 2550`, with no stale read.
- The rename and filter-during-arrival cases are therefore unverified by test.

## Fail-before

- **R3-B1.** The scratch test fails on this head with a filter (397 lines) and passes on the same head without one (0 lines). The control row shows it measures the bug and not the fixture. I did not try the fix, so no pass-after is shown.
- **R3-I2.** The model test fails on this head (lagging 1).
- **The round-3 fixes.** The implementer's `docs/verification/A1/mutations-review2.txt` records 23 mutations, all caught. Among them:
  - `r2b1-clear-button`, `r2b1-invert-button`, `r2b2-restore-deferred` (297, 287 and 273 lines), `r2b3-row-number`
  - the three footer mutations, `settles-on-every-command`, `cursor-tick-silent`, `every-folder-tick-announced`
  - `r2b1-accessible-press` and the three `totals-kept-past-*`

  I had prepared mutations of my own: no `dataChanged` forget, no `ChangingRows` in `merge` or in `publishBatches`, no cursor emit, and the four `PhotoGrid.qml` fixes reverted. **They did not run** before the pause, because the gate lock was held by other sessions for most of the available time.

## ctest output

Unmodified head, clean build, in a build slot:

```text
1/9 Test  #4: directory_listing ................   Passed   25.89 sec
2/9 Test  #7: viewer_acceptance ................   Passed   12.64 sec
3/9 Test #16: directory_model ..................   Passed   28.43 sec
4/9 Test #20: grid_search ......................   Passed    0.39 sec
5/9 Test #26: fs_watch_ui ......................   Passed    3.69 sec
6/9 Test #30: filter ...........................   Passed    3.45 sec
7/9 Test #48: translation_coverage_ui ..........   Passed   26.32 sec
8/9 Test #51: perf_enum ........................   Passed   55.01 sec
9/9 Test #52: perf_enum_ui .....................***Failed   57.77 sec
89% tests passed, 1 tests failed out of 9
Total Test time (real) = 214.05 sec
```

The one failure is R3-I1, with its reruns above. `folder_grid` under the gate lock passed on its first run, so no rerun was needed:

```text
1/1 Test #12: folder_grid ......................   Passed  205.83 sec
100% tests passed out of 1
folder_grid Totals: 74 passed, 0 failed, 4 skipped, 0 blacklisted, 205675ms
```

`pr_guard`, the head's own copy, against `main` as merged:

```text
python scripts/ci/pr_guard.py --base 20f1210 --head-ref 5018b8fb86979b6003f688b0d81fd0ecff1db105
::notice::2757 changed lines; aim for about 1500
pr_guard: code=True, 0 error(s)
```

## Missing cases

- A filtered listing at the definitive order (R3-B1).
- A kept selection whose rows arrive in ticks that do not move the cursor (R3-I2).
- **Escape after type-ahead when the starting file has not arrived yet.** The lookup by path returns -1 and nothing is selected, so the cursor stays on the match. That is safe, but it is not a return to the start.

## Not verified

- **Targeted runs not done:** `input_ui`, `fs_watch_budget`, `stack_confirm_ui`, `export_ui`, and the rest of the handoff's 19. The paused machine had a full gate holding the lock for most of the time available.
- **Mutation spot-checks:** none of mine ran. See Fail-before.
- **Scratch tests that stopped on setup errors:** the rename, sort and filter-during-arrival totals cases, and the UI chip probe.
- **Not exercised:** a real display, a GPU, the threaded render loop, and a real slow network share.
- **Not run:** the full suite and `docker build`. They belong to the hosted `viewer` job and the tester's gate.

The scratch tests are in `docs/verification/A1-REVIEW-R3-scratch-tests.patch` (names start with `z`). They apply to `5018b8f` with `git apply`.
