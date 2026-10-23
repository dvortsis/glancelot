# EN-04 shared test harness wiring: independent review

Reviewer: fresh-context Claude (Opus) reviewer, which implemented no part of EN-04. Machine: the lead's Windows PC, MSVC 2022 and Qt 6.8.3.
Packet: `claude/en-04-harness-wiring` at `f4ee14d`. The commits are `996f067` (code), `148b2c0` (provisional CMake) and the handoff commit. The base is `afeac1a`.

## Verdict: CHANGES REQUIRED

The wiring is correct. The MSVC build is clean. Three UI test functions fail every time on this branch and pass on `afeac1a` on the same machine.

In those three functions the cause lies in the tests, not the product. Each one assumed the Favorites and History UI was missing. Without `favoritesModel` in the harness, the folder row menu's "Add to Favorites" item was disabled and the Favorites section was hidden. EN-04 now gives the harness the same QML context as the app, so these tests have to match the shipped UI.

## Stage 1: code review, no build

1. **Wiring matches the app (no finding).** `src/app/viewer_context.hpp` sets the same ten context names on the same objects that `app/main.cpp` set at `afeac1a`. The call still comes after `addImageProvider` and before `loadFromModule`. After this branch, `setContextProperty` exists only in that helper.
2. **Lifetimes and isolation are correct (no finding).** In every harness, `favorites` and `history` are declared before `engine`, so the engine is destroyed first.
   - `GLANCELOT_CONFIG_DIR` is set before the harness is constructed: grid `init()`, input and pathbar `init()`, monitor `init()`, and fs_watch `initTestCase()`.
   - No test writes favorites.
3. **The CMake loop is provisional (low).** The loop at `CMakeLists.txt:321-329` works:
   - AUTOMOC is on through `qt_standard_project_setup`.
   - All five targets exist when the loop runs.
   - `settings_retry.hpp` resolves next to its source.
   - `saveWithRetry` is defined in `settings_file.cpp`, which all five targets already compile.
4. **MSVC (low).** At all five call sites the designators follow declaration order, which C++20 requires. Binding reference members through aggregate designated initialization is valid C++20. The repository had no designated initializers before this change. Stage 2 confirmed that MSVC accepts them.
5. **Geometry is a risk (medium).**
   - The Favorites section is now visible in tests. Even when empty, it adds about 85 px above "All locations".
   - Back and Forward take width from the address field.
   - The tests most at risk are `reviewAllLocationsAtMinimumSize`, `addressEditingClipboardAndEscape` and `treeRowClickEnterAndChevron`.
6. **Behaviour that should still pass (low).**
   - `folderFavoriteToggle` is now enabled, so `input_ui_tests.cpp:385` needs 3 Down presses instead of 2. Its loop allows 4.
   - The accessibility audit now inspects `addCurrentFavorite`.
   - The `failOnWarning("[Bb]inding loop")` check now covers the Favorites bindings.
   - The Tab-order tests are unaffected.
7. **Existing app bug (info).** Alt+Left and Alt+Right navigate twice. `Main.qml:859-860` and `FolderTree.qml:1030-1033` both handle `navigate-back`/`navigate-forward` from the same `viewCommand` signal.
8. **Plan question (info).** LEAD-PLAN line 239 says to split `folder_grid` "when EN-04 touches it". The packet did not split it.

### Lead decisions

- **Finding 3:** at integration, EN-04 adapts to EN-05's `cmake/ViewerSources.cmake` with a separate source list for the five shipped-QML targets. It is not folded into `GLANCELOT_VIEWER_SOURCES`.
- **Finding 7:** routed to the EN-06 reviewer.
- **Finding 8:** splitting `folder_grid` is deferred to its own packet.

## Stage 2: Windows build and targeted tests

### Build

The build ran in the worktree `.worktrees/review-en-04` at `f4ee14d`:

```text
scripts/build.ps1 -BuildDir build-review-en-04
[557/557] Linking CXX executable monitor_customization_ui_tests.exe
```

There were 0 errors. The only warnings were two existing `glancelot_core` warnings: C4100 in `navigation.cpp` and C4996 in `perf_trace.cpp`. Nothing warned in `viewer_context.hpp` or the changed harnesses.

### Targeted tests

The tests ran under the gate lock with `ctest -R '^(folder_grid|input_ui|pathbar_ui|fs_watch_ui|monitor_customization_ui)$'`:

```text
1/5 Test #12: folder_grid ......................***Failed  246.41 sec
2/5 Test #13: input_ui .........................***Failed   89.14 sec
3/5 Test #14: pathbar_ui .......................   Passed    4.42 sec
4/5 Test #25: fs_watch_ui ......................   Passed    3.19 sec
5/5 Test #43: monitor_customization_ui .........   Passed   28.44 sec
folder_grid:  Totals: 72 passed, 2 failed, 4 skipped
input_ui:     Totals: 31 passed, 1 failed, 1 skipped
FAIL!  : GridUiTests::treeRowClickEnterAndChevron()   grid_ui_tests.cpp(1391)
FAIL!  : GridUiTests::deleteBrowsingFolderFromTreeVisitsNearestAncestor()   grid_ui_tests.cpp(2428)
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch()   input_ui_tests.cpp(1133)
```

The results files contain no `Binding loop`, `Polish loop`, `TypeError` or `ReferenceError`.

### Rerun and baseline

I reran the three functions once under the lock, and they failed again in the same way. I then ran the same three functions under the lock from the `afeac1a` build in `.worktrees/lead-tester/build-lead-tester`, and they all passed. That build's full run from earlier today also passed: `folder_grid` 74/0 and `input_ui` 32/0.

```text
EN-04 rerun:   grid 2 passed, 2 failed; input 2 passed, 1 failed (same three functions)
afeac1a:       PASS treeRowClickEnterAndChevron, PASS deleteBrowsingFolderFromTreeVisitsNearestAncestor,
               PASS treeRowAndPathBarMenusOpenAndMatch
```

### Required changes (EN-04 lane, test files only)

- **`deleteBrowsingFolderFromTreeVisitsNearestAncestor`, grid :2425-2427.** The test presses Down exactly twice and assumes "Open folder" is the first enabled row. "Add to Favorites" is now enabled and comes first, so Return opens the folder and no confirmation appears. Fix it the way `input_ui_tests.cpp:385` does: press Down until `menuItem-delete-selection` is highlighted.
- **`treeRowAndPathBarMenusOpenAndMatch`, input :1131-1133.** The test right-clicks `namedAll("folderRow").first()` without bringing it into view. The Favorites section shortens the list's viewport. The first delegate is probably outside the visible area, so the click lands on the panel and not on the row (likely cause, not confirmed). Scroll the row into view first, as `reveal()` does in `grid_ui_tests.cpp`, or pick the row for the current folder.
- **`treeRowClickEnterAndChevron`, grid :1389-1391.** After `reveal()`, the chevron click no longer expands "album". The shorter viewport is the likely cause (not confirmed), for example if the click lands before the view settles or on an overlapping item. The implementer must diagnose this. It fails every time.
- After these fixes, rerun all five targeted suites on Windows.

The product code (`viewer_context.hpp`, `main.cpp`) needs no change.
