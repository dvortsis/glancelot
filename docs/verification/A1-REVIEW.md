# A1 review: incremental cancellable folder listing

**Verdict: CHANGES REQUIRED**

- **Reviewed head:** `e425a903da9b8ba8b0c672338e7f5c8e2fe732f5` (`claude/a1-perf-enum`, PR 7). Base `3a14044`. The last code commit is `902a765`; `e425a90` adds only the handoff and evidence.
- **Reviewer:** fresh context, wrote none of this. Windows 11, MSVC, Qt 6.8.3, offscreen with the software backend. Physical hardware and a real slow share are not verified.
- **UI skills:** `application-interface-design` read. No layout, geometry or icon change here, so the other skills did not apply.

The model layer is sound. Rows act on the right file, and generation fencing, the queue bound and worker teardown all hold. What fails is the QML placement and viewport code around arrival, plus one lost wakeup in the drain. Every finding below except B5 reproduces with a real scenario against `e425a90`, using the local tests in `docs/verification/A1-REVIEW-scratch-tests.patch` (apply it to `e425a90` with `git apply --ignore-whitespace`; test names below start with `r`). B5 needs an injected delay and says so.

## Blocking findings

### B1. Keyboard and scrollbar use during arrival is overridden when the placement fires (high)

`ui/PhotoGrid.qml:134`, `:138-150`, `:538`, `:564`.

`settleByUser()` runs only from `clickRow` and `onMovementStarted`. Keys and scrollbar drags do not settle, so `placeCursor` and `revealPath` stay armed. When the viewed photo's row arrives, or the definitive order lands, `reveal()` calls `select(row)` in Replace mode and moves the view. The longer the listing, the wider this window, so a slow share is the worst case (not tested on a real share).

Actual output against `e425a90`:

```text
rKeyboardDuringArrivalIsNotReplaced: keyboard chose ".../f02939.txt" selection now QList(".../p1.png")   FAIL
rSelectAllDuringArrivalIsNotReplaced: ctrl+a selected 1024 selection now 1                               FAIL
rScrollbarDuringArrivalKeepsTheView: placeCursor after the scrollbar press QVariant(bool, true)
                                     anchor row 2383 top row 2976 selection QList(".../p1.png")          FAIL
```

The selection the user just made is silently replaced, and the view jumps away. A following delete or copy acts on the replaced selection. It still acts on real paths and the row is correct, but it is not what the user chose. This is the "selection or anchor jumping" the brief rules out. A real wheel event does settle (`rRealWheelSettles` passes), so the wheel path is fine.

**Fix.** Call `panel.settleByUser()` at the top of `Keys.onPressed`, and add `UiScrollBar { onPressedChanged: if (pressed) panel.settleByUser() }`. Right-click (`openGridMenu`) also selects, so settle there too. Add a test for each. Verified locally in two runs, and the five shipped `perf_enum_ui` tests passed in both:

- With the settle changes and the B4 change, the keyboard and Ctrl+A tests pass. The scrollbar test no longer jumps to the photo, but still misses its anchor because of B2.
- With the B2 `forceLayout` added as well, the scrollbar test passes.

The right-click change was not tried.

### B2. The viewport restore at the definitive order reads geometry the view has not laid out yet (high)

`ui/PhotoGrid.qml:256-260`, `:152-176`; `src/app/directory_entry_model.cpp:217-231`.

The last rows of an arrival and the definitive order are published in one synchronous call when they fall in the same drain tick. GridView applies model changes at polish, so `rememberViewport` and `restoreViewport` see `originY`, `contentY` and `contentHeight` from before those inserts while `pathAt` and `rowOf` already reflect them. That gives two visible results:

- **Folders in the last tick:** `first` points at the wrong row, and the view lands off the anchor by the number of folders inserted in that tick.
- **An anchor beyond the stale `contentHeight`:** the restore is clamped short of its row.

```text
rFolderInsertsKeepTheAnchor: columns 6 final anchor row 2518 top 2664   FAIL (24 lines off, twice in two runs)
scrollbar anchor, B1 fix only: anchor row 2383 top row 1128             (clamped)
```

`scrollDuringArrivalKeepsTheVisibleRow` misses both. Its rows are all files, and its comment says it picks an anchor "rather than clamped at either end".

**Fix.** Lay out first: `function onLayoutAboutToBeChanged() { grid.forceLayout(); panel.rememberViewport() }`. Verified locally: `rFolderInsertsKeepTheAnchor` gives anchor 2518, top 2514 (same line), and the scrollbar case gives anchor 2383, top 2382. Add both as committed tests: mixed folder rows with a scrolled view, and an anchor that lands near the end.

The final position is the oracle. The in-arrival drift the scratch test logs is informational: its 4 ms sampler can fire between an insert and the view's polish. It read 0 in most runs and 32 lines in one, which is about what the remaining 198 folder rows in 6 columns give if they land in one tick. I attribute that reading to the sampler and the same unpolished state, without proving it. It is not asserted in the patch.

### B3. Try again on a large or slow folder loses the browsed position (medium, regression)

`ui/PhotoGrid.qml:152-157`, `:256-260`; `src/app/directory_entry_model.cpp:145-149`.

`rememberViewport` deliberately keeps the viewport saved before a failed relisting, and `reviewRetryRestoresViewportAfterFailure` holds that for 300 rows, which take the quick path. A recovery that arrives has no reset, so nothing restores the saved viewport. `onLayoutAboutToBeChanged` then overwrites it with the current position at the top. A transient share failure is the case the rule exists for, and that share is also likely to pass the 1 s floor.

```text
rRetryQuickPathRestoresViewport: arrival false anchor row 1488 top row after retry 1488   PASS
rRetryArrivalRestoresViewport:   arrival true  anchor row 1488 top row after retry 0      FAIL
```

**Fix.** When an arrival starts with `count === 0` and a saved viewport for this folder, keep that viewport aside. `onLayoutAboutToBeChanged` must not overwrite it. Restore it when `arriving` clears, whether or not a layout change fired, and drop it if the user settles first. This fix was not tried locally. Add the arrival variant of the retry test, covering both a changed and an unchanged order.

### B4. A file renamed while the grid was hidden loses the cursor (medium, regression)

`ui/PhotoGrid.qml:142`, `:148`; `src/app/directory_entry_model.cpp:315-323`.

`reveal()` now runs at the definitive order, before the identity baseline lands. The cursor's old key has no row yet, so `cursorRow` is -1 and `reveal()` runs `select(0)`, replacing the selection and cursor. When the baseline lands, `apply()` has nothing left to carry across the rename. On `main`, `reveal()` waits for `!loading`, after the rename is applied.

```text
rRenameWhileHiddenKeepsTheCursor: during the probe window QList(".../p1.png")
                                  after the baseline QList(".../p1.png")      FAIL (expected .../renamed.png)
```

The renamed-folder path restores the previous selection in the same `apply()` block, so it should have a related fault: the cursor comes back, but after `select(0)` has already scrolled to the top. That part is from reading the code and was not run.

**Fix.** Only the revealed photo may be placed while the baseline is outstanding: `if (fileGrid.loading && row < 0) return`, and settle on `fileGrid.loading && placeCursor`. `onLoadingChanged` already re-runs `reveal()`. Verified locally: the test passes with `renamed.png`, and the shipped UI tests still pass. The "so far" label stays on `arriving`.

### B5. A lost wakeup can leave `loading` true and refresh dead for that folder (medium, low probability)

`src/app/directory_entry_model.cpp:188`, `:215`, `:232`, `:241`.

`publishBatches` computes `dry` from the budget clock, then `publishDefinitive` stops the drain timer. If `finished` is already set, which is normal at 100k because the worker ends while rows still drain, and the clock passes 6 ms between the loop check and `dry`, `publishBatches` returns false. `drain()` returns without publishing the finished scan, and nothing will call it again. `loading` stays true. `arrivals` stays set, so every `refresh()` only sets `refreshAfterArrival` and the folder stops following disk changes until the user changes folder or sort. `arriving` does clear.

The window is narrow: it needs preemption or a boundary crossing at the wrong moment. I reproduced it only by injecting 7 ms there, on a backlog built the way a large folder builds one:

```text
no delay, five runs: rFinishedScanIsNotLostAtTheDefinitiveBatch PASS x5
7 ms injected:       FAIL! '!model.loading()' returned FALSE.
7 ms injected, return dry || definitive.has_value(): PASS
```

**Fix.** The definitive batch is always the end of the queue, so `publishBatches` must return true once it has published it. Keep a seam or test that holds the event loop until the worker has finished.

## Required correction to the evidence

**C1. The 10k before and after numbers are measured but not like for like, and overstate the gain about threefold.** `docs/handoff/A1-HANDOFF.md:12`, `:23-24`; `tests/perf_enum_tests.cpp:148`.

The "before" numbers (1465 and 1836 ms) and the "after" numbers (51 and 52 ms) are real command output from different runs. Only the after run is stated to be under the gate lock. I built `main`'s model (`3a14044`) into a small trace target on the same machine and ran it back to back with `perf_enum_tests` under one gate lock:

```text
BASE_TRACE main@3a14044 10k first_row_ms=566 811 923 686 (round 1)   525 552 585 538 (round 2)
          loading_false_ms=582 826 939 701 (round 1)   540 566 600 553 (round 2)
A1 trace10k first_row_ms=51 41 (round 1)   52 51 (round 2)
          settled_ms=461 460 (round 1)   461 512 (round 2)
```

So at 10k, time to first row falls from about 530 to 920 ms to about 50 ms, roughly tenfold, not from 1.5 to 1.8 s. Time to `loading` false barely moves: about 550 ms to about 460 ms in the quieter round. The change is real and meets R1, but the handoff line "instead of after 1.3 to 1.8 s" and the test comment need the like-for-like numbers. The 100k "before" was not rerun. A 100k after run here gave first row 141 and 140 ms, definitive order 552 and 546 ms, `loading` false 568 and 559 ms, matching the handoff.

The base trace differs slightly from `perf_enum_tests`: same mix of names, no heartbeat and no recorder. Neither explains a threefold gap, since enumeration, sort and probes are unchanged in it, and those are all it runs.

## Non-blocking

Fix these in the same round if convenient.

- **N1. "1 items so far" and "1 matches so far".** `ui/PhotoGrid.qml:330` drops the singular handling that `:331-332` keeps. It is easy to reach with a narrow filter during arrival or past the 1 s floor on a slow share. Add two translation entries.
- **N2. `cursorRow` is stale in bindings during arrival and the probe window.** `reorder()` and folder inserts move the cursor's row without `selectionChanged` until `apply()`, so `photoGrid.selectedIndex` and the "N of M" label in `ui/Main.qml:2027` can be wrong for that time. It is cosmetic: `activate()`, `deletionTargets()` and the menu read live values or cached paths, and I found no wrong-file path. Emit `selectionChanged` after a folder insert when the cursor is set, and in `publishDefinitive`. From reading the code.
- **N3. A deleted row stays visible until the probes end.** A refresh in the probe window is coalesced by design. A delete there leaves the row showing until the baseline finishes, and on a slow share that could be long. It fails safe, since the path is gone, but consider restarting the scan once the definitive order is out.
- **N4. Synchronous reentrancy could publish a stale definitive.** `publishBatches` holds `definitive` across `emit countChanged`, so a slot that changes folder synchronously would publish the old listing into the new folder. No caller does this today (QML uses `Qt.callLater`, and the C++ wiring has no such slot). Guard with a generation check, or by checking `arrivals` is unchanged, before `publishDefinitive`.
- **N5. Weak oracle.** `trace10k`'s `firstRowMs < 500` caught my probes-first revert only because this machine was loaded (760 and 867 ms). On a quiet run, probes-first would sit near 500. Compare against `settledMs`, or rely on `definitiveOrderDoesNotWaitForIdentityProbes`.
- **N6. `fs_watch` publication stats.** `publishFinished` clears `mergeMs`, `resetMs` and `projectionMs` recorded by `publishDefinitive`, so the 100k log line shows zero for them. The oracles use `totalMs`, which adds the definitive share, so no budget is weakened. Only the log line misleads.
- **N7. PR description.** The table in PR 7's body still shows the earlier run. Update it with C1.

## Lead's conditions on the `arriving` grant

| Condition | Result |
| --- | --- |
| Delete, rename, copy or move before the probe lands acts on the right file, with tests for a mutation in the window and a real rename on disk | **Met.** Selection, `deletionTargets()` and `gridMenu.targets` are paths, read live or fixed at menu open. `renameOnDiskDuringTheProbeWindowKeepsPathsRight` and `deleteDuringTheProbeWindowDropsTheRow` run on real files. B4 is a different window, a rename while hidden. |
| Probes off the GUI thread, carrying the generation, bounded, cancelled by a newer open; stale probes cannot change a newer listing | **Met.** The worker checks the token per file, the byte budget holds, and `staleProbesCannotChangeANewerListing` passes. |
| `arriving` always clears: success, failure, denied, empty, cancellation, filter change, each tested | **Met.** `arrivingAlwaysClears`, read against every exit from the arrival path. B5 sticks `loading`, not `arriving`. |
| 10k and 100k first row and `loading` false recorded before and after | **Recorded; correct the 10k comparison** (C1). |

## Checked and clean

- **Generation fence:** `abandonScan` bumps the generation, requests stop and drops the queue and pending result; a stale watcher result is dropped.
- **Bounds:** 16,384 queued rows, plus at most about one batch per side. Reproduced "worker stopped after 1279 entries with a 1024-row queue". The entry cap is unchanged at 100k. The definitive batch counts as zero rows but occurs once.
- **Lifetime:** the job owns its request by value and shares the queue by `shared_ptr`. A blocked push wakes on stop. The destructor waits 1 s, then abandons. Reproduced 14 ms destruction during arrival.
- **GUI thread:** no decode, scan or stat added. Sort, keys, index and probes are all on the worker.
- **Sort parity:** `nameKey` and `compareKeys` match `compareNames` by reading (digit-run marker, length, significant digits, zero-only runs, end-of-string rule), and `sortKeysMatchTheViewersOrder` passes. `enumerate()` matches `listDirectory` including the truncation and failure branches.
- **Model semantics:** `reorder()` uses `VerticalSortHint` with correct persistent remapping. The provisional segment index is exact.
- **Consumers:** this model does not feed next/previous or the filmstrip. `perf_replay` gates on `loading`. `fs_watch`, `recovery` and `viewer_acceptance` tests pass with no test edits.
- **Budgets:** no edits to `fs_watch_budget`, `folder_grid`, `directory_model` or `cache` tests.
- **Fixed sleeps:** the four in new tests are all `sleep-ok` negative assertions or worker-settling waits. No plain 5 s `QTRY_*`.
- **Grant:** the `git diff --name-only` list matches the handoff. `pr_guard` reports 0 errors and the size notice.

## Fail-before spot checks

Each hunk was reverted locally, rebuilt, run and restored. All fail as the handoff claims.

```text
generation fence (token != generation dropped) -> aFinishedOlderListingCannotPublishIntoANewerOne: exit 1, '!model.loading() && model.count() == 10' returned FALSE
refresh coalescing dropped                     -> refreshDuringArrivalFinishesThenRefreshes: exit 1, Compared values are not the same
probes before the definitive order             -> trace10k: exit 1, 10k first row took 867 ms
settleByUser removed from clickRow             -> clickDuringArrivalSurvivesTheDefinitiveOrder: exit 1, Compared values are not the same
```

## ctest output

`e425a90`, unmodified, under `scripts/gate-lock.ps1`:

```text
 1/13 Test  #7: viewer_acceptance ................   Passed   15.36 sec
 2/13 Test #12: folder_grid ......................   Passed  276.66 sec
 3/13 Test #16: directory_model ..................   Passed   47.47 sec
 4/13 Test #23: fs_watch .........................   Passed   92.64 sec
 5/13 Test #24: fs_watch_budget ..................   Passed  126.16 sec
 6/13 Test #26: fs_watch_ui ......................   Passed    4.76 sec
 7/13 Test #40: recovery .........................   Passed   15.54 sec
 8/13 Test #41: recovery_startup .................   Passed    0.87 sec
 9/13 Test #42: recovery_controller ..............   Passed   16.62 sec
10/13 Test #43: recovery_process .................   Passed   36.09 sec
11/13 Test #44: tabs_recovery ....................   Passed   69.80 sec
12/13 Test #50: perf_enum ........................   Passed   77.07 sec
13/13 Test #51: perf_enum_ui .....................   Passed   20.21 sec
100% tests passed, 0 tests failed out of 13
Total Test time (real) = 799.31 sec
```

`perf_enum` traces from that run:

```text
PERF_ENUM_TRACE 10k-cold first_row_ms=73 full_count_ms=73 definitive_ms=74 settled_ms=920 worst_gui_gap_ms=12
PERF_ENUM_TRACE 10k-warm first_row_ms=51 full_count_ms=51 definitive_ms=52 settled_ms=919 worst_gui_gap_ms=13
PERF_ENUM_TRACE 100k-cold first_row_ms=141 full_count_ms=467 definitive_ms=552 settled_ms=568 worst_gui_gap_ms=14 inserts=23 largest_insert=8192
PERF_ENUM_TRACE 100k-warm first_row_ms=140 full_count_ms=458 definitive_ms=546 settled_ms=559 worst_gui_gap_ms=17 inserts=21 largest_insert=10240
```

The full suite was not run here: the hosted `viewer` job passed on `e425a90`, and the tester's gate covers the merge head.

## Not verified

- A real slow or dead network share. The scenarios above use the seam.
- Physical hardware and a real display.
- The B3 fix, and the renamed-folder case in B4.
