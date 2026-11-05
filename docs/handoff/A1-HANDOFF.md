# A1 handoff: incremental cancellable folder listing

PR 1 of 2 for A1 (PERF-ENUM + F016). The continuation beyond the 100k cap ("Show more") is the stacked PR 2, as the lead approved. It has its own section in this file on its branch.

- **Branch:** `claude/a1-perf-enum`
- **Code tip:** `902a765` (the full suite ran here). The commit after it adds only this file and `docs/verification/A1/`.
- **Exact base:** `3a14044` (`origin/main`, not stacked). `main` had not moved when this was written.
- **PR:** https://github.com/dvortsis/glancelot/pull/7

## What a user sees

- A large folder shows rows at about 140 ms instead of after 1.3 to 1.8 s. The count reads "N items so far" until the order is final.
- The order is published once. Delegates stay in place, the row at the top stays at the top, and a row clicked during arrival stays selected.
- A folder that lists within 120 ms, or has too few rows, publishes once with no provisional rows, as before.
- Opening another folder or closing the window stops the listing, even while the worker is blocked on a full queue.

## Before and after

Generated mixed media on this PC, milliseconds. "Rows" is time to first row. "Done" is time to `loading` false. Full lines are in `docs/verification/A1/traces-before.txt` and `traces-after.txt`; the after run is at `902a765` under the gate lock.

| Trace | Rows, before | Rows, after | Definitive order, after | Done, before | Done, after | Worst GUI gap, before → after |
| --- | --- | --- | --- | --- | --- | --- |
| 10k cold | 1836 | 51 | 51 | 1887 | 562 | 13 → 12 |
| 10k warm | 1465 | 52 | 52 | 1482 | 512 | 12 → 12 |
| 100k cold | 1602 | 141 | 496 | 1621 | 522 | 45 → 14 |
| 100k warm | 1319 | 142 | 534 | 1328 | 562 | 12 → 12 |

- Sorting 100k fell from 776 to 900 ms to about 40 ms, from precomputed keys.
- Enumeration at 100k rose from about 250 to about 430 ms, because the worker now builds rows and sort keys as it goes. Rows still reach the screen far sooner.
- 10k "done" varied between runs: 562 and 512 here, 1231 and 920 in an earlier run on a busier machine. It is almost all identity probes, about 140 µs each by subtraction.
- At 100k the identity baseline is over its byte budget, so no probes run and "done" follows the definitive order closely.
- Closing during arrival took 13 ms against 29 ms before. The worker stopped at 1279 entries with a 1024-row queue.

## Files changed

Grant check, `git diff --name-only 3a14044..902a765`:

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

- `ui/PhotoGrid.qml`, thin call-site changes the brief allows:
  - `settleByUser()`, so a click or scroll during arrival wins over the waiting placement.
  - `reveal()` and four Connections handlers react to `arriving`, the count and layout changes.
  - The count label says "so far".
  - The thumbnail binding also depends on `cell.path`, so a delegate kept in place rebinds.
- `translations/glancelot_en_XA.ts`: two new strings, added by hand because `lupdate` is not available on Windows here.

`ui/Main.qml` and `src/core/*` are untouched. `pr_guard` reports 0 errors and the notice `1883 changed lines; aim for about 1500`. About 940 of those lines are tests; the source change is about 870. I did not split further because the arrival code and its tests only make sense together.

## Tests

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

## Decisions taken

- **Probes run after the definitive order, not before.** The advisor suggested probing before publishing. The baseline showed enumeration at about 25 ms and sorting at about 60 ms out of 1.5 s at 10k, so nearly all the wait was probes. Publishing first is the change that moves time to first row. This needed `arriving`, because `loading` now outlives the final order. Lead approved.
- **Tuning:** patience 120 ms, slow floor 1000 ms, minimum 2048 rows, 2048 rows per batch, 100 ms flush, 16,384 rows queued. GUI drain is 6 ms in each 16 ms tick, with at most one folder insert and one file insert. These are starting points set from the traces above.
- **Definitive order as `layoutChanged` with `VerticalSortHint`, not a reset:** QML rebinds delegates in place and keeps `contentY`. No signal if the order is unchanged. Tested through the shipped `ui/Main.qml`.
- **Streaming `enumerate()` duplicates the `listDirectory` policy in `src/app/directory_scan.hpp`** rather than editing `src/core`, which is outside the grant. `enumeratorMatchesListDirectory` guards the two against drifting.
- **Header-only scan helper**, so about 15 targets that compile the model need no CMake edits.
- **Provisional index by segment:** files by position in the file segment, folders flagged, decoded in `rowOfKey`. A folder insert then does not rewrite the index.
- **Refresh during arrival is coalesced** rather than restarting, so a busy folder cannot starve its own first listing.
- **Selection during the probe window is by path.** If a rename lands before its baseline exists, that row leaves the selection instead of following a guess.

## Open questions for the lead

- **Sorting or toggling hidden files in a truncated folder looks like it does nothing.** `publishRows` skips a truncated listing while rows are showing, so the new order would not appear. `main` has the same condition (`if (!truncated || (filtering() ? rawListing.rows.empty() : rows.empty()))`), so A1 did not cause it. I read this from the code and did not reproduce it. It is outside this packet, so I left it. It should be a small fix once you decide where it goes.

## Not verified

- A slow or dead network share. The patience, floor and flush paths are covered through the seam only.
- Physical hardware and a real display. All UI evidence is offscreen with the software backend.
- `docker build` was not part of my run. The lead's tester gates the merge head.

## UI skills

`application-interface-design` was read before the QML edits. The only visible change in this PR is the "so far" wording in the existing count label, with no new control or layout. `layout-spacing`, `component-geometry`, `responsive-layout` and `typography-ui` were read for the banner in PR 2.

---

# PR 2: Show more for folders beyond the first 100,000 entries

- **Branch:** `claude/a1-load-more`
- **Code tip:** `ac4e040`. The commit after it adds only this section and evidence.
- **Exact base:** `e425a90`, the tip of `claude/a1-perf-enum` (PR 1). The PR targets that branch.
- **PR:** the pull request from this branch into `claude/a1-perf-enum`.

## What a user sees

- A folder over the cap shows the banner "Showing 100,000 items. This folder has more that are not listed here." with a **Show more** button.
- Each press lists 100,000 more, up to 1,000,000. The banner says "Listing more items…" while it works, and the rows change once, when the page lands.
- The selection, the cursor and the row in view stay put. If the keyboard cursor was in view it stays in view. If the user was browsing with the wheel, the row at the top stays at the top.
- At 1,000,000 the button goes and the banner says that is the most Glancelot can list. When everything is listed the banner goes. Either way, focus moves to the grid rather than being lost with the button.
- With a filter, the banner gives no number and says some matches may be missing, because the count on screen is not how much of the folder is listed.

## Please look at this first: a bug in PR 1

`eeb1c42` fixes a bug that is also in PR 7, which is under review at `e425a90`. I have not pushed anything to that branch, so the review's pinned head is unaffected. **Your call whether to put `eeb1c42` on `claude/a1-perf-enum` with the review's fixes.** It is one QML line and one test.

- **Symptom:** `scrollDuringArrivalKeepsTheVisibleRow` failed once in the targeted run under load, and passed alone straight after. I did not treat it as a flake, because the check waits 10 s and then still fails, so the view had stopped in the wrong place.
- **Cause:** the view learns its new height in a frame. When no frame runs between the last rows and the definitive order, `restoreViewport` clamps the anchor to the old height, and the view lands far above the row the user was reading.
- **Fix:** `grid.forceLayout()` before reading `contentHeight`.
- **Evidence:** new test `definitiveOrderBeforeAnyFrameKeepsTheVisibleRow` hides the window, which stops frames. It fails without the line and passes with it. Both tests then passed 15 runs of 15 in a build slot.
- **What I did not prove:** that this caused that one failure. The new test reproduces the same symptom by the mechanism above and the original has not recurred, but I did not catch the original in the act.

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

## Files changed

Grant check, `git diff --name-only e425a90..ac4e040`:

```text
include/glancelot/directory_entry_model.hpp
src/app/directory_entry_model.cpp
src/app/directory_scan.hpp
tests/perf_enum_tests.cpp
tests/perf_enum_ui_tests.cpp
translations/glancelot_en_XA.ts
ui/PhotoGrid.qml
```

All are in the brief's grant except the header, which the lead granted, and the translation file, which the lead's copy condition requires. The commit after adds this file and `docs/verification/A1/`. `ui/Main.qml` is untouched.

Lead's grant for `loadMore()`: "Grant for PR 2 only: add exactly that `loadMore()` declaration and comment". Lead's grant for `canLoadMore`: "approved for PR 2, with one change. `canLoadMore` must notify whenever its value changes, including after `loadMore()` reaches the limit in the same folder, when `directoryChanged` does not fire. Use the signal `truncated` uses if it fires on every such change; otherwise add one small notify signal to this grant and show that diff in the handoff." The diff:

```diff
+    // True when `truncated` is set and the hard limit of 1,000,000 entries has not been reached, so loadMore() would list more.
+    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY directoryChanged)
+    bool canLoadMore() const;
+    // Lists the next page when `truncated` is set: re-enumerates with a higher cap (hard limit 1,000,000 entries) and publishes the new order once. No-op otherwise.
+    Q_INVOKABLE void loadMore();
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

`translations/glancelot_en_XA.ts` was edited by hand, because `lupdate` is not available on Windows here.

## Tests

Targeted, in a build slot at `ac4e040`. Two runs, because the first found the bug above:

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

`docs/verification/A1/mutations-more.txt`. 29 mutations, one per behaviour.

- **24 failed on the first run.**
- **4 survived, so those tests could not fail.** `ac4e040` adds tests, and all four now fail:
  - `no-hard-limit`: the shipped limit is a multiple of the step, so the clamp never mattered. New test `aHardLimitBetweenStepsIsStillTheLimit`.
  - `raw-rows-not-updated` and `raw-index-not-updated`: no test set a filter after a page. Without those lines the filter finds nothing and clearing it loses every row's index. New test `aFilterAfterLoadMoreSeesTheNewPageAndClearingItRestoresTheIndex`.
  - `filter-not-fed-the-page`: the existing test's last page was not truncated, so it published anyway. New test `aFilteredPageThatIsStillTruncatedShowsItsMatches`.
- **1 still survives: `rows-copied-on-gui`.** Copying a million rows on the GUI thread costs about 60 ms (publish 61 ms and gap 63 ms, against 0 and 12), under the 250 ms budget, so the budget check cannot see it. I kept the worker-side copy because it saves about four frames for a few lines. I did not add a tighter threshold to make this mutation fail, since any number would be arbitrary on a shared machine. If a reviewer prefers less code, removing `copyRows` stays in budget.
- **`old-rows-freed-on-gui` is caught by timing,** and `traceOneMillion` only runs when `GLANCELOT_PERF_ENUM_MILLION` is set, so CI does not run either check.
- **The fix in `eeb1c42`** has its before and after at the end of the same file.

## Failure and cancellation cases covered

- A newer folder during a page cancels it, and the new folder starts at the first cap.
- Going inactive during a page keeps the raised cap. If that page was the last one, the offer ends and QML is notified.
- A folder failing during a page clears the rows, marks it inaccessible and ends the offer.
- A refresh during a page waits for it and then refreshes. An ordinary refresh keeps a truncated folder's rows.
- Pressing again while listing, pressing at the limit and pressing when inactive do nothing.
- A filter hiding every listed row keeps the offer, because the match may be further on.

## Decisions taken

- **A page is a model reset, not a merge.** New rows land anywhere in the order, and merging would hash up to a million rows on the GUI thread to save a reset. The view restores its anchor after the reset, and selection is by key. The alternative, a layout change plus inserts, is far more work for the same picture.
- **The worker makes the second copy and the pool frees the old rows.** Together with the reset, this removes the 390 ms stall.
- **Re-enumerate rather than hold a find handle open.** A held handle would be cheaper per page, but it keeps the folder open while idle and goes stale as files change. The cost is that each page re-reads the rows before it, which is why the page wait grows.
- **The button stays enabled while listing,** so keyboard focus is not thrown away. A second press does nothing, and the banner text shows the work.
- **Filtered messages carry no number,** because a filtered count is not how much of the folder is listed.
- **The banner stays when a filter hides every listed row,** because that is when further pages matter most.

## Open questions for the lead

- **Whether `eeb1c42` goes on PR 7.** See above.
- **Sorting or toggling hidden files in a truncated folder looks like it does nothing.** This is PR 1's open question, still from reading the code and not reproduced. It matters more now that truncated folders are a normal path: a user who pressed Show more and then changes the sort sees no result. It is outside this packet, so I left it.
- **Memory.** A million rows cost about 950 MB at rest and 1.65 GB at peak with these short names, and longer names cost more. Your condition asked for the numbers rather than a bound, so nothing limits this below 1,000,000 entries. A user has to press Show more nine times to get there.

## Not verified

- QML delegate rebuild cost at 1,000,000. The trace covers the GUI thread during the publish, but not the first frame after it with a real renderer.
- A slow or dead network share. Those paths are covered through the seam only.
- Physical hardware and a real display. All UI evidence is offscreen with the software backend.
- `docker build`, and the full suite locally (see "Tests").
- The pseudo-locale on screen. `translation_coverage_ui` passes, but I edited the `.ts` file by hand and did not look at the result.

## UI skills

`application-interface-design`, `component-geometry`, `responsive-layout`, `layout-spacing` and `typography-ui` were read before the banner work. `design-review` was not run: the change is one outline button and one line of text in an existing banner.
