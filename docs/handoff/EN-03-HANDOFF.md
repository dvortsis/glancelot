# EN-03 handoff: CTest labels and resource locks

Session: S1 build (cloud), Phase 0 cycle 1. Scope and acceptance: docs/handoff/LEAD-PLAN-2026-10-22.md section 3 (EN-03 row) and docs/handoff/PHASE0-CYCLE1-2026-10-22.md.

| Item | Value |
| --- | --- |
| Branch | `claude/en-03-ctest-labels`, stacked on EN-05 |
| Base SHA | `d355edd67c6751f56d1b8d351bc7af9d2529f093` (EN-05 branch tip). Contract base below EN-05: `afeac1a` |
| Code tip SHA (built and tested) | `dcebfceb02171959d12ac33aeee65bc07c296496` |
| Branch tip | the commit that adds this handoff and the `docs/TESTING.md` section, directly on top of `dcebfce`; docs only |
| Review diff | `git diff d355edd <branch tip>` |
| Review round 1 | `5c2dd62` (`build(EN-03): review round 1`, on top of `b1ca735`) plus a docs-only follow-up commit with the extra negative checks; configure-only checks, no build or test run |

## What changed

> **Current design (review round 2):**
> - `cmake/TestLabels.cmake` is included near the top of the root `CMakeLists.txt`, before any `add_test`. It defines `glancelot_test_labels(<test> [LABELS ...] [LOCKS ...])` and holds the central declarations for the existing 43 tests.
> - `glancelot_finalize_test_labels()` runs at the very end of the root file. It applies the declarations and computes `fast`.
> - A new test needs no edit to this file and is `fast` by default.
>
> Items 1 and 2 below describe the original version; see "Review round 2".

1. **`cmake/TestLabels.cmake` (new).** The root `CMakeLists.txt` includes it once, at the end, under `if(BUILD_TESTING)`, after every test, including those in `MonitorCustomization.cmake`, is registered. It holds one name list per label and:
   - appends `LABELS` to each listed test. (Review round 1: a listed name that is not a registered test now fails configure; with `GLANCELOT_BUILD_VIEWER=OFF` only viewer test names are dropped. See "Review round 1 fixes".);
   - appends `RESOURCE_LOCK budget` to the seven budget tests and `RESOURCE_LOCK clipboard` to the clipboard tests;
   - reads the directory `TESTS` property and stops configure with `FATAL_ERROR` if any registered test has no label, so a new test cannot silently miss the fast loop. I checked this by temporarily dropping `animation` from the lists: configure printed `Test 'animation' has no CTest label. Add it to a list in cmake/TestLabels.cmake (see docs/TESTING.md).` I then restored the list. (Superseded in review round 2: the no-label check is gone, and a new target such as EN-06's `command_routing` no longer needs a line in this file.)
2. **`CMakeLists.txt`:** three added lines (the include). No `add_test` changed, so every QTest `-o <name>-results.txt,txt -o -,txt` argument is unchanged (`ctest -N -V` shows them, for example `grid_ui_tests "-o" ".../folder_grid-results.txt,txt" "-o" "-,txt"`).
3. **`docs/TESTING.md`:** a new section, "Run part of the automated suite", with a label table, `ctest -L fast`, `ctest -LE budget`, `-L ui`, `-N -L`, the lock behaviour under `-j`, and how to label a new test.

### Assignment (43 tests, all labelled; updated in review round 1)

| Label | Count | Tests | How a test qualifies (from the test source) |
| --- | --- | --- | --- |
| `fast` | 27 | every test that is not `ui`, `budget` or `native` | quick loop; computed at configure since round 2 |
| `ui` | 5 | `folder_grid`, `input_ui`, `pathbar_ui`, `fs_watch_ui`, `monitor_customization_ui` | loads the shipped `ui/Main.qml` in a `QQuickWindow` |
| `budget` | 7 | `fs_watch_budget`, `fs_watch`, `folder_grid`, `cache`, `window_slots`, `recovery_controller`, `input_ui` | the seven tests the plan names (also AGENTS.md); `RESOURCE_LOCK budget` |
| `native` | 9 | `platform_integration`, `file_operation_service`, `recovery_process`, `folder_grid`, `window_slots`, `fs_watch`, `file_operations`, `directory_listing`, `selection_export` | real OS desktop shell, trash, `subst`, `cmd` `mklink`, or a window on the real platform plugin (not forced offscreen). Details in "Review round 1 fixes" |
| `process` | 23 (round 3) | see "Review round 2" and "Review round 3" | any child process, directly or through product code (decoder, helper, app, Python, `cmd`, `subst`). Can overlap `fast`. Round 1 had 8 tests, under a narrower rule |
| `clipboard` | 3 | `folder_grid`, `input_ui`, `metadata` | reaches `QGuiApplication::clipboard()` directly or through product code. `RESOURCE_LOCK clipboard` |

## Build environment

The build, timing, lock and suite outputs from here to "Review round 1 fixes" were recorded at `dcebfce`, before review round 1, so they show the old label counts (`fast` 32, `native` 6, `clipboard` 2). Round 1 changed only label assignment and configure checks, not tests or commands.

The same as EN-05: Ubuntu 24.04.5, g++ 13.3.0, CMake 3.28.3, Ninja, and Qt 6.8.3 from conda-forge (`qt6-main 6.8.3 hb82b983_5`) in `/opt/qt683`, because the network policy blocks `download.qt.io`. Fresh build directory `build-claude-en-03`: 564 steps, 6 min 35 s on 4 vCPU.

```
$ ctest -N | tail -1
Total Tests: 43
$ for l in fast ui budget process native clipboard; do echo "$l: $(ctest -N -L "^$l\$" | grep -c 'Test *#')"; done
fast: 32
ui: 5
budget: 7
process: 8
native: 6
clipboard: 2
$ ctest -N -LE budget | tail -1
Total Tests: 36
```

## `ctest -L fast` timing (acceptance: under about 3 minutes offscreen)

Serial, timed with `date` around the command: **44.2 s wall**.

```
$ QT_QPA_PLATFORM=offscreen ctest -L fast --output-on-failure
 1/32 Test  #1: selection_export .................   Passed    0.03 sec
 2/32 Test  #2: perf_trace .......................   Passed    0.03 sec
 3/32 Test  #3: navigation .......................   Passed    0.14 sec
 4/32 Test  #4: directory_listing ................   Passed    0.64 sec
 5/32 Test  #5: file_operations ..................   Passed    0.01 sec
 6/32 Test  #6: startup_window_geometry ..........   Passed    0.01 sec
 7/32 Test  #7: viewer_acceptance ................   Passed    5.17 sec
 8/32 Test  #8: thumbnails .......................   Passed    3.63 sec
 9/32 Test  #9: folder_tree ......................   Passed    6.05 sec
10/32 Test #10: pane_layout ......................   Passed    0.09 sec
11/32 Test #15: file_operation_service ...........   Passed    0.97 sec
12/32 Test #16: directory_model ..................***Failed    3.93 sec
13/32 Test #17: viewport .........................   Passed    0.03 sec
14/32 Test #18: themes ...........................   Passed    0.14 sec
15/32 Test #19: platform_integration .............   Passed    0.47 sec
16/32 Test #20: grid_search ......................   Passed    0.11 sec
17/32 Test #21: grid_density .....................   Passed    0.07 sec
18/32 Test #22: ui_settings ......................   Passed    0.42 sec
19/32 Test #26: favorites ........................   Passed    0.04 sec
20/32 Test #27: folder_history ...................   Passed    0.01 sec
21/32 Test #28: shortcuts_help ...................   Passed    0.21 sec
22/32 Test #29: filter ...........................   Passed    1.41 sec
23/32 Test #31: io_scheduler .....................   Passed    0.05 sec
24/32 Test #32: metadata .........................   Passed    0.02 sec
25/32 Test #33: animation ........................   Passed    0.37 sec
26/32 Test #34: mutation .........................   Passed    0.05 sec
27/32 Test #35: copy_move ........................   Passed    0.06 sec
28/32 Test #36: format ...........................   Passed    0.02 sec
29/32 Test #37: monitor_output ...................   Passed    2.08 sec
30/32 Test #38: recovery .........................   Passed   10.92 sec
31/32 Test #39: recovery_startup .................   Passed    0.16 sec
32/32 Test #42: monitor_customization ............   Passed    6.80 sec
97% tests passed, 1 tests failed out of 32
Label Time Summary:
fast       =  44.15 sec*proc (32 tests)
native     =  10.32 sec*proc (4 tests)
process    =  15.23 sec*proc (5 tests)
Total Test time (real) =  44.16 sec
The following tests FAILED:
	 16 - directory_model (Failed)
wall 44.173814982 s
```

With `-j4`: **13.9 s**.

```
$ QT_QPA_PLATFORM=offscreen ctest -L fast -j4 --output-on-failure
 1/32 Test  #7: viewer_acceptance ................   Passed    5.15 sec
 2/32 Test #42: monitor_customization ............   Passed    6.75 sec
 3/32 Test  #8: thumbnails .......................   Passed    3.71 sec
 4/32 Test  #6: startup_window_geometry ..........   Passed    0.01 sec
 5/32 Test #38: recovery .........................   Passed   10.84 sec
 6/32 Test #37: monitor_output ...................   Passed    2.27 sec
 7/32 Test #16: directory_model ..................***Failed   11.60 sec
 8/32 Test #15: file_operation_service ...........   Passed    0.93 sec
 9/32 Test #29: filter ...........................   Passed    1.47 sec
10/32 Test #22: ui_settings ......................   Passed    0.51 sec
11/32 Test #35: copy_move ........................   Passed    0.07 sec
12/32 Test #19: platform_integration .............   Passed    0.49 sec
13/32 Test  #9: folder_tree ......................   Passed    6.23 sec
14/32 Test #33: animation ........................   Passed    0.39 sec
15/32 Test #28: shortcuts_help ...................   Passed    0.27 sec
16/32 Test #17: viewport .........................   Passed    0.03 sec
17/32 Test #26: favorites ........................   Passed    0.10 sec
18/32 Test #18: themes ...........................   Passed    0.28 sec
19/32 Test #10: pane_layout ......................   Passed    0.27 sec
20/32 Test #32: metadata .........................   Passed    0.03 sec
21/32 Test #39: recovery_startup .................   Passed    0.21 sec
22/32 Test #31: io_scheduler .....................   Passed    0.07 sec
23/32 Test #21: grid_density .....................   Passed    0.10 sec
24/32 Test #34: mutation .........................   Passed    0.05 sec
25/32 Test  #1: selection_export .................   Passed    0.05 sec
26/32 Test #36: format ...........................   Passed    0.03 sec
27/32 Test  #3: navigation .......................   Passed    0.71 sec
28/32 Test #27: folder_history ...................   Passed    0.02 sec
29/32 Test  #2: perf_trace .......................   Passed    0.03 sec
30/32 Test #20: grid_search ......................   Passed    0.11 sec
31/32 Test  #5: file_operations ..................   Passed    0.04 sec
32/32 Test  #4: directory_listing ................   Passed    2.29 sec
97% tests passed, 1 tests failed out of 32
Total Test time (real) =  13.89 sec
	 16 - directory_model (Failed)
```

`directory_model` is the pre-existing environment failure from the EN-05 handoff: this container's filesystem reports no birth time, and the model proves the rename by inode. It fails the same way at the base `afeac1a`.

## Lock check

The three budget tests run with `-j3`; `RESOURCE_LOCK budget` serialises them. Each one starts only after the previous one finished, and the total equals the sum of their durations:

```
$ QT_QPA_PLATFORM=offscreen ctest -R '^(cache|fs_watch_budget|recovery_controller)$' -j3 -V | grep -E 'Start|Test +#|Total Test'
    Start 24: fs_watch_budget
1/3 Test #24: fs_watch_budget ..................   Passed   36.88 sec
    Start 40: recovery_controller
2/3 Test #40: recovery_controller ..............   Passed   12.39 sec
    Start 30: cache
3/3 Test #30: cache ............................   Passed    2.16 sec
Total Test time (real) =  51.43 sec
```

## Full offscreen suite, at `dcebfce`

```
$ QT_QPA_PLATFORM=offscreen ctest --output-on-failure
 1/43 Test  #1: selection_export .................   Passed    0.11 sec
 2/43 Test  #2: perf_trace .......................   Passed    0.04 sec
 3/43 Test  #3: navigation .......................   Passed    0.35 sec
 4/43 Test  #4: directory_listing ................   Passed    0.99 sec
 5/43 Test  #5: file_operations ..................   Passed    0.01 sec
 6/43 Test  #6: startup_window_geometry ..........   Passed    2.97 sec
 7/43 Test  #7: viewer_acceptance ................   Passed   10.76 sec
 8/43 Test  #8: thumbnails .......................   Passed    6.99 sec
 9/43 Test  #9: folder_tree ......................   Passed    6.26 sec
10/43 Test #10: pane_layout ......................   Passed    0.25 sec
11/43 Test #11: window_slots .....................   Passed   15.64 sec
12/43 Test #12: folder_grid ......................***Failed  149.97 sec
FAIL!  : GridUiTests::externalRenameAndDeleteRefreshKeepSelection() Compared lists have different sizes.
13/43 Test #13: input_ui .........................***Failed   65.34 sec
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()
14/43 Test #14: pathbar_ui .......................   Passed    2.13 sec
15/43 Test #15: file_operation_service ...........   Passed    1.04 sec
16/43 Test #16: directory_model ..................***Failed    5.19 sec
FAIL!  : DirectoryModelTests::selectionSurvivesRelistSortAndDropsRemovedKeys() Compared lists have different sizes.
17/43 Test #17: viewport .........................   Passed    0.26 sec
18/43 Test #18: themes ...........................   Passed    0.28 sec
19/43 Test #19: platform_integration .............   Passed    0.48 sec
20/43 Test #20: grid_search ......................   Passed    0.11 sec
21/43 Test #21: grid_density .....................   Passed    0.12 sec
22/43 Test #22: ui_settings ......................   Passed    0.76 sec
23/43 Test #23: fs_watch .........................   Passed   70.09 sec
24/43 Test #24: fs_watch_budget ..................   Passed   23.10 sec
25/43 Test #25: fs_watch_ui ......................   Passed    1.94 sec
26/43 Test #26: favorites ........................   Passed    0.25 sec
27/43 Test #27: folder_history ...................   Passed    0.02 sec
28/43 Test #28: shortcuts_help ...................   Passed    0.36 sec
29/43 Test #29: filter ...........................   Passed    1.73 sec
30/43 Test #30: cache ............................   Passed    2.38 sec
31/43 Test #31: io_scheduler .....................   Passed    0.13 sec
32/43 Test #32: metadata .........................   Passed    0.20 sec
33/43 Test #33: animation ........................   Passed    0.38 sec
34/43 Test #34: mutation .........................   Passed    0.11 sec
35/43 Test #35: copy_move ........................   Passed    0.41 sec
36/43 Test #36: format ...........................   Passed    0.08 sec
37/43 Test #37: monitor_output ...................   Passed    2.22 sec
38/43 Test #38: recovery .........................   Passed   11.33 sec
39/43 Test #39: recovery_startup .................   Passed    0.22 sec
40/43 Test #40: recovery_controller ..............   Passed   12.64 sec
41/43 Test #41: recovery_process .................   Passed   27.81 sec
42/43 Test #42: monitor_customization ............   Passed    7.28 sec
43/43 Test #43: monitor_customization_ui .........   Passed   15.48 sec
93% tests passed, 3 tests failed out of 43
Label Time Summary:
budget       = 339.16 sec*proc (7 tests)
clipboard    = 215.30 sec*proc (2 tests)
fast         =  61.69 sec*proc (32 tests)
native       =  54.31 sec*proc (6 tests)
process      = 132.68 sec*proc (8 tests)
ui           = 234.85 sec*proc (5 tests)
Total Test time (real) = 448.24 sec
The following tests FAILED:
	 12 - folder_grid (Failed)
	 13 - input_ui (Failed)
	 16 - directory_model (Failed)
```

That is 43 tests, the same set and the same results as EN-05 and the unmodified base, with the same three failures. The new `Label Time Summary` block is the only change to the output.

Rerun of the failures once (budget-test rule):

```
$ QT_QPA_PLATFORM=offscreen ctest -R '^(folder_grid|input_ui|directory_model)$' --output-on-failure
1/3 Test #12: folder_grid ......................***Failed  147.55 sec
FAIL!  : GridUiTests::externalRenameAndDeleteRefreshKeepSelection() Compared lists have different sizes.
Totals: 73 passed, 1 failed, 4 skipped, 0 blacklisted, 147506ms
2/3 Test #13: input_ui .........................***Failed   65.67 sec
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()
Totals: 31 passed, 1 failed, 1 skipped, 0 blacklisted, 65629ms
3/3 Test #16: directory_model ..................***Failed    6.19 sec
FAIL!  : DirectoryModelTests::selectionSurvivesRelistSortAndDropsRemovedKeys() Compared lists have different sizes.
Totals: 19 passed, 1 failed, 0 skipped, 0 blacklisted, 6172ms
0% tests passed, 3 tests failed out of 3
Total Test time (real) = 219.41 sec
```

These failures are deterministic and environmental, the same as at `afeac1a`; see docs/handoff/EN-05-HANDOFF.md for the base build and the causes. An earlier full-suite attempt was cut off when the cloud worker restarted (exit 137) before any result; the run above is a complete rerun.

## Review round 1 fixes

The lead decided these changes after the stage-1 review. The original implementer was not reachable, so a fix implementer applied them on the lead's Windows PC (Windows 11, MSVC 2022 x64 environment from `VsDevCmd.bat` as `scripts/build.ps1` sets it up, CMake 4.4.4 from `.tools/python`, Ninja, Qt 6.8.3 msvc2022_64 from `.tools/Qt`). The checks were configure-only, as the lead asked: nothing was built and no test was run.

### 1. Clipboard lock covers product-code clipboard use

`src/` writes `QGuiApplication::clipboard()` in two places:
- `MetadataModel::copySummary`, `copyGps` and `copyTag` (`src/app/metadata_model.cpp:206-226`);
- `ViewerController::copyDirectory`, `copyFilePath`, `copyFileUrls` and `copyImageBitmap` (`src/app/viewer_controller.cpp:1279-1366`).

Tests reach them in these ways:

| Path to the clipboard | Test |
| --- | --- |
| `metadata_tests.cpp:497-506` calls `copySummary`, `copyGps` and `copyTag` directly | `metadata`, **added** |
| `grid_ui_tests.cpp:2598-2678` calls `copyFilePath`, `copyFileUrls` and `copyImageBitmap`. `:865-877` clicks `copyFolderAddress`, which calls `copyDirectory`, then pastes with Ctrl+V | `folder_grid` (already locked) |
| `input_ui_tests.cpp:1153-1155` clicks `menuItem-copy-folder-path`, which calls `copyDirectory` | `input_ui` (already locked) |

These paths do not reach the clipboard from any test:
- No test calls `trigger("copy-path"/"copy-file"/"copy-image")` (the `viewer_controller.cpp:1609-1620` fallbacks).
- No test sends a copy id through `invoke`. The `invoke` calls in tests use only `rotate`, `monitor-*`, `layout-reset` and `pane-detach-metadata`.
- No test clicks the `MetadataHud.qml` copy buttons or the favorites "Copy path" item (`FolderTree.qml:944`).
- No test presses Ctrl+C, Ctrl+Shift+C or Ctrl+Alt+C. `grid_ui_tests.cpp:2585-2595` and `shortcuts_help_tests.cpp:106` only read binding names. `window_slot_tests` binds the `Ctrl+K, Ctrl+C` string to `rotate`.
- `copy_move` uses only the service's internal cut/copy state.
- `pathbar_ui` includes `<QClipboard>` through `ui_test_support.hpp` but never uses it.

The `clipboard` list is now `folder_grid input_ui metadata`.

### 2. Label rule

The definitions now match the lead's rule. `docs/TESTING.md` and the header of `cmake/TestLabels.cmake` use the same wording.

- **`fast`** is every test that is not `ui`, `budget` or `native`. Configure now enforces this: a test that is none of the three must be in the `fast` list, and a test that is one of them must not be. Either mismatch fails configure.
- **`native`** covers tests that touch the real OS: desktop shell integration, the trash, `subst`, `cmd` `mklink`, or a window on the real platform plugin (not forced offscreen).

| Test | Why it is `native` | Was `fast`? |
| --- | --- | --- |
| `platform_integration` | real shell integration (reveal/terminal through COM/`explorer`, launcher fixtures) | yes, now removed |
| `file_operation_service` | real `platform_trash::make()` and `subst` (`file_operation_service_tests.cpp:273-307`) | yes, now removed |
| `recovery_process` | starts the real app with `QT_QPA_PLATFORM=windows` (or `xcb` under Xvfb), `scripts/verify-recovery.py:128` | no |
| `folder_grid` | `cmd /c mklink /J` (`grid_ui_tests.cpp:1453`) | no (`ui`, `budget`) |
| `window_slots` | `cmd.exe /c mklink /J` (`window_slot_tests.cpp:611`) | no (`budget`) |
| `fs_watch` | `cmd.exe /d /c mklink /J` (`fs_watch_tests.cpp:40`) | no (`budget`) |
| `file_operations` | `mklink /J` through `system()` (`file_operations_tests.cpp:42`) | yes, now removed (core test) |
| `directory_listing` | `cmd /d /c mklink /J` (`directory_listing_tests.cpp:105`) | yes, now removed (core test) |
| `selection_export` | `cmd /c mklink /J` (`selection_export_tests.cpp:510`) | yes, now removed (core test) |

**Removed from `native`:** `monitor_output`, `monitor_customization` and `monitor_customization_ui`.
- All three are forced offscreen: `CMakeLists.txt:313` and `cmake/MonitorCustomization.cmake:50` set `ENVIRONMENT QT_QPA_PLATFORM=offscreen`, and `monitor_output_tests.cpp:764` sets it again unless `GLANCELOT_MONITOR_NATIVE_COLOR_PROOF` opts in.
- They use synthetic screens (`tests/monitor_customization_screens.hpp`).
- `monitor_output` and `monitor_customization` stay in `fast`; `monitor_customization_ui` stays `ui`.

**Native through product code (checked):** I checked whether a `fast` test reaches the real shell or trash through product code. None does.
- The real trash is reached only through `platform_trash::make()`, which the default `FileOperationService` constructor uses (`src/app/file_operation_service.cpp:281-283`). No `fast` test constructs a `FileOperationService`. The default backend appears only in `file_operation_service` (native), in `ui_test_support.hpp`, and in `fs_watch_ui` and `monitor_customization_ui`, which are `ui` and never delete. `grid_ui` and `input_ui` inject fake backends.
- Core `executeDeletion` takes the backend as a parameter, and `file_operations` passes a fake one.
- The shell is reached only through `PlatformIntegration` (`startDetached` to `explorer`, a terminal or `xdg-open`, and `SHOpenFolderAndSelectItems`). Only `platform_integration` calls it with a valid path. `folder_grid` passes invalid paths, and `shortcuts_help` only reads binding names.

**Unchanged:** `navigation` stays `fast`, because it makes links with `std::filesystem` only and starts no shell. `process` has the same 8 tests: it is for hermetic children, and a test whose only child is `cmd`, `subst` or `explorer` is not `process`.

Five tests were removed from `fast`: `selection_export`, `directory_listing`, `file_operations`, `file_operation_service` and `platform_integration`. That leaves 27, down from 32. With `GLANCELOT_BUILD_VIEWER=OFF`, three of the five core tests are `native`, so only `perf_trace` and `navigation` remain `fast`.

**`fast` time (estimate):**
- **Linux, serial:** about 42.0 s. The `ctest -L fast` run above took 44.15 s; the five removed tests account for 0.03 + 0.64 + 0.01 + 0.97 + 0.47 = 2.12 s of it.
- **Windows:** about 71 s, scaling the 75 s figure under "Unverified".

Neither figure was measured. The `mklink` branches never run on Linux, so their Windows time is not part of any measurement.

### 3. A misspelled name fails configure

The `if(TEST ...)` skips are gone. Before any label or lock is applied, every name in every list (`fast ui budget native process clipboard`) is checked against the directory `TESTS` property, so the check covers both the label loop and the lock loop. A name that is not registered stops configure with `message(FATAL_ERROR ...)`.
- **Viewer on:** every listed name must exist.
- **`GLANCELOT_BUILD_VIEWER=OFF`:** names that are not in the new `glancelot_tests_core` list (`selection_export perf_trace navigation directory_listing file_operations`) are dropped, because viewer tests do not exist then. Core names are still required, and the core list itself is checked.

Limit: with the viewer off, a misspelled name in a viewer-only list cannot be told apart from a viewer test. The viewer-on configure, which is the normal and gate configuration, catches it. A misspelled core name with the viewer off is still caught, by the missing-label or `fast` checks, as shown below.

### Configure and `ctest -N` output

Viewer on (`build-en03-cfg`):

```
> cmake -S . -B build-en03-cfg -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DGLANCELOT_BUILD_VIEWER=ON -DCMAKE_PREFIX_PATH=C:\src\Github\Glancelot\.tools\Qt\6.8.3\msvc2022_64
-- Configuring done (88.8s)
-- Generating done (9.1s)
-- Build files have been written to: C:/src/Github/Glancelot/.worktrees/claude-en-03/build-en03-cfg
configure exit: 0
> ctest --test-dir build-en03-cfg -N
Total Tests: 43
> ctest --test-dir build-en03-cfg --print-labels
All Labels:
  budget
  clipboard
  fast
  native
  process
  ui
> per label: ctest -N -L ^<label>$
fast : 27
ui : 5
budget : 7
native : 9
process : 8
clipboard : 3
> ctest --test-dir build-en03-cfg -N -LE budget
Total Tests: 36
> ctest --test-dir build-en03-cfg -N -L ^fast$
  Test  #2: perf_trace
  Test  #3: navigation
  Test  #6: startup_window_geometry
  Test  #7: viewer_acceptance
  Test  #8: thumbnails
  Test  #9: folder_tree
  Test #10: pane_layout
  Test #16: directory_model
  Test #17: viewport
  Test #18: themes
  Test #20: grid_search
  Test #21: grid_density
  Test #22: ui_settings
  Test #26: favorites
  Test #27: folder_history
  Test #28: shortcuts_help
  Test #29: filter
  Test #31: io_scheduler
  Test #32: metadata
  Test #33: animation
  Test #34: mutation
  Test #35: copy_move
  Test #36: format
  Test #37: monitor_output
  Test #38: recovery
  Test #39: recovery_startup
  Test #42: monitor_customization
Total Tests: 27
> ctest --test-dir build-en03-cfg -N -L ^native$
  Test  #1: selection_export
  Test  #4: directory_listing
  Test  #5: file_operations
  Test #11: window_slots
  Test #12: folder_grid
  Test #15: file_operation_service
  Test #19: platform_integration
  Test #23: fs_watch
  Test #41: recovery_process
Total Tests: 9
> ctest --test-dir build-en03-cfg -N -L ^clipboard$
  Test #12: folder_grid
  Test #13: input_ui
  Test #32: metadata
Total Tests: 3
> ctest --test-dir build-en03-cfg -N -L ^fast$ -L ^native$     (both labels; CMake 4.4 ANDs repeated -L)
Total Tests: 0
> RESOURCE_LOCK from ctest --show-only=json-v1
window_slots: budget
folder_grid: budget,clipboard
input_ui: budget,clipboard
fs_watch: budget
fs_watch_budget: budget
cache: budget
metadata: clipboard
recovery_controller: budget
```

The count matches the rule: 43 tests, minus the 16 in the union of `ui`, `budget` and `native`, leaves 27 `fast`. (The unfiltered `ctest -N` output also prints "Could not find executable" for each test, because nothing was built; the listings above keep only the test lines.)

Viewer off (`build-en03-core`):

```
> cmake -S . -B build-en03-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DGLANCELOT_BUILD_VIEWER=OFF
-- Configuring done (12.0s)
-- Generating done (0.3s)
configure exit: 0
> ctest --test-dir build-en03-core -N
  Test #1: selection_export
  Test #2: perf_trace
  Test #3: navigation
  Test #4: directory_listing
  Test #5: file_operations
Total Tests: 5
> ctest --test-dir build-en03-core --print-labels
All Labels:
  fast
  native
> -L ^fast$   -> perf_trace, navigation            Total Tests: 2
> -L ^native$ -> selection_export, directory_listing, file_operations   Total Tests: 3
```

Negative checks: a name was misspelled temporarily, configure ran in a separate scratch directory, and then the name was restored.

```
# clipboard list: metadata -> metdata, viewer on (build-en03-neg)
CMake Error at cmake/TestLabels.cmake:45 (message):
  cmake/TestLabels.cmake: 'metdata' in the clipboard list is not a registered
  test.  Fix the name or remove it.
Call Stack (most recent call first):
  CMakeLists.txt:324 (include)
-- Configuring incomplete, errors occurred!
configure exit: 1

# same misspelling, viewer off (build-en03-negcore): viewer names are dropped by design
-- Configuring done (10.8s)
configure exit: 0

# fast list: navigation -> navigaton, viewer off (build-en03-negcore)
CMake Error at cmake/TestLabels.cmake:66 (message):
  Test 'navigation' has no CTest label.  Add it to a list in
  cmake/TestLabels.cmake (see docs/TESTING.md).
Call Stack (most recent call first):
  CMakeLists.txt:324 (include)
-- Configuring incomplete, errors occurred!
configure exit: 1

# fast mismatch A: perf_trace removed from fast and put only in process, viewer off (build-en03-negA)
CMake Error at cmake/TestLabels.cmake:81 (message):
  Test 'perf_trace' is not ui, budget or native, so it must be in the fast
  list in cmake/TestLabels.cmake (see docs/TESTING.md).
Call Stack (most recent call first):
  CMakeLists.txt:324 (include)
-- Configuring incomplete, errors occurred!
configure exit: 1

# fast mismatch B: navigation added to native and kept in fast, viewer off (build-en03-negB)
CMake Error at cmake/TestLabels.cmake:83 (message):
  Test 'navigation' is ui, budget or native, so it must not be in the fast
  list in cmake/TestLabels.cmake (see docs/TESTING.md).
Call Stack (most recent call first):
  CMakeLists.txt:324 (include)
-- Configuring incomplete, errors occurred!
configure exit: 1
```

After each negative check the file was restored, and `git diff HEAD` was empty. All scratch build directories (`build-en03-cfg`, `-core`, `-neg`, `-negcore`, `-negA`, `-negB`) were deleted afterwards.

## Review round 2

This round answers `docs/verification/EN-03-REVIEW.md` at `origin/claude/review-en-03` (`2c4ac8a`, CHANGES REQUIRED: B1, B2) and implements the lead's design. The environment is the same as round 1: the Windows PC, configure only, no build and no test run.

### B1. Computed `fast` and a per-feature helper

**`cmake/TestLabels.cmake`** is now included right after `option(GLANCELOT_BUILD_VIEWER ...)` in the root `CMakeLists.txt`, before any `add_test` (the first is in `cmake/ExportTests.cmake`). It defines:
- **`glancelot_test_labels(<test> [LABELS ui budget native process clipboard] [LOCKS budget clipboard])`.** A feature cmake file calls it next to its `add_test`.
  - It records the declaration in global properties, so it can also be called before the test exists.
  - An unknown label or lock fails at the call. `fast` is not a valid label.
  - `LABELS budget` and `LABELS clipboard` also add the matching `RESOURCE_LOCK`.
- **`glancelot_finalize_test_labels()`**, which the root file calls at its very end under `if(BUILD_TESTING)`, after every include. It does three things:
  - fails if any declared test is not in the root directory's `TESTS` property;
  - applies the declared `LABELS` and `RESOURCE_LOCK`;
  - appends `fast` to every registered test without `ui`, `budget` or `native`.

  It also walks `SUBDIRECTORIES` and fails if one registers tests. Today there are none, and labels are applied only in the root directory. (Changed in round 3: subdirectory tests are now collected and labelled.)

**What was removed:** the hand-kept `fast` list, both `fast`-mismatch errors, the "no CTest label" error and the core-name list.

**The existing 43 tests** are still declared centrally, at the bottom of `TestLabels.cmake`. Core tests are declared unconditionally and viewer tests inside `if(GLANCELOT_BUILD_VIEWER)`, so every declaration must exist in either configuration. New packets add their own `glancelot_test_labels` calls instead of editing this file.

### B2. `process` = any child process

The rule is now: a test is `process` if it starts any child process, directly or through product code. That includes `glancelot-decoder` through `ViewerController::open` (`viewer_controller.cpp:870`), its prefetch worker (`:613`) or `ThumbnailService` (`thumbnail_service.cpp:179-201`), a test helper, the app, Python, `cmd` and `subst`. The `PlatformIntegration` `startDetached` calls are the remaining product-code children (see round 1).

I audited all 43 tests statically. The reviewer's process poller saw `acceptance_tests`, `folder_tree_tests`, `filter_tests` and `cache_tests` start the decoder, which agrees with this audit.

**Added** (14):

| Test | Child |
| --- | --- |
| `viewer_acceptance`, `folder_tree`, `filter`, `cache` | decoder via `ViewerController::open` (reviewer observed it) |
| `copy_move` | `controller.open(sample.jpg)` (`copy_move_tests.cpp:354`) |
| `fs_watch_budget` | `viewer.open(image)` (`fs_watch_budget_tests.cpp:81`) |
| `folder_grid`, `input_ui`, `monitor_customization_ui`, `recovery_controller` | open generated images through the viewer (33, 6, 3 and 10 `open`/`openFolder`/`next` calls). `folder_grid` also runs `cmd mklink` |
| `file_operation_service` | `QProcess::execute("subst", ...)` |
| `file_operations`, `directory_listing`, `selection_export` | `cmd`/`std::system` `mklink /J` (Windows branches) |

**Kept** (8): `thumbnails`, `io_scheduler`, `fs_watch`, `window_slots`, `recovery`, `recovery_startup`, `platform_integration`, `recovery_process`.

**Not `process`**, because no child is started:
- ~~`pathbar_ui`~~: this was wrong, and round 3 corrects it. `pathbar_ui` builds `Media` (`p1.png`, `p2.png`), and `openFolder(h, media)` calls `viewer.open` (`tests/ui_test_support.hpp:126`), which starts the decoder.
- `fs_watch_ui`: navigates an empty folder.
- `monitor_output`: images reach the presentation window through `updateImage`; the viewer is never asked to open a file.
- `monitor_customization`: in-memory images, no `ViewerController`.
- `mutation`: reconciles non-existent paths only.
- `ui_settings`, `shortcuts_help`, `themes`: `ViewerController` used only for settings and bindings.
- `format`: `RawPreviewExtractor` in process. Its CMake defines `GLANCELOT_DECODER_PATH`, but the source never uses it.
- `metadata`, `animation`, `directory_model`, `grid_search`, `grid_density`, `favorites`, `folder_history`, `viewport`, `pane_layout`, `startup_window_geometry`, `perf_trace`, `navigation`: no process or viewer open.

The audit is static, so a test could still start the decoder through a path I missed. A missed `process` label adds no lock and changes no selection.

Labels are platform-independent, so a Windows-only `mklink` branch still makes a test `process` and `native`.

### Output

Viewer on, clean tree (`build-en03-cfg`):

```
configure exit: 0
> ctest -N                Total Tests: 43
> ctest --print-labels    budget clipboard fast native process ui
> -N -L ^fast$  (27)  perf_trace navigation startup_window_geometry viewer_acceptance thumbnails folder_tree pane_layout directory_model viewport themes grid_search grid_density ui_settings favorites folder_history shortcuts_help filter io_scheduler metadata animation mutation copy_move format monitor_output recovery recovery_startup monitor_customization
> -N -L ^ui$    (5)   folder_grid input_ui pathbar_ui fs_watch_ui monitor_customization_ui
> -N -L ^budget$ (7)  window_slots folder_grid input_ui fs_watch fs_watch_budget cache recovery_controller
> -N -L ^native$ (9)  selection_export directory_listing file_operations window_slots folder_grid file_operation_service platform_integration fs_watch recovery_process
> -N -L ^process$ (22) selection_export directory_listing file_operations viewer_acceptance thumbnails folder_tree window_slots folder_grid input_ui file_operation_service platform_integration fs_watch fs_watch_budget filter cache io_scheduler copy_move recovery recovery_startup recovery_controller recovery_process monitor_customization_ui
> -N -L ^clipboard$ (3) folder_grid input_ui metadata
> -N -LE budget           Total Tests: 36
> -N -L ^fast$ -L ^native$  Total Tests: 0
> RESOURCE_LOCK: window_slots budget; folder_grid budget,clipboard; input_ui budget,clipboard; fs_watch budget; fs_watch_budget budget; cache budget; metadata clipboard; recovery_controller budget
```

The `fast` set is identical to round 1 (27 tests), so the reviewer's measured serial Windows run still applies: 118.0 s, 27 of 27 passed. `docs/TESTING.md` now quotes it.

Viewer off (`build-en03-core`):

```
configure exit: 0
> ctest -N   Total Tests: 5
> labels     fast native process
> fast: perf_trace navigation | native: selection_export directory_listing file_operations | process: same three | clipboard: none
```

Scratch proofs: a temporary `cmake/ZzDummy.cmake` was included from `CMakeLists.txt` just before the finalize call. Neither file change was committed, and `git status` afterwards showed only the intended files.

```
# P1, viewer on. TestLabels.cmake untouched.
add_test(NAME zz_dummy COMMAND ${CMAKE_COMMAND} -E true)
add_test(NAME zz_declared COMMAND ${CMAKE_COMMAND} -E true)
glancelot_test_labels(zz_declared LABELS process LOCKS clipboard)
-- Configuring done (39.6s)
configure exit: 0
Total Tests: 45
All Labels: budget clipboard fast native process ui
fast: Total Tests: 29   (includes #44 zz_dummy, #45 zz_declared)
process: Total Tests: 23
zz_dummy: LABELS=fast RESOURCE_LOCK=
zz_declared: LABELS=fast,process RESOURCE_LOCK=clipboard

# N1, viewer off: glancelot_test_labels(zz_declared LABELS process LOCKS clipbaord)
  glancelot_test_labels(zz_declared): unknown lock 'clipbaord'.  Allowed:
  budget;clipboard.
Call Stack (most recent call first):
  cmake/ZzDummy.cmake:2 (glancelot_test_labels)
  CMakeLists.txt:325 (include)
-- Configuring incomplete, errors occurred!
configure exit: 1

# N2, viewer off: glancelot_test_labels(zz_declard LOCKS clipboard)   (test name misspelled)
CMake Error at cmake/TestLabels.cmake:68 (message):
  glancelot_test_labels(zz_declard ...): 'zz_declard' is not a registered
  test.  Fix the name or remove the declaration.
Call Stack (most recent call first):
  CMakeLists.txt:328 (glancelot_finalize_test_labels)
-- Configuring incomplete, errors occurred!
configure exit: 1

# N3, viewer off: glancelot_test_labels(zz_declared LABELS natve)
  glancelot_test_labels(zz_declared): unknown label 'natve'.  Allowed:
  ui;budget;native;process;clipboard (fast is computed).
Call Stack (most recent call first):
  cmake/ZzDummy.cmake:2 (glancelot_test_labels)
  CMakeLists.txt:325 (include)
-- Configuring incomplete, errors occurred!
configure exit: 1
```

In N1 and N3, the first line, `CMake Error at cmake/TestLabels.cmake:<line> (message):`, was cut off by the helper script's last-8-lines filter. The scratch build directories were deleted afterwards.

## Review round 3

This round answers `docs/verification/EN-03-REVIEW-R3.md` at `origin/claude/review-en-03-r3` (`91d27ba`). The checks were configure-only, on the same Windows PC.

1. **R3-1: `pathbar_ui` is now `ui process`.** Its tests write `p1.png` and `p2.png` through `Media`, and `openFolder(h, media)` runs `h.viewer.open` (`tests/ui_test_support.hpp:126`), which starts `glancelot-decoder`. The reviewer's poller saw it start the decoder. My static grep had matched only `viewer.open`, not the `openFolder(h, ...)` helper. The only other user of that helper is `input_ui`, which is already `process`.
   - `process` now has 23 tests.
   - `fast` is unchanged, because `pathbar_ui` is `ui`.
2. **N1: subdirectory tests are collected.** `glancelot_finalize_test_labels()` no longer fails when a subdirectory registers tests. It walks `SUBDIRECTORIES` recursively and gathers each directory's `TESTS`. A declaration is valid if the test exists in any of those directories. Labels, locks and `fast` are applied with `set_property(TEST <name> DIRECTORY <dir> ...)`. On CMake older than 3.28, which lacks that form, configure fails only when a subdirectory actually has tests; the project minimum is 3.24.

Output:

```
> configure (viewer on)            configure exit: 0
> ctest -N                         Total Tests: 43
> -N -L ^fast$     (27) unchanged
> -N -L ^ui$       (5)  folder_grid input_ui pathbar_ui fs_watch_ui monitor_customization_ui
> -N -L ^native$   (9)  unchanged
> -N -L ^process$  (23) selection_export directory_listing file_operations viewer_acceptance thumbnails folder_tree window_slots folder_grid input_ui pathbar_ui file_operation_service platform_integration fs_watch fs_watch_budget filter cache io_scheduler copy_move recovery recovery_startup recovery_controller recovery_process monitor_customization_ui
> -N -L ^clipboard$ (3) folder_grid input_ui metadata

# Scratch, not committed: add_subdirectory(zzsub) before the finalize call, viewer off.
# zzsub/CMakeLists.txt registers zz_sub_plain (undeclared) and
# zz_sub_native (glancelot_test_labels(zz_sub_native LABELS native process LOCKS clipboard)).
configure exit: 0
Total Tests: 7
selection_export: LABELS=native,process LOCK=
perf_trace: LABELS=fast LOCK=
navigation: LABELS=fast LOCK=
directory_listing: LABELS=native,process LOCK=
file_operations: LABELS=native,process LOCK=
zz_sub_plain: LABELS=fast LOCK=
zz_sub_native: LABELS=native,process LOCK=clipboard
```

The scratch subdirectory, the `CMakeLists.txt` edit and both build directories were removed. `git status` afterwards showed only the intended files.

## Unverified

- Windows: `scripts/build.ps1 -Test` and the authoritative gate have not been run. Both gate paths run CTest serially (`scripts/build.ps1:40` and `Dockerfile:22` call `ctest --output-on-failure` without `-j`). On a serial run, labels and `RESOURCE_LOCK` do not change which tests run or in what order, so the gate behaves as before.
- `ctest -L fast` timing on Windows: the last gate measured 722 s of tests against about 426 s here, so I would expect about 75 s there for the old 32-test set and about 71 s for the round 1 set of 27. Both figures are estimates, not measurements.
- Linux results come from the conda-forge Qt 6.8.3, not aqt.
- Review round 2 was configure-only, and the `process` audit is static (see "Review round 2").
- Review round 1 was configure-only. `ctest -L fast` with the 27-test set, and the `clipboard` lock on `metadata`, have not been run on any platform.
