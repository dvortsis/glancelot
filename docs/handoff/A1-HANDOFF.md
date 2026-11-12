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

---

# PR 2: Show more for folders beyond the first 100,000 entries

- **Branch:** `claude/a1-load-more`
- **Code tip:** `a80f9bf`, the fixes for the PR 12 review. The commit after it changes only this file and `docs/verification/A1/`.
- **Exact base:** `5018b8f`, the tip of `claude/a1-perf-enum` (PR 1) under its round 3 review, merged in `3170dcd`. `main` at `30803be` (EN-07) is merged in `973fca9`, as the lead asked. PR 1 does not have that merge yet, so the PR's diff against its base also shows EN-07's files until it does. The PR targets PR 1's branch, and stays a draft until PR 1 merges.
- **PR:** the pull request from this branch into `claude/a1-perf-enum`.

## What a user sees

- A folder over the cap shows the banner "Showing 100,000 items. This folder has more that are not listed here." with a **Show more** button.
- Each press lists 100,000 more, up to 1,000,000. The banner says "Listing more items…" while it works, and the rows change once, when the page lands.
- With too little free memory for the next page, the rows stay and the banner says "Showing 100,000 items. There is not enough free memory to list more. Close other apps, then try again." **Show more** stays, and each press checks free memory again, so the page starts as soon as it fits.
- Changing the sort or the hidden-files setting in a truncated folder shows its result, with the selection and the row at the top kept. While the folder is listed again the banner says "Sorting the 100,000 items listed here…", then that more items are not listed.
- While a truncated folder is listed again (coming back from a photo, for example), Show more is not offered until its rows are in order.
- The selection, the cursor and the row in view stay put. If the keyboard cursor was in view it stays in view. If the user was browsing with the wheel, the row at the top stays at the top.
- At 1,000,000 the button goes and the banner says that is the most Glancelot can list. When everything is listed the banner goes. Either way, focus moves to the grid rather than being lost with the button.
- With a filter, the banner gives no number and says some matches may be missing, because the count on screen is not how much of the folder is listed.

## Before and after

`docs/verification/A1/million-traces.txt` has every line. Milliseconds and MB, under the gate lock. An empty process is 15 MB.

| Rows | Page wait, real files | Page wait, synthetic | Working set, real / synthetic | Peak, real / synthetic | Publish on the GUI thread, real → synthetic |
| --- | --- | --- | --- | --- | --- |
| 100,000 | 622 | 884 | 94 / 90 | 137 / 137 | 15 → 46 |
| 200,000 | 1113 | 1585 | 174 / 228 | 270 / 288 | 37 → 0 |
| 500,000 | 2963 | 2866 | 418 / 641 | 751 / 800 | 162 → 0 |
| 800,000 | 4416 | 2569 | 671 / 1153 | 1222 / 1303 | 294 → 0 |
| 1,000,000 | 5614 | 3386 | 989 / 1463 | 1563 / 1650 | 387 → 0 |

- **"Real" is 1,000,100 generated empty files, measured before the page moved off the GUI thread.** I did not repeat it: generating the files takes over 10 minutes, and the change does not touch enumeration. **"Synthetic" is the seam folder, on the code in this PR.** The two columns differ in code as well as in source, so the page wait is not a before and after. The machine was also not equally quiet between runs, which is why the synthetic wait does not rise evenly.
- The 100,000 row is the first listing, not a page, so it does not use the new publish path.
- The synthetic working set is sampled just after the page lands, before the pool has freed the old rows. **Once it has, a million rows rest at 944 MB.**
- **Worst GUI gap with a page landing: 12 to 15 ms at every size**, against 403 ms at 1,000,000 before this change, both with the fixed tracer. The first page's gap was 58 ms, on the first-listing path and under the 250 ms budget.
- Setting a filter across 1,000,000 rows takes 165 ms on the GUI thread and clearing it 40 ms. Filtering has always run there. It is now reachable at this size and is under budget.

**The tracer's blind spot.** Its heartbeat stopped when `loading` cleared, so the last publish went uncounted. The first measurement here reported 14 ms for a 390 ms stall. This PR fixes it. PR 1 has the same tracer, but its last publish is 12 to 13 ms at 100,000, so PR 1's table stands.

## PR 12 review: CHANGES REQUIRED, and what changed

The review is `docs/verification/A1-REVIEW-PR12.md` on `claude/review-a1-pr12`, at `a687a30`. Everything is fixed in `a80f9bf`.

- **B1, a sort while the grid is hidden or re-arriving.** Two defences, both kept. `replaceRows` now clears the provisional offsets, the reviewer's fix. `setSort` and `setIncludeHidden` now ask for a republish only when the folder is truncated, active and in its definitive order, the reviewer's other suggestion. Otherwise the next first listing publishes the new order anyway. Tests, with folders in the listing and every row lookup checked, including the refresh afterwards:
  - `aSortWhileTheGridIsHiddenKeepsEveryLookupRight` and `aSortDuringReArrivalKeepsEveryLookupRight`, on the model.
  - `aSortInThePhotoViewKeepsTheGridsRowsRight`, through the shipped QML and the real `grid` and `sort-reverse` commands.

  With both defences off, all three fail. With either one off they pass, because either defence alone closes the bug.
- **B2, a refusal with no way forward.** The lead chose the reviewer's recommendation. Show more stays visible and enabled, and each press checks free memory again. The banner ends "Close other apps, then try again." `allowLoadMoreAgain` is gone, along with `aRefusedPageIsOfferedAgainWhenTheFolderIsOpenedAgain`, which called `setDirectory` directly. Tests:
  - `aRefusedPressIsCheckedAgainOnTheNextPress`, and `loadMoreChecksFreeMemory`, updated.
  - Through the shipped QML: `notEnoughMemorySaysWhatToDoAndTheNextPressChecksAgain`, which presses the real button, is refused, frees memory and presses again, and `aRefusedPressDuringARefreshSaysWhy`.
- **B3, large frees and copies on the GUI thread.** Every large free now goes to a dedicated one-thread pool: the unpublished listing, the rows a merge replaces, `release()`, a reorder, a reset, a filter and a large selection. Sharing the scan pool held a first row back 0.8 s. The definitive order also takes the worker's copy of the rows instead of copying them on the GUI thread. Numbers are under "Before and after, this round".
- **N0 and N1, selection cost at a million rows.** The selected count and bytes are now kept. Each selection command and each arrival tick updates them, and only a change of which rows exist has them counted again, once. Past 64 changed keys, one `dataChanged` covers every row. PR 1 has the same change at 100,000 rows.
- **N2, the memory estimate.** Corrected under "Memory bound".
- **N3, no sign of a sort at large sizes.** The banner says "Sorting the %1 items listed here…" while an explicit sort lists a truncated folder again. Test: `aSortOfATruncatedFolderSaysItIsSorting`.
- **N4, Show more during re-arrival.** `canLoadMore` is false while arriving, and the end of arrival notifies. Tests: `showMoreIsNotOfferedWhileATruncatedFolderReArrives` and `showMoreIsNotOfferedWhileTheFolderReArrives`.
- **N5, "Listing more items…" after a change of folder.** A new folder clears it. Test: `anotherFolderDuringAPageDoesNotSayListingMore`.
- **N6, a refused press during a refresh.** "Listing more items…" shows only for a page that really started. Test: `aRefusedPressDuringARefreshSaysWhy`.
- **N7, the 1180 px check.** `bannerFitsAtNarrowAndWideWidths` now waits for the banner to grow by 500 px before checking it at 1180. The screenshots are under "Not verified".
- **N8.** `aFilterHidingEveryListedRowKeepsTheOffer` now checks the filtered wording while more can be listed.
- **N9, page cache in a cgroup.** `memory.stat`'s `inactive_file` (`total_inactive_file` on cgroup v1) no longer counts as used. New parser cases are in `linuxFreeMemoryReadsMemAvailableAndTheCgroup`.
- **N10.** The `canLoadMore` and `loadMoreRefused` comments now say what they do, under the lead's grant. The diff is under "Files changed".
- **The memory race.** A listing at a raised cap now checks free memory as it starts, whatever started it: a page, a sort or a refresh. If the cap no longer fits, it lists the most whole steps that do. Then the cap drops to what was listed, and the banner gives the refusal. A whole folder refreshed with too little memory publishes what fits rather than throwing it away. If an allocation fails on the worker anyway, the model catches `std::bad_alloc`, keeps the rows, puts the cap back and sets the refusal, or says the folder could not be listed if it had no rows. Tests:
  - `aRaisedCapThatNoLongerFitsListsWhatDoes`, `aWholeFolderRefreshedWithTooLittleMemoryShowsWhatFits` and `largestCapThatFitsStepsDown`.
  - `runningOutOfMemoryDuringAPageKeepsTheRows` and `runningOutOfMemoryOnAFirstListingSaysSo`.
- **Merges.** PR 1's `5018b8f` merged clean in `3170dcd`. `main`'s `30803be` merged in `973fca9`. `CMakeLists.txt` keeps both test includes. EN-07 moved the grid commands into PhotoGrid's `Connections { target: viewer }` handler, and PR 1's menu settle line now sits before that handler's command chain.

### Before and after, this round

1,000,000 rows, synthetic seam folder, under the gate lock. Worst GUI gap or call time, in milliseconds.

- **Before:** `a687a30`'s code, measured by me on 2026-11-05. The whole-folder refresh is the reviewer's figure, because my tracer could not measure it then. The selection rows were measured on PR 1's code before its totals change.
- **After:** `a80f9bf`, on 2026-11-12. Every line is in `docs/verification/A1/million-traces-pr12-review.txt`.

| Path | Before | After |
| --- | --- | --- |
| Refresh of a truncated folder | 308 | 12 |
| Refresh of a folder listed whole | 586 | 162 |
| Coming back to the grid, worst gap | 339 | 85 |
| Coming back to the grid, first row | 786 | 24 |
| Leaving the grid | 508 | 0 |
| Opening another folder | 557 | 0 |
| One selection signal, every row selected | 298 | 0.01 |
| Select all | 751 | 205 |
| Clear | 235 | 0.2 |
| Invert | 244 | 155 |

- **Every path is now under the 250 ms budget.**
- **The whole-folder refresh** still merges on the GUI thread, comparing a million rows: `publish_ms=162`. It is in budget, so I did not move the merge to the worker.
- **A page** still costs the GUI thread 12 to 13 ms at every size. It takes 0.5 to 2.6 s to land from 200,000 to 1,000,000 rows.
- **A million rows** rest at 855 MB and peak at 1,671 MB.
- **Select all and invert** are explicit commands, and they visit every row once.

## Memory bound

Before a page starts, `loadMore()` estimates the page's peak and compares it with free memory. `GlobalMemoryStatusEx` gives free memory on Windows. On Linux it is `MemAvailable`, or what the process's cgroup has left if that is less.

- **Estimate:** the average of `directory_scan::rowBytes` over up to about 1,024 evenly spaced rows of this folder, times the page's row count, times 2.
- **Why 2:** the new listing exists beside the rows on screen until it is published, with its rows, their copy for the filter, the index, the identity baseline and the sort keys. These short names average 450 bytes, so the last page is estimated at 858 MB.
- **The estimate is about 6% short (review N2).** The reviewer measured the last page at `a687a30`, after the pool had freed the rows before it, and found a peak 908 MB over the rows at rest, against the 858 MB estimate. My earlier 372 MB read its baseline before the pool had freed the old rows, so it was wrong, and my 767 MB real-file figure predates the move off the GUI thread. About half of each row's cost does not scale with the name, so the estimate falls further short with very short paths, about 150 MB at a million rows by the reviewer's arithmetic, and over-estimates with long ones.
- **Why it is still safe:** the 512 MB headroom covers the shortfall. The last page leaves about 460 MB free rather than 512 MB, and nothing pages. I kept the factor rather than adding a fixed per-row term, so the check refuses no more than before. The race between the check and the page is closed separately: see "The memory race" above.
- **Headroom:** the page must also leave 512 MB free, so the last page of this folder needs about 1.37 GB free.
- **A cheap way to lower the peak:** I did not find one. The old rows are on screen until the new ones are published, so they cannot go earlier. Dropping the filter's copy of them would save about 96 bytes a row, but a filter set during the page needs it back, which is a rework. The peak is unchanged: 1,673 MB against 1.65 GB before.

Today's numbers are at the end of `docs/verification/A1/million-traces.txt`.

## Files changed

Grant check, `git diff --name-only ac49031..e057520`, with `docs/` left out of the list:

```text
include/glancelot/directory_entry_model.hpp
src/app/directory_entry_model.cpp
src/app/directory_scan.hpp
tests/perf_enum_tests.cpp
tests/perf_enum_ui_tests.cpp
translations/glancelot_en_XA.ts
ui/PhotoGrid.qml
```

All are in the brief's grant except the header, which the lead granted, and the translation file, which the lead's copy condition requires. `ui/Main.qml` is untouched.

Lead's grant for `loadMore()`: "Grant for PR 2 only: add exactly that `loadMore()` declaration and comment". Lead's grant for `canLoadMore`: "approved for PR 2, with one change. `canLoadMore` must notify whenever its value changes, including after `loadMore()` reaches the limit in the same folder, when `directoryChanged` does not fire. Use the signal `truncated` uses if it fires on every such change; otherwise add one small notify signal to this grant and show that diff in the handoff." The diff:

```diff
+    // True when `truncated` is set and the hard limit of 1,000,000 entries has not been reached, so loadMore() would list more.
+    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY directoryChanged)
+    bool canLoadMore() const;
+    // Lists the next page when `truncated` is set: re-enumerates with a higher cap (hard limit 1,000,000 entries) and publishes the new order once. No-op otherwise.
+    Q_INVOKABLE void loadMore();
```

**This round,** `git diff --name-only 973fca9..a80f9bf`:

```text
include/glancelot/directory_entry_model.hpp
src/app/directory_entry_model.cpp
src/app/directory_scan.hpp
tests/perf_enum_test_support.hpp
tests/perf_enum_tests.cpp
tests/perf_enum_ui_tests.cpp
translations/glancelot_en_XA.ts
ui/PhotoGrid.qml
```

All are in the grant. `tests/perf_enum_test_support.hpp` is PR 1's seam folder, which gains a size offset. The header change is the comment the lead granted under N10:

```diff
-    // True when `truncated` is set and the hard limit of 1,000,000 entries has not been reached, so loadMore() would list more.
+    // True when `truncated` is set, the folder is not arriving and the hard limit of 1,000,000 entries has not been reached,
+    // so loadMore() would list more if the page fits in free memory. Free memory is checked again on each call.
     Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY directoryChanged)
-    // True when loadMore() found too little free memory for the next page. Rows stay; opening the folder again clears it.
+    // True when the last attempt to list more did not fit in free memory: a refused loadMore(), or a listing at a raised cap
+    // cut back to what fits. Rows stay. The next loadMore() that fits, or another folder, clears it.
     Q_PROPERTY(bool loadMoreRefused READ loadMoreRefused NOTIFY directoryChanged)
```

Against PR 1's branch, the PR also shows the files `main` brought in `973fca9`, among them `scripts/ci/*`, `AGENTS.md` and root `CMakeLists.txt`. They are EN-07 and the commit rules, not this packet's changes, and they leave the PR's diff once PR 1 has `main` too.

Lead's grant for the refusal: "You're granted one added property or signal for the refusal reason in `directory_entry_model.hpp`, beyond the existing `loadMore()` and `canLoadMore` grant." The diff:

```diff
+    // True when loadMore() found too little free memory for the next page. Rows stay; opening the folder again clears it.
+    Q_PROPERTY(bool loadMoreRefused READ loadMoreRefused NOTIFY directoryChanged)
-    bool canLoadMore() const;
+    bool canLoadMore() const; bool loadMoreRefused() const;
```

**Notify condition:** no new signal was needed. `truncated` uses `directoryChanged`, and I made it fire on every `canLoadMore` change: `apply()` emits it when the value differs from the start of the call, and `setActive(false)` emits it when abandoning the last page ends the offer. Tests: `theHardLimitStopsOffering`, `abandoningTheLastPageNotifiesThatTheOfferEnded`, and through QML `atTheLimitTheBannerSaysSoAndFocusGoesToTheGrid`, where the button becomes the limit message in place without reopening the folder.

## Lead's conditions

| Condition | Where |
| --- | --- |
| Selection and anchor survive, and the photo stays in view | `selectionCursorAndAnchorSurviveLoadMore`, `showMoreFromTheKeyboardKeepsThePhotoInView`, `showMoreKeepsTheVisibleRowWhileBrowsing` |
| Bounded step, published once | `shippedContinuationSteps`, `loadMoreListsTheNextPageAndPublishesOnce` |
| A newer folder cancels it | `aNewerFolderCancelsLoadMoreAndStartsAtTheFirstCap` |
| A second press does nothing | `aSecondPressWhileListingDoesNothing` |
| Keyboard, accessible name, `qsTr`, sentence case | `showMoreFromTheKeyboardKeepsThePhotoInView` |
| Checked at 560 and 1180 px | `bannerFitsAtNarrowAndWideWidths`, plus `docs/verification/A1/banner-560.png` and `banner-1180.png`, which I looked at |
| The limit says so rather than offering again | `theHardLimitStopsOffering`, `atTheLimitTheBannerSaysSoAndFocusGoesToTheGrid`, `aHardLimitBetweenStepsIsStillTheLimit` |
| Memory and time at 100,000 and 1,000,000 | The table above |
| An explicit sort or hidden-files change republishes through the single publish, keeping the selection and the row at the top | `aSortChangeRepublishesATruncatedFolderOnce`, `aHiddenFilesToggleRepublishesATruncatedFolderOnce`, `aSortChangeUnderAFilterReachesTheWholeListing`, `aSortChangeInATruncatedFolderKeepsTheVisibleRowAndTheBanner` |
| File-watch refreshes stay on the current rule | `anOrdinaryRefreshKeepsATruncatedFoldersRows`, `aRefreshAfterASortChangeDoesNotPublish`, `aRefreshDuringASortChangeWaitsForIt` |
| The banner still says more items are not listed | `aSortChangeInATruncatedFolderKeepsTheVisibleRowAndTheBanner` |
| A bound below 1,000,000, from the rows' actual cost | `aFolderOfLongNamesNeedsMoreFreeMemory`, `averageRowBytesComesFromTheRows`, `pageFitsLeavesTheHeadroomFree` |
| Refused, allowed and failed reading | `loadMoreChecksFreeMemory` |
| Refused: rows kept, Show more kept, the reason and what to do in plain words, checked again on the next press (the lead's B2 decision) | `notEnoughMemorySaysWhatToDoAndTheNextPressChecksAgain`, `aRefusedPressIsCheckedAgainOnTheNextPress` |
| `MemAvailable` and the cgroup limit on Linux | `linuxFreeMemoryReadsMemAvailableAndTheCgroup`, `thisMachineReportsFreeMemory` |

`translations/glancelot_en_XA.ts` was edited by hand, because `lupdate` is not available on Windows here.

## Tests

**This round, at `a80f9bf`.** The 20 targeted suites ran in a build slot on 2026-11-12, all passing on their first run:

```text
 1/20 Test  #4: directory_listing ................   Passed   14.74 sec
 2/20 Test  #7: viewer_acceptance ................   Passed    9.96 sec
 3/20 Test #16: directory_model ..................   Passed   21.35 sec
 4/20 Test #20: grid_search ......................   Passed    0.27 sec
 5/20 Test #21: grid_density .....................   Passed    0.19 sec
 6/20 Test #23: fs_watch .........................   Passed   86.65 sec
 7/20 Test #26: fs_watch_ui ......................   Passed    2.83 sec
 8/20 Test #30: filter ...........................   Passed    2.61 sec
 9/20 Test #31: cache ............................   Passed    5.50 sec
10/20 Test #40: recovery .........................   Passed   13.23 sec
11/20 Test #41: recovery_startup .................   Passed    0.49 sec
12/20 Test #42: recovery_controller ..............   Passed   14.57 sec
13/20 Test #43: recovery_process .................   Passed   28.31 sec
14/20 Test #44: tabs_recovery ....................   Passed   65.64 sec
15/20 Test #48: translation_coverage_ui ..........   Passed   22.84 sec
16/20 Test #51: perf_enum ........................   Passed   43.75 sec
17/20 Test #52: perf_enum_ui .....................   Passed   84.06 sec
18/20 Test #53: command_handlers_ui ..............   Passed   62.51 sec
19/20 Test #56: export_ui ........................   Passed   74.86 sec
20/20 Test #57: stack_confirm_ui .................   Passed  243.46 sec
100% tests passed out of 20
Total Test time (real) = 798.64 sec
perf_enum    Totals: 71 passed, 0 failed, 1 skipped, 0 blacklisted, 30469ms
perf_enum_ui Totals: 52 passed, 0 failed, 1 skipped, 0 blacklisted, 83813ms
```

The budget suites ran under the gate lock. They passed on their first run, so no rerun was needed:

```text
1/3 Test #12: folder_grid ......................   Passed  195.77 sec
2/3 Test #13: input_ui .........................   Passed   83.11 sec
3/3 Test #24: fs_watch_budget ..................   Passed   76.68 sec
100% tests passed out of 3
Total Test time (real) = 355.75 sec
folder_grid Totals: 74 passed, 0 failed, 4 skipped, 0 blacklisted, 195634ms
input_ui Totals: 33 passed, 0 failed, 1 skipped, 0 blacklisted, 82997ms
```

**A flake to know about.** The run before the pause, on 2026-11-05 at the same code, failed `perf_enum_ui` once, in PR 1's `theViewIsOnTheAnchorBeforeAnyFrameAfterTheDefinitiveOrder(every 7th a folder)`, with "1 lines from the anchor". It passed 4 of 4 alone on both branches, and it passed in today's run. It is PR 1's test and code, so I left it for PR 1's next round rather than change it here.

**At `9a9482e`.** One test changed, to drive the real `sort-reverse` command instead of calling the model. `perf_enum_ui` passes in a build slot: 35 passed, 0 failed, 77.69 sec. Its first version failed `tryAgainWithTheOrderUnchangedReturnsToTheBrowsedPosition` (`layouts.count()` 1, expected 0), because the command saves the sort and the next test started from it. That test passed alone and failed straight after the sort test. The sort test now has its own config folder. The saved settings were in the run's temporary folder, never the user's.

**At `e057520`,** every other suite. The first 18 ran in a build slot, and the last 3 under the gate lock, once a review's gate had finished. All pass on their first run. No local full suite ran.

```text
 1/18 Test  #4: directory_listing ................   Passed   27.88 sec
 2/18 Test  #7: viewer_acceptance ................   Passed   14.12 sec
 3/18 Test #16: directory_model ..................   Passed   42.46 sec
 4/18 Test #20: grid_search ......................   Passed    0.38 sec
 5/18 Test #21: grid_density .....................   Passed    0.28 sec
 6/18 Test #23: fs_watch .........................   Passed  102.28 sec
 7/18 Test #26: fs_watch_ui ......................   Passed    5.92 sec
 8/18 Test #30: filter ...........................   Passed    4.85 sec
 9/18 Test #31: cache ............................   Passed    8.12 sec
10/18 Test #40: recovery .........................   Passed   16.08 sec
11/18 Test #41: recovery_startup .................   Passed    0.72 sec
12/18 Test #42: recovery_controller ..............   Passed   16.28 sec
13/18 Test #43: recovery_process .................   Passed   38.47 sec
14/18 Test #44: tabs_recovery ....................   Passed   69.71 sec
15/18 Test #48: translation_coverage_ui ..........   Passed   30.14 sec
16/18 Test #51: perf_enum ........................   Passed   85.13 sec
17/18 Test #52: perf_enum_ui .....................   Passed   82.35 sec
18/18 Test #55: stack_confirm_ui .................   Passed  230.31 sec
100% tests passed out of 18
1/3 Test #12: folder_grid ......................   Passed  382.64 sec
2/3 Test #13: input_ui .........................   Passed  165.19 sec
3/3 Test #24: fs_watch_budget ..................   Passed  108.01 sec
100% tests passed out of 3
folder_grid Totals: 74 passed, 0 failed, 4 skipped, 0 blacklisted, 382341ms
input_ui Totals: 33 passed, 0 failed, 1 skipped, 0 blacklisted, 164879ms
```

`ac49031` was merged clean in `f734041`. The bug below was reproduced before the fix, at `f734041` plus the new tests, in `docs/verification/A1/mutations-sort-memory.txt`. The 29 Show more mutations from the first round were not rerun after these merges and fixes.

**Earlier,** at `ac4e040`. Two runs, because the first found the bug now fixed on PR 1 (`definitiveOrderBeforeAnyFrameKeepsTheVisibleRow`):

```text
First run, before the fix (the three new model tests were not yet in):
13/13 Test #51: perf_enum_ui .....................***Failed   58.36 sec
FAIL!  : PerfEnumUiTests::scrollDuringArrivalKeepsTheVisibleRow() 'anchored()' returned FALSE. ()
92% tests passed, 1 tests failed out of 13

Run at ac4e040:
 1/13 Test  #4: directory_listing ................   Passed   26.48 sec
 2/13 Test #12: folder_grid ......................   Passed  306.40 sec
 3/13 Test #16: directory_model ..................   Passed   25.43 sec
 4/13 Test #20: grid_search ......................   Passed    0.34 sec
 5/13 Test #21: grid_density .....................   Passed    0.22 sec
 6/13 Test #23: fs_watch .........................   Passed   87.78 sec
 7/13 Test #24: fs_watch_budget ..................   Passed  109.92 sec
 8/13 Test #26: fs_watch_ui ......................   Passed    3.47 sec
 9/13 Test #30: filter ...........................   Passed    3.04 sec
10/13 Test #31: cache ............................   Passed    5.13 sec
11/13 Test #47: translation_coverage_ui ..........   Passed   24.05 sec
12/13 Test #50: perf_enum ........................   Passed   62.82 sec
13/13 Test #51: perf_enum_ui .....................   Passed   23.11 sec
100% tests passed out of 13
Total Test time (real) = 678.94 sec
```

No budget oracle changed.

**Full suite:** by the lead's decision, implementers no longer queue local full suites. The PR's hosted `viewer` job is the full suite, and the tester gates the exact head before merge. The last local full suite was PR 1's, at `902a765`: 53 of 53.

## Fail-before evidence

**This round:** `docs/verification/A1/mutations-pr12-review.txt`. There are 28 mutations, one per fix.

- **Rerun today at `a80f9bf`: 10 of them,** the B1, B2 and N4 set. Then I stopped the run to fit the session's time. All 10 behave as expected:
  - Eight fail their tests.
  - `b1-only-the-clear-off` and `b1-only-the-guard-off` pass, by design, because either defence alone closes B1. With both off, all three B1 tests fail.
- **The other 18 ran on 2026-11-05,** at the code before the last selection-totals changes. That log was overwritten when today's run started, so only the summary below remains.
  - 16 failed their tests: N5, N6, N3, the four race mutations, the three out-of-memory mutations, N9, the cap step-down, and three of the totals mutations.
  - **`totals-rename-not-recounted` survived.** No test reaches it. Dropping the totals on a rename is a conservative reset, so I kept it.
  - **`totals-metadata-kept` survived then.** `selectedBytesFollowANewSize` was added for it afterwards, and was not rerun against it.
- **Two mutations were dropped,** because the code they mutated has since gone: `race-no-cost-without-rows` and `totals-prune-not-recounted`.

**Earlier rounds:** `docs/verification/A1/mutations-more.txt`. 29 mutations, one per behaviour.

- **24 failed on the first run.**
- **4 survived, so those tests could not fail.** `ac4e040` adds tests, and all four now fail:
  - `no-hard-limit`: the shipped limit is a multiple of the step, so the clamp never mattered. New test `aHardLimitBetweenStepsIsStillTheLimit`.
  - `raw-rows-not-updated` and `raw-index-not-updated`: no test set a filter after a page. Without those lines the filter finds nothing and clearing it loses every row's index. New test `aFilterAfterLoadMoreSeesTheNewPageAndClearingItRestoresTheIndex`.
  - `filter-not-fed-the-page`: the existing test's last page was not truncated, so it published anyway. New test `aFilteredPageThatIsStillTruncatedShowsItsMatches`.
- **1 still survives: `rows-copied-on-gui`.** Copying a million rows on the GUI thread costs about 60 ms (publish 61 ms and gap 63 ms, against 0 and 12), under the 250 ms budget, so the budget check cannot see it. I kept the worker-side copy because it saves about four frames for a few lines. I did not add a tighter threshold to make this mutation fail, since any number would be arbitrary on a shared machine. If a reviewer prefers less code, removing `copyRows` stays in budget.
- **`old-rows-freed-on-gui` is caught by timing,** and `traceOneMillion` only runs when `GLANCELOT_PERF_ENUM_MILLION` is set, so CI does not run either check.
- **The `forceLayout` fix,** now on PR 1, has its before and after at the end of the same file.
- **Sort, hidden files and free memory:** `docs/verification/A1/mutations-sort-memory.txt`, with the reproduction before the fix. 27 mutations, of which 26 fail their tests. `sort-change-merges` first ran against a test where a merge also resets, so it is recorded against the hidden-files test too, which catches it.
- **`no-rows-copy-for-a-sort-change` survives,** for the same reason as `rows-copied-on-gui`: the copy then runs on the GUI thread, which costs about 60 ms at a million rows, under the budget.

## Failure and cancellation cases covered

- A newer folder during a page cancels it, and the new folder starts at the first cap.
- Going inactive during a page keeps the raised cap. If that page was the last one, the offer ends and QML is notified.
- A folder failing during a page clears the rows, marks it inaccessible and ends the offer.
- A refresh during a page or a sort change waits for it and then refreshes. An ordinary refresh keeps a truncated folder's rows.
- Too little free memory refuses the page before any scan starts. A failed reading allows it, and the 1,000,000 limit still holds.
- Pressing again while listing, pressing at the limit and pressing when inactive do nothing.
- A filter hiding every listed row keeps the offer, because the match may be further on.

## Decisions taken

- **A page is a model reset, not a merge.** New rows land anywhere in the order, and merging would hash up to a million rows on the GUI thread to save a reset. The view restores its anchor after the reset, and selection is by key. The alternative, a layout change plus inserts, is far more work for the same picture.
- **The worker makes the second copy and the pool frees the old rows.** Together with the reset, this removes the 390 ms stall.
- **Re-enumerate rather than hold a find handle open.** A held handle would be cheaper per page, but it keeps the folder open while idle and goes stale as files change. The cost is that each page re-reads the rows before it, which is why the page wait grows.
- **The button stays enabled while listing,** so keyboard focus is not thrown away. A second press does nothing, and the banner text shows the work.
- **Filtered messages carry no number,** because a filtered count is not how much of the folder is listed.
- **The banner stays when a filter hides every listed row,** because that is when further pages matter most.
- **A sort change restarts the scan and publishes with the same reset as a page.** The folder's first rows are read again, so a folder that changed meanwhile can list slightly different rows, which is what a whole-folder relist would show. The alternative, sorting the rows held in memory, would miss a hidden-files change and need a second code path.
- **A selected hidden file that leaves the list stays in the selection keys** and comes back if hidden files are shown again. Selection counts and delete targets read the listed rows, so an unlisted row is never acted on.
- **Refused keeps Show more enabled, and each press checks again.** This was the lead's decision on B2, replacing my earlier choice to hide the button. It costs no timer or watcher, and focus stays on the button.
- **A cap that no longer fits is cut to whole steps, never below the first 100,000.** A folder is never listed with less than it showed before Show more existed. The cut lowers the folder's cap to what was listed, so the next press is a normal page that checks memory again.
- **Running out of memory on the worker keeps the rows and sets the refusal,** instead of ending the app. This uses the same banner as a refusal, because the user's next step is the same.
- **B1 keeps both defences.** Either alone fixes the bug. The clear in `replaceRows` makes the publish correct whatever state it meets. The guard in `setSort` avoids a reset that a first listing does not need.
- **A dedicated pool for frees,** one thread, so freeing a million rows never queues ahead of a listing. It waits up to 5 s at teardown, because it only frees memory.
- **Selection totals are kept, not cached between signals.** Caching still recounted after every change: 300 ms at a million rows with everything selected. Keeping them makes a selection signal cost the same at any size. A change of which rows exist still recounts once.
- **512 MB headroom, not the viewer's 40% of free memory.** 40% would refuse the last page on a machine with 2 GB free, even though about 1.1 GB would remain. A fixed figure ties the check to the question asked: can this page start the machine paging?

## Open questions for the lead

- **`guard` against PR 1's branch.** `python scripts/ci/pr_guard.py --base origin/claude/a1-perf-enum --head-ref claude/a1-load-more --pr 12` reports three errors, all `scripts/ci/*` files that came from `main` in `973fca9`: `commit_lint.py`, `pr_guard.py` and `test_pr_guard.py`, "lead-owned; a packet needs the lead's lead-grant label". This packet does not change them. They go once PR 1 has `main` too. Until then, the hosted `guard` on PR 12 is expected to fail on them, unless the lead adds the label. Against `origin/main` the only error is the size of PR 1 and PR 2 together, 4,217 lines.

## Not verified

- QML delegate rebuild cost at 1,000,000. The trace covers the GUI thread during the publish, but not the first frame after it with a real renderer.
- A slow or dead network share. Those paths are covered through the seam only.
- Physical hardware and a real display. All UI evidence is offscreen with the software backend.
- `docker build`, and the full suite locally (see "Tests").
- A real low-memory machine. The refusal is driven through the test seam, and this PC only proved that a reading comes back.
- A hidden-files change from the app. Nothing in the app calls `setIncludeHidden` today, so that half of the fix is tested on the model and users cannot reach it yet. The sort half is: `sort` and `sort-reverse` reach `grid.setSort` through `file_grid_wiring::followViewer`, and `aSortChangeInATruncatedFolderKeepsTheVisibleRowAndTheBanner` drives `sort-reverse`.
- A real cgroup. The Linux parser is tested with sample text on every platform, including `memory.stat`, and `thisMachineReportsFreeMemory` runs on Linux only in the hosted jobs. The reader takes the process's own group and cgroup v1's root, not the tightest ancestor.
- A real `std::bad_alloc` on the worker. The tests raise one through the seam.
- `banner-560.png` and `banner-1180.png` were not retaken (review N7). `bannerFitsAtNarrowAndWideWidths` now checks that the banner has grown at 1180, but nobody has looked at a new 1180 picture.
- The 18 mutations not rerun today (see "Fail-before evidence").
- The pseudo-locale on screen. `translation_coverage_ui` passes, but I edited the `.ts` file by hand and did not look at the result.

## UI skills

`application-interface-design`, `component-geometry`, `responsive-layout`, `layout-spacing` and `typography-ui` were read before the banner work. `design-review` was not run: the change is one outline button and one line of text in an existing banner.
