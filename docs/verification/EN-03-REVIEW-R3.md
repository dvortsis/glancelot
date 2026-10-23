# EN-03 re-review (round 3): CTest labels and resource locks

Verdict: **CHANGES REQUIRED (one small item, no gate risk)** at branch tip `fdc508c` (`claude/en-03-ctest-labels`).

B1 is resolved. The computed `fast` label, the `glancelot_test_labels()` helper and the FATAL rules follow the lead's design, and they work on Windows:
- 43 tests; `fast` is 27.
- A scratch undeclared test configures and lands in `fast`.
- A misspelled test name or lock still fails configure.
- No command, `TIMEOUT` or `ENVIRONMENT` value changed.
- The serial `fast` run inside the gate lock passed 27 of 27 in 96.1 s.

B2 is resolved except for one test, which is still mislabelled:
- **R3-1.** `pathbar_ui` starts `glancelot-decoder.exe` in every observed run, but it is not labelled `process`. The handoff gives a factually wrong reason for leaving it out.

`process` adds no lock and selects nothing in the documented commands. The lead can waive R3-1 or fold it into integration as a one-line fix.

## Review setup

| Item | Value |
| --- | --- |
| Reviewer | Fresh-context Claude reviewer (Opus). It did not implement EN-03 and did not do the round-1 or round-2 review |
| Diff reviewed | `git diff e5c1e5c origin/claude/en-03-ctest-labels` (`fdc508c`): `CMakeLists.txt` (+4/-1), `cmake/TestLabels.cmake` (rewritten), `docs/TESTING.md`, handoff "Review round 2" |
| Code worktree | `.worktrees/review-en-05`, moved from detached `e5c1e5c` to detached `fdc508c`. It stays there, clean |
| Build directory | `build-review-en-05`, reconfigured and built incrementally |
| Platform | Windows 11 Pro 10.0.26200, MSVC 2022 x64, CMake/CTest from `.tools/python/Scripts`, Ninja, Qt 6.8.3 msvc2022_64 |

No source, test, queue or shared-contract file was committed. Every scratch change (`cmake/ZzDummy.cmake` and its include) was reverted, `git status` in the code worktree is clean, and the scratch viewer-off build directory was deleted. No UI changed, so the ui-design-skills requirement does not apply.

## Blocking finding

### R3-1. `pathbar_ui` starts the decoder but is not `process`

The handoff's "Not `process`" list says:

> `pathbar_ui`: only folders and `.txt` files, and its `Harness` uses the fake `PreviewProvider` for thumbnails.

The source says otherwise:
- `tests/pathbar_ui_tests.cpp:22` and `:44` build `Media media(2, QSize(64, 48))`. `Media` (`tests/ui_test_support.hpp:48-60`) writes `p1.png` and `p2.png`.
- `:23` and `:45` call `openFolder(h, media)`, which runs `h.viewer.open(media.path(0))` (`tests/ui_test_support.hpp:126`). That is `ViewerController::open`, which starts `glancelot-decoder` (`src/app/viewer_controller.cpp:870`, `:987`).
- The fake `PreviewProvider` replaces only the `thumb` image provider. It does not stop the viewer's own decode.

The process poller (method below) saw it in both poller sessions, so in at least two of three runs. The poller keeps one line per test and session, so session 2's two runs collapse into one line. The static path above is decisive on its own.

```
pathbar_ui: pathbar_ui_tests.exe -> glancelot-decoder.exe     (poller session 1: 1 run)
pathbar_ui: pathbar_ui_tests.exe -> glancelot-decoder.exe     (poller session 2: 2 runs, one line)
```

**Required:**
- Change the declaration in `cmake/TestLabels.cmake` to `glancelot_test_labels(pathbar_ui LABELS ui process)`.
- Update the `process` row in `docs/TESTING.md` (22 to 23 tests, add `pathbar_ui`).
- Correct the handoff's "Not `process`" bullet for `pathbar_ui` and the `process` count.

`fast` does not change, because `pathbar_ui` is `ui` either way.

## Round-2 findings

| Round-2 finding | Status | Evidence |
| --- | --- | --- |
| B1: hand-kept lists fail configure for every new test | **Resolved** | Computed `fast`, per-feature helper, FATAL only for unknown names, labels and locks. Scratch checks below |
| B2: `process` rule inconsistent | **Mostly resolved; R3-1 open** | The rule is now "any child, directly or through product code", applied to 22 tests. Dynamic spot-check confirms three of the new labels and the two unlabelled tests checked; `pathbar_ui` is the exception |
| Docs and handoff match the design | **Resolved** except the `pathbar_ui` line | `TestLabels.cmake` header, `docs/TESTING.md` "When a packet adds a test", handoff "Review round 2" |
| Optional: "left `fast`" wording, measured Windows time | **Done** | Handoff now says "removed from `fast`"; `docs/TESTING.md` quotes 118 s |

## CMake logic

Read at `fdc508c`.

- **Include order.** `cmake/TestLabels.cmake` is included at root line 11, right after `option(GLANCELOT_BUILD_VIEWER ...)` (line 9) and before the first `add_test` (`cmake/ExportTests.cmake`, line 19). The central declarations therefore see the real `GLANCELOT_BUILD_VIEWER` value. The helper records into global properties only, so it works before or after the matching `add_test`; I checked a declaration placed before its `add_test` (`zz_early` below).
- **Finalize runs last.** `glancelot_finalize_test_labels()` is the last statement of the root file (lines 326-328, under `if(BUILD_TESTING)`). It comes after every include that registers tests: `ExportTests`, `StartupWindowGeometryTests`, `PlatformTestFixtures`, `FSWatchTests`, `RecoveryTests` and `MonitorCustomization` (line 319). Nothing follows it.
- **Computed `fast`.** `get_property(registered DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY TESTS)` reads every root-directory test. Declared labels are applied first, then `fast` is appended to each test whose `LABELS` contain none of `ui`, `budget` or `native`. It reads the real `LABELS` property, so a test labelled with plain `set_tests_properties(... LABELS ui)` is handled too.
- **FATAL rules.** An unknown label (including `fast`) or lock fails at the helper call. A declared name that is not registered fails at finalize. An undeclared test is not an error. This matches the lead's design, with one addition (N1 below).
- **Helper semantics.** `LABELS budget` and `LABELS clipboard` also add the matching `RESOURCE_LOCK`. `LOCKS` adds a lock without the label, so `LOCKS budget` alone leaves a test `fast` while it holds the budget lock; `docs/TESTING.md` documents that. Labels and locks are de-duplicated before they are applied.
- **Viewer off.** Core declarations are unconditional and viewer declarations sit inside `if(GLANCELOT_BUILD_VIEWER)`, so both configurations validate every declaration they make. A fresh viewer-off configure passed (output below).
- **Subdirectories.** The root uses no `add_subdirectory` or `FetchContent`, so `TESTS` on the root directory sees every test today.
- **No other property changes.** The function sets only `LABELS` and `RESOURCE_LOCK`. A json-v1 comparison of `e5c1e5c` and `fdc508c` from the same build tree confirms it (below).

### Non-blocking notes

- **N1. An extra FATAL for subdirectory tests.** Finalize walks `SUBDIRECTORIES` and fails if any registers tests. The lead's design names only unknown names, labels and locks. Nothing hits it today, but a future vendored dependency with tests would break configure. The alternative (silently missing `fast`, because root `TESTS` does not list them) is worse, so this is defensible. A `WARNING`, or a documented exception for third-party directories, is the lead's call.
- **N2. `CMAKE_SOURCE_DIR`.** Finalize reads `CMAKE_SOURCE_DIR`. If Glancelot were ever added to another project with `add_subdirectory`, that would point at the parent. `CMAKE_CURRENT_SOURCE_DIR` or `PROJECT_SOURCE_DIR` would be safer. This is minor.
- **N3. Platform-only tests.** A test registered only on one platform must be declared inside the same `if()` as its `add_test`, or finalize fails on the other platform. The documented "label it right after its `add_test`" pattern already does this; a sentence in `docs/TESTING.md` would make it explicit.
- **N4. `copy_move` is labelled `process` but was never seen starting a child.** In 8 runs (3 at a 5 ms poll interval, 5 at about 15 ms) no child appeared. `ViewerController::open` defers `decode()` until the watch baseline or a 100 ms timer (`viewer_controller.cpp:329-350`), and the test moves the file first, so `decode()` usually returns early on `file != path`. The decoder can still start when the baseline wins the race, so the label is conservative and harmless. Removal is not required.

## Windows stage

### Incremental build

In `.worktrees/review-en-05`: `git checkout --detach origin/claude/en-03-ctest-labels` (HEAD `fdc508c`), then `& .\scripts\build.ps1 -BuildDir build-review-en-05`:

```
-- Configuring done (24.2s)
-- Generating done (2.4s)
-- Build files have been written to: C:/src/Github/Glancelot/.worktrees/review-en-05/build-review-en-05
[40/57] Automatic MOC and UIC for target io_scheduler_tests
...
[45/47] Automatic MOC and UIC for target format_tests
exit: 0
elapsed 00:00:34.18
```

Only AutoMoc/UIC steps ran. A second run printed no `Building` or `Linking` line (count 0).

### Listings

```
> ctest --test-dir build-review-en-05 -N
Total Tests: 43
> ctest --print-labels
All Labels:  budget  clipboard  fast  native  process  ui
== fast : Total Tests: 27
perf_trace navigation startup_window_geometry viewer_acceptance thumbnails folder_tree pane_layout directory_model viewport themes grid_search grid_density ui_settings favorites folder_history shortcuts_help filter io_scheduler metadata animation mutation copy_move format monitor_output recovery recovery_startup monitor_customization
== ui : Total Tests: 5
folder_grid input_ui pathbar_ui fs_watch_ui monitor_customization_ui
== budget : Total Tests: 7
window_slots folder_grid input_ui fs_watch fs_watch_budget cache recovery_controller
== native : Total Tests: 9
selection_export directory_listing file_operations window_slots folder_grid file_operation_service platform_integration fs_watch recovery_process
== process : Total Tests: 22
selection_export directory_listing file_operations viewer_acceptance thumbnails folder_tree window_slots folder_grid input_ui file_operation_service platform_integration fs_watch fs_watch_budget filter cache io_scheduler copy_move recovery recovery_startup recovery_controller recovery_process monitor_customization_ui
== clipboard : Total Tests: 3
folder_grid input_ui metadata
LE budget: Total Tests: 36
fast+native: Total Tests: 0
```

The `fast` set is the same 27 tests as round 2.

### json-v1: `e5c1e5c` against `fdc508c`

I reconfigured the same build tree at `e5c1e5c`, saved `ctest --show-only=json-v1`, then went back to `fdc508c` and compared test by test:

```
names/order identical: True
non-label/lock diffs: 0          (command, TIMEOUT, ENVIRONMENT, WORKING_DIRECTORY)
selection_export LABELS: [native] -> [native,process]
directory_listing LABELS: [native] -> [native,process]
file_operations LABELS: [native] -> [native,process]
viewer_acceptance LABELS: [fast] -> [fast,process]
folder_tree LABELS: [fast] -> [fast,process]
folder_grid LABELS: [budget,clipboard,native,ui] -> [budget,clipboard,native,process,ui]
input_ui LABELS: [budget,clipboard,ui] -> [budget,clipboard,process,ui]
file_operation_service LABELS: [native] -> [native,process]
fs_watch_budget LABELS: [budget] -> [budget,process]
filter LABELS: [fast] -> [fast,process]
cache LABELS: [budget] -> [budget,process]
copy_move LABELS: [fast] -> [fast,process]
recovery_controller LABELS: [budget] -> [budget,process]
monitor_customization_ui LABELS: [ui] -> [process,ui]
```

The only change is `process` on the 14 tests the handoff lists. `RESOURCE_LOCK` is unchanged:

```
window_slots: lock=budget            folder_grid: lock=budget,clipboard   input_ui: lock=budget,clipboard
fs_watch: lock=budget                fs_watch_budget: lock=budget         cache: lock=budget
metadata: lock=clipboard             recovery_controller: lock=budget
```

### Scratch undeclared test (not committed, reverted)

I added `cmake/ZzDummy.cmake` and included it from `CMakeLists.txt` right after `include(cmake/MonitorCustomization.cmake)`, so before finalize. `TestLabels.cmake` was untouched.

```cmake
add_test(NAME zz_dummy COMMAND ${CMAKE_COMMAND} -E true)
glancelot_test_labels(zz_early LABELS budget)
add_test(NAME zz_early COMMAND ${CMAKE_COMMAND} -E true)
```

```
-- Configuring done (23.8s)
configure exit: 0
Total Tests: 45
fast: Total Tests: 28
  Test #44: zz_dummy
budget: Total Tests: 8
zz_dummy: LABELS=fast RESOURCE_LOCK=
zz_early: LABELS=budget RESOURCE_LOCK=budget
```

The undeclared test is `fast`. A declaration made before its `add_test` works, and `LABELS budget` adds the lock and keeps the test out of `fast`.

Negative checks, same scratch file:

```
# appended: glancelot_test_labels(zz_dumy LABELS process)
CMake Error at cmake/TestLabels.cmake:68 (message):
  glancelot_test_labels(zz_dumy ...): 'zz_dumy' is not a registered test.
  Fix the name or remove the declaration.
Call Stack (most recent call first):
  CMakeLists.txt:328 (glancelot_finalize_test_labels)
-- Configuring incomplete, errors occurred!
configure exit: 1

# replaced with: glancelot_test_labels(zz_dummy LOCKS budgte) + add_test(zz_dummy)
CMake Error at cmake/TestLabels.cmake:42 (message):
  glancelot_test_labels(zz_dummy): unknown lock 'budgte'.  Allowed:
  budget;clipboard.
Call Stack (most recent call first):
  cmake/ZzDummy.cmake:1 (glancelot_test_labels)
  CMakeLists.txt:320 (include)
-- Configuring incomplete, errors occurred!
configure exit: 1
```

Afterwards I deleted `cmake/ZzDummy.cmake`, ran `git checkout -- CMakeLists.txt` (`git status --short` empty) and reconfigured: exit 0, 43 tests, 27 `fast`.

### Viewer off

A fresh configure of the same source with `-DGLANCELOT_BUILD_VIEWER=OFF` in a scratch directory outside the repo, deleted afterwards:

```
-- Configuring done (2.1s)
configure exit: 0
Total Tests: 5
All Labels:  fast  native  process
fast : perf_trace navigation
native : selection_export directory_listing file_operations
process : selection_export directory_listing file_operations
```

### `process` spot-check (B2)

**Method.** A C# Toolhelp32 snapshot poller ran in the background while targeted `ctest -R ^<test>$` runs went one at a time, outside the gate lock (targeted runs are exempt). It recorded every process whose parent was a `*_tests.exe` whose image path lies in `build-review-en-05`, so other agents' builds are excluded. It polled every 5 ms plus the snapshot cost (2324 polls in 76.8 s, about 33 ms per cycle).

The poller misses short-lived children. The positive control `io_scheduler` was missed in at least one of three runs (seen in session 2 only). The poller keeps one line per test and session, so repeated runs in a session collapse. A run with no child is therefore weak evidence by itself, so each "no child" result below is paired with the static checks.

**Three tests newly labelled `process`:**

| Test | Observed | Static reason |
| --- | --- | --- |
| `monitor_customization_ui` | `monitor_customization_ui_tests.exe -> glancelot-decoder.exe` | opens generated images through the viewer |
| `recovery_controller` | `recovery_controller_tests.exe -> glancelot-decoder.exe` (2 of 2 runs) | opens images through `ViewerController` |
| `file_operation_service` | `file_operation_service_tests.exe -> subst.exe` | `QProcess::execute("subst", ...)` |
| `copy_move` (extra) | none in 8 runs | see N4 |

**Tests left without `process`:**

| Test | Observed | Static check |
| --- | --- | --- |
| `monitor_output` | none | no `open`, `openFolder`, `ThumbnailService` or `QProcess` in the test source |
| `format` | none (2 runs) | same; `GLANCELOT_DECODER_PATH` is defined but unused |
| `fs_watch_ui` | none (2 runs) | `navigateDirectory` on an empty `QTemporaryDir` (`fs_watch_ui_tests.cpp:34`, `:51`) |
| `favorites`, `mutation`, `shortcuts_help`, `ui_settings`, `monitor_customization`, `themes`, `metadata` | none | same grep finds nothing |
| **`pathbar_ui`** | **`pathbar_ui_tests.exe -> glancelot-decoder.exe` (both sessions, at least 2 of 3 runs)** | `openFolder` -> `viewer.open(p1.png)` (R3-1) |

Static cross-check: of the targets with `add_dependencies(<t> glancelot-decoder)`, the ones not labelled `process` are `favorites`, `format`, `fs_watch_ui`, `monitor_output`, `monitor_customization`, `mutation`, `pathbar_ui`, `shortcuts_help` and `ui_settings`. A grep of `tests/` for `openFolder(`, `viewer.open`, `controller.open`, `ThumbnailService`, `QProcess`, `std::system`, `startDetached` and `navigateDirectory` hits only `pathbar_ui` (through `ui_test_support.hpp`) and `fs_watch_ui` (empty folder) among them. So `pathbar_ui` is the only remaining mislabel I found.

Poller output, verbatim:

```
io_scheduler : 100% tests passed out of 1 (1.1 s)
copy_move : 100% tests passed out of 1 (1.4 s)
monitor_customization_ui : 100% tests passed out of 1 (43.9 s)
recovery_controller : 100% tests passed out of 1 (16.2 s)
file_operation_service : 100% tests passed out of 1 (1.8 s)
pathbar_ui : 100% tests passed out of 1 (5.1 s)
monitor_output : 100% tests passed out of 1 (3.1 s)
format : 100% tests passed out of 1 (0.5 s)
fs_watch_ui : 100% tests passed out of 1 (3.6 s)
polls: 2324 in 76.8 s
children seen:
file_operation_service: file_operation_service_tests.exe -> subst.exe
monitor_customization_ui: monitor_customization_ui_tests.exe -> glancelot-decoder.exe
pathbar_ui: pathbar_ui_tests.exe -> glancelot-decoder.exe
recovery_controller: recovery_controller_tests.exe -> glancelot-decoder.exe

pathbar_ui, copy_move, io_scheduler, pathbar_ui, copy_move, io_scheduler, thumbnails (all passed)
children seen:
io_scheduler: io_scheduler_tests.exe -> glancelot-decoder.exe
pathbar_ui: pathbar_ui_tests.exe -> glancelot-decoder.exe
thumbnails: thumbnail_tests.exe -> glancelot-decoder.exe
thumbnails: thumbnail_tests.exe -> thumbnail_tests.exe
thumbnails: thumbnail_tests.exe -> WerFault.exe

favorites, mutation, shortcuts_help, ui_settings, monitor_customization, themes, metadata (all passed)
children seen: (none)
```

An earlier CIM-based poller (about 350 ms per cycle) saw only `recovery_controller_tests.exe -> glancelot-decoder.exe` over the same kind of run, which is why I switched to the faster one. `thumbnail_tests.exe -> thumbnail_tests.exe` and `-> WerFault.exe` come from the test's own fault cases: it builds `ThumbnailService(QCoreApplication::applicationFilePath())` (`tests/thumbnail_tests.cpp:383`) and runs itself as a fake decoder, and its `crash` mode calls `std::abort()` (`:101`). The test passed.

### Serial `fast` run inside the gate lock

The run waited for the lock, which `claude/en-06-fix`, `review/en-02`, `review/en-06` and `lead/integrate-cycle1-a` held in turn, and then ran alone:

```
> & C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch review/en-03-r3 -Run { $env:PATH = "...python\Scripts;...Qt\6.8.3\msvc2022_64\bin;$env:PATH"; Measure-Command { ctest --test-dir C:\src\Github\Glancelot\.worktrees\review-en-05\build-review-en-05 -L fast --output-on-failure | Out-Host } }
 1/27 Test  #2: perf_trace .......................   Passed    0.09 sec
 2/27 Test  #3: navigation .......................   Passed    2.85 sec
 3/27 Test  #6: startup_window_geometry ..........   Passed    0.08 sec
 4/27 Test  #7: viewer_acceptance ................   Passed   10.75 sec
 5/27 Test  #8: thumbnails .......................   Passed    6.03 sec
 6/27 Test  #9: folder_tree ......................   Passed   10.10 sec
 7/27 Test #10: pane_layout ......................   Passed    1.22 sec
 8/27 Test #16: directory_model ..................   Passed   27.55 sec
 9/27 Test #17: viewport .........................   Passed    0.09 sec
10/27 Test #18: themes ...........................   Passed    0.67 sec
11/27 Test #20: grid_search ......................   Passed    0.36 sec
12/27 Test #21: grid_density .....................   Passed    0.24 sec
13/27 Test #22: ui_settings ......................   Passed    1.55 sec
14/27 Test #26: favorites ........................   Passed    0.64 sec
15/27 Test #27: folder_history ...................   Passed    0.09 sec
16/27 Test #28: shortcuts_help ...................   Passed    0.89 sec
17/27 Test #29: filter ...........................   Passed    3.34 sec
18/27 Test #31: io_scheduler .....................   Passed    0.25 sec
19/27 Test #32: metadata .........................   Passed    0.10 sec
20/27 Test #33: animation ........................   Passed    0.46 sec
21/27 Test #34: mutation .........................   Passed    0.17 sec
22/27 Test #35: copy_move ........................   Passed    0.21 sec
23/27 Test #36: format ...........................   Passed    0.10 sec
24/27 Test #37: monitor_output ...................   Passed    2.49 sec
25/27 Test #38: recovery .........................   Passed   14.31 sec
26/27 Test #39: recovery_startup .................   Passed    0.50 sec
27/27 Test #42: monitor_customization ............   Passed    8.80 sec

100% tests passed out of 27

Label Time Summary:
clipboard    =   0.10 sec*proc (1 test)
fast         =  93.94 sec*proc (27 tests)
process      =  45.50 sec*proc (8 tests)

Total Test time (real) =  95.13 sec
ctest exit: 0
Measure-Command: 96.066834 s
```

Acceptance ("under about 3 minutes, all pass") is met: 96.1 s, 27 of 27 passed. Round 2 measured 118.0 s for the same 27 tests; the difference is machine load, not the branch. `docs/TESTING.md` quotes 118 s, which is still a fair upper figure.

## Not verified

- Docker and Linux were not run in this round. The Linux viewer-on and viewer-off configures are in the handoff ("Review round 2"), not reproduced here.
- The full gate (`scripts/build.ps1 -Test`) was not run; both gate paths still run `ctest` without `-L` or `-j`.

## Required before approval

1. **R3-1:** label `pathbar_ui` `process`, and update `docs/TESTING.md` (23 `process` tests) and the handoff's "Not `process`" list. No gate risk, so the lead may waive it or apply it at integration.

Optional: N1 (WARNING instead of FATAL for subdirectory tests), N2 (`PROJECT_SOURCE_DIR`), N3 (one sentence on platform-only tests).
