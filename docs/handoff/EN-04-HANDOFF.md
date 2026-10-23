# EN-04 Shared test harness wiring: handoff

Written for: the lead and the EN-04 reviewer. Session S2 (environment), Phase 0 cycle 1, October 22-23, 2026.

## Status: implemented and type-checked; build and UI suites not run

Update, review round 1: built and run on Windows. One regression is fixed, and two are blocked on a `ui/FolderTree.qml` layout bug. See "Review round 1 fixes" at the end.

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

### Status: one regression fixed; two blocked on a product bug in `ui/FolderTree.qml`

The Windows build is now done, and the five targeted suites have run. `deleteBrowsingFolderFromTreeVisitsNearestAncestor` passes. The other two functions still fail because of a layout bug in the Favorites section, which shipped with FAVORITES-01 and is already on `main`. EN-04 only exposed the bug: before EN-04, no test harness defined `favoritesModel`. The brief says not to patch QML in this packet, so the fix is the lead's decision.

### Product bug: the Favorites section takes the folder list's height

- `favoritesSection` in `ui/FolderTree.qml` (about line 194) is a nested `ColumnLayout`. Qt Quick layouts default to `Layout.fillHeight: true`, so the section stretches.
- Its header `favHeaderArea` also sets `Layout.fillHeight: true`.
- Together these take the panel's spare height away from `folderList`, the other item that fills.
- Measured at 1180x780 with the tree pinned (temporary instrumentation in `treeRowClickEnterAndChevron`, since removed):

```text
DIAG item folderTree QRectF(0,160 280x572)
DIAG col child QQuickColumnLayout "" QRectF(0,204 279x420)     <- favoritesSection, natural height about 81
DIAG item folderList QRectF(0,728 279x4)                       <- the folder list is 4 px tall
DIAG click QPointF(102,742) albumRect QRectF(8,728 263x28) ... index QVariant(int, 656)
DIAG  hit ... QQuickGridLayout "" QRectF(12,740 1156x32)       <- the click lands on the bar below the panel
```

- `git diff afeac1a HEAD -- ui/` is empty, so EN-04 changed no QML.
- The app installs the same context on the same QML, so it should show the same 4 px list. This was not checked in the running app.
- One-line experiment, applied locally and then reverted, never committed: `Layout.fillHeight: false` on `favoritesSection`. With it, the section is 81 px, the list is 343 px (`QRectF(0,389 279x343)`), and both blocked functions pass (output below). Whether that line, a cap on the section, or something else is the right fix is for the lead and FAVORITES-01 to decide, under the ui-design-skills rules.

### Cause and change per function

1. **`GridUiTests::deleteBrowsingFolderFromTreeVisitsNearestAncestor` (fixed).**
   - Cause: the test pressed Down exactly twice. With `favoritesModel` installed, "Add to Favorites" is enabled and is the first row, so two presses highlighted "Open folder". Return then opened the folder, and no confirmation appeared.
   - Change: the test finds `menuItem-delete-selection` by identity and checks that it is enabled. It presses Down until that row is highlighted (at most 4 presses, the same pattern as `input_ui_tests.cpp:385`), checks that it is highlighted, and then presses Return. Everything after that is unchanged.
2. **`InputUiTests::treeRowAndPathBarMenusOpenAndMatch` (test made robust; blocked by the product bug).**
   - Cause: the product bug above. The test also had its own weakness: it right-clicked `namedAll("folderRow").first()`, which is the first delegate found. That delegate can be a cached row outside the clipped viewport.
   - Change: the test now picks row 0 (a drive) by its `index` and checks that row 0 is not the cursor row. It calls `positionViewAtIndex(0, ListView.Contain)` and waits until the row's centre lies inside the list's scene rectangle. Then it right-clicks the row. Two new checks prove the click landed on that row: `folderRowMenu.path` equals the row's path, and `folderList.currentIndex` is 0. The earlier keyboard steps had left the menu and the cursor on the current folder, so these checks really test the click. The test is stricter than before.
   - It cannot pass while the list is 4 px tall. It passes with the one-line experiment.
3. **`GridUiTests::treeRowClickEnterAndChevron` (no test change; blocked by the product bug).**
   - Cause: `reveal()` scrolls "album" into the 4 px viewport. The row's top edge, at y 728, is inside the viewport, but the chevron point at y 742 is below it. The click falls through to the bar below the panel, so "album" never expands. It is still collapsed 500 ms after the click.
   - Change: none. The test is correct as written, and it passes unchanged once the list has its height back.

Only `tests/grid_ui_tests.cpp` (inside `deleteBrowsingFolderFromTreeVisitsNearestAncestor` only) and `tests/input_ui_tests.cpp` changed. There are no product changes.

### Evidence (actual output)

Build: `scripts/build.ps1 -BuildDir build-claude-en-04`, `[557/557] Linking CXX executable monitor_customization_ui_tests.exe`, exit 0. It rebuilt incrementally after the test edits.

Five targeted suites, twice, under the gate lock (`gate-lock.ps1 -Branch claude/en-04-fix -Run { ctest --test-dir build-claude-en-04 -R '^(folder_grid|input_ui|pathbar_ui|fs_watch_ui|monitor_customization_ui)$' --output-on-failure }`):

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
1/5 Test #12: folder_grid ......................***Failed  256.87 sec
2/5 Test #13: input_ui .........................***Failed  123.04 sec
3/5 Test #14: pathbar_ui .......................   Passed    3.28 sec
4/5 Test #25: fs_watch_ui ......................   Passed    2.44 sec
5/5 Test #43: monitor_customization_ui .........   Passed   24.15 sec
folder_grid: FAIL!  : GridUiTests::treeRowClickEnterAndChevron() 'rowFor(media.filePath("album")) && rowFor(media.filePath("album"))->property("expanded").toBool()' returned FALSE. ()
folder_grid: PASS   : GridUiTests::deleteBrowsingFolderFromTreeVisitsNearestAncestor()
folder_grid: Totals: 73 passed, 1 failed, 4 skipped, 0 blacklisted, 256704ms
input_ui:    FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'rowAt(target) && list->mapRectToScene(list->boundingRect()).contains(sceneCenter(rowAt(target)))' returned FALSE. ()
input_ui:    Totals: 31 passed, 1 failed, 1 skipped, 0 blacklisted, 122919ms
```

`auditReadyCurrentPhotoDoesNotReopen` (`grid_ui_tests.cpp:945`, `changed.count()` 1, expected 0) failed only in run 1. That run was slow (390 s against 257 s), and the function passed in run 2. EN-04 does not touch it. It is recorded here as a budget-style flake under load. The authoritative gate decides.

With the one-line experiment applied (uncommitted, then reverted), the three functions ran twice under the gate lock with `grid_ui_tests.exe treeRowClickEnterAndChevron deleteBrowsingFolderFromTreeVisitsNearestAncestor` and `input_ui_tests.exe treeRowAndPathBarMenusOpenAndMatch` (`QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`):

```text
exp1-grid:  PASS   : GridUiTests::treeRowClickEnterAndChevron()
exp1-grid:  PASS   : GridUiTests::deleteBrowsingFolderFromTreeVisitsNearestAncestor()
exp1-grid:  Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 8555ms
exp1-input: PASS   : InputUiTests::treeRowAndPathBarMenusOpenAndMatch()
exp1-input: Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 4608ms
exp2-grid:  PASS   : GridUiTests::treeRowClickEnterAndChevron()
exp2-grid:  PASS   : GridUiTests::deleteBrowsingFolderFromTreeVisitsNearestAncestor()
exp2-grid:  Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 7170ms
exp2-input: PASS   : InputUiTests::treeRowAndPathBarMenusOpenAndMatch()
exp2-input: Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 4083ms
```

### Unverified and open

- The product fix for the Favorites layout is not part of this commit. Until it lands, `folder_grid` and `input_ui` still fail on this branch, in exactly the two functions above.
- Whether the running app shows the 4 px list was not checked.
- The full offscreen suite was not run, only the five targeted suites.
- The ui-design-skills were not used, because no QML or visual change is made here. A QML fix for the Favorites section must apply them.
