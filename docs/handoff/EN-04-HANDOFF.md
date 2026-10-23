# EN-04 Shared test harness wiring: handoff

Written for: the lead and the EN-04 reviewer. Session S2 (environment), Phase 0 cycle 1, October 22-23, 2026.

## Status: implemented and type-checked; build and UI suites not run

Update, review round 1: built and run on Windows. All three regressions are resolved, and the five targeted suites pass twice. Two of the regressions came from a `ui/FolderTree.qml` Favorites layout bug, fixed under a lead grant. See "Review round 1 fixes" at the end.

This session cannot install Qt 6.8.3: the cloud network policy denies `download.qt.io` (see docs/handoff/EN-02-HANDOFF.md on `claude/en-02-linux-bootstrap`). No targeted or full `ctest` ran. The changed files were type-checked against Ubuntu's Qt 6.4 headers (method and output below) and show exactly the same errors as unmodified `afeac1a`, all from Qt 6.5+ APIs the old headers lack. The acceptance "all UI suites pass unchanged" is **unverified**.

| Item | Value |
| --- | --- |
| Branch | `claude/en-04-harness-wiring` |
| Base SHA | `afeac1a5d5b1da088d7b35447a933a2f64400f49` (`origin/main`) |
| Commits | `996f067` test(EN-04) code; `148b2c0` cmake: provisional change for EN-04; then this handoff |
| Tip SHA | the commit that adds this handoff (`git log -1 claude/en-04-harness-wiring`) |

## What changed

- `src/app/viewer_context.hpp` (new, header-only, private app helper like `file_grid_wiring.hpp`): `struct ViewerContext` holds a reference to each of the ten services, and `installViewerContext(QQmlContext&, const ViewerContext&)` sets `viewer`, `paneLayout`, `monitorCustomization`, `filmstrip`, `folderTree`, `favoritesModel`, `folderHistory`, `fileGrid`, `theme`, `fileOperations`. References make every service required at compile time, so a harness cannot silently omit one.
- `app/main.cpp`: the ten `setContextProperty` lines become one `installViewerContext` call (designated initializers). Same names, same objects, same point before `loadFromModule`. No behaviour change in the app.
- `tests/ui_test_support.hpp` (used by `input_ui` and `pathbar_ui`), `tests/grid_ui_tests.cpp` (Harness wiring block only), `tests/fs_watch_ui_tests.cpp`, `tests/monitor_customization_ui_tests.cpp`: each harness owns a `FavoritesModel favorites` and `FolderHistory history`, declared before the engine so the engine is destroyed first, and calls `installViewerContext` instead of its own list.
- `CMakeLists.txt`, commit `cmake: provisional change for EN-04`: after `include(cmake/MonitorCustomization.cmake)`, a `BUILD_TESTING` loop adds `src/app/favorites_model.cpp` and `src/app/folder_history.cpp` (with headers, for AUTOMOC) to `input_ui_tests`, `pathbar_ui_tests`, `grid_ui_tests`, `fs_watch_ui_tests` and `monitor_customization_ui_tests`. This is unavoidable: those targets did not compile either class. S1 or the lead can fold the two sources into EN-05's `GLANCELOT_VIEWER_SOURCES` (or the UI-test source list) and drop the loop. `favorites_model.cpp` needs `settings_file.cpp`, which all five targets already compile; its quoted `settings_retry.hpp` include resolves beside the source.

One wiring site: after this branch, `grep -rn setContextProperty app src tests` matches only `src/app/viewer_context.hpp`.

## Behaviour in tests (for the reviewer)

The app is unchanged. In the five UI test executables `favoritesModel` and `folderHistory` are now defined, as in the app, so `ui/FolderTree.qml` takes its `typeof ... !== "undefined"` branches: the Favorites header and the Back/Forward buttons become visible in tests. That is the parity the packet asks for, but tree-geometry or focus-order assertions in `folder_grid`, `input_ui` and `pathbar_ui` are the places to watch. Isolation: `FavoritesModel` reads `settings.json` at construction; every harness sets `GLANCELOT_CONFIG_DIR` to a temporary directory before constructing it (`input_ui`/`pathbar_ui` `init()`, `grid_ui` `init()`, `monitor_customization_ui` `init()`, `fs_watch_ui` `initTestCase()`), and nothing writes favorites unless a test mutates them.

Kept out of scope: `main.cpp`'s FolderHistory and Favorites signal connections (`recordVisit`, `navigationRequested`, `setCurrentDirectory`) stay in `main.cpp`. In tests the history is installed but not fed, so Back/Forward stay disabled. Extracting those connections into a shared helper like `file_grid_wiring` would be a behaviour change for the UI suites; a candidate follow-up.

## Evidence (actual output, this session)

Type check of the six translation units that include the changed code, with `g++ 13.3 -std=c++20 -fsyntax-only` against Ubuntu 24.04's `qt6-base-dev`/`qt6-declarative-dev` 6.4.2 headers (installed only for this check), small stubs for the Qt 6.6+ headers `QQuickRhiItem` and `rhi/qrhi.h`, and empty `.moc` files. Run once on a detached `afeac1a` worktree and once on this branch; errors normalised by line number and compared:

```text
$ diff <(normalise syn-base.log | sort) <(normalise syn-new.log | sort) && echo "same errors as baseline"
same errors as baseline
$ grep -i -E "viewer_context|ViewerContext|favorites|history" syn-new.log
(no output)
```

Errors present in both (Qt 6.4 lacks these APIs):

```text
app/main.cpp:273:12: error: 'class QQmlApplicationEngine' has no member named 'loadFromModule'
app/main.cpp:335:78: error: 'CpuDevice' is not a member of 'QRhiDriverInfo'
src/app/native_title_bar.hpp:96:58: error: 'colorSchemeChanged' is not a member of 'QStyleHints'
src/render/output_color.hpp:28:39: error: no matching function for call to 'QImage::convertedToColorSpace(const QColorSpace&, QImage::Format ...
tests/grid_ui_tests.cpp:360:63 (and 415, 472, 591), tests/input_ui_tests.cpp:649:63: error: no matching function for call to 'QQmlApplicationEngine::singletonInstance<QObject*>(const char [10], ...
/usr/include/x86_64-linux-gnu/qt6/QtCore/qmetatype.h:842:23: error: invalid application of 'sizeof' to incomplete type 'QQuickCloseEvent'
```

The files checked: `app/main.cpp`, `tests/grid_ui_tests.cpp`, `tests/fs_watch_ui_tests.cpp`, `tests/monitor_customization_ui_tests.cpp`, `tests/input_ui_tests.cpp`, `tests/pathbar_ui_tests.cpp`; `src/app/viewer_context.hpp` alone compiled with no output.

## Unverified

- Configure, build and link with Qt 6.8.3, including the provisional CMake loop (AUTOMOC for the two added headers).
- Targeted `ctest -R "folder_grid|input_ui|pathbar_ui|fs_watch_ui|monitor_customization_ui"` and the full offscreen suite. The reviewer or the gate must supply this output; budget tests (`folder_grid`, `input_ui`) may need the documented single rerun.
- MSVC: designated initializers that bind reference members are standard C++20 and accepted by GCC 13; the Windows gate is the first MSVC compile of this pattern in the repository.
- The ui-design-skills do not apply: no QML or visual change was made.

## Review round 1 fixes

Written by: the EN-04 fix implementer, a local Claude Code (Opus) session on the lead's Windows PC (MSVC 2022, Qt 6.8.3), October 22, 2026. It answers `docs/verification/EN-04-REVIEW.md` on `claude/review-en-04` (`211e81b`, CHANGES REQUIRED). Worktree `.worktrees/claude-en-04`, build directory `build-claude-en-04`.

### Status: three regressions resolved; one product bug found and fixed under a lead grant

The Windows build and the five targeted suites now run. Round 1 fixed one test (`deleteBrowsingFolderFromTreeVisitsNearestAncestor`). The other two failures came from a layout bug in the Favorites section of `ui/FolderTree.qml`.
- The bug shipped with FAVORITES-01 and is identical on `main` (`git diff afeac1a origin/main -- ui/FolderTree.qml` is empty).
- EN-04 only exposed it: before EN-04, no test harness defined `favoritesModel`.
- I stopped and reported it. The lead then granted EN-04 `ui/FolderTree.qml`, limited to the sizing of the Favorites section (decision of October 22, 2026), and this round fixes it.

### Product bug: the Favorites section took the folder list's height

- `favoritesSection` (about line 194) is a nested `ColumnLayout`. Qt Quick nested layouts default to `Layout.fillHeight: true`.
- Its header `favHeaderArea` sets `Layout.fillHeight: true`, so the section has no maximum height.
- The section therefore shared the panel's spare height with `folderList`, the only other item that fills, and took nearly all of it.
- In the `folder_grid` harness at 1180x780 with the tree pinned (temporary instrumentation, since removed), the section was 420 px against its natural 81 px. `folderList` was 4 px tall, so clicks on tree rows fell through to the bar below the panel:

```text
DIAG item folderTree QRectF(0,160 280x572)
DIAG col child QQuickColumnLayout "" QRectF(0,204 279x420)     <- favoritesSection
DIAG item folderList QRectF(0,728 279x4)                       <- the folder list
DIAG click QPointF(102,742) albumRect QRectF(8,728 263x28) ... index QVariant(int, 656)
DIAG  hit ... QQuickGridLayout "" QRectF(12,740 1156x32)       <- the chevron click lands below the panel
```

- The app installs the same context on the same QML, so it shows the same collapsed tree whenever the Favorites section is visible. The screenshots below come from the shipped `ui/Main.qml` in the `input_ui` harness on the native Windows platform. I did not check the packaged app binary itself.

### Fix (`ui/FolderTree.qml`, Favorites section sizing only)

- `favoritesSection` gets `Layout.fillHeight: false`, so it is exactly as tall as its content.
- `favHeaderArea` keeps `Layout.fillHeight: true`. It still has to fill the header row, which is `Theme.controlHeight` tall, so its hover surface covers the whole row. Once the section no longer stretches, it can no longer take the panel's height.
- Cap on `favoritesList`: whole rows, at most 5, and at most a fifth of the panel height, never less than one row. As code: `maxRows = max(1, min(5, floor(panel.height / (5 * Theme.rowHeight))))`. A longer list scrolls inside the section, which keeps its existing `UiScrollBar` and `clip`.
- The old fixed cap was 144 px, about 5.1 rows, which cut a row in half. At 540x360 it would still have squeezed the folder list to 15 px with only two favorites.
- `folderList` keeps `Layout.fillHeight: true` and gets all remaining height.

Design note (ui-design-skills: application-interface-design, layout-spacing, layout-grids and responsive-layout were read for this change):
- The folder tree is the panel's primary region. Favorites is a secondary shortcut list, so it gets a bounded region sized to its content, and the tree gets the remaining area. This follows the rule "important regions receive appropriate area".
- The cap is relative to the panel. That keeps the tree usable from the minimum window size (540x360, panel 264 px, one favorites row) up to tall windows (5 rows). A long list scrolls in place and never pushes the tree off-screen.
- Rows stay whole, so the edge of the list stays clean. The visible scroll bar shows that more rows exist.
- No spacing, colour, type, icon or theme tokens changed. The section keeps its margins, the separator and the empty-state hint.

| Panel (window) | Favorites | Section before / after | Folder list before / after |
| --- | --- | --- | --- |
| 684 px (1180x780) | 0 | 531 / 83 | 5 / 453 |
| 684 px (1180x780) | 2 | 532 / 101 | 4 / 435 |
| 684 px (1180x780) | 12 | stretched / 157 (list 112, scrolls 336) | / 379 |
| 264 px (540x360) | 0 | 116 / 83 | 0 / 33 |
| 264 px (540x360) | 2 | 116 / 73 (list 28, scrolls 56) | 0 / 43 |

The figures come from the native-platform runs of the regression test below. Offscreen software runs give the same values within 2 px.

Screenshots of the tree panel with two favorites, captured through `GLANCELOT_REVIEW_CAPTURE` on the native Windows platform with RHI. The "before" set is `ui/FolderTree.qml` from `HEAD`, restored temporarily.
- Before: `docs/verification/EN-04/tree-before-{dark,light}-{1180x780,540x360}.png`. The tree area is empty: there is a large blank gap above and below the Favorites header, and no folder rows.
- After: `docs/verification/EN-04/tree-after-{dark,light}-{1180x780,540x360}.png`. The Favorites section is compact and the folder rows fill the panel. At 540x360 one favorite row shows (the list scrolls) and the tree shows its rows.

The software backend (`QT_QUICK_BACKEND=software`) does not clip `UiIcon` shapes in a clipped `ListView`. In software captures, the star of a scrolled-out favorite row draws below the list. Native RHI captures do not show this, so it is a capture artifact and not a product issue. The committed screenshots are the native ones.

### Cause and change per function

1. **`GridUiTests::deleteBrowsingFolderFromTreeVisitsNearestAncestor` (test fix).**
   - Cause: the test pressed Down exactly twice. "Add to Favorites" is now enabled and is the first row, so two presses highlighted "Open folder", Return opened the folder, and no confirmation appeared.
   - Change: the test finds `menuItem-delete-selection` by identity and checks that it is enabled. It presses Down until that row is highlighted (at most 4 presses, as at `input_ui_tests.cpp:385`), checks the highlight, and then presses Return.
2. **`InputUiTests::treeRowAndPathBarMenusOpenAndMatch` (product bug, plus the test made robust).**
   - Cause: the 4 px folder list. The test also right-clicked `namedAll("folderRow").first()`, which can be a cached delegate outside the clipped viewport.
   - Change: the test picks row 0 (a drive) by `index` and checks that row 0 is not the cursor row. It calls `positionViewAtIndex(0, ListView.Contain)` and waits until the row's centre is inside the list's scene rectangle, then right-clicks. Two new checks prove the click chose that row: `folderRowMenu.path` equals the row's path, and `folderList.currentIndex` is 0. The test is stricter than before.
3. **`GridUiTests::treeRowClickEnterAndChevron` (product bug; no test change).**
   - Cause: `reveal()` scrolled "album" into the 4 px viewport. The row's top edge was inside it, but the chevron point was 14 px below it, so the click hit the bar below the panel.
   - Change: none. The test is correct and passes once the list has its height back.
4. **New regression test `InputUiTests::favoritesSectionLeavesFolderListTheHeight` (`tests/input_ui_tests.cpp`).**
   - Setup: it runs with 0, 2 and 12 favorites, at 1180x780 and at the minimum 540x360.
   - Section check: the Favorites section height equals its `implicitHeight` (content).
   - Favorites list checks: the list is at least one row and at most `count` rows tall, and with 12 favorites it scrolls (content taller than the view).
   - Folder list checks: at 1180x780 the folder list is at least 50% of the panel, and at 540x360 it is at least one row.
   - Before the fix, it fails on the first check:

```text
FAIL!  : InputUiTests::favoritesSectionLeavesFolderListTheHeight() 'qAbs(section->height() - section->implicitHeight()) < 1' returned FALSE. (0 favorites at 1180x780: panel 684, favorites section 530 (content 81), favorites list 56 (content 0), folder list 6)
...\tests\input_ui_tests.cpp(1224) : failure location
```

   With only `Layout.fillHeight: false` and the old 144 px cap, it still failed at the minimum size. That failure is why the cap is relative to the panel:

```text
FAIL!  : InputUiTests::favoritesSectionLeavesFolderListTheHeight() 'list->height() >= rowHeight' returned FALSE. (2 favorites at 540x360: panel 264, favorites section 101 (content 101), favorites list 56 (content 56), folder list 15)
```

Files changed this round:
- `ui/FolderTree.qml`: Favorites section sizing only, under the lead grant.
- `tests/grid_ui_tests.cpp`: only inside `deleteBrowsingFolderFromTreeVisitsNearestAncestor`.
- `tests/input_ui_tests.cpp`.
- `docs/verification/EN-04/*.png`.
- This handoff.

### Evidence (actual output)

Build: `scripts/build.ps1 -BuildDir build-claude-en-04`, a full build `[557/557]` with exit 0, then incremental rebuilds after each edit, each with exit 0.

Five targeted suites before the QML fix (round 1 test changes only), twice, under the gate lock:

```text
=== run 1
1/5 Test #12: folder_grid ......................***Failed  389.95 sec
2/5 Test #13: input_ui .........................***Failed  102.23 sec
3/5 Test #14: pathbar_ui .......................   Passed    5.26 sec
4/5 Test #25: fs_watch_ui ......................   Passed    3.58 sec
5/5 Test #43: monitor_customization_ui .........   Passed   30.61 sec
folder_grid: FAIL!  : GridUiTests::auditReadyCurrentPhotoDoesNotReopen() Compared values are not the same
folder_grid: FAIL!  : GridUiTests::treeRowClickEnterAndChevron() 'rowFor(media.filePath("album")) && rowFor(media.filePath("album"))->property("expanded").toBool()' returned FALSE. ()
folder_grid: PASS   : GridUiTests::deleteBrowsingFolderFromTreeVisitsNearestAncestor()
folder_grid: Totals: 72 passed, 2 failed, 4 skipped, 0 blacklisted, 389554ms
input_ui:    FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'rowAt(target) && list->mapRectToScene(list->boundingRect()).contains(sceneCenter(rowAt(target)))' returned FALSE. ()
input_ui:    Totals: 31 passed, 1 failed, 1 skipped, 0 blacklisted, 102115ms
=== run 2
folder_grid: FAIL!  : GridUiTests::treeRowClickEnterAndChevron() ... returned FALSE. ()
folder_grid: PASS   : GridUiTests::deleteBrowsingFolderFromTreeVisitsNearestAncestor()
folder_grid: Totals: 73 passed, 1 failed, 4 skipped, 0 blacklisted, 256704ms
input_ui:    FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() ... returned FALSE. ()
input_ui:    Totals: 31 passed, 1 failed, 1 skipped, 0 blacklisted, 122919ms
(pathbar_ui, fs_watch_ui and monitor_customization_ui passed in both runs)
```

`auditReadyCurrentPhotoDoesNotReopen` (`grid_ui_tests.cpp:945`) failed only in run 1. That run was slow (390 s against 257 s), and EN-04 does not touch the function. It is recorded as a flake under load. The authoritative gate decides.

Five targeted suites after the QML fix, twice, under the gate lock (`gate-lock.ps1 -Branch claude/en-04-fix -Run { ctest --test-dir build-claude-en-04 -R '^(folder_grid|input_ui|pathbar_ui|fs_watch_ui|monitor_customization_ui)$' --output-on-failure }`):

```text
=== run 3
1/5 Test #12: folder_grid ......................   Passed  324.34 sec
2/5 Test #13: input_ui .........................   Passed   73.79 sec
3/5 Test #14: pathbar_ui .......................   Passed    3.92 sec
4/5 Test #25: fs_watch_ui ......................   Passed    2.78 sec
5/5 Test #43: monitor_customization_ui .........   Passed   23.16 sec
100% tests passed out of 5
folder_grid: PASS   : GridUiTests::treeRowClickEnterAndChevron()
folder_grid: PASS   : GridUiTests::deleteBrowsingFolderFromTreeVisitsNearestAncestor()
folder_grid: Totals: 74 passed, 0 failed, 4 skipped, 0 blacklisted, 324090ms
input_ui: PASS   : InputUiTests::treeRowAndPathBarMenusOpenAndMatch()
input_ui: PASS   : InputUiTests::favoritesSectionLeavesFolderListTheHeight()
input_ui: Totals: 33 passed, 0 failed, 1 skipped, 0 blacklisted, 73690ms
monitor_customization_ui: Totals: 13 passed, 0 failed, 1 skipped, 0 blacklisted, 23018ms
pathbar_ui: Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 3793ms
=== run 4
1/5 Test #12: folder_grid ......................   Passed  201.77 sec
2/5 Test #13: input_ui .........................   Passed   73.84 sec
3/5 Test #14: pathbar_ui .......................   Passed    3.35 sec
4/5 Test #25: fs_watch_ui ......................   Passed    2.43 sec
5/5 Test #43: monitor_customization_ui .........   Passed   23.61 sec
100% tests passed out of 5
folder_grid: PASS   : GridUiTests::treeRowClickEnterAndChevron()
folder_grid: PASS   : GridUiTests::deleteBrowsingFolderFromTreeVisitsNearestAncestor()
folder_grid: Totals: 74 passed, 0 failed, 4 skipped, 0 blacklisted, 201681ms
input_ui: PASS   : InputUiTests::treeRowAndPathBarMenusOpenAndMatch()
input_ui: PASS   : InputUiTests::favoritesSectionLeavesFolderListTheHeight()
input_ui: Totals: 33 passed, 0 failed, 1 skipped, 0 blacklisted, 73761ms
monitor_customization_ui: Totals: 13 passed, 0 failed, 1 skipped, 0 blacklisted, 23551ms
pathbar_ui: Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 3278ms
```

No results file from runs 3 or 4 contains `Binding loop`, `Polish loop`, `TypeError`, `ReferenceError` or a `FolderTree.qml` warning.

### Unverified and open

- The full offscreen suite was not run, only the five targeted suites. The authoritative gate decides.
- The packaged app binary was not launched. The screenshots use the shipped QML in the `input_ui` harness on the native platform.
- Review finding 5 also named `reviewAllLocationsAtMinimumSize` and `addressEditingClipboardAndEscape`. Both pass in the runs above.
- The Back and Forward buttons narrow the address field, which shows only the tail of the path in the screenshots. That behaviour is unchanged and outside this grant.
