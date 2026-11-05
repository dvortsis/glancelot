# A1 handoff: incremental cancellable folder listing

PR 1 of 2 for A1 (PERF-ENUM + F016). The continuation beyond the 100k cap ("Show more") is the stacked PR 2, as the lead approved. It has its own section in this file on its branch.

- **Branch:** `claude/a1-perf-enum`
- **Code tip:** `bc7b33e`, after the second review's fixes. The commit after it changes only this file and `docs/verification/A1/`. No local full suite ran after `902a765`. By the lead's decision the PR's hosted `viewer` job is the full suite.
- **Exact base:** `20f1210`, the `main` commit merged in `c353d26`, so the head the reviewer approves can be the head the gate runs on. Not stacked. The second review was against `ac49031`, the first against `3a14044`.
- **PR:** https://github.com/dvortsis/glancelot/pull/7

## What a user sees

- A 10k folder shows rows at about 50 ms instead of about 530 to 920 ms. A 100k folder shows them at about 140 ms. The count reads "N items so far" until the order is final, and so does the footer: "3 of 1,024 so far", or "1,024 selected of 1,024 so far".
- Selecting a row in a 100,000-row folder no longer costs about 50 ms of GUI time for every selection signal. It is now about 15 µs.
- Time to `loading` false barely moves at 10k, about 550 ms to about 460 ms. Nearly all of that wait is identity probes, which now run after the rows are showing.
- The order is published once. Delegates stay in place, the row at the top stays at the top, and a row clicked during arrival stays selected.
- A folder that lists within 120 ms, or has too few rows, publishes once with no provisional rows, as before.
- Opening another folder or closing the window stops the listing, even while the worker is blocked on a full queue.

## Before and after

**Correction (review C1).** The first version of this table compared a "before" of 1.3 to 1.8 s at 10k with an "after" of about 50 ms. Both were real output, but from different runs, and only the after run was under the gate lock. That overstated the 10k gain about threefold. The 10k figures below are the reviewer's: `main`'s model at `3a14044` and this branch, back to back on this PC under one gate lock (`docs/verification/A1-REVIEW.md` on `claude/review-a1`). The base trace has the same mix of names, without the heartbeat or recorder.

| 10k, like for like (ms) | Before, `3a14044` | After |
| --- | --- | --- |
| First row, round 1 | 566, 811, 923, 686 | 51, 41 |
| First row, round 2 | 525, 552, 585, 538 | 52, 51 |
| `loading` false, round 1 | 582, 826, 939, 701 | 461, 460 |
| `loading` false, round 2 | 540, 566, 600, 553 | 461, 512 |

So at 10k the first row falls from about 530 to 920 ms to about 50 ms, roughly tenfold. Time to `loading` false falls only from about 550 ms to about 460 ms in the quieter round.

**100k is not like for like.** Its "before" comes from my earlier run, not under one gate lock with the after, and the reviewer did not rerun it. The same procedure overstated 10k, so treat these "before" figures as an upper bound on the gain.

| 100k (ms) | Rows, before | Rows, after | Definitive order, after | Done, before | Done, after | Worst GUI gap, before → after |
| --- | --- | --- | --- | --- | --- | --- |
| Cold | 1602 | 141 | 496 | 1621 | 522 | 45 → 14 |
| Warm | 1319 | 142 | 534 | 1328 | 562 | 12 → 12 |

The "after" lines are in `docs/verification/A1/traces-after.txt` (`902a765`, gate lock). The reviewer measured 141 and 140 ms to first row and 568 and 559 ms to `loading` false, which agrees. `traces-before.txt` holds the first "before" run and is kept only as the record of what the correction replaced.

- Sorting 100k fell from 776 to 900 ms to about 40 ms, from precomputed keys.
- Enumeration at 100k rose from about 250 to about 430 ms, because the worker now builds rows and sort keys as it goes. Rows still reach the screen far sooner.
- 10k "done" varies with the machine: 512 to 562 ms in my run, 460 to 512 in the reviewer's, and 919 and 920 in the reviewer's busier targeted run. It is almost all identity probes.
- At 100k the identity baseline is over its byte budget, so no probes run and "done" follows the definitive order closely.
- Closing during arrival took 13 ms against 29 ms before. The worker stopped at 1279 entries with a 1024-row queue.

## First review: CHANGES REQUIRED, and what changed

`docs/verification/A1-REVIEW.md` on `claude/review-a1` (`ec92bb5`), against `e425a90`. Each fix's fail-before evidence is in `docs/verification/A1/mutations-review.txt`.

| Finding | Fix | Tests |
| --- | --- | --- |
| **B1.** Keys, Ctrl+A and the scrollbar during arrival were overridden when the placement fired | `settleByUser()` from `Keys.onPressed`, the scrollbar and the right-click handler | `keyboardDuringArrivalIsNotReplaced`, `selectAllDuringArrivalIsNotReplaced`, `scrollbarDuringArrivalKeepsTheView`, `rightClickDuringArrivalIsNotReplaced`, `aKeyBeforeAnyRowKeepsThePlacement` |
| **B2.** The restore at the definitive order read geometry the view had not laid out | `grid.forceLayout()` in `onLayoutAboutToBeChanged`, and `eeb1c42`'s in `restoreViewport` | `foldersInTheLastRowsKeepTheAnchor`, `definitiveOrderBeforeAnyFrameKeepsTheVisibleRow`, `tryAgainWithTheOrderUnchangedReturnsToTheBrowsedPosition` |
| **B3.** Try again on a folder that arrives lost the browsed position | `recoveryViewport`, kept when an arrival starts with no rows, restored when `arriving` clears, dropped if the user settles first | `tryAgainOnAnArrivingFolderReturnsToTheBrowsedPosition`, `tryAgainWithTheOrderUnchangedReturnsToTheBrowsedPosition`, `failingAgainKeepsTheBrowsedPositionForTheNextTry`, `scrollingDuringTryAgainWinsOverTheBrowsedPosition` |
| **B4.** A file renamed while the grid was hidden lost the cursor | `reveal()` waits for the baseline when the cursor has no row; settling covers the whole of `loading` | `renamedWhileHiddenKeepsTheCursor`, `aClickInTheProbeWindowDecidesThePlacement`, `reenteringPlacesTheCursorAtTheDefinitiveOrder` |
| **B5.** A lost wakeup could leave `loading` stuck | The definitive batch ends the drain whatever the budget clock says | `aFinishedScanIsNotLostAtTheDefinitiveBatch`, with a 7 ms delay injected through a new seam, `beforeDryCheck` |
| **C1.** The 10k numbers overstated the gain | The table above, this file's summary, the comment in `trace10k` and the PR description | None |
| **N1.** "1 items so far" | Singular strings, added to `en_XA` | `oneItemSoFarReadsInTheSingular`, `oneMatchSoFarReadsInTheSingular` |
| **N2.** `cursorRow` stale during arrival | `selectionChanged` after a folder insert and at the definitive order | `cursorRowIsNotifiedWhenRowsMoveAroundIt` |
| **N5.** Weak oracle in `trace10k` | First row and definitive order compared with the end of loading, not with a fixed 500 ms | `trace10k` |
| **N7.** PR description | Updated with C1 | None |

**Both `forceLayout` calls stay, because they fix different paths.** B2's runs before the position is read, and folders landing above the view make that read wrong without it. Mine runs before `contentHeight` is read when the viewport is restored. When the order is unchanged no layout signal fires, so B2's never runs on that path, and Try again would clamp short. Each has a mutation that fails only its own test.

**Not done, from the non-blocking list:**
- **N3** (a deleted row stays visible until the probes end). It fails safe, and restarting a scan changes the cancellation behaviour the review just cleared, so I'd rather not add that in a fix round.
- **N4** (a slot that changes folder synchronously in `countChanged`). No caller does this today, and I could not build a test that fails reliably without a further seam.
- **N6** (`fs_watch` log line shows zeros). No oracle reads it.

## Second review: CHANGES REQUIRED, and what changed

`docs/verification/A1-REVIEW-R2.md` on `claude/review-a1` (`8c92a68`), against `ac49031`. Fail-before evidence is in `docs/verification/A1/mutations-review2.txt`.

| Finding | Fix | Tests |
| --- | --- | --- |
| **R2-B1.** The grid's Clear and Invert buttons lost to the placement | `panel.settleByUser()` first in both. Auditing every other selection path found one more, a screen reader's press on a row, fixed the same way | `gridButtonsDuringArrivalAreNotReplaced` (clear, invert), `accessiblePressDuringArrivalIsNotReplaced` |
| **R2-B2.** The first frame after the definitive order showed the old position | `onLayoutChanged` restores at once instead of through `Qt.callLater` | `theViewIsOnTheAnchorBeforeAnyFrameAfterTheDefinitiveOrder`, three listings. It reads the view inside the `arrivingChanged` slot, which comes before any frame, so it cannot pass with no frame drawn. It also checks every frame swapped after the definitive order, and that the order really moved the rows |
| **R2-B3.** Escape from type-ahead returned to a row number | `preSearchPath` replaces `preSearchRow`, looked up on Escape | `escapeFromTypeAheadReturnsToTheSameFile` |
| **N1.** Two stale `cursorRow` cases | One `selectionChanged` after both of a tick's inserts, only when the cursor's row changed | `aTickThatBringsTheCursorsRowNotifiesIt`, `folderTicksBeforeTheCursorArrivesAreNotAnnounced` |
| **N2.** The "does not settle" half of the menu rule | Nothing to fix. The menu test now drives `window.invoke`, the path menu items take | `otherCommandsDuringArrivalKeepThePlacement` (`fit`, `toggle-metadata`, `grid-zoom-in`, `filter`), `menuSelectionDuringArrivalIsNotReplaced` |
| **The lead's question 2.** The footer said "3 of 1,024" during arrival | The one edit granted in `ui/Main.qml`, at the footer count | `theFooterSaysSoFarWhileRowsArrive` |
| **The cost of the first review's N2 fix** | Measured directly, and it was real. Fixed by keeping the selection totals: see below | `selectionTotalsFollowEveryChange`, `traceSelectionChangedCost` (opt-in) |

The selection controls I audited: the delegate's click and double-click (`clickRow` settles), keys, Ctrl+A, the scrollbar, the wheel, right click, the menu key, type-ahead, the menu commands, the grid's Clear and Invert buttons, and the accessible press. `ui/Main.qml`'s own selection calls are the menu commands and `openGridMenu`, which runs only after a right click or the menu key. The lead's answer 1 keeps Select all on the rows that have arrived, which the tests already pin.

**What one `selectionChanged` costs, measured.** A settled 100,000-row grid in the shipped QML, under the gate lock, 41 runs per case, three rounds. Each signal makes QML read the count, the bytes and the summary several times, and before the fix each read walked every row.

| Case | Before, median (worst), ms | After, median (worst) |
| --- | --- | --- |
| No selection | 0.01 (0.04) | 0.01 (0.02) |
| One row selected | 49 to 53 (63) | 0.014 to 0.015 (0.039) |
| Selecting the next row | 48 to 59 (73) | 0.036 to 0.040 (0.158) |
| Every row selected | 139 to 159 (231) | 17.8 to 18.3 (20.3) |

The totals now come from the selected keys, once, and are kept until the rows or the selection change. They are forgotten at every change signal, through connections made before anything outside the model can connect. Inside a merge or a tick's inserts the index lags the rows: a merge emits `rowsRemoved` before it rebuilds the index, which is also true on `main`. So a reader woken from inside one walks the rows as before, and nothing is kept. `selectionTotalsFollowEveryChange` found that by reading the totals at every change signal. With every row selected it still costs 18 ms, against the 6 ms drain budget. That is one explicit command away, and arrival now emits only when the cursor's row moves. At a million rows the same case is about ten times that, which PR 2's review raises as its N0 and N1.

**Not done, from the non-blocking list:** N3 (any non-modifier key settles, Tab included), which follows the first review's suggestion, and N4 (a fixed 200 ms wait in `aFinishedScanIsNotLostAtTheDefinitiveBatch`), which cannot fail falsely.

## Files changed

Grant check, `git diff --name-only 20f1210..bc7b33e`, with `docs/` left out of the list:

```text
CMakeLists.txt
cmake/PerfEnumTests.cmake
include/glancelot/directory_entry_model.hpp
src/app/directory_entry_model.cpp
src/app/directory_scan.hpp
tests/perf_enum_test_support.hpp
tests/perf_enum_tests.cpp
tests/perf_enum_ui_tests.cpp
translations/glancelot_en_XA.ts
ui/PhotoGrid.qml
```

Everything is in the brief's grant except these, which it allows or the lead granted:

- `include/glancelot/directory_entry_model.hpp`. The lead's message: "approved. Add exactly that `arriving` diff in PR 1, and quote this grant and the diff in the handoff." The diff:

  ```diff
  +    // True during a folder's first listing until its definitive order is published; rows may reorder once when it clears.
  +    Q_PROPERTY(bool arriving READ arriving NOTIFY arrivingChanged)
  -    ... bool truncated() const;
  +    ... bool truncated() const; bool arriving() const;
  -    void directoryChanged(); void countChanged(); void loadingChanged(); void selectionChanged();
  +    void directoryChanged(); void countChanged(); void loadingChanged(); void selectionChanged(); void arrivingChanged();
  ```

- `ui/Main.qml`, one edit at the footer count. The lead's grant: "You're granted one edit in `ui/Main.qml` at the footer count (line 2152 at the reviewed head), for example "3 of 1,024 so far" while loading. Keep it to that expression, and put the string in `qsTr` with the catalog regenerated." The edit adds two branches to that expression and nothing else.
- `ui/PhotoGrid.qml`, thin call-site changes the brief allows:
  - `settleByUser()`, so a click or scroll during arrival wins over the waiting placement.
  - `reveal()` and four Connections handlers react to `arriving`, the count and layout changes.
  - The key handler, the scrollbar and the right-click handler settle the placement, and Try again keeps the browsed position (first review).
  - The grid's Clear and Invert buttons and the accessible press settle the placement, `onLayoutChanged` restores at once, and type-ahead remembers a path (second review).
  - The grid's existing `viewCommand` connection settles the placement for `select-all`, `grid-select-all`, `clear-selection` and `invert-selection` (the lead's answer on menu selections).
  - The count label says "so far".
  - The thumbnail binding also depends on `cell.path`, so a delegate kept in place rebinds.
- `translations/glancelot_en_XA.ts`: six new strings, added by hand because `lupdate` is not available on Windows here.

`src/core/*` is untouched. `pr_guard` reports 0 errors and the notice `2757 changed lines; aim for about 1500`. Most of the growth since the last round is tests. I did not split further because the arrival code and its tests only make sense together.

## Tests

**After the second review, at `bc7b33e`, with `main` at `20f1210` merged in.** The first 19 ran in a build slot. `folder_grid`, `input_ui` and `fs_watch_budget` ran under the gate lock, and the traces after them. Everything passed on its first run. No budget oracle changed.

```text
 1/19 Test  #4: directory_listing ................   Passed   16.74 sec
 2/19 Test  #7: viewer_acceptance ................   Passed   12.26 sec
 3/19 Test #16: directory_model ..................   Passed   43.65 sec
 4/19 Test #20: grid_search ......................   Passed    0.36 sec
 5/19 Test #21: grid_density .....................   Passed    0.26 sec
 6/19 Test #23: fs_watch .........................   Passed   92.27 sec
 7/19 Test #26: fs_watch_ui ......................   Passed    4.80 sec
 8/19 Test #30: filter ...........................   Passed    6.78 sec
 9/19 Test #31: cache ............................   Passed   11.86 sec
10/19 Test #40: recovery .........................   Passed   14.66 sec
11/19 Test #41: recovery_startup .................   Passed    0.69 sec
12/19 Test #42: recovery_controller ..............   Passed   15.79 sec
13/19 Test #43: recovery_process .................   Passed   32.37 sec
14/19 Test #44: tabs_recovery ....................   Passed   69.47 sec
15/19 Test #48: translation_coverage_ui ..........   Passed   32.08 sec
16/19 Test #51: perf_enum ........................   Passed   62.72 sec
17/19 Test #52: perf_enum_ui .....................   Passed   67.07 sec
18/19 Test #55: export_ui ........................   Passed   37.11 sec
19/19 Test #56: stack_confirm_ui .................   Passed  176.47 sec
100% tests passed out of 19
Total Test time (real) = 698.44 sec
perf-enum Totals: 32 passed, 0 failed, 0 skipped, 0 blacklisted, 40651ms
perf-enum-ui Totals: 39 passed, 0 failed, 1 skipped, 0 blacklisted, 66888ms
1/3 Test #12: folder_grid ......................   Passed  277.80 sec
2/3 Test #13: input_ui .........................   Passed   81.48 sec
3/3 Test #24: fs_watch_budget ..................   Passed   88.75 sec
100% tests passed out of 3
Total Test time (real) = 448.25 sec
folder_grid Totals: 74 passed, 0 failed, 4 skipped, 0 blacklisted, 277612ms
input_ui Totals: 33 passed, 0 failed, 1 skipped, 0 blacklisted, 81325ms
```

Traces from the same lock, against the earlier figures:

```text
10k-cold first_row_ms=52 full_count_ms=52 definitive_ms=52 settled_ms=510 worst_gui_gap_ms=12 enumerate_ms=35 
10k-warm first_row_ms=51 full_count_ms=51 definitive_ms=51 settled_ms=512 worst_gui_gap_ms=12 enumerate_ms=31 
100k-cold first_row_ms=141 full_count_ms=388 definitive_ms=461 settled_ms=461 worst_gui_gap_ms=13 enumerate_ms
100k-warm first_row_ms=142 full_count_ms=386 definitive_ms=451 settled_ms=459 worst_gui_gap_ms=12 enumerate_ms
```

**After merging `main` (`7c0da34`) and fixing menu selections.** `2876dbf` resolved two conflicts in `ui/PhotoGrid.qml`, keeping both sides as the lead specified. No budget oracle changed. No local full suite ran.

```text
At 2876dbf, the merge, 21 suites in a build slot, with the claude/b3-export-ui gate holding the lock for part of the run:
 3/21 Test #12: folder_grid ......................***Timeout 400.16 sec
95% tests passed, 1 tests failed out of 21    (the other 20 passed)
  input_ui 179.40 sec, perf_enum 54.54 sec, perf_enum_ui 40.70 sec, stack_confirm_ui 174.04 sec

At 2810c5c, the menu fix, the four suites it could affect, in a build slot:
1/4 Test #13: input_ui .........................***Timeout 239.99 sec
2/4 Test #48: translation_coverage_ui ..........   Passed   34.67 sec
3/4 Test #51: perf_enum ........................   Passed  154.67 sec
4/4 Test #52: perf_enum_ui .....................   Passed  138.06 sec

At 2810c5c, the two that timed out, rerun once under the gate lock:
1/2 Test #12: folder_grid ......................   Passed  253.52 sec
2/2 Test #13: input_ui .........................   Passed   93.39 sec
100% tests passed out of 2
folder_grid Totals: 74 passed, 0 failed, 4 skipped, 0 blacklisted, 253365ms
input_ui Totals: 33 passed, 0 failed, 1 skipped, 0 blacklisted, 93261ms
```

The 21 are the 19 listed below plus `input_ui` and `stack_confirm_ui`, which `main` brought or changed and which drive the grid's keys. `folder_grid` had got through 42 functions with no failures when it timed out. I ran it in a build slot while a gate held the lock, which `AGENTS.md` does not allow, and that probably contributed. `input_ui` took 93 s under the lock and timed out at 240 s beside other builds. Those timeouts look like load, but neither run identified a specific cause. The other 16 of the 21 did not rerun after `2810c5c`, which changes only the grid's QML and adds one test.

**After the first review, at `88f995d`, before merging `main`, in build slots.** No budget oracle changed, and by the lead's decision no local full suite ran. Three runs, because the first two were on a heavily loaded machine (`perf_enum` took 228 s against 63 s on a quieter one):

```text
Run 1, all 19 targeted, at 2b7c197:
 3/19 Test #12: folder_grid ......................***Timeout 399.99 sec
 8/19 Test #24: fs_watch_budget ..................***Timeout 179.94 sec
15/19 Test #43: recovery_process .................***Failed   43.10 sec
84% tests passed, 3 tests failed out of 19    (the other 16 passed)

Run 2, those three plus perf_enum_ui:
1/4 Test #12: folder_grid ......................***Timeout 400.07 sec
2/4 Test #24: fs_watch_budget ..................   Passed  163.53 sec
3/4 Test #43: recovery_process .................   Passed   70.12 sec
4/4 Test #51: perf_enum_ui .....................***Timeout 119.98 sec

Run 3, at the final code, with perf_enum_ui's timeout raised from 120 to 400 s:
1/2 Test #12: folder_grid ......................   Passed  230.91 sec
2/2 Test #51: perf_enum_ui .....................   Passed   40.20 sec
100% tests passed out of 2
```

The 19 are `directory_listing`, `viewer_acceptance`, `folder_grid`, `directory_model`, `grid_search`, `grid_density`, `fs_watch`, `fs_watch_budget`, `fs_watch_ui`, `filter`, `cache`, `recovery`, `recovery_startup`, `recovery_controller`, `recovery_process`, `tabs_recovery`, `translation_coverage_ui`, `perf_enum` and `perf_enum_ui`.

- **`recovery_process` run 1:** `glancelot.exe --quit-after 2500` exited 3 instead of 0, on the loaded machine. It passed on rerun. I did not find out why it exited 3.
- **`perf_enum_ui`'s timeout was too small for this PR:** the suite went from 7 to 23 test functions.
- **`virtualizedAndHidden` failed once, and I could not reproduce it.** In one standalone `grid_ui_tests` run on the loaded machine (513 s), it read 28 live delegates against 29 cells, 100 ms after a fixed wait. That test is on `main` and I did not change it. It then passed 12 of 12 with these fixes, 12 of 12 without them, and in run 3. It looks like a timing race in the test under load, but that is my reading, not something I proved. If the tester's gate hits it, it is worth a look.

**Before the first review:**

Targeted, after the final test commit:

```text
1/2 Test #50: perf_enum ........................   Passed
2/2 Test #51: perf_enum_ui .....................   Passed   13.01 sec
100% tests passed out of 2
Total Test time (real) = 178.06 sec
```

Full suite, `scripts/build.ps1 -BuildDir build-claude-a1-perf-enum -Test` at `902a765` under the gate lock. No baseline failures, nothing rerun:

```text
100% tests passed out of 53
Total Test time (real) = 1093.51 sec
```

Budget and listing tests from that run, oracles unchanged:

```text
 4/53 Test  #4: directory_listing ................   Passed   18.43 sec
12/53 Test #12: folder_grid ......................   Passed  243.86 sec
16/53 Test #16: directory_model ..................   Passed   29.75 sec
20/53 Test #20: grid_search ......................   Passed    0.35 sec
21/53 Test #21: grid_density .....................   Passed    0.22 sec
23/53 Test #23: fs_watch .........................   Passed   88.44 sec
24/53 Test #24: fs_watch_budget ..................   Passed  129.22 sec
26/53 Test #26: fs_watch_ui ......................   Passed    3.04 sec
30/53 Test #30: filter ...........................   Passed    3.15 sec
31/53 Test #31: cache ............................   Passed    5.15 sec
50/53 Test #50: perf_enum ........................   Passed   56.60 sec
51/53 Test #51: perf_enum_ui .....................   Passed   10.63 sec
```

`ci-ok` and the other checks passed on the first push (`ade4c9a`, 5 of 5, including the `windows-2022` viewer job).

## Fail-before evidence

- **Against base:** the public-API tests were written first and four failed on `main`: `trace100k` (no partial rows), `arrivedRowsDoNotMoveUntilTheDefinitiveOrder` (nothing arrived, since renamed `arrivedRowsKeepTheirOrderUntilTheDefinitiveOrder`), `newerFolderOpenedDuringArrivalWins` and `closingDuringArrivalIsBounded` (no arrival to catch). The output is at the end of `docs/verification/A1/traces-before.txt`: `Totals: 3 passed, 4 failed`. The seam and `arriving` tests do not compile on base, so they are covered by mutations.
- **Second review's fixes:** `docs/verification/A1/mutations-review2.txt`. 23 mutations, all of which fail their named tests. My first accessible-press test used an image row, so pressing it opened the photo and the test failed for that reason, with or without the fix. It now uses a non-photo row, and the mutation was rerun. `totals-never-cached` fails only because its readings change, since caching is a speed change. The measure above is its real evidence.
- **First review's fixes and menu selections:** `docs/verification/A1/mutations-review.txt`. 21 mutations, all of which fail their named tests. Without the fix, `menuSelectionDuringArrivalIsNotReplaced` fails for all four commands. One survived its first run (`b3-failing-again-forgets`), because its test failed before any row arrived. The test now fails with rows showing.
- **Mutations:** `docs/verification/A1/mutations.txt`. 32 mutations, one per behaviour; all 32 fail their named tests. One is an abort (`queue-unbounded`) and one a hang (`wait-ignores-stop`), both of which are what those mutations should cause.
- **What the first mutation run found:** four mutations survived, so these tests could not fail. `902a765` fixes them.
  - `definitiveOrderDoesNotWaitForIdentityProbes` read `loading` inside the call that clears it just afterwards.
  - `foldersArriveAboveFilesAndTheIndexStaysExact` could land every row in one tick.
  - `definitiveOrderRebindsDelegatesInPlace` looked only at a delegate that had just been created.
  - No test held a listing that finished before a newer folder opened, where only the generation stops it. New test: `aFinishedOlderListingCannotPublishIntoANewerOne`.
- Two earlier mutations of the shipped tuning also survived, because patience and minimum rows are ANDed. `arrivalGatesHoldBackQuickAndSmallListings` now tests each gate alone.

## Failure and cancellation cases covered

- Newer folder opened during arrival, during the probe window, and after the older listing finished.
- Window closing during arrival, including with the worker blocked on the full queue.
- Folder failing or denied partway: rows clear, selection is kept for recovery, the failure signal fires.
- `arriving` clears on success, empty, denied or missing, failing partway, going inactive, no folder, filter changes, and a filter hiding everything. It is also safe when destroyed while set.
- Rename on disk and delete during the probe window: paths stay right and a row that cannot be proven goes rather than guesses.
- Stale probes cannot change a newer listing.
- Refresh during arrival finishes, then refreshes. A sort change restarts. A filter projects each batch.
- A screen reader's press, and the grid's own buttons, during arrival keep what they chose.
- A reader inside a merge gets totals from the rows, not from an index that lags them.

## Decisions taken

- **Probes run after the definitive order, not before.** The advisor suggested probing before publishing. The baseline showed enumeration at about 25 ms and sorting at about 60 ms out of 1.5 s at 10k, so nearly all the wait was probes. Publishing first is the change that moves time to first row. This needed `arriving`, because `loading` now outlives the final order. Lead approved.
- **Tuning:** patience 120 ms, slow floor 1000 ms, minimum 2048 rows, 2048 rows per batch, 100 ms flush, 16,384 rows queued. GUI drain is 6 ms in each 16 ms tick, with at most one folder insert and one file insert. These are starting points set from the traces above.
- **Definitive order as `layoutChanged` with `VerticalSortHint`, not a reset:** QML rebinds delegates in place and keeps `contentY`. No signal if the order is unchanged. Tested through the shipped `ui/Main.qml`.
- **Streaming `enumerate()` duplicates the `listDirectory` policy in `src/app/directory_scan.hpp`** rather than editing `src/core`, which is outside the grant. `enumeratorMatchesListDirectory` guards the two against drifting.
- **Header-only scan helper**, so about 15 targets that compile the model need no CMake edits.
- **Provisional index by segment:** files by position in the file segment, folders flagged, decoded in `rowOfKey`. A folder insert then does not rewrite the index.
- **Refresh during arrival is coalesced** rather than restarting, so a busy folder cannot starve its own first listing.
- **Selection during the probe window is by path.** If a rename lands before its baseline exists, that row leaves the selection instead of following a guess.
- **Going further than the review's B4 suggestion.** It suggested `if (fileGrid.loading && row < 0) return`. That also holds back a cursor that already has its row, so re-entering a 10k folder would leave the view at the top for about 400 ms and then jump. Only a cursor with no row waits now.
- **A key or click with no rows yet settles nothing.** Pressing the grid key and then an arrow straight away would otherwise cancel the placement on the photo, with nothing to show for it. The review's suggestion did not cover this.
- **A modifier alone settles nothing,** because pressing Ctrl on its way to Ctrl+A has not chosen anything.
- **Menu selections settle in `ui/PhotoGrid.qml`, not `ui/Main.qml`.** Every menu item and shortcut for these commands goes through `viewer.trigger` and the `viewCommand` signal, and the grid already listens to it. The lead's grant for a small `ui/Main.qml` edit was not needed, so EN-07 has nothing to work around. `Clear selection` counts as a choice too: it stays clear when the listing lands. Before any row has arrived, these commands settle nothing, like a key.
- **Footer wording during arrival:** "3 of 1,024 so far" for one row, and "1,024 selected of 1,024 so far" for several. Saying "so far" only in the single-row case would leave Select all looking complete.
- **Totals from the selected keys, cached, rather than a running count.** A count kept as each selection command runs would need every path that adds or removes rows to adjust it. Forgetting on every change signal needs one place. With every row selected, a read is still one pass over the keys.

## Open questions for the lead

None. The lead answered the review's two product questions: Select all stays on the rows that have arrived, and the footer says so.

## Not verified

- A slow or dead network share. The patience, floor and flush paths are covered through the seam only.
- Physical hardware and a real display. All UI evidence is offscreen with the software backend. The first-frame check reads the view at the definitive order itself, but how a threaded render loop schedules that frame is not verified.
- Selection cost at 1,000,000 rows. Measured at 100,000 only. With every row selected it is about 18 ms there, so about ten times that at a million.
- `docker build` was not part of my run, and no local full suite ran after `902a765`, so `main`'s merge has only the targeted runs above. The hosted `viewer` job and the lead's tester cover the head.

## UI skills

`application-interface-design` was read before the QML edits. The visible changes in this PR are the "so far" wording in the existing count label and in the footer, with no new control or layout. `layout-spacing`, `component-geometry`, `responsive-layout` and `typography-ui` were read for the banner in PR 2.
