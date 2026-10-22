**Verdict: not accepted.** The patch fixes the failing `K,S` case, but it breaks default type-to-search in the grid.

**What it gets right**
- The failing case works. `K` arms the chord, and `S` as the second step reaches `sort-reverse`.
- Space keeps native priority, because the `printable` range starts at 0x21 (`ui/Main.qml:415`).
- Arrows, Enter, Backspace, Escape and Page keys stay native unless they are explicitly remapped to a grid or tree command.
- Ctrl, Alt and Meta combinations are excluded from the new rule.
- The tree's yield list at `ui/FolderTree.qml:530` is unchanged.

**The regression**
`paneCommand` now returns true for any scope when the key is printable (`ui/Main.qml:417`). `PhotoGrid.qml:514` then declines to accept the ShortcutOverride, so the window shortcut fires instead of type-to-search.

The default registry has global, single-step, unmodified printable bindings (`src/app/viewer_controller.cpp:94`):
- `grid` on `G`
- `sort` on `S`
- `sort-reverse` on `Shift+S`

These bindings were global, so they used to lose to type-to-search in the grid. Now `g` or `s` typed in the grid toggles the grid or cycles the sort. This happens even in the middle of a query: typing "vacation" is cut off at the "s" in "gas"-style names, or at any "g" or "s".

That contradicts the GRID-SEARCH-01 acceptance criterion ("typing alphanumeric characters in grid starts instant search"). It also fails the check you asked for, that unbound type-search is preserved.

**Test gap**
No test covers typing `g` or `s` in the grid. `tests/grid_search_tests.cpp` only tests the model's `findMatch`. So your 4/0 targeted result and a full gate would likely both stay green despite the regression.

**Required corrections**
1. Narrow the new rule in `remapped()` to chord steps only. Extend `paneCommand` for printable keys only when `chordPending` is true (the second step), or when the first key starts a two-step command (`commandInfo[id].steps.length === 2`). Single-step global printable bindings must keep yielding to type-to-search, as before.
2. Optionally, have `PhotoGrid.qml:514` stop yielding printable non-pane commands while `panel.searchQuery.length > 0`. That way a chord starter such as `K` can't interrupt a query that is already under way.
3. Add a test in a new per-feature test file, not `tests/grid_ui_tests.cpp`:
   - With the default map and the grid focused, `G` and `S` extend `searchQuery` and do not change `gridMode` or the sort.
   - The `K,S` chord still works.

I did not read the UI design skills. This change is key routing with no visual change, so I treated them as not applicable. I ran no builds or tests, and I make no full-gate claim.
