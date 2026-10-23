# EN-04 shared test harness wiring: re-review (round 2)

## Verdict: APPROVED

The three round 1 regressions are fixed without weakening what the tests prove. The `ui/FolderTree.qml` change stays inside the lead's grant (Favorites sizing only) and is correct at every size checked. The new regression test fails on the old QML and passes on the new one. All five targeted suites pass on this machine. Nothing below blocks the merge. The integration notes at the end are for the lead.

| Item | Value |
| --- | --- |
| Reviewer | Fresh-context Claude (Opus) re-reviewer. It implemented no part of EN-04 and did not write the round 1 review. |
| Machine | The lead's Windows PC, MSVC 2022, Qt 6.8.3 |
| Packet | `claude/en-04-harness-wiring` at `fc9b5f1` (base `afeac1a`) |
| Round 1 review | `docs/verification/EN-04-REVIEW.md` on `claude/review-en-04` (`211e81b`) |
| Skills read | ui-design-skills: application-interface-design, layout-spacing, layout-grids, responsive-layout, design-review |

## 1. Round 1 test changes: no weakening

1. **`GridUiTests::deleteBrowsingFolderFromTreeVisitsNearestAncestor` (still proves the same thing).**
   - Before: two Down presses, which assumed "Open folder" was the first enabled row.
   - Now: the test finds `menuItem-delete-selection` by identity and checks it is enabled. It presses Down until that row is highlighted (at most 4 presses), checks the highlight, then presses Return. The rest is unchanged: the confirmation, the scope text and the nearest-ancestor navigation.
   - The test still drives the delete through the keyboard path from the tree row menu.
   - **Low:** the old comment said the destructive row "stays last". The old code never asserted that, and the new code does not either. No other test pins the row order. If the lead wants that INPUT-02 rule enforced, add one check that no enabled menu item comes after `menuItem-delete-selection`. Optional.
2. **`InputUiTests::treeRowAndPathBarMenusOpenAndMatch` (stricter).**
   - Before: it right-clicked `namedAll("folderRow").first()`, which could be any cached delegate.
   - Now: it picks row 0 by `index` and asserts that row 0 is not the cursor row. It scrolls the row into the clipped viewport with `positionViewAtIndex(..., ListView.Contain)` and waits until the row centre is inside the list.
   - Two new checks prove the click chose that row: `folderRowMenu.path` equals the row's path, and `folderList.currentIndex` equals 0. The `menuProblems` audit is unchanged.
3. **`GridUiTests::treeRowClickEnterAndChevron` (unchanged).** `git diff afeac1a fc9b5f1 -- tests/grid_ui_tests.cpp` touches only the Harness wiring block and the delete test. The test passes again because the folder list has its height back.

## 2. `ui/FolderTree.qml` change

The diff has two hunks and touches only Favorites sizing, as the grant allows.

- `favoritesSection` gets `Layout.fillHeight: false`. The cause is right: a nested `ColumnLayout` fills by default, and `favHeaderArea` (`Layout.fillHeight: true`) gave it no maximum height. The section therefore shared the spare panel height with `folderList`.
- `favoritesList` caps at `maxRows = max(1, min(5, floor(panel.height / (5 * Theme.rowHeight))))` whole rows. This replaces the literal 144 px, which cut a row in half. A longer list scrolls in place, with its existing `clip` and `UiScrollBar`.
- **Binding loop: none.** `maxRows` reads `panel.height`. In Main.qml that height comes from `app.paneRect("tree").height`, and for a detached tree from the host's `paneContent.height`. No host sizes the panel from `implicitHeight`, so it cannot feed back. No results file contains `Binding loop` or `Polish loop`, and `input_ui` fails on binding-loop warnings.
- **Sizes and modes.**

| Mode | Panel height | Result |
| --- | --- | --- |
| Hidden | 0 | `maxRows` is 1, but the panel is invisible and its model is null. |
| Pinned, 1180x780 | 684 px | `maxRows` 4. Fixer's measurements: 0 favorites, section 83 and list 453. 12 favorites, favorites list 112 (scrolls) and folder list 379. |
| Overlay, 540x360 (width under 720) | 264 px | `maxRows` 1. Fixer's measurements: folder list 33 to 43 px. The 540x360 runs of the test are this mode. |
| Detached | at least 280 px (window `minimumHeight`) | `maxRows` 2. Not run; reasoned from `DetachedPaneHost.qml`. |

- **Keyboard and accessibility: unchanged.** The Favorites keys (`Keys.onShortcutOverride`, Up/Down, Alt+Up/Down reorder, Return, Delete, Menu) are untouched. So are `Accessible.role`/`Accessible.name` and the tab order. `ListView` keeps the current item in view while the arrow keys move it, so a one-row list should still reach every favorite. That is standard Qt behaviour; I did not run it.
- **Theme tokens: unchanged.** `Theme.rowHeight` is the only token used. No colour, spacing, radius or type values changed, and the hard-coded 144 went away.
- **Hover highlight.** `favHeaderArea` keeps `Layout.fillHeight: true` inside a header `RowLayout` that is `Theme.controlHeight` tall. The hover rectangle is therefore bounded to the header row, at `Theme.radiusSmall`, as FAVORITES-01 intended. Before the fix it filled the stretched section, so the hover surface was hundreds of pixels tall. No committed screenshot shows hover. This finding comes from the code.

## 3. New regression test fails on the old QML

I made a scratch worktree at `fc9b5f1` (`.worktrees/review-en-04-r2-revert`, removed afterwards) and ran `git checkout afeac1a -- ui/FolderTree.qml` in it. I built only `input_ui_tests` in its own build directory and ran the one function under the gate lock with `QT_QPA_PLATFORM=offscreen` and `QT_QUICK_BACKEND=software`. I then restored the file and ran it again:

```text
--- FolderTree.qml from afeac1a
FAIL!  : InputUiTests::favoritesSectionLeavesFolderListTheHeight() 'qAbs(section->height() - section->implicitHeight()) < 1' returned FALSE. (0 favorites at 1180x780: panel 684, favorites section 530 (content 81), favorites list 0 (content 0), folder list 6)
...\tests\input_ui_tests.cpp(1224) : failure location
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 2588ms
--- FolderTree.qml restored to fc9b5f1
PASS   : InputUiTests::favoritesSectionLeavesFolderListTheHeight()
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 2622ms
```

The fixer's worktree `.worktrees/claude-en-04` was not modified. After the runs it was still clean at `fc9b5f1`.

The test covers 0, 2 and 12 favorites at 1180x780 and 540x360, and its checks are the right ones:
- The section height equals its content.
- The favorites list is between one row and `count` rows tall, and 12 favorites scroll.
- The folder list is at least half the panel at the default size, and at least one row at the minimum size.

## 4. Design review (screenshots in `docs/verification/EN-04/`)

The screenshots are panel crops of the shipped QML in the `input_ui` harness, with two favorites, in dark and light. Theme parity holds: both themes show the same geometry and keep their tokens.

**Blocking:** none. The before images (1180x780) show the defect: the Favorites header floats mid-panel, there are large blank gaps and no folder rows. It is a real product bug from FAVORITES-01 that also affects the app. The fix removes it.

**Important (follow-up, not blocking):**
- **What:** at the minimum size (540x360) the Favorites list shows one whole row of two, and nothing shows that a second row exists. `UiScrollBar` stays at opacity 0 unless it is `active` (scrolling or hovered), and whole-row capping removed the half-row peek the old 144 px cap gave by accident. The only cue is the "(2)" count in the header. The handoff says "the visible scroll bar shows that more rows exist", but that is not true at rest.
- **Why:** responsive-layout says nothing essential may be unreachable or depend on hover. The rows can be reached with the wheel and the keyboard, so this is discoverability, not access.
- **Fix:** make the cap fractional when `count > maxRows`, for example `(maxRows - 1) * rowHeight + rowHeight / 2` with at least half a row, so a partial row shows that the list scrolls. Then check that the minimum-size folder list still keeps at least one row. Another option is to show `UiScrollBar` with `policy: ScrollBar.AlwaysOn` while the list overflows, which changes the scroll bar rather than sizing. Both are small, but the second goes beyond the current grant.

**Polish:**
- At 540x360 the panel is 264 px and is mostly chrome: the Folders header, Favorites, All locations, the address field and the back/forward/Copy row. That leaves 1 to 1.5 folder rows. The test's floor of one row keeps the panel usable. A later compact mode could collapse Favorites by default, or merge the two address rows, when the panel is under about 300 px. Outside this grant.
- The Back and Forward buttons narrow the address field to its tail (`.tests-RGmSKH`). The handoff already records this as unchanged and outside the grant.

**Proportions (layout-grids: "important regions receive appropriate area"):**
- At 1180x780 the folder list has 435 to 453 of 684 px (64 to 66 %). The Favorites section is 83 to 157 px.
- The primary region now dominates, and the secondary shortcut list is bounded. Leading edges are unchanged: the Favorites rows and the header share the `Theme.gapSm` inset, as before.

**Strengths:**
- The root cause was fixed in the layout, not patched in the tests.
- The cap is relative to the panel, so it scales from the minimum window to tall windows.
- Whole rows give a clean list edge.
- The regression test measures proportions at both sizes, not just pixels.

**Highest-leverage change:** a scroll cue at the minimum size (the half-row peek above).

## 5. Windows evidence

The build `.worktrees/claude-en-04/build-claude-en-04` is current at `fc9b5f1`. `scripts/build.ps1 -BuildDir build-claude-en-04` exited 0, then `cmake --build build-claude-en-04 -- -n` printed `ninja: no work to do.`

The targeted suites ran under the gate lock: `gate-lock.ps1 -Branch review/en-04-r2 -Run { ... ctest --test-dir build-claude-en-04 -R '^(folder_grid|input_ui|pathbar_ui|fs_watch_ui|monitor_customization_ui)$' --output-on-failure }`. They waited for the cycle1-b gate and ran at 23:44-23:50.

```text
1/5 Test #12: folder_grid ......................   Passed  203.53 sec
2/5 Test #13: input_ui .........................   Passed   75.31 sec
3/5 Test #14: pathbar_ui .......................   Passed    3.80 sec
4/5 Test #25: fs_watch_ui ......................   Passed    3.15 sec
5/5 Test #43: monitor_customization_ui .........   Passed   23.27 sec
100% tests passed out of 5
folder_grid:              Totals: 74 passed, 0 failed, 4 skipped, 0 blacklisted, 203447ms
input_ui:                 Totals: 33 passed, 0 failed, 1 skipped, 0 blacklisted, 75217ms
pathbar_ui:               Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 3651ms
monitor_customization_ui: Totals: 13 passed, 0 failed, 1 skipped, 0 blacklisted, 23142ms
PASS   : GridUiTests::treeRowClickEnterAndChevron()
PASS   : GridUiTests::deleteBrowsingFolderFromTreeVisitsNearestAncestor()
PASS   : InputUiTests::treeRowAndPathBarMenusOpenAndMatch()
PASS   : InputUiTests::favoritesSectionLeavesFolderListTheHeight()
```

`fs_watch_ui` writes no results file. No results file contains `Binding loop`, `Polish loop`, `TypeError`, `ReferenceError` or a `FolderTree.qml` warning.

**Independent captures.** I captured my own screenshots so as not to rely on the fixer's images. I ran `input_ui_tests.exe favoritesSectionLeavesFolderListTheHeight` from the same build under the gate lock, on the native Windows platform with `GLANCELOT_REVIEW_CAPTURE` set. It exited 0 and wrote eight images: 0 and 2 favorites, dark and light, 1180x780 and 540x360. They are not committed, because they show this PC's real drive listing.
- They match the committed "after" images.
- With 0 favorites, the empty-state hint ("No favorite folders yet...") wraps to two lines, and the folder list still gets its height: about 15 rows at 1180x780 and about one row at 540x360.
- With 2 favorites at 540x360, one favorite row shows and nothing signals the second (see section 4).

**Not verified:**
- The full offscreen suite. The authoritative gate decides.
- The packaged app binary.
- The detached-tree size, which is reasoned above, not run.

## 6. Integration notes for the lead

1. **`origin/main` (EN-05 merged).** `git merge-tree --write-tree --name-only origin/main origin/claude/en-04-harness-wiring` gives `b4eeb72` with no conflicts.
   - `settings_file.cpp`, which `favorites_model.cpp` needs, is now in `GLANCELOT_VIEWER_SOURCES`. All five targets compile that list. `monitor_customization_ui` does so through `cmake/MonitorCustomization.cmake`.
   - The provisional loop therefore still links. CMake drops duplicate sources within a target.
2. **`origin/lead/integrate-cycle1-b` (EN-02, EN-03, EN-06, EN-08) conflicts.** The conflict is in `CMakeLists.txt`, in the `BUILD_TESTING` block after `include(cmake/MonitorCustomization.cmake)`: EN-06's `include(cmake/CommandRoutingTests.cmake)` against EN-04's loop. Keep both statements.
3. **`command_routing_tests` would fail to link (must fix at integration).**
   - EN-06's `tests/command_routing_tests.cpp` includes `ui_test_support.hpp`. After EN-04 its `Harness` has a `FavoritesModel` member.
   - `cmake/CommandRoutingTests.cmake` compiles `folder_history.cpp` but not `favorites_model.cpp`, and the provisional loop names only five targets.
   - Expect an unresolved `FavoritesModel` link error unless command_routing gets the source too.
4. **Recommended adaptation**, per the lead decision for a separate list and not `GLANCELOT_VIEWER_SOURCES`:
   - In `cmake/ViewerSources.cmake`, define a shipped-QML context list, for example `GLANCELOT_VIEWER_CONTEXT_SOURCES`. It holds `src/app/favorites_model.cpp`, `include/glancelot/favorites_model.hpp`, `src/app/folder_history.cpp` and `include/glancelot/folder_history.hpp`, with the headers for AUTOMOC.
   - Add the list to all six shipped-QML targets:
     - `input_ui_tests`, `pathbar_ui_tests` and `grid_ui_tests` (root `CMakeLists.txt`)
     - `fs_watch_ui_tests` (`cmake/FSWatchTests.cmake`)
     - `monitor_customization_ui_tests` (`cmake/MonitorCustomization.cmake`)
     - `command_routing_tests` (`cmake/CommandRoutingTests.cmake`)
   - Drop command_routing's explicit `folder_history.cpp`, then delete the provisional loop.
5. **"One wiring site" stops being true after the merge.**
   - `command_routing_tests.cpp` (`folderHistoryStepsOnceOnEveryRoute`) still calls `h.engine.rootContext()->setContextProperty("folderHistory", &history)`, which replaces the Harness's `history` after load.
   - It works, but it is now redundant. A follow-up can use `h.history`.
   - The Favorites section also becomes visible in `command_routing`. Run `ctest -R command_routing` on the merged tree, since its geometry changes as `input_ui`'s did.
6. **EN-03 labels.** EN-04 adds no ctest test, so it needs no `glancelot_test_labels` call. The new function runs inside `input_ui`, which `cmake/TestLabels.cmake` already labels `ui budget process clipboard`.
7. **AGENTS rule (Low).** AGENTS says new UI tests go in a per-feature file. `favoritesSectionLeavesFolderListTheHeight` was added to `tests/input_ui_tests.cpp`. It fits there, because it shares the `input_ui` harness and its tree-panel helpers. If the lead moves it to a per-feature file (for example `tests/favorites_ui_tests.cpp`), the new test target needs its own `cmake/<Feature>Tests.cmake`, the context source list and `glancelot_test_labels(<name> LABELS ui)`.
8. **Out of scope, for a FAVORITES follow-up (info).** The keyboard context menu opens at `favMenu.popup(panel, Theme.gapXl, (row + 1) * Theme.rowHeight)`. That position ignores the list's offset in the panel and its scroll position. With a list that is shorter and scrolls, the menu can open away from its row. This predates EN-04 and is outside the grant.
