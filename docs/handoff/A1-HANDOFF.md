# A1 handoff: incremental cancellable folder listing

PR 1 of 2 for A1 (PERF-ENUM + F016). The continuation beyond the 100k cap ("Show more") is the stacked PR 2, as the lead approved. It has its own section in this file on its branch.

- **Branch:** `claude/a1-perf-enum`
- **Code tip:** `43be88d`, after the first review's fixes. The commit after it adds only this file and `docs/verification/A1/`. The last local full suite was at `902a765`. By the lead's decision the PR's hosted `viewer` job is now the full suite.
- **Exact base:** `3a14044` (`origin/main`, not stacked). `main` had not moved when this was written.
- **PR:** https://github.com/dvortsis/glancelot/pull/7

## What a user sees

- A 10k folder shows rows at about 50 ms instead of about 530 to 920 ms. A 100k folder shows them at about 140 ms. The count reads "N items so far" until the order is final.
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
| **N5.** Weak oracle in `trace10k` | First row compared with the end of loading, not with a fixed 500 ms | `trace10k` |
| **N7.** PR description | Updated with C1 | None |

**Both `forceLayout` calls stay, because they fix different paths.** B2's runs before the position is read, and folders landing above the view make that read wrong without it. Mine runs before `contentHeight` is read when the viewport is restored. When the order is unchanged no layout signal fires, so B2's never runs on that path, and Try again would clamp short. Each has a mutation that fails only its own test.

**Not done, from the non-blocking list:**
- **N3** (a deleted row stays visible until the probes end). It fails safe, and restarting a scan changes the cancellation behaviour the review just cleared, so I'd rather not add that in a fix round.
- **N4** (a slot that changes folder synchronously in `countChanged`). No caller does this today, and I could not build a test that fails reliably without a further seam.
- **N6** (`fs_watch` log line shows zeros). No oracle reads it.

## Files changed

Grant check, `git diff --name-only 3a14044..43be88d`:

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
  - The key handler, the scrollbar and the right-click handler settle the placement, and Try again keeps the browsed position (first review).
  - The count label says "so far".
  - The thumbnail binding also depends on `cell.path`, so a delegate kept in place rebinds.
- `translations/glancelot_en_XA.ts`: four new strings, added by hand because `lupdate` is not available on Windows here.

`ui/Main.qml` and `src/core/*` are untouched. `pr_guard` reports 0 errors and the notice `2286 changed lines; aim for about 1500`. About 1,300 of those lines are tests. I did not split further because the arrival code and its tests only make sense together.

## Tests

**After the first review, at `43be88d`, in build slots.** No budget oracle changed, and by the lead's decision no local full suite ran. Three runs, because the first two were on a heavily loaded machine (`perf_enum` took 228 s against 63 s on a quieter one):

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
- **First review's fixes:** `docs/verification/A1/mutations-review.txt`. 20 mutations, all of which fail their named tests. One survived its first run (`b3-failing-again-forgets`), because its test failed before any row arrived. The test now fails with rows showing.
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
- **Going further than the review's B4 suggestion.** It suggested `if (fileGrid.loading && row < 0) return`. That also holds back a cursor that already has its row, so re-entering a 10k folder would leave the view at the top for about 400 ms and then jump. Only a cursor with no row waits now.
- **A key or click with no rows yet settles nothing.** Pressing the grid key and then an arrow straight away would otherwise cancel the placement on the photo, with nothing to show for it. The review's suggestion did not cover this.
- **A modifier alone settles nothing,** because pressing Ctrl on its way to Ctrl+A has not chosen anything.

## Open questions for the lead

- **Selections made from the app menu during arrival** still lose to the placement, because settling them needs a change in `ui/Main.qml`. Do you want that grant, or is it fine as a follow-up? The window is short except on a slow share.
- **Sorting or toggling hidden files in a truncated folder looks like it does nothing.** `publishRows` skips a truncated listing while rows are showing, so the new order would not appear. `main` has the same condition (`if (!truncated || (filtering() ? rawListing.rows.empty() : rows.empty()))`), so A1 did not cause it. I read this from the code and did not reproduce it. It is outside this packet, so I left it. It should be a small fix once you decide where it goes.

## Not verified

- **A selection made from the app menu during arrival.** `select-all`, `clear-selection` and `invert-selection` in `ui/Main.qml` do not settle the placement, so the placement can still replace them. `ui/Main.qml` is outside this packet's grant. The keyboard shortcuts and the grid's right-click are covered.
- A slow or dead network share. The patience, floor and flush paths are covered through the seam only.
- Physical hardware and a real display. All UI evidence is offscreen with the software backend.
- `docker build` was not part of my run, and no local full suite ran after `902a765`. The hosted `viewer` job and the lead's tester cover the head.

## UI skills

`application-interface-design` was read before the QML edits. The only visible change in this PR is the "so far" wording in the existing count label, with no new control or layout. `layout-spacing`, `component-geometry`, `responsive-layout` and `typography-ui` were read for the banner in PR 2.
