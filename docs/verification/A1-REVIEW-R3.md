# A1 third review: incremental cancellable folder listing

**Verdict: INCOMPLETE, paused**

- **Reviewed head:** `5018b8fb86979b6003f688b0d81fd0ecff1db105` (`claude/a1-perf-enum`, PR 7). Code tip `bc7b33e`. It includes merge `c353d26` of `main` at `20f1210`. Packet base `3a14044`. Round 2 reviewed `ac49031`.
- **Reviewer:** fresh context. I wrote neither this code nor the earlier reviews. Windows 11, MSVC, Qt 6.8.3, offscreen with the software backend. Worktree `.worktrees/review-a1-r3`, rebuilt from an empty `build-review-a1-r3`.
- **UI skills:** `application-interface-design` loaded. The QML changes add no layout, geometry, icon or motion work, so the other skills did not apply.

The user paused all work at 5 pm. This document records what was verified before then. No status is posted on it.

## What is verified so far

### Round-2 findings, by reading `git diff ac49031 5018b8f`

| Finding | Status | Evidence |
| --- | --- | --- |
| R2-B1, the Clear and Invert buttons | Fixed in code | `panel.settleByUser()` is first in both `onClicked` handlers. The accessible press got the same fix. |
| R2-B2, the first frame after the definitive order | Fixed in code for `layoutChanged` | `onLayoutChanged` calls `panel.restoreViewport(saved)` directly. See finding 1 for the filtered path. |
| R2-B3, Escape from type-ahead | Fixed in code | `preSearchPath` replaces `preSearchRow` and is looked up with `rowOf` on Escape. |
| R2-N1, stale `cursorRow` | Fixed in code | One `selectionChanged` after both of a tick's inserts, when `rowOfKey(cursorKey)` changed. |
| R2-N2, the "does not settle" half | Covered | `otherCommandsDuringArrivalKeepThePlacement`, and the menu test drives `window.invoke`. |
| R2-N3, R2-N4 | Left open with reasons | I accept both reasons. |
| Question 2, the footer | Done under the lead's grant | Two branches added to the one expression at `ui/Main.qml:2152`, with strings in `en_XA`. |

### The merge `c353d26`

`git show --remerge-diff c353d26 -- src ui include tests cmake CMakeLists.txt translations` prints nothing, so the merge resolved no conflict in code.

### Selection totals cache, by reading

The totals are cleared on `selectionChanged`, `countChanged`, `directoryChanged`, `rowsInserted`, `rowsRemoved`, `modelReset`, `layoutChanged` and `dataChanged`. They are connected in `Private`'s constructor, before anything outside the model can connect. I traced every write to `rows`, `index`, `selected`, `accessible`, `provisional`, `provisionalFolders` and row sizes:

- `select`, `selectAll`, `clearSelection` and `invertSelection` emit `selectionChanged` after every write.
- `applyFilter` and `release` reset the model.
- `setDirectory` emits `directoryChanged` and `selectionChanged`.
- In `publishBatches`, `admit` writes the index ahead of the rows, but no signal fires until the inserts. The inserts run inside `ChangingRows`, so readers there walk the rows and nothing is kept.
- `merge` runs inside `ChangingRows`. Its final index swap in `updateMetadata` comes after the last row signal, but a reader inside the merge keeps nothing. After it, `countChanged` fires (`publishDefinitive`, sizes differ) or `apply` ends with `selectionChanged`.
- `apply()` changes `selected` (renames, prune) and `accessible`, then emits `arrivingChanged` and `loadingChanged` before its final `selectionChanged`. Those two are not forget signals. A reader there gets a value computed after the row changes, and the rename mapping runs before `publishRows`, so by reading the value is current.

I found no stale path by reading. The scratch tests below were written to check it.

### `pr_guard`

```text
python scripts/ci/pr_guard.py --base 20f1210 --head-ref 5018b8fb86979b6003f688b0d81fd0ecff1db105
::notice::2757 changed lines; aim for about 1500
pr_guard: code=True, 0 error(s)
```

## Open findings

1. **Filtered definitive order (candidate R2-B2 class).** With a text or kind filter active, `publishRows` publishes through `applyFilter`, which is a model reset. `ui/PhotoGrid.qml:310` restores through `Qt.callLater(panel.restoreViewport, panel.resetViewport)`, so a frame can be drawn first. Scratch test: `zFilteredDefinitiveOrderFirstFrame`.
2. **`selectedCount` lags while a kept selection arrives.** `publishBatches` emits `selectionChanged` only when the cursor's row moves. Kept selected rows that arrive in other ticks change the count, bytes and summary with no notify. The chip (`PhotoGrid.qml:503`), Clear and Invert, and the footer read through that notify. Actions read live values: `deletionTargets()` and `selectedPaths()`, and the trash confirmation counts its targets. Scratch tests: `zSelectedCountNotifiedAsSelectedRowsArrive` (model) and `zKeptSelectionCountDuringArrival` (UI).
3. **Footer number format (non-blocking).** The arriving branches format with `toLocaleString` ("1,024 selected of 1,024 so far"). The settled branch does not ("1024 selected"), so the same count changes its format when the listing lands.
4. **Selection cost at a million rows (carry-forward).** `kMaxEntries` is 100,000 in this PR. Every cache miss costs one lookup per selected key, measured by the implementer at about 18 ms with all 100k selected. That is about 180 ms at a million, which is PR 2's concern. No QML binding reads `fileGrid.selectedCount` or `selectedSummary` with a dependency on `count`, so arrival ticks do not force misses. `Main.qml:2163` also depends on `viewer.status`.

## Not yet run

To be filled in: targeted `ctest`, `folder_grid` under the gate lock, the scratch tests, and the mutation spot-checks.
