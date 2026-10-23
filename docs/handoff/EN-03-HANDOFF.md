# EN-03 handoff: CTest labels and resource locks

Session: S1 build (cloud), Phase 0 cycle 1. Scope and acceptance: docs/handoff/LEAD-PLAN-2026-10-22.md section 3 (EN-03 row) and docs/handoff/PHASE0-CYCLE1-2026-10-22.md.

| Item | Value |
| --- | --- |
| Branch | `claude/en-03-ctest-labels`, stacked on EN-05 |
| Base SHA | `d355edd67c6751f56d1b8d351bc7af9d2529f093` (EN-05 branch tip). Contract base below EN-05: `afeac1a` |
| Code tip SHA (built and tested) | `dcebfceb02171959d12ac33aeee65bc07c296496` |
| Branch tip | the commit that adds this handoff and the `docs/TESTING.md` section, directly on top of `dcebfce`; docs only |
| Review diff | `git diff d355edd <branch tip>` |

## What changed

1. **`cmake/TestLabels.cmake` (new).** The root `CMakeLists.txt` includes it once, at the end, under `if(BUILD_TESTING)`, after every test, including those in `MonitorCustomization.cmake`, is registered. It holds one name list per label and:
   - appends `LABELS` to each listed test. Names are skipped with `if(TEST ...)` when `GLANCELOT_BUILD_VIEWER=OFF`, so a core-only configure still works;
   - appends `RESOURCE_LOCK budget` to the seven budget tests and `RESOURCE_LOCK clipboard` to the clipboard tests;
   - reads the directory `TESTS` property and stops configure with `FATAL_ERROR` if any registered test has no label, so a new test cannot silently miss the fast loop. I checked this by temporarily dropping `animation` from the lists: configure printed `Test 'animation' has no CTest label. Add it to a list in cmake/TestLabels.cmake (see docs/TESTING.md).` I then restored the list. **Note for the lead:** a new target, such as EN-06's `command_routing` test, needs one line in this file.
2. **`CMakeLists.txt`:** three added lines (the include). No `add_test` changed, so every QTest `-o <name>-results.txt,txt -o -,txt` argument is unchanged (`ctest -N -V` shows them, for example `grid_ui_tests "-o" ".../folder_grid-results.txt,txt" "-o" "-,txt"`).
3. **`docs/TESTING.md`:** a new section, "Run part of the automated suite", with a label table, `ctest -L fast`, `ctest -LE budget`, `-L ui`, `-N -L`, the lock behaviour under `-j`, and how to label a new test.

### Assignment (43 tests, all labelled)

| Label | Count | Tests | How a test qualifies (from the test source) |
| --- | --- | --- | --- |
| `fast` | 32 | all tests not `ui` or `budget`, except `recovery_process` | quick loop; `recovery_process` drives the real app under Xvfb with a 180 s timeout |
| `ui` | 5 | `folder_grid`, `input_ui`, `pathbar_ui`, `fs_watch_ui`, `monitor_customization_ui` | loads the shipped `ui/Main.qml` in a `QQuickWindow` |
| `budget` | 7 | `fs_watch_budget`, `fs_watch`, `folder_grid`, `cache`, `window_slots`, `recovery_controller`, `input_ui` | the seven tests the plan names (also AGENTS.md); `RESOURCE_LOCK budget` |
| `process` | 8 | `thumbnails`, `io_scheduler`, `fs_watch`, `window_slots`, `recovery`, `recovery_startup`, `platform_integration`, `recovery_process` | the test itself starts a `QProcess`, the decoder by `GLANCELOT_DECODER_PATH`, or Python. Controller tests that start the decoder only indirectly by opening an image are not labelled `process` |
| `native` | 6 | `platform_integration`, `file_operation_service`, `monitor_output`, `monitor_customization`, `monitor_customization_ui`, `recovery_process` | platform shell or launcher fixtures, trash and `subst`, screen topology and presentation windows, or a real app window |
| `clipboard` | 2 | `folder_grid`, `input_ui` | the only tests that call `QGuiApplication::clipboard()`. `pathbar_ui` includes `ui_test_support.hpp`, which includes `<QClipboard>`, but never uses it. `copy_move` and `shortcuts_help` use only the service's internal clipboard or a binding name. `RESOURCE_LOCK clipboard` |

The `process` and `native` boundaries are judgement calls; the reviewer may want to move tests between them. `fast` is the only label the acceptance criterion depends on.

## Build environment

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

## Unverified

- Windows: `scripts/build.ps1 -Test` and the authoritative gate have not been run. Both gate paths run CTest serially (`scripts/build.ps1:40` and `Dockerfile:22` call `ctest --output-on-failure` without `-j`). On a serial run, labels and `RESOURCE_LOCK` do not change which tests run or in what order, so the gate behaves as before.
- `ctest -L fast` timing on Windows: the last gate measured 722 s of tests against about 426 s here, so I would expect about 75 s there. That figure is an estimate, not a measurement.
- Linux results come from the conda-forge Qt 6.8.3, not aqt.
