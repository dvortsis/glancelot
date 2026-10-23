# EN-05 handoff: viewer source list and decode-cache extraction

Session: S1 build (cloud), Phase 0 cycle 1. Scope and acceptance: docs/handoff/LEAD-PLAN-2026-10-22.md section 3 (EN-05 row) and docs/handoff/PHASE0-CYCLE1-2026-10-22.md.

| Item | Value |
| --- | --- |
| Branch | `claude/en-05-viewer-sources` |
| Base (contract) SHA | `afeac1a5d5b1da088d7b35447a933a2f64400f49` (`origin/main`, "chore(lead): freeze Phase 0 cycle 1 contracts") |
| Code tip SHA (what was built and tested) | `9c7b203153963a1a721cb33f66a814eba4c9f232` |
| Branch tip | the commit that adds this handoff, directly on top of `9c7b203`; docs only |
| Review diff | `git diff afeac1a 9c7b203` |

## What changed

1. **`cmake/ViewerSources.cmake` (new).** It defines `GLANCELOT_VIEWER_SOURCES`: `viewer_controller.{cpp,hpp}`, `decode_cache.{cpp,hpp}` and `settings_file.{cpp,hpp}`. It has an include guard and is included once from the root, just before `qt_add_executable(glancelot ...)`. `settings_file` is in the list because every executable that compiles the controller also listed it by hand.
2. **Every target that listed `src/app/viewer_controller.cpp` now uses the list.** That covers 21 executables: the `glancelot` app; in `CMakeLists.txt` `acceptance_tests`, `folder_tree_tests`, `window_slot_tests`, `grid_ui_tests`, `input_ui_tests`, `pathbar_ui_tests`, `theme_tests`, `ui_settings_tests`, `shortcuts_help_tests`, `filter_tests`, `cache_tests`, `io_scheduler_tests`, `mutation_tests`, `copy_move_tests` and `monitor_output_tests`; in `cmake/FSWatchTests.cmake` `fs_watch_tests`, `fs_watch_budget_tests` (through `fs_watch_backend_sources`) and `fs_watch_ui_tests`; in `cmake/MonitorCustomization.cmake` `monitor_customization_tests` and `monitor_customization_ui_tests`; in `cmake/RecoveryTests.cmake` `recovery_tests` and `recovery_controller_tests`. Their hand-listed `settings_file` pairs were removed too. `grep -rn viewer_controller.cpp CMakeLists.txt cmake/` now matches only `cmake/ViewerSources.cmake` and the existing regex in `cmake/RecoverySources.cmake`. A new controller dependency is one edit to `ViewerSources.cmake`.
   - `recovery_tests` and `recovery_controller_tests` also get `src/app/settings_file.cpp` through `${glancelot_recovery_provider_sources}`. CMake removes the duplicate: `build.ninja` has exactly one `settings_file.cpp.o` rule for each of those targets. The `list(FIND)` checks in `glancelot_attach_monitor_customization` and `glancelot_attach_recovery`, and the source regex in `glancelot_attach_recovery_consumers`, work as before because `SOURCES` holds the expanded paths.
3. **`src/app/decode_cache.{hpp,cpp}` (new), `glancelot::detail::DecodeCache`.** This is the former `QCache<QString,CachedImage> cache{128*1024}` block, with the same behaviour:
   - capacity `128*1024` KiB; cost `(sizeInBytes()+1023)/1024` KiB;
   - `insert` refuses an empty source token or a cost above capacity, and returns whether it stored the pixels. The decode path still calls `remove(key)` when nothing was stored; the prefetch path still notifies only when it stored;
   - `reuse(key, token)` returns the pixels only when the token is not empty and equals the stored token. It still goes through `QCache::object()`, so the LRU touch is unchanged. The revision-only path still removes the entry and decodes fresh when `reuse` returns a null image;
   - `bytes()` is `totalCost()*1024`, used by `decodedCacheBytes()` and by both memory-admission checks. `count()`, `contains()`, `remove()` and `clear()` pass straight through to `QCache`;
   - `clearDecodedCache()` is unchanged: it cancels look-ahead, clears the cache and emits `cacheChanged`.
4. **`src/app/viewer_controller.cpp`.** Changes are limited to the decode-cache block and its users: the include, the member, the two admission sums, the prefetch insert, the revision reuse, the decode insert and `decodedCacheBytes()`. `#include <QCache>` moved to `decode_cache.hpp`. No other region was touched.
5. **Stale scaffolding comments removed.** In `cmake/FSWatchTests.cmake:1-2`, `cmake/MonitorCustomization.cmake:1-2` and `cmake/RecoveryTests.cmake:1-2` and `:51-52`, plus the same kind of "once the feature exists / on the feature branch" wording in `cmake/ExportTests.cmake:1-2` and `cmake/RecoverySources.cmake:1-2`. **Decision for review:** I also removed the `MONITOR-CUSTOM-01 requires ...` `FATAL_ERROR` source-presence loop from `MonitorCustomization.cmake`, because its own comment said it was temporary and must be removed before integration. A missing file still fails configure, with CMake's own "Cannot find source file" error. The security guard in `cmake/PlatformTestFixtures.cmake` is not scaffolding, so it stays.

`include/glancelot/` has no diff (`git diff --quiet afeac1a -- include/` returns 0). Nothing in `ui/`, the queue, workflows or AGENTS.md changed. The test count is still 43 (`ctest -N`: `Total Tests: 43`).

## Build environment (cloud, Linux)

- Ubuntu 24.04.5, g++ 13.3.0, CMake 3.28.3, Ninja. The cycle document's apt packages are installed (Xvfb, Mesa and so on).
- **Qt deviation:** the environment's network policy denies `download.qt.io` and its mirrors (proxy 403), so `aqt install-qt` failed. I installed Qt 6.8.3 from conda-forge instead (`qt6-main 6.8.3 hb82b983_5`, which includes qtbase, qtdeclarative and qtshadertools) into `/opt/qt683` with micromamba, then configured with `-DCMAKE_PREFIX_PATH=/opt/qt683`. CMake prints a harmless "runtime search path may be hidden" warning for libGL/libEGL under `/opt/qt683/lib`.
- Configure: `cmake -S . -B build-claude-en-05 -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=/opt/qt683`. Full build: 564 steps, 7 min 25 s wall on 4 vCPU, no compiler warnings from the new files.

## Targeted tests (acceptance set), at `9c7b203`

```
$ QT_QPA_PLATFORM=offscreen ctest -R '^(cache|viewer_acceptance|fs_watch)$' --output-on-failure
    Start  7: viewer_acceptance
1/3 Test  #7: viewer_acceptance ................   Passed    5.35 sec
    Start 23: fs_watch
2/3 Test #23: fs_watch .........................   Passed   69.87 sec
    Start 30: cache
3/3 Test #30: cache ............................   Passed    2.26 sec

100% tests passed, 0 tests failed out of 3

Total Test time (real) =  77.48 sec
```

## Full offscreen suite, at `9c7b203`

```
$ QT_QPA_PLATFORM=offscreen ctest --output-on-failure
 1/43 Test  #1: selection_export .................   Passed    0.03 sec
 2/43 Test  #2: perf_trace .......................   Passed    0.03 sec
 3/43 Test  #3: navigation .......................   Passed    0.17 sec
 4/43 Test  #4: directory_listing ................   Passed    1.10 sec
 5/43 Test  #5: file_operations ..................   Passed    0.01 sec
 6/43 Test  #6: startup_window_geometry ..........   Passed    0.01 sec
 7/43 Test  #7: viewer_acceptance ................   Passed    5.10 sec
 8/43 Test  #8: thumbnails .......................   Passed    3.72 sec
 9/43 Test  #9: folder_tree ......................   Passed    6.05 sec
10/43 Test #10: pane_layout ......................   Passed    0.08 sec
11/43 Test #11: window_slots .....................   Passed   14.72 sec
12/43 Test #12: folder_grid ......................***Failed  146.89 sec
13/43 Test #13: input_ui .........................***Failed   64.95 sec
14/43 Test #14: pathbar_ui .......................   Passed    2.14 sec
15/43 Test #15: file_operation_service ...........   Passed    0.98 sec
16/43 Test #16: directory_model ..................***Failed    6.67 sec
17/43 Test #17: viewport .........................   Passed    0.03 sec
18/43 Test #18: themes ...........................   Passed    0.16 sec
19/43 Test #19: platform_integration .............   Passed    0.48 sec
20/43 Test #20: grid_search ......................   Passed    0.11 sec
21/43 Test #21: grid_density .....................   Passed    0.07 sec
22/43 Test #22: ui_settings ......................   Passed    0.48 sec
23/43 Test #23: fs_watch .........................   Passed   70.16 sec
24/43 Test #24: fs_watch_budget ..................   Passed   25.08 sec
25/43 Test #25: fs_watch_ui ......................   Passed    1.97 sec
26/43 Test #26: favorites ........................   Passed    0.07 sec
27/43 Test #27: folder_history ...................   Passed    0.02 sec
28/43 Test #28: shortcuts_help ...................   Passed    0.24 sec
29/43 Test #29: filter ...........................   Passed    1.51 sec
30/43 Test #30: cache ............................   Passed    2.16 sec
31/43 Test #31: io_scheduler .....................   Passed    0.05 sec
32/43 Test #32: metadata .........................   Passed    0.02 sec
33/43 Test #33: animation ........................   Passed    0.37 sec
34/43 Test #34: mutation .........................   Passed    0.05 sec
35/43 Test #35: copy_move ........................   Passed    0.06 sec
36/43 Test #36: format ...........................   Passed    0.03 sec
37/43 Test #37: monitor_output ...................   Passed    2.03 sec
38/43 Test #38: recovery .........................   Passed   11.02 sec
39/43 Test #39: recovery_startup .................   Passed    0.19 sec
40/43 Test #40: recovery_controller ..............   Passed   12.22 sec
41/43 Test #41: recovery_process .................   Passed   23.49 sec
42/43 Test #42: monitor_customization ............   Passed    6.50 sec
43/43 Test #43: monitor_customization_ui .........   Passed   14.97 sec

93% tests passed, 3 tests failed out of 43

Total Test time (real) = 426.22 sec

The following tests FAILED:
	 12 - folder_grid (Failed)
	 13 - input_ui (Failed)
	 16 - directory_model (Failed)
```

### The three failures already fail at the base and come from this environment

To check, I built `grid_ui_tests`, `input_ui_tests`, `directory_model_tests` and `glancelot-decoder` from an **unmodified `afeac1a`** worktree with the same Qt and toolchain. The three tests fail the same way there:

```
$ QT_QPA_PLATFORM=offscreen ctest -R '^(folder_grid|input_ui|directory_model)$' --output-on-failure   # base afeac1a
1/3 Test #12: folder_grid ......................***Failed  140.18 sec
FAIL!  : GridUiTests::externalRenameAndDeleteRefreshKeepSelection() Compared lists have different sizes.
Totals: 73 passed, 1 failed, 4 skipped, 0 blacklisted, 140139ms
2/3 Test #13: input_ui .........................***Failed   63.59 sec
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()
Totals: 31 passed, 1 failed, 1 skipped, 0 blacklisted, 63555ms
3/3 Test #16: directory_model ..................***Failed    6.49 sec
FAIL!  : DirectoryModelTests::selectionSurvivesRelistSortAndDropsRemovedKeys() Compared lists have different sizes.
Totals: 19 passed, 1 failed, 0 skipped, 0 blacklisted, 6485ms
0% tests passed, 3 tests failed out of 3
```

- `directory_model` does not compile the controller or the decode cache, so EN-05 cannot affect it.
- `directory_model` (`tests/directory_model_tests.cpp:163`) and `folder_grid` (`tests/grid_ui_tests.cpp:1646`) fail for the same reason. Each test expects a renamed file to keep its selection only when `QFileInfo::birthTime()` is valid. This container's filesystem reports no birth time, but the model still proves the rename by inode, so it keeps 2 selected paths where the test expects 1. The test's expectation does not hold in this environment; that is test debt for the lead to route.
- `input_ui` `treeRowAndPathBarMenusOpenAndMatch` (`tests/input_ui_tests.cpp:1133`): the folder-tree row context menu does not open under offscreen with the conda-forge Qt build. I did not find the root cause; it fails identically at the base.

Rerun on the EN-05 build (budget-test rule: rerun once and record both results):

```
$ QT_QPA_PLATFORM=offscreen ctest -R '^(folder_grid|input_ui|directory_model)$' --output-on-failure   # EN-05 9c7b203, rerun
1/3 Test #12: folder_grid ......................***Failed  138.99 sec
FAIL!  : GridUiTests::externalRenameAndDeleteRefreshKeepSelection() Compared lists have different sizes.
Totals: 73 passed, 1 failed, 4 skipped, 0 blacklisted, 138956ms
2/3 Test #13: input_ui .........................***Failed   62.99 sec
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()
Totals: 31 passed, 1 failed, 1 skipped, 0 blacklisted, 62958ms
3/3 Test #16: directory_model ..................***Failed    5.84 sec
FAIL!  : DirectoryModelTests::selectionSurvivesRelistSortAndDropsRemovedKeys() Compared lists have different sizes.
Totals: 19 passed, 1 failed, 0 skipped, 0 blacklisted, 5832ms
0% tests passed, 3 tests failed out of 3
Total Test time (real) = 207.83 sec
```

The same three test functions fail every time, on the base and on EN-05. They are not load flakes. Every other test function in those three executables passes.

## Failure and cancellation cases covered

The existing tests cover these paths through the extracted cache. No test was added or changed, so the count stays 43; the packet grants no test-source edits.

- `cache`: byte and count accounting from zero, `clearDecodedCache`, and the cache-hit flag.
- `fs_watch`: revision-only reuse when the source token is unchanged, and a fresh decode when the token changed, the file was replaced, or the folder was invalidated (`decodedCacheCount()` assertions at `tests/fs_watch_tests.cpp:150,154,214,240,688`). It also covers prefetch insertion and look-ahead cancellation.
- `viewer_acceptance`: decode, stale-generation rejection and navigation through the controller.

## Unverified

- Windows (MSVC) and the Docker build have not been run. The authoritative `lead-windows-gate` decides. MSVC builds the new `decode_cache.cpp` with the target's flags; it uses only Qt and standard C++.
- Linux results come from the conda-forge Qt 6.8.3, not the aqt build the cycle document specifies, because `download.qt.io` is blocked here.
- Root cause of the `input_ui` context-menu failure in this environment.
