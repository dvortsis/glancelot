# EN-06 handoff: routing correctness

Written for: the lead and the fresh EN-06 reviewer. Session: S3 routing (cloud, Phase 0 cycle 1).

## Branch

- Branch: `claude/en-06-routing`
- Base: `origin/main` at `afeac1a` (chore(lead): freeze Phase 0 cycle 1 contracts)
- Code tip: `7bb7f8b`. The commit that adds this handoff sits on top of it; review with `git diff afeac1a <branch tip>`.

| Commit | What |
| --- | --- |
| `2c62567` | test(EN-06): routed-once and QML-warning tests through the shipped Main.qml |
| `21934df` | cmake: provisional target for EN-06 |
| `3f81c27` | test(EN-06): report every command's execution count, not only the first failure |
| `7bb7f8b` | fix(EN-06): run toggle-metadata and animation-* once; stop re-broadcasting storage-profile-* |

## Files changed

| File | Change |
| --- | --- |
| `src/app/viewer_controller.cpp` | Command-emit region only (`ViewerController::trigger`): `storage-profile-*` no longer emits `viewCommand`; one comment on the `toggle-metadata`/`animation-*` emits |
| `ui/Main.qml` | Removed the seven `handleViewCommand` branches for `toggle-metadata` and `animation-*` (one comment line replaces them); split the two merge-joined lines |
| `tests/command_routing_tests.cpp` | New |
| `cmake/CommandRoutingTests.cmake` | New, provisional (see below) |
| `CMakeLists.txt` | Three lines that include the provisional file (commit `21934df`) |

## What was wrong and what changed

1. **Double execution (confirmed).** `ViewerController::trigger` ran `toggle-metadata` and all six `animation-*` commands, then emitted `viewCommand`. `Main.qml` `handleViewCommand` ran them again. So `I` toggled the metadata HUD twice (no visible change), `Space` played and paused again, `.`/`,` stepped two frames, `]`/`[` changed speed by 2.25x, and toggle-loop did nothing.
   - **Fixed side: QML.** The controller stays the only executor. This matches `presentation-*`, which runs in C++, emits, and has no QML branch. Removing the QML branches keeps every other caller correct: C++ callers (`app/main.cpp`, core tests, a future non-QML front end) still execute the command, and the emit still lets `handleViewCommand` run its shared first step (`canvas.endLoupe()`) as it does for every command. If the emit had been removed instead, those commands would stop ending the loupe, which is a behaviour change. The `wakeHud()` that the QML `toggle-metadata` branch called still runs, because `invoke()` calls it after `viewer.trigger`. Nothing calls `handleViewCommand` directly with these ids (callers: `onViewCommand`, mouse back/forward, and `ShortcutCheatSheet` with `"settings"`).
2. **`storage-profile-*`: no longer emitted.** These commands set I/O scheduler state (`setStorageProfile`) and have no work in the view. `Main.qml` had no branch for them, so the emit only ended the loupe. The commands have no default key, so they are reached from the command search, which is modal and ends the loupe anyway. `tests/io_scheduler_tests.cpp`, which calls `controller.trigger("storage-profile-*")`, still passes.
3. **Merge leftovers** at `Main.qml:1327` and `:2051`. Each had a closing `}` and the next sibling object on one line: `}    PhotoGrid {` (end of the navigator panel `Rectangle`, then `PhotoGrid`) and `}    FileDialog {` (end of the reference canvas block, then `openDialog`). Both still parsed but broke the one-object-per-line layout and could confuse later line-based merges. The code history here starts at `7f08701` (a merge that imported the whole tree), so the cause cannot be traced further. Fixed by splitting each onto two lines. No content was lost: the surrounding objects are intact, and nothing else in `ui/*.qml` matches the pattern `}    [A-Z]`.
4. **`ShortcutCheatSheet` `ReferenceError`: already gone on `main`.** `ui/ShortcutCheatSheet.qml` binds the key badges to `keyBadges.keys`. The test `mainQmlLoadsWithoutQmlWarnings` now guards this. A message handler records every warning or critical message that comes from QML (file ending `.qml`, a `.qml:` location in the text, or a `qml`/`js` category) while `Main.qml` loads and the cheat sheet opens and closes. The test fails on any such message. To check that the guard works, I temporarily changed line 328 back to `model: keys`. The test then failed and listed `file:///…/ui/ShortcutCheatSheet.qml:328: ReferenceError: keys is not defined`. I reverted the file with `git checkout`; it is not changed on the branch.

## Test: `tests/command_routing_tests.cpp`

All cases drive the shipped `Main.qml` through the shared `Harness`. The fixture is a disposable 2-frame GIF89a, with the same bytes as `animation_tests`. Each command's notify signal is counted, and the command must leave the state that one run produces. Two runs leave a different state: a toggle flips back, a 2-frame step wraps, and 1.5x compounds to 2.25x.

- `explicitRouteExecutesOnce`: `app.invoke(id)` (the toolbar, command-search and menu route) for `toggle-metadata` and all six `animation-*`.
- `defaultKeysExecuteOnce`: the default keys on the focused canvas (`I`, `Space`, `.`, `,`, `]`, `[`; toggle-loop has no default key).
- `storageProfileIsControllerOnly`: each profile applies once (`storageProfileChanged` count 1), and no `storage-profile-*` id is emitted on `viewCommand`.
- `mainQmlLoadsWithoutQmlWarnings`: described in item 4 above.

Failure cases covered: a double run (count 2), a command that does not apply, and a QML warning (shown by the temporary injection). Cancellation does not apply to these synchronous commands.

### Fail before (test commits on the old code, before `7bb7f8b`)

```
1/1 Test #44: command_routing ..................***Failed    3.44 sec
PASS   : CommandRoutingTests::initTestCase()
FAIL!  : CommandRoutingTests::explicitRouteExecutesOnce() 'problems.isEmpty()' returned FALSE. (toggle-metadata executed 2 times; animation-toggle-play-pause executed 2 times; animation-next-frame executed 2 times; animation-previous-frame executed 2 times; animation-speed-up executed 2 times; animation-speed-down executed 2 times; animation-toggle-loop executed 2 times)
FAIL!  : CommandRoutingTests::defaultKeysExecuteOnce() 'problems.isEmpty()' returned FALSE. (toggle-metadata executed 2 times; animation-toggle-play-pause executed 2 times; animation-next-frame executed 2 times; animation-previous-frame executed 2 times; animation-speed-up executed 2 times; animation-speed-down executed 2 times)
FAIL!  : CommandRoutingTests::storageProfileIsControllerOnly() '!emitted.first().toString().startsWith("storage-profile-")' returned FALSE. (storage-profile-hdd)
PASS   : CommandRoutingTests::mainQmlLoadsWithoutQmlWarnings()
PASS   : CommandRoutingTests::cleanupTestCase()
Totals: 3 passed, 3 failed, 0 skipped, 0 blacklisted, 3407ms
0% tests passed, 1 tests failed out of 1
```

### Pass after (`7bb7f8b`)

```
44: PASS   : CommandRoutingTests::initTestCase()
44: PASS   : CommandRoutingTests::explicitRouteExecutesOnce()
44: PASS   : CommandRoutingTests::defaultKeysExecuteOnce()
44: PASS   : CommandRoutingTests::storageProfileIsControllerOnly()
44: PASS   : CommandRoutingTests::mainQmlLoadsWithoutQmlWarnings()
44: PASS   : CommandRoutingTests::cleanupTestCase()
44: Totals: 6 passed, 0 failed, 0 skipped, 0 blacklisted, 3295ms
1/1 Test #44: command_routing ..................   Passed    3.32 sec
100% tests passed, 0 tests failed out of 1
```

## Provisional CMake target (for S1 or the lead to replace)

`cmake/CommandRoutingTests.cmake`, included from `CMakeLists.txt` after `include(cmake/MonitorCustomization.cmake)` inside `if(BUILD_TESTING)`. It has to come after that include because it calls `glancelot_attach_monitor_customization`: the shared `Harness` constructs `MonitorCustomization`, and the consumer list in `cmake/MonitorCustomization.cmake` is hard-coded. A cleaner fix is to add `command_routing_tests` to that list and move the include back next to the other test targets.

- Target `command_routing_tests`, test name `command_routing`.
- Sources: `tests/command_routing_tests.cpp`, plus the same app sources as `input_ui_tests`: `viewer_controller`, `settings_file`, `pane_layout`, `directory_entry_model`, `theme_manager`, `filmstrip_model`, `folder_tree_model`, `file_operation_service`, `src/render/image_canvas`. Monitor-customization sources come through `glancelot_attach_monitor_customization`; recovery sources are attached automatically by `glancelot_attach_recovery_consumers()`.
- Links `glancelot_core glancelot_platform Qt6::Quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test Qt6::Concurrent`; the theme catalog, fonts and brand resources; the canvas shaders; `add_dependencies(... glancelot-decoder)`; `GLANCELOT_TEST_SOURCE_DIR`.
- Environment `QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software`. Timeout 120 s; it runs in about 3.5 s.
- For EN-03 labels: `ui`. It does not touch the clipboard and has no timing budget.

## Build and test environment (Linux cloud)

- The session's network policy blocks `download.qt.io` and its mirrors (proxy CONNECT 403), so the cycle doc's `aqt install-qt` failed. I used **Qt 6.8.3 from conda-forge** instead: package `qt6-main-6.8.3-hb82b983_5`, installed with micromamba into `$HOME/qtconda`. It includes qtbase, qtdeclarative and qtshadertools, with private headers. This is the same Qt version but a different build from the Dockerfile's aqt `linux_gcc_64`. System: Ubuntu 24.04, GCC 13, CMake and Ninja from apt, Xvfb installed.
- `cmake -S . -B build-claude-en-06-routing -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=$HOME/qtconda`, then `cmake --build … --parallel 4`, then `QT_QPA_PLATFORM=offscreen ctest --test-dir build-claude-en-06-routing --output-on-failure`.
- For EN-02, the cycle doc's Qt download path does not work from this cloud environment.

### Targeted run (fix applied)

```
1/7 Test #12: folder_grid ......................***Failed  149.90 sec
FAIL!  : GridUiTests::externalRenameAndDeleteRefreshKeepSelection() Compared lists have different sizes.
Totals: 73 passed, 1 failed, 4 skipped, 0 blacklisted, 149851ms
2/7 Test #13: input_ui .........................***Failed   66.75 sec
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()
Totals: 31 passed, 1 failed, 1 skipped, 0 blacklisted, 66707ms
3/7 Test #28: shortcuts_help ...................   Passed    0.26 sec
4/7 Test #31: io_scheduler .....................   Passed    0.06 sec
5/7 Test #32: metadata .........................   Passed    0.02 sec
6/7 Test #33: animation ........................   Passed    0.38 sec
7/7 Test #44: command_routing ..................   Passed    3.64 sec
71% tests passed, 2 tests failed out of 7
```

Rerun of `folder_grid` and `input_ui`: the same single case failed in each (73/1/4 and 31/1/1).

### Full offscreen suite, once (fix applied)

```
 1/44 selection_export Passed   ·  2/44 perf_trace Passed  ·  3/44 navigation Passed  ·  4/44 directory_listing Passed
 5/44 file_operations Passed    ·  6/44 startup_window_geometry Passed  ·  7/44 viewer_acceptance Passed  ·  8/44 thumbnails Passed
 9/44 folder_tree Passed        · 10/44 pane_layout Passed  · 11/44 window_slots Passed (15.20 s)
12/44 Test #12: folder_grid ......................***Failed  151.46 sec
13/44 Test #13: input_ui .........................***Failed   66.55 sec
14/44 pathbar_ui Passed         · 15/44 file_operation_service Passed
16/44 Test #16: directory_model ..................***Failed    4.36 sec
17-22 viewport, themes, platform_integration, grid_search, grid_density, ui_settings: Passed
23/44 fs_watch Passed (70.92 s) · 24/44 fs_watch_budget Passed (35.61 s) · 25/44 fs_watch_ui Passed
26-36 favorites, folder_history, shortcuts_help, filter, cache, io_scheduler, metadata, animation, mutation, copy_move, format: Passed
37-43 monitor_output, recovery, recovery_startup, recovery_controller, recovery_process, monitor_customization, monitor_customization_ui: Passed
44/44 Test #44: command_routing ..................   Passed    3.66 sec
93% tests passed, 3 tests failed out of 44
Total Test time (real) = 450.50 sec
```

The `ctest` lines are condensed above. The failures are quoted exactly:

```
FAIL!  : GridUiTests::externalRenameAndDeleteRefreshKeepSelection() Compared lists have different sizes.   (grid_ui_tests.cpp:1646)
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()        (input_ui_tests.cpp:1133)
FAIL!  : DirectoryModelTests::selectionSurvivesRelistSortAndDropsRemovedKeys() Compared lists have different sizes.  (directory_model_tests.cpp:163)
```

The rerun of those three gave the same three cases failing again (`0% tests passed, 3 tests failed out of 3`).

### The three failures are not caused by EN-06

- **They fail the same way on the base code.** I stashed the fix, rebuilt `grid_ui_tests` and `input_ui_tests` from the unfixed `viewer_controller.cpp` and `Main.qml`, and ran the two cases. Both failed at the same lines: `grid_ui_tests.cpp(1646)`, totals 2 passed / 1 failed; `input_ui_tests.cpp(1133)`, totals 2 passed / 1 failed.
- **`directory_model_tests` cannot be affected by this change.** It compiles only `directory_entry_model.cpp`: no controller and no QML.
- **Likely cause of the two rename failures.** `directory_model` and `folder_grid` fail in the same way. In this container (ext4 overlay), `birthTime()` is valid, so the tests expect a renamed file to stay selected, but the model does not keep the selection across the rename.
- **`input_ui`.** In `treeRowAndPathBarMenusOpenAndMatch`, right-clicking a folder-tree row does not open its menu offscreen.
- **The Windows gate is authoritative** for these three. The Docker gate on `ec05e42` reported 43/43, so these may be specific to this cloud environment or to the conda-forge Qt build.

## Unverified

- **Windows and Docker.** Not run here, because the machine lock and Docker are not available in the cloud. The authoritative `lead-windows-gate` decides.
- **The three pre-existing failures** above, on the gate machine.
- **Physical hardware.** Nothing in this packet needs it.
- **ui-design-skills (application-interface-design and the rest).** These skills are not available in this session. The QML change has no visual effect: it removes duplicate command branches and splits two joined lines. No layout, spacing, typography or motion changed.

## Notes for the lead and for later packets

- **Stale "temporary" comments.** EN-06's plan row asks the lead to remove them from `cmake/MonitorCustomization.cmake` (lines 1–2), `cmake/RecoveryTests.cmake` and `cmake/FSWatchTests.cmake`. They were not touched here, because S1 owns those files this cycle.
- **Convention for EN-07.** An id that the controller executes in `trigger` (now `toggle-metadata`, `animation-*`, `presentation-*`) emits `viewCommand` only so the view can run its shared first step. It must not have an executing branch in QML. The `copy-*` commands use the other pattern: C++ executes them only when nothing listens, and otherwise QML executes them.
- **STACK-F054.** It rebases onto this branch. The `Main.qml` diff is three small hunks: around line 685, and the two split lines near 1321 and 2046.
