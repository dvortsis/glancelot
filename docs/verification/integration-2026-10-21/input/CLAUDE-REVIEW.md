**CHANGES REQUIRED.** One defect, in a test. The source otherwise looks correct. This is a read-only source and diff review. I did not build or run anything, and I make no gate claim.

## Required fix

**`tests/input_ui_tests.cpp:752` and `:755`** rebind `rotate` to `Ctrl+Alt+R`.
- `reveal-in-file-manager` already owns `Ctrl+Alt+R` by default (`src/app/viewer_controller.cpp:100`, and `tests/grid_ui_tests.cpp:2544` asserts it).
- Defaults are loaded into the bindings map at `viewer_controller.cpp:363`, and `reveal-in-file-manager` has the `global` scope, which overlaps `canvas`.
- `setBinding("rotate","Ctrl+Alt+R")` should therefore return false through `bindingConflict`, and the `QVERIFY` at line 752 would fail.
- `rehearsal-delta.patch` shows the rehearsal used `Ctrl+Alt+Y` here. The sync changed it, probably by mistake.
- **Fix:** restore `Ctrl+Alt+Y` at both lines. Nothing else in the repo uses it.

## What I checked and found sound

- **Merge hygiene:**
  - No conflict markers remain in `ui/`, `src/`, `include/`, `tests/`, `cmake/`, the root `CMakeLists.txt` or the docs.
  - `StartupWindowGeometryTests.cmake` and `startup_window_geometry.hpp` from main are kept. The `.cpp` guard is intentionally absent.
  - The `startup_window_geometry` header and CMake include come through from main intact.
- **Registry:**
  - Main's commands are all intact: refresh, favorites, history, shortcuts-help, IO profiles, filter, metadata, animation, rename, copy/move, presentation, and the three delete commands with Recycle Bin/Trash labels.
  - INPUT-02 scopes, `hold-actual`, and the grid/tree/path commands are layered on top.
- **Trash command, menu and Delete parity:**
  - The tree row is a `CommandMenuItem` for `delete-selection` (`ui/FolderTree.qml:735-743`).
  - `commandBlockedReason` (`Main.qml:339-344`) feeds `scopeAllows`, `routeAllows`, `routeBlockedReason` and the row label.
  - The old `deleteRequested` signal and the `folderMoveToTrash` object are gone from both the QML and the tests.
  - Grid trash still goes through `deleteFlow.request` (`Main.qml:902`).
- **Exact targets, root and busy safeguards:**
  - `openRowMenu` sets `list.currentIndex = row` (`FolderTree.qml:32`).
  - `currentSelectedFolder` accepts `rowMenu.visible` (`FolderTree.qml:76`).
  - `DeleteFlow.request` still has its own busy backstop (`DeleteFlow.qml:85`).
  - The root and busy tests assert the exact target path and use an injected `HeldTrash` backend that never mutates media.
- **Clipboard distinction:**
  - `copy-path` (`Ctrl+C`) still routes through `copyFilePath(targets)` (`Main.qml:620-633`), labelled "Copy file path".
  - New `copy-folder-path` is global and unbound, and routes to `viewer.copyDirectory()` (`Main.qml:543`).
  - The address-bar "Copy" `PanelButton` and the favorites `copyFilePath([path])` are untouched, and `grid_ui_tests.cpp:2575-2577` still asserts `copy-path`.
  - The input test checks the clipboard against `viewer.directory()` (`input_ui_tests.cpp:875-877`).
- **Registry scopes, chord and holds:**
  - Animation commands are `canvas`, and the metadata inspector and grid-selection commands are `workspace`.
  - Holds must be single-key (`usableBindings`, `setBinding`).
  - `hold-actual` ends on key release, window deactivation or modal open (`Main.qml:647-648`, `ImageCanvas::eventFilter`).
  - The chord Shortcuts, Escape ordering and the `remapped` yield for `PhotoGrid` match the plan.
- **CMake:**
  - `input_ui_tests` has the same source list as `grid_ui_tests`, plus its own resources, shaders and decoder dependency.
  - `ui/CommandMenuItem.qml` is listed once.
  - `grid_ui_tests` stays untouched apart from required deltas.
- **Main's virtualized delegate test** is retained at `grid_ui_tests.cpp:2325-2346`.
  - The Down-twice and `menuItem-delete-selection` edits are applied (`:2417-2418`, `:2440`).
  - The earlier temporary testfix is not present.

## Design review

I read all nine skills and applied them to native QML.
- **Components and tokens:** `CommandMenuItem` is a single primitive with finite variants (`danger`, `available`, `iconName`). All geometry, spacing and type come from Theme tokens, there is no new icon family, and there is no new motion.
- **Destructive row:** it keeps trash icon, danger colour and text together, so meaning does not rest on colour alone.
- **Disabled rows:** they stay visible and explain themselves.
- **Not exercised:** compact-width placement and screenshots. The opt-in `reviewCaptures` test exists, but I did not run it.

## Non-blocking advisories (carried over, no change needed)

- **`ui/Main.qml:502-503`:** this blocked-notice branch is unreachable, because `invoke` and the Shortcut `enabled` check both refuse first. Delete on a root or while busy is a silent no-op for keyboard users.
- **`ui/PhotoGrid.qml:443`:** the grid Trash button keeps its own `!fileOperations.busy`. `DeleteFlow` is the backstop.
- **`tests/grid_ui_tests.cpp:2344`:** `QCOMPARE(reasons.size(), 2)` is trivially true. The real checks are the model count at `:2328` and the content assertions.
- **`cursorIsRoot()`:** `rowInfo` has no change signal. This is latent and truthfulness-only, not a safety issue.

## Remaining checks

- Apply the `Ctrl+Alt+Y` fix, then run `ctest -R input_ui` as a targeted run.
- Run the full Windows `scripts/build.ps1 -Test` and the Docker gate under the gate lock, as already planned.
- Hardware, IME, mixed DPI and the GPU path remain unverified.

I did not write a plan file or call ExitPlanMode. This was a read-only review with no implementation to plan.
