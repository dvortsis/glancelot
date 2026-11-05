# A1 PR 12 review: Show more beyond the first 100,000 entries

**Verdict: CHANGES REQUIRED**

- **Reviewed:** `a687a30770ce1d9464a649973f2a4fffcf6cd473` on `claude/a1-load-more`, as `git diff ac49031 a687a30`. PR 7 (`ac49031`) has its own reviewer and was not re-reviewed.
- **Reviewer:** fresh-context Opus reviewer. It implemented none of this and wrote no earlier review.
- **Environment:** this Windows PC, MSVC, Qt 6.8.3, offscreen with the software backend. Checkout `.worktrees/review-a1-pr12`, build directory `build-review-a1-pr12`.
- **UI skills read:** `application-interface-design` and `component-geometry`.

## Summary

Paging itself is sound. A page publishes once, a newer folder cancels it, stale results are fenced by the generation, the raised cap survives going inactive, and selection and the visible row survive. The sort decision and the memory check work on the path they were built for.

Three things need fixing:

- **B1.** A sort change that lands while the grid is hidden, or while a truncated folder is re-arriving, publishes through `replaceRows` and leaves the model in its provisional state. Every row lookup is then off by the number of folders, so Enter opens a different item than the one highlighted and a Shift range selects extra files.
- **B2.** After a memory refusal the banner gives no way forward, and the way the handoff describes does not work in the app.
- **B3.** At a million rows, a refresh, leaving the grid and opening another folder each stall the GUI for 0.4 to 0.9 s. These paths still free or copy every row on the GUI thread.

The memory check is safe, but its estimate falls about 6% short of the measured peak, and the headroom absorbs that (N2).

## Findings

### Blocking

#### B1. A sort change while hidden or re-arriving leaves the row lookup wrong

**Cause.** Commit `e057520` added `replaceRows`, which never clears `provisional` or `provisionalFolders`. The filtered branch beside it and `merge()` both clear them. `setActive(false)` does not clear `truncated`, so:

1. `setSort` while the grid is hidden sets `orderAsked = truncated`, which is true. `request()` does nothing because the model is inactive, so `orderAsked` stays set.
2. Re-entering the grid lists the folder as a first arrival. Past the patience window its batches set `provisional`.
3. The definitive batch reaches `publishRows` with `asked` true, so it goes through `replaceRows`. `index` now holds true rows, but `rowOfKey` still adds `provisionalFolders`.

The same state comes from pressing S or Shift+S while a truncated folder is re-arriving after G, because `truncated` is still set then too. That second path is read from the code; the probes below drive the first.

**How a user reaches it.** Open a folder of more than 100,000 entries that has subfolders, open a photo, press Shift+S, press G. `sort-reverse` has no scope in `src/app/command_registry.cpp`, and `file_grid_wiring::followViewer` forwards it to the grid whether or not the grid is visible. At 100,000 rows the arrival gate opens, so this is not an artifact of the test seam.

**What it does.** Probe in the shipped `ui/Main.qml`, driving the real `grid` and `sort-reverse` commands, on 1,000 listed rows of which 99 are folders:

```text
PROBE hidden truncated= true count= 0
PROBE sort after command descending= true
PROBE after_reentry wrong_rows= 1000 delta= 99 cursorRow= 198 pathAt(cursor)= f02391.txt wanted=p1.png selected= 1
PROBE refresh_after_reentry lowest_count= 0 resets= 1
FAIL!  : PerfEnumUiTests::probeSortInPhotoViewThenBackToGrid() Compared values are not the same
   Actual   (wrong): 1000
```

Probe on the model, 1,000 listed rows with 100 folders:

```text
PROBE before wrong_rows= 0
PROBE after-sort-while-hidden wrong_rows= 1000 delta= 100
PROBE range-of-one selected= 101 cursorRow= 600 chosen_row=500
PROBE refresh lowest_count= 0 count= 1000
```

- **Wrong item opened.** `cursorRow` is off by the folder count, and `activate()` reads the path at `cursorRow`. In the UI probe the selected photo is `p1.png`, and the row the cursor points at is `f02391.txt`.
- **Wrong files selected.** A Shift range of one row selected 101. `selectedPaths` feeds delete and copy, so this is a data-safety problem as well as a display one.
- **Out-of-bounds read.** `select(Range)` indexes `rows` from `rowOfKey(anchorKey)`. An anchor within the folder count of the end of the list reads past the end of the vector. This is from the code; I did not run it.
- **The grid blanks.** The next F5 or file-watch `request()` sees `provisional`, calls `release()`, empties the grid and lists it again. `lowest_count= 0` shows that. This part also happens in a folder with no subfolders, where the row lookup happens to stay right.

**Candidate fix, checked.** Adding `provisional = false; provisionalFolders = 0;` at the top of `replaceRows` clears all of it:

```text
PROBE after-sort-while-hidden wrong_rows= 0 delta= 0
PROBE range-of-one selected= 1 cursorRow= 500 chosen_row=500
PROBE refresh lowest_count= 1000 count= 1000
PROBE after_reentry wrong_rows= 0 delta= 0 cursorRow= 99 pathAt(cursor)= p1.png wanted=p1.png selected= 1
PROBE refresh_after_reentry lowest_count= 1000 resets= 0
```

**Also consider** not setting `orderAsked` while inactive or while `provisional` is set. A first arrival publishes anyway, and through the path that keeps delegates in place. The implementer should choose.

**Tests needed.** No fixture in this PR has folders, and none sorts while inactive, which is why nothing caught this. Add a model test and a test through the shipped QML, each with folders in the listing, covering sort while hidden, sort during re-arrival, and the refresh afterwards. Check them against the reverted fix.

#### B2. A refusal gives no way forward, and the documented one does not work in the app

The handoff says "Opening the folder again offers the page again", and the code comment on `memoryRefused` says the same. In the app it does not. `file_grid_wiring::followViewer` calls `setDirectory` only when the folder key differs, and nothing else calls it, so `allowLoadMoreAgain()` in the same-folder branch of `setDirectory` is unreachable from the app. Probe through the real commands:

```text
PROBE refused text= Showing 1,000 items. This folder has more, but there is not enough free memory to list them.
PROBE after_reopen_same_folder refused= true button= false
PROBE after_F5 refused= true button= false
```

`aRefusedPageIsOfferedAgainWhenTheFolderIsOpenedAgain` passes because it calls `model.setDirectory(sameDir)` directly.

What does work is pressing G twice, opening a photo and coming back, or visiting another folder and coming back. The last one also drops the rows back to the first 100,000. The banner mentions none of these, and memory freeing up does nothing by itself, so the user is left at a dead end with no button and no next step. `application-interface-design` asks that a disabled state explain its prerequisites and offer one relevant next step.

**Required this round:** the user must have a way forward that the app really offers, and the banner must say what it is. Correct the handoff and the code comment, and drive the recovery test through the real commands.

**Question for the lead** (it changes what users see): which way forward?

- **Recommended:** keep Show more visible and enabled, and re-check free memory on each press. The refusal then stays a sentence in the banner, it recovers the moment memory frees up, and it costs no timer or watcher. The check is cheap and there is no focus handling to add. The lead's earlier words were "disable Show more", so this needs the lead's word.
- **Alternative:** keep the button gone, and add to the banner something like "Close other apps, then leave this view and come back."

#### B3. Other paths at a million rows stall the GUI past the 250 ms budget

The PR moved a page's publish off the GUI thread, and that works: its gaps stay at about 12 to 15 ms. The same large free and copy still run on the GUI thread on other paths that this PR makes reachable at ten times the old size. Synthetic seam folder, 1,000,000 listed rows, under the gate lock, with no other build running:

| Path | Worst GUI gap or call time | Where the time goes |
| --- | --- | --- |
| File-watch refresh or F5, truncated folder | **379 ms** (`publish_ms=377`) | `publishRows` takes the listing by value and does not publish it, so all its rows are freed on the GUI thread when it returns |
| File-watch refresh or F5, whole folder listed | **586 ms** (`publish_ms=575`) | `merge`, `updateMetadata`, `rawListing.rows = rows` and the free of the incoming listing |
| Leaving the grid, `setActive(false)` | **911 ms** | `release()` clears the rows and the raw copy on the GUI thread |
| Opening another folder, `setDirectory` | **519 ms** | `release()` again |
| Coming back to the grid, `setActive(true)` | 249 ms | At the budget. Arrival at 1,000,000 rows |

```text
PERF_ENUM_TRACE refresh-truncated-1m ... settled_ms=3739 worst_gui_gap_ms=379 enumerate_ms=0 sort_ms=313 publish_ms=377 resets=0 ...
PERF_ENUM_TRACE refresh-whole-1m ... settled_ms=8441 worst_gui_gap_ms=586 enumerate_ms=0 sort_ms=582 publish_ms=575 resets=0 ...
PERF_ENUM_TRACE reenter-grid-1m first_row_ms=22 full_count_ms=4961 definitive_ms=6116 settled_ms=6126 worst_gui_gap_ms=249 ...
PROBE_MS leave_folder_setDirectory 519
PROBE_MS leave_grid_setActive_false 911
```

The truncated refresh matters most: the result is thrown away, so the user sees nothing change except a freeze, and it repeats whenever the folder changes. A folder that is being written to freezes the grid for about 0.4 s each time. `traceOneMillion` traces only pages, so nothing in the PR measures these.

**Required:** move these frees to the pool with the existing `retire()`: the unpublished listing, the incoming listing after a merge, and the rows in `release()`. Then measure the refresh and leave paths in `traceOneMillion`. The whole-folder merge also copies and compares on the GUI thread, so it may need the worker-made copy that a page already uses.

### Non-blocking, should fix

- **N0. Each selection change may cost about 0.4 s at a million rows. Measure it first; if it holds, treat it as blocking.** `selectedCount`, `selectedBytes`, `selectedSummary` and `selectedPaths` each walk every row and hash its key whenever the selection is not empty. On the model at 1,000,000 rows, with one item selected, under the gate lock:

  ```text
  PROBE_MS selectedCount 33    selectedBytes 30    selectedSummary 93
  PROBE_MS selectedCount 34    selectedBytes 44    selectedSummary 112
  PROBE_MS selectedCount 46    selectedBytes 46    selectedSummary 104
  ```

  These values are bound in QML and re-read on every `selectionChanged`: `ui/Main.qml` lines 923, 940, 2151, 2160 and 2161, and `ui/PhotoGrid.qml` lines 14, 514, 515, 560 and 567. The status line alone reads `selectedSummary` twice and `selectedCount` twice. So one arrow key with a selection probably costs 0.4 s or more on the GUI thread, against about 40 ms at 100,000 rows. I tried to time an arrow key in the shipped QML, but my probe's key did not reach the grid's handler, and I ran out of gate time to fix it, so this figure is an estimate. A count kept up to date as the selection changes, and bytes summed only over the selected keys, would make it independent of the row count.
- **N1. Selecting everything at a million rows.** `selectAll` took 541 ms, `clearSelection` 196 ms and one read of `selectedCount` with all selected 177 ms. These are explicit commands with visible results, so I would not hold the PR for them, but they are over budget.
- **N2. The memory estimate falls short of the measured peak.** See "Memory bound" below. The check is still safe because the headroom covers it.
- **N3. No sign of work during a sort at large sizes.** A sort at 1,000,000 rows re-enumerates for several seconds, and the old order stays with nothing on screen to say anything is happening. "Listing more items…" shows only for a page, and the grid's loading state needs a count of zero. At the lead's 1,000,000 limit, pressing S looks like nothing happened for several seconds, which is the complaint the sort decision set out to fix. The banner could show a short status while `loading` is set in a truncated folder.
- **N4. Show more is a dead button during re-arrival.** `truncated` survives `setActive(false)`, so while a truncated folder re-arrives the button shows. `loadMore()` returns early while `arriving`, but QML sets `moreRequested = fileGrid.loading`, which is true, so the banner says "Listing more items…" though no page was asked for. From the code, not run.
- **N5. `moreRequested` outlives a change of folder.** A newer folder during a page leaves it set until `loading` clears. If the new folder is truncated, its banner says "Listing more items…" for the moment between its definitive order and the end of `loading`. From the code, not run; the moment is short at 100,000 rows.
- **N6. A refused press during a refresh says "Listing more items…".** The same line sets `moreRequested` when `loading` is true for a refresh, and that text wins over the refusal until the refresh ends. From the code, not run.

### Non-blocking, for the record

- **N7. `banner-1180.png` shows the 560 px layout.** The image is 1180 wide with the UI drawn in the left 560 px, so it does not show the banner at 1180. The test's 1180 pass has the same blind spot: its checks hold for a layout that has not grown yet. The first listing runs at 1180, so a real problem there is unlikely. Retake the screenshot and have the test check that the banner has grown.
- **N8. A filtered banner's wording while more can be listed is untested.** `aFilterHidingEveryListedRowKeepsTheOffer` checks visibility only. The refused and at-limit filter wordings are asserted. My probe confirms this one reads correctly.
- **N9. Linux cgroup reading can refuse wrongly.** `memory.current` includes page cache, which the kernel can reclaim, so a viewer that has read many photos in a memory-limited container can read as almost out of memory. `memory.stat`'s `inactive_file` is the usual correction. No file in the repository sets a container memory limit (`git grep` of `docs/DOCKER.md`, compose files and the `Dockerfile`), so nothing ships this way today.
- **N10. The header comment on `canLoadMore` is stale.** It does not mention the refusal. The implementer left it as outside the grant, so it is the lead's to allow.

## What I checked, by the brief's items

### 1. Show more

| Check | Result |
| --- | --- |
| Re-enumerates with a higher cap, up to 1,000,000, publishing once | Holds. `loadMoreListsTheNextPageAndPublishesOnce`, `theHardLimitStopsOffering`, `aHardLimitBetweenStepsIsStillTheLimit` |
| A newer folder cancels a page | Holds. `abandonScan` stops the token and raises the generation; `aNewerFolderCancelsLoadMoreAndStartsAtTheFirstCap` |
| Going inactive during a page keeps state | Holds. `entryLimit` stays raised and `loadingMore` clears; `goingInactiveDuringLoadMoreKeepsTheRaisedCap` |
| Stale results cannot replace a newer folder | Holds. The watcher compares its token with `generation` before publishing |
| A page's rows are not freed on the GUI thread | Holds for a page. `replaceRows` retires the old listing to the pool. Other paths are in the million-row section |
| Visible row, focus and selection survive | Holds on the page path. Tests through the shipped QML: `showMoreFromTheKeyboardKeepsThePhotoInView`, `showMoreKeepsTheVisibleRowWhileBrowsing` |

### 2. Sort and hidden files in a truncated folder

- **Holds:** an explicit sort republishes through one reset, and the selection, the row at the top and the banner stay. Refreshes stay unpublished, and a refresh during a sort waits for it. The exception is B1.
- **The test drives the real command.** `aSortChangeInATruncatedFolderKeepsTheVisibleRowAndTheBanner` uses `viewer.trigger("sort-reverse")`, which reaches `grid.setSort` through the shipped wiring.
- **Banner:** "Showing N items. This folder has more that are not listed here." It claims no whole-folder order.
- **A filter's text path:** my probe filtered through `viewer.setFilter`, pressed `sort-reverse`, and got the filtered rows reversed with the banner still reading "This folder has more items that are not listed here, so some matches may be missing." Show more stayed visible.
- **What commit `a687a30` says users cannot reach yet:** the hidden-files half. I confirmed it: nothing in `src/` or `ui/` outside the model calls `setIncludeHidden`, and neither `docs/REQUIREMENTS.md` nor the queue mentions a hidden-files toggle. The decision's model behaviour is done and tested, so nothing of the decision is undone for today's app. Whoever adds the toggle should test it through QML, and the B1 path applies to it too because `setIncludeHidden` sets `orderAsked` the same way.

### 3. Memory bound

- **Decision implemented:** the estimate comes from the rows' real cost, sampled evenly over about 1,024 rows. Windows reads `GlobalMemoryStatusEx`, and Linux reads `MemAvailable` and the cgroup. A refusal starts no scan and keeps every row, and the banner gives the reason. A failed reading allows the page, and the entry limit still holds. Free memory is injectable, and the refused, allowed and failed-reading cases all exist and fail when the check is removed.
- **Is the estimate conservative enough? Not by itself, but the page is still safe.** I measured the last page at the pinned production code, under the gate lock, after giving the pool 3 s to free the previous rows:

  ```text
  PROBE_MEM rows=1000000 rest900_ws=758 peak=1666 rest1000_ws=850 delta_peak_over_rest=908 avg=450 estimate_mb=858
  ```

  The page's real peak is **908 MB against an estimate of 858 MB, about 6% short**. The handoff says the factor leaves about 12% over its worst measurement, and its other figure, 372 MB (1,673 against 1,301), reads its baseline just after a page lands, before the pool has freed the old rows, as the handoff itself notes. Its 767 MB real-file figure predates the move off the GUI thread. The 512 MB headroom absorbs the shortfall, so this page leaves about 460 MB free rather than 512 MB. Nothing pages because of it.
- **Short and long names.** `generate()`'s real names and a temp path come to about 458 bytes a row by arithmetic, so the synthetic 450 is a fair stand-in. About half of the 952 bytes a row measured does not scale with the text: the raw-row copy, the hash index, the scan's entries and sort keys. So the estimate is short by more with very short paths, about 150 MB at 1,000,000 rows by my arithmetic, which the headroom still covers, and it over-estimates with long names. A fixed per-row term in `pageFits` would be more honest than the factor alone. Correct the handoff either way.
- **Could it refuse wrongly on a normal machine?** I do not think so. The last page of ordinary names needs about 1.37 GB available, and the 100,000 to 200,000 page about 680 MB. `ullAvailPhys` includes the standby cache, so a machine with free-looking cache is not refused. The exception is the cgroup case in N9.
- **The race between the check and the page.** Memory is read once, at the press. Nothing re-checks during the seconds a page takes, and a sort change or a file-watch refresh at a raised cap reaches the same peak with no check at all. A refresh does so to produce a result it throws away. If an allocation fails on the worker, nothing in `scan` catches `std::bad_alloc`, so from the code `takeResult()` rethrows it on the GUI thread and the app ends. Viewing writes nothing, so no data is lost, and on Windows the machine pages first. This is the lead's accepted scope ("before each page"), so I record it as non-blocking. A `catch (const std::bad_alloc&)` around the scan that keeps the rows and sets the refusal would close it cheaply.
- **Does the disabled state recover, or say how?** No to both: B2.

### 4. User experience

- **Banner wording** is plain, sentence case and translatable. All seven strings are in `translations/glancelot_en_XA.ts`, and `translation_coverage_ui` passes. The pseudo-locale was not looked at on screen.
- **Keyboard reach:** from the grid, Shift+Tab reaches Show more in 1 press and Tab in 13. Enter and Space work through `UiButton`, focus stays on the button after a page, and goes to the grid when the button leaves.
- **Accessible name:** "Show more items in this folder", from the existing test.
- **Geometry:** `banner-560.png` shows the existing banner with one outline button at the control height, with no overlap. It reuses `UiButton` and the theme tokens.
- **The disabled state** explains why but not what to do: B2.

### Grant

`git diff --name-only ac49031 a687a30`, outside `docs/`:

```text
include/glancelot/directory_entry_model.hpp
src/app/directory_entry_model.cpp
src/app/directory_scan.hpp
tests/perf_enum_tests.cpp
tests/perf_enum_ui_tests.cpp
translations/glancelot_en_XA.ts
ui/PhotoGrid.qml
```

The header adds `loadMore()`, `canLoadMore` and `loadMoreRefused` and nothing else, which matches the grant. `ui/Main.qml` is untouched. `python scripts/ci/pr_guard.py --base ac49031 --head-ref a687a30` reports `code=True, 0 error(s)`. New waits have deadlines of at least 10 s, and each fixed wait carries a `sleep-ok` reason.

## Test output

Pinned head, unmodified, in a build slot, machine busy:

```text
1/2 Test #51: perf_enum ........................   Passed  102.18 sec
2/2 Test #52: perf_enum_ui .....................   Passed  139.15 sec
100% tests passed out of 2
```

Production code unmodified, in a build slot. These targets do not compile the probes:

```text
1/6 Test  #4: directory_listing ................   Passed   63.92 sec
2/6 Test #16: directory_model ..................   Passed   86.91 sec
3/6 Test #20: grid_search ......................   Passed    0.45 sec
4/6 Test #26: fs_watch_ui ......................   Passed   10.48 sec
5/6 Test #30: filter ...........................   Passed    8.26 sec
6/6 Test #48: translation_coverage_ui ..........   Passed   56.52 sec
100% tests passed out of 6
```

Budget suites under the gate lock, passing on the first run, so no rerun was needed:

```text
1/2 Test #12: folder_grid ......................   Passed  288.25 sec
2/2 Test #13: input_ui .........................   Passed  101.90 sec
100% tests passed out of 2
folder_grid Totals: 74 passed, 0 failed, 4 skipped, 0 blacklisted, 288025ms
input_ui Totals: 33 passed, 0 failed, 1 skipped, 0 blacklisted, 101730ms
```

The probes quoted in this document were temporary additions to `tests/perf_enum_tests.cpp` and `tests/perf_enum_ui_tests.cpp`, removed afterwards. None is in the PR. The implementer should turn B1's and B2's into real tests.

## Fail-before, spot-checked

Each mutation was applied to `src/app/directory_entry_model.cpp`, rebuilt, run, and restored with `git checkout`.

**Sort:** `setSort` no longer sets `orderAsked`.

```text
FAIL!  : PerfEnumTests::aSortChangeRepublishesATruncatedFolderOnce() Compared lists differ at index 0.
FAIL!  : PerfEnumTests::aSortChangeUnderAFilterReachesTheWholeListing() Compared lists differ at index 0.
FAIL!  : PerfEnumUiTests::aSortChangeInATruncatedFolderKeepsTheVisibleRowAndTheBanner() '!f.h->fileGrid.loading() && f.h->fileGrid.rowOf(anchor) != rowBefore' returned FALSE. ()
```

**Memory:** `loadMore` skips the `pageFits` check.

```text
FAIL!  : PerfEnumTests::loadMoreChecksFreeMemory(too little free) 'model.loadMoreRefused()' returned FALSE. ()
PASS   : PerfEnumTests::loadMoreChecksFreeMemory(enough free)
PASS   : PerfEnumTests::loadMoreChecksFreeMemory(reading failed)
FAIL!  : PerfEnumTests::aFolderOfLongNamesNeedsMoreFreeMemory() 'tryNames(240)' returned FALSE. ()
FAIL!  : PerfEnumUiTests::notEnoughMemoryKeepsTheRowsAndSaysWhy() '!f.showMore()->isVisible()' returned FALSE. ()
```

The tests catch both. The handoff's mutation tables were not otherwise rerun.

## Not verified

- The first frame after a million-row publish with a real renderer, and delegate rebuild cost at that size.
- A real low-memory machine, and a real cgroup. Both are as the handoff says.
- A real 100,000-entry folder for B1. The probes use the seam with patience off; that the arrival gate opens at 100,000 rows comes from the handoff's first-row times.
- The out-of-bounds read in B1, and N4 to N6, which are from reading the code.
- The arrow-key cost in N0, which is estimated from the model timings and the bindings, not measured in QML.
- `docker build` and the full suite, by the rules for this role.
- Linux. Hosted CI covers it. On `a687a30`, `guard`, `core (ubuntu-24.04)`, `core (windows-2022)` and `viewer` had passed when this was written, and `ci-ok` was still queued.
