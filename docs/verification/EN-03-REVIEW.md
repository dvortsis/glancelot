# EN-03 re-review (round 2): CTest labels and resource locks

Verdict: **CHANGES REQUIRED** at branch tip `e5c1e5c3535440e885b7dfd28188370b716e2d51` (`claude/en-03-ctest-labels`, stacked on EN-05 branch tip `d355edd`, EN-05 code tip `9c7b203`).

The labels, locks and `fast` subset work on Windows: 43 tests, `fast` is 27 tests, and the serial `fast` run passed in 118 s. Two problems block approval:

- **B1.** Any new test in another packet's `cmake/*.cmake` fails configure until someone edits `cmake/TestLabels.cmake`.
- **B2.** The round-1 `process` gap is still open.

## Review setup

| Item | Value |
| --- | --- |
| Reviewer | Fresh-context Claude reviewer (Opus). It did not implement EN-03 and did not do the round-1 review |
| Diff reviewed | `git diff origin/claude/en-05-viewer-sources origin/claude/en-03-ctest-labels`, which changes `CMakeLists.txt` (+3), new `cmake/TestLabels.cmake`, `docs/TESTING.md` and the handoff |
| Code worktree | `.worktrees/review-en-05`, moved from `9c7b203` to detached `e5c1e5c`. It stays checked out at the EN-03 tip |
| Build directory | `build-review-en-05`, the existing EN-05 tree, reconfigured and built incrementally |
| Platform | Windows 11 Pro 10.0.26200, MSVC 2022 x64, CMake/CTest 4.4.4, Ninja, Qt 6.8.3 msvc2022_64 |

No source, test, queue or shared-contract file was committed. The scratch dummy-test check (B1) was reverted, and its build directory was outside the repo and then deleted. No UI changed, so the ui-design-skills requirement does not apply.

## Blocking findings

### B1. The hand-kept lists make every new test fail configure

Each pending packet adds test targets in its own `cmake/*.cmake`, for example `reference_pin`, `tabs_recovery`, `command_routing`, `translation_coverage_ui`, `layout_presets_ui` and `copy_safety`. With this branch, each of them breaks configure until `cmake/TestLabels.cmake` is edited too, so every one of those packets has to touch this file.

I checked this with a scratch file that was never committed. I added `cmake/ZzDummy.cmake` containing `add_test(NAME zz_dummy COMMAND ${CMAKE_COMMAND} -E true)` and included it from `CMakeLists.txt` before the `TestLabels.cmake` include. I left `TestLabels.cmake` untouched. Then I configured a new scratch directory with the viewer on:

```
CMake Error at cmake/TestLabels.cmake:66 (message):
  Test 'zz_dummy' has no CTest label.  Add it to a list in
  cmake/TestLabels.cmake (see docs/TESTING.md).
Call Stack (most recent call first):
  CMakeLists.txt:325 (include)
-- Configuring incomplete, errors occurred!
configure exit: 1
```

The no-label check stopped configure first. If it had not, the `fast` check at line 81 would have stopped it, because `zz_dummy` is not ui, budget or native and is not in the hand-kept `fast` list. Afterwards the scratch file and the include were removed (`git status` clean) and the scratch build directory was deleted.

**Required design (lead):**

- **Compute `fast`.** `fast` is every registered test that has none of the `ui`, `budget` or `native` labels. Drop the hand-kept `glancelot_tests_fast` list and both `fast`-mismatch `FATAL_ERROR`s.
- **Let feature files label their own tests.** Add a small helper, for example `glancelot_test_labels(<test> LABELS ui native process ... LOCKS clipboard budget)`, so each feature `cmake/*.cmake` labels its tests next to its `add_test`. The existing tests can keep central lists or move to the helper.
- **Fail only on names that do not exist.** `FATAL_ERROR` fires only when a label or lock list (or a helper call) names a test that is not registered. An unlabelled test is not an error; it becomes `fast`.
- **Run the computation last.** `fast` is computed once, at the end of the root `CMakeLists.txt`, after every test is registered. That is where `TestLabels.cmake` is included now.
- **Update the documentation to match:** the `cmake/TestLabels.cmake` header, the "When you add a test" list in `docs/TESTING.md`, and the handoff's note that a new target "needs one line in this file".

### B2. `process` misses tests that start the decoder through product code (round-1 finding, still open)

The documented rule is that `process` covers a test that "starts a hermetic child process (decoder, test helper, the app or Python)". The branch applies it to a child started through product code: `io_scheduler` is `process` only because it constructs `ThumbnailService(GLANCELOT_DECODER_PATH)` (`tests/io_scheduler_tests.cpp:182`); it has no `QProcess` of its own. `ViewerController::open` also starts `glancelot-decoder` (`src/app/viewer_controller.cpp:870`), but none of the tests that reach it that way are labelled.

I watched which test executables became the parent of a `glancelot-decoder` process. A thread polled every 20 ms during `ctest -R` over 14 candidate tests. The result:

```
io_scheduler_tests -> glancelot-decoder      (labelled process)
thumbnail_tests    -> glancelot-decoder      (labelled process)
acceptance_tests   -> glancelot-decoder      (viewer_acceptance: NOT process)
folder_tree_tests  -> glancelot-decoder      (folder_tree: NOT process)
filter_tests       -> glancelot-decoder      (filter: NOT process)
cache_tests        -> glancelot-decoder      (cache: NOT process)
```

The `grid_ui_tests` parent the poller also saw came from another agent's build tree (`.worktrees/claude-en-06`), not from this run.

The poller can miss short-lived decoders, so this list is a lower bound. The static evidence points to more candidates:

- `add_dependencies(<t> glancelot-decoder)` targets that construct a `ViewerController` and open images;
- tests that load `ui/Main.qml` through `ui_test_support.hpp`: `folder_grid`, `input_ui`, `pathbar_ui`, `fs_watch_ui` and `monitor_customization_ui`;
- `recovery_controller` (`RecoveryTests.cmake:43`).

**Required:** pick one rule and apply it to every test. Either:

- `process` includes a decoder or helper child started through product code. Then add at least `viewer_acceptance`, `folder_tree`, `filter` and `cache`, and audit the other decoder-dependent targets.
- `process` covers only a child the test source starts itself. Then remove `io_scheduler`, and reword the definition in the header, in `docs/TESTING.md` and in the handoff.

`process` adds no lock and selects nothing in the documented commands, so this is a labelling bug, not a gate risk. It is still the round-1 finding, and it is still open.

## Round-1 findings

| Round-1 finding | Status | Evidence |
| --- | --- | --- |
| `metadata` missing `RESOURCE_LOCK clipboard` | **Resolved** | `--show-only=json-v1`: `metadata: clipboard`; `folder_grid` and `input_ui`: `budget,clipboard`. A grep of `tests/` for `clipboard(`, `copyFilePath`, `copyFileUrls`, `copyImageBitmap`, `copyDirectory`, `copySummary`, `copyGps`, `copyTag`, `copyFolderAddress` and `"copy-` finds only `grid_ui_tests`, `input_ui_tests` and `metadata_tests`, plus `shortcuts_help_tests.cpp:106`, which only reads `bindingFor("copy-path")` |
| `fast` included OS-touching tests | **Resolved** | `selection_export`, `directory_listing`, `file_operations`, `file_operation_service` and `platform_integration` are `native` and not in `fast`. The four `fast` tests without an offscreen `ENVIRONMENT` (`perf_trace`, `navigation`, `startup_window_geometry`, `pane_layout`) link only the core library or `Qt6::Core`, so they cannot open a window |
| `process` label gaps | **Not resolved** | B2 |
| Misspelled names skipped silently | **Resolved, but too strict** | A listed name that is not registered now fails configure. The same mechanism also fails configure for an unlisted new test (B1), which the lead's design does not want |

## The handoff's "left fast" wording

The handoff says "Five tests left `fast`: `selection_export`, `directory_listing`, `file_operations`, ..." That means they were **removed from** `fast`, which matches its "Was fast? yes, now removed" column. The code agrees: all three are `native` and none is `fast` (`ctest -N -L ^native$` and `-L ^fast$` below). The labels follow the lead's rule, because each test runs `mklink` on Windows:

| Test | Where it runs `mklink` |
| --- | --- |
| `file_operations` | `tests/file_operations_tests.cpp:42`: `mklink /J` through `std::system`, always on `_WIN32` |
| `directory_listing` | `tests/directory_listing_tests.cpp:105`: `cmd /d /c mklink /J`, always on `_WIN32` |
| `selection_export` | `tests/selection_export_tests.cpp:510`: `cmd /c mklink /J`, only when `create_directory_symlink` fails. Without Developer Mode or elevation it does fail, so this is the usual Windows path |

`navigation` creates links only with `std::filesystem::create_directory_symlink` and `create_symlink` (`navigation_tests.cpp:244-288`) and never runs `mklink`. Under the literal rule it stays `fast`, which is correct. Suggested wording for the handoff: "removed from `fast`" instead of "left `fast`". This does not block.

## Dropping the monitor tests from `native`

Dropping `monitor_output`, `monitor_customization` and `monitor_customization_ui` from `native` is correct.

- **Forced offscreen:** CTest sets `QT_QPA_PLATFORM=offscreen` for all three (json-v1 below; `CMakeLists.txt:313`, `cmake/MonitorCustomization.cmake:50`). `monitor_output_tests.cpp:764` forces it again unless `GLANCELOT_MONITOR_NATIVE_COLOR_PROOF` is set, and even then the CTest `ENVIRONMENT` still applies under `ctest`.
- **No real screens:** the screens are the offscreen plugin's or synthetic (`tests/monitor_customization_screens.hpp`). No test calls a monitor-enumeration API.
- **One native call, and it is read-only:** the only native call they can reach is `output_color::systemProfile` (`src/render/output_color.hpp`). It calls GDI `CreateDCW`/`GetICMProfileW` with the offscreen screen name and falls back to sRGB when that fails. It touches no shell, trash, `subst`, `mklink` or native window, so it is outside the lead's `native` definition.

## Windows stage

### Incremental build

In `.worktrees/review-en-05`, I ran `git checkout --detach origin/claude/en-03-ctest-labels` (HEAD `e5c1e5c`), then `& .\scripts\build.ps1 -BuildDir build-review-en-05`:

```
-- Configuring done (25.4s)
-- Generating done (1.8s)
-- Build files have been written to: C:/src/Github/Glancelot/.worktrees/review-en-05/build-review-en-05
[1/185] Automatic MOC and UIC for target glancelot_test_argv_capture
...
[45/47] Automatic MOC and UIC for target format_tests
elapsed 00:00:36.33
```

45 Ninja steps ran, all of them AutoMoc/UIC checks. Nothing was compiled or linked.

### Listings

```
> ctest --test-dir build-review-en-05 -N
Total Tests: 43
> ctest --test-dir build-review-en-05 --print-labels
All Labels:
  budget
  clipboard
  fast
  native
  process
  ui
> -N -L ^fast$       Total Tests: 27
  perf_trace navigation startup_window_geometry viewer_acceptance thumbnails folder_tree pane_layout
  directory_model viewport themes grid_search grid_density ui_settings favorites folder_history
  shortcuts_help filter io_scheduler metadata animation mutation copy_move format monitor_output
  recovery recovery_startup monitor_customization
> -N -L ^ui$         Total Tests: 5
> -N -L ^budget$     Total Tests: 7
> -N -L ^native$     Total Tests: 9
  selection_export directory_listing file_operations window_slots folder_grid
  file_operation_service platform_integration fs_watch recovery_process
> -N -L ^process$    Total Tests: 8
  thumbnails window_slots platform_integration fs_watch io_scheduler recovery recovery_startup recovery_process
> -N -L ^clipboard$  Total Tests: 3
  folder_grid input_ui metadata
> -N -LE budget      Total Tests: 36
```

### json-v1 against EN-05

I saved `ctest --show-only=json-v1` from the same build tree at `9c7b203` before the checkout and compared it with the output at `e5c1e5c`, test by test:

```
names/order identical: True
non-label/lock diffs: 0          (command, TIMEOUT, ENVIRONMENT, WORKING_DIRECTORY)
property names seen: ENVIRONMENT,LABELS,RESOURCE_LOCK,TIMEOUT,WORKING_DIRECTORY
RESOURCE_LOCK:
  window_slots: budget
  folder_grid: budget,clipboard
  input_ui: budget,clipboard
  fs_watch: budget
  fs_watch_budget: budget
  cache: budget
  metadata: clipboard
  recovery_controller: budget
```

Every TIMEOUT and ENVIRONMENT value is unchanged from EN-05. The only additions are `LABELS` and `RESOURCE_LOCK`.

### Serial `fast` run inside the gate lock

The run waited for the lock, which `claude/en-04-fix` held at the time, and then ran:

```
> & C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch review/en-03 -Run { ...; Measure-Command { ctest --test-dir <abs>\build-review-en-05 -L fast --output-on-failure | Out-Host } }
 1/27 Test  #2: perf_trace .......................   Passed    0.09 sec
 2/27 Test  #3: navigation .......................   Passed    4.04 sec
 3/27 Test  #6: startup_window_geometry ..........   Passed    0.08 sec
 4/27 Test  #7: viewer_acceptance ................   Passed   12.61 sec
 5/27 Test  #8: thumbnails .......................   Passed    7.34 sec
 6/27 Test  #9: folder_tree ......................   Passed   11.78 sec
 7/27 Test #10: pane_layout ......................   Passed    1.63 sec
 8/27 Test #16: directory_model ..................   Passed   39.12 sec
 9/27 Test #17: viewport .........................   Passed    0.14 sec
10/27 Test #18: themes ...........................   Passed    0.81 sec
11/27 Test #20: grid_search ......................   Passed    0.37 sec
12/27 Test #21: grid_density .....................   Passed    0.24 sec
13/27 Test #22: ui_settings ......................   Passed    1.87 sec
14/27 Test #26: favorites ........................   Passed    0.49 sec
15/27 Test #27: folder_history ...................   Passed    0.10 sec
16/27 Test #28: shortcuts_help ...................   Passed    0.87 sec
17/27 Test #29: filter ...........................   Passed    3.55 sec
18/27 Test #31: io_scheduler .....................   Passed    0.34 sec
19/27 Test #32: metadata .........................   Passed    0.24 sec
20/27 Test #33: animation ........................   Passed    0.52 sec
21/27 Test #34: mutation .........................   Passed    0.30 sec
22/27 Test #35: copy_move ........................   Passed    0.40 sec
23/27 Test #36: format ...........................   Passed    0.20 sec
24/27 Test #37: monitor_output ...................   Passed    3.02 sec
25/27 Test #38: recovery .........................   Passed   15.90 sec
26/27 Test #39: recovery_startup .................   Passed    0.83 sec
27/27 Test #42: monitor_customization ............   Passed    9.40 sec
100% tests passed out of 27
Label Time Summary:
clipboard    =   0.24 sec*proc (1 test)
fast         = 116.27 sec*proc (27 tests)
process      =  24.42 sec*proc (4 tests)
Total Test time (real) = 117.40 sec
ctest exit: 0
Measure-Command: 118.0223692 s
```

Acceptance ("under about 3 minutes, all pass") is **met**: 118.0 s, 27 of 27 passed. `docs/TESTING.md` still gives the Linux estimate (about 42 s). Adding the measured Windows figure is suggested, not required.

### Note: one `viewer_acceptance` failure outside the gate lock

During the decoder-observation run for B2, which used targeted `ctest -R` outside the lock, `viewer_acceptance` failed once after 51.94 s. Other agents' `grid_ui_tests` (`claude-en-06`) and `input_ui_tests` (`claude-en-04`) were running on the machine at the same time, and `claude/en-06-fix` held the gate. A rerun on its own passed:

```
1/1 Test #7: viewer_acceptance ................   Passed   20.00 sec
100% tests passed out of 1
```

EN-03 changes no test command, timeout or environment (see the json comparison), so this load-related failure is not caused by the branch. It passed inside the gate lock.

## Other checks

- The include sits at the end of the root `CMakeLists.txt` under `if(BUILD_TESTING)`, after `cmake/MonitorCustomization.cmake` and every other test file. This is the right place for a computed `fast`.
- `RESOURCE_LOCK budget` is on exactly the seven budget tests the plan names. `RESOURCE_LOCK clipboard` is on `folder_grid`, `input_ui` and `metadata`.
- The gate paths (`scripts/build.ps1`, `Dockerfile`) run `ctest` without `-L` or `-j`, so the full gate still runs all 43 tests serially.

## Required before approval

1. **B1:** compute `fast`, add a per-feature label/lock helper, and keep `FATAL_ERROR` only for a listed name that does not exist. Show that the `zz_dummy` scratch test configures and lands in `fast`, and that a misspelled lock or label name still fails configure.
2. **B2:** pick one `process` rule and apply it to every test, with the evidence in the handoff.
3. Update the `TestLabels.cmake` header, `docs/TESTING.md` and the handoff to match. Optionally, reword "left `fast`" and add the measured Windows `fast` time.
