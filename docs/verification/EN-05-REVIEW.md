# EN-05 independent review: viewer source list and decode-cache extraction

Verdict: **APPROVED** at code tip `9c7b203153963a1a721cb33f66a814eba4c9f232` (branch `claude/en-05-viewer-sources`, base `afeac1a`). There are no blocking findings.

## Review setup

| Item | Value |
| --- | --- |
| Reviewer | Fresh-context Claude reviewer (Opus) that did not implement EN-05 |
| Diff reviewed | `git diff afeac1a 9c7b203` |
| Code worktree | Read-only detached worktree `.worktrees/review-en-05` at `9c7b203` |
| Build directory | `build-review-en-05` |
| Docs | This file is the only commit on `claude/review-en-05`, on top of the handoff commit `d355edd` |

No source, CMake, test, queue or shared-contract file was edited. No UI changed, so the ui-design-skills requirement does not apply.

## Stage 1: code review

### Source sets

For every executable that compiled `src/app/viewer_controller.cpp` at `afeac1a`, I compared its source list before and after, across `CMakeLists.txt` and `cmake/*.cmake`.

- **What changed:** the only change in effective sources is the added `decode_cache.cpp`/`.hpp`.
- **Targets:** 23 executables are affected: 16 in `CMakeLists.txt`, 3 in `FSWatchTests.cmake`, 2 in `MonitorCustomization.cmake` and 2 in `RecoveryTests.cmake`.
- **`settings_file.cpp`:** no target lost it.
  - The monitor targets now get it from `GLANCELOT_VIEWER_SOURCES`. The `list(FIND)` check in `glancelot_attach_monitor_customization` therefore skips adding it again.
  - The recovery targets list it twice: once from `glancelot_recovery_provider_sources` and once from the new list. CMake removes the duplicate.
- **Recovery consumers:** the source regex in `glancelot_attach_recovery_consumers` still selects the same targets, because the `SOURCES` property holds the expanded paths.
- **Scope of the variable:** every use of `GLANCELOT_VIEWER_SOURCES` comes after `include(cmake/ViewerSources.cmake)` at `CMakeLists.txt:53`, inside `if(GLANCELOT_BUILD_VIEWER)`.

### DecodeCache semantics

I checked `DecodeCache` against the Qt 6.8.3 `qcache.h` in `.tools/Qt/6.8.3/msvc2022_64/include/QtCore`. Its behaviour is identical to the old `QCache` block.

- **Insert result:**
  - `QCache::insert` returns `false` only when `cost > mx` (line 186, which also removes the key and deletes the object).
  - Every other path returns `true` (line 206).
  - The two call sites therefore reproduce the old branches exactly. Prefetch notifies only when the insert stored the pixels. The decode path calls `remove(key)` when nothing was stored.
- **Capacity and cost:** capacity is `128*1024` KiB. Cost is `(sizeInBytes()+1023)/1024`. Insert refuses an empty token or an oversize cost before it reaches `QCache`, as before.
- **Reuse:** `reuse()` goes through `QCache::object()`, which is `const` and calls `relink` (line 211), so the LRU touch is kept. The token check is unchanged: the token must be non-empty and equal the stored token.
- **Byte totals:** `bytes()` is `qint64(totalCost())*1024`. It is used by both memory-admission sums and by `decodedCacheBytes()`.
- **Pass-through calls:** `contains` (no LRU touch, as before), `remove`, `count` and `clear` pass straight to `QCache`.
- **`clearDecodedCache()`:** unchanged. It cancels look-ahead, clears the cache and emits `cacheChanged`.
- **Threads and ownership:** thread affinity and ownership are unchanged, since the object is the same `Private` member used from the same call sites.

### Scope

- `include/glancelot/viewer_controller.hpp` and the rest of `include/` have no diff.
- The `viewer_controller.cpp` hunks are limited to the decode-cache block and its users.
- The handoff commit `d355edd` touches only `docs/handoff/EN-05-HANDOFF.md`.

### MSVC portability

I found no issues.

- Narrowing conversions are explicit casts.
- The `QCache` constructor is `explicit`, and the brace-initialised member is fine with it.
- There is no `/WX`.

The Stage 2 build confirms this.

### Baseline Linux failures

The handoff says three Linux failures already fail at the base. That is plausible.

- `directory_model_tests` does not compile the controller, so EN-05 cannot affect it.
- Both rename tests make their expectation depend on `QFileInfo::birthTime().isValid()`: `tests/directory_model_tests.cpp:156` and `tests/grid_ui_tests.cpp:1631`. The handoff cites nearby lines.
- On Windows, NTFS reports a birth time, and `directory_model` passes (see Stage 2).

### Findings

All findings are non-blocking.

1. **Removed `FATAL_ERROR` loop.** `cmake/MonitorCustomization.cmake` at base lines 1-8 had a `FATAL_ERROR` source-presence loop, and the packet removed it along with comment edits in `ExportTests.cmake` and `RecoverySources.cmake`.
   - Removing the loop is safe. Each file it checked is a real argument to `target_sources`, `qt_add_executable` or `qt_target_qml_sources`, so a missing file still fails at generate.
   - Scope was the open question. **The lead accepted it**, because the loop's own comment required removal before integration.
2. **Wrong count in the handoff.** It says "21 executables" but lists, and the build has, 23. This is a documentation error only.
3. **Archived verification files.** `docs/verification/safe-mode-01/{controller,legacy-slots,safe-environment}-component-CMakeLists.txt` compile `viewer_controller.cpp` without `decode_cache.cpp`. Replaying them against the new tree would fail to link. They are archived evidence and nothing builds them.
4. **Missing include.** `src/app/decode_cache.cpp` uses `std::move` without including `<utility>`. It compiles through the Qt headers. This is a nit.

## Stage 2: Windows build and targeted tests

### Build

Command: `& .\scripts\build.ps1 -BuildDir build-review-en-05`. This is MSVC 2022 with Ninja, Release, and Qt 6.8.3 `msvc2022_64`. It ran without `-Test`, so it did not take the gate lock.

```text
[566/570] Linking CXX executable recovery_startup_tests.exe
[567/570] Linking CXX executable recovery_tests.exe
[568/570] Linking CXX executable recovery_controller_tests.exe
[569/570] Linking CXX executable monitor_customization_tests.exe
[570/570] Linking CXX executable monitor_customization_ui_tests.exe
exit code 0
```

The build log contains no warning that mentions `decode_cache`.

### build.ninja checks

`decode_cache.cpp.obj` is built for exactly 23 targets. `viewer_controller.cpp.obj` is also built for 23 targets, so the two match one to one.

```text
acceptance_tests, cache_tests, copy_move_tests, filter_tests, folder_tree_tests, fs_watch_budget_tests,
fs_watch_tests, fs_watch_ui_tests, glancelot, grid_ui_tests, input_ui_tests, io_scheduler_tests,
monitor_customization_tests, monitor_customization_ui_tests, monitor_output_tests, mutation_tests,
pathbar_ui_tests, recovery_controller_tests, recovery_tests, shortcuts_help_tests, theme_tests,
ui_settings_tests, window_slot_tests
```

`settings_file.cpp.obj` appears exactly once for each recovery and monitor target:

```text
monitor_customization_tests=1
monitor_customization_ui_tests=1
monitor_output_tests=1
recovery_controller_tests=1
recovery_startup_tests=1
recovery_tests=1
```

### Targeted ctest

The tests ran inside the machine gate lock:

```powershell
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch review/en-05 -Run { ctest --test-dir build-review-en-05 -R '^(cache|viewer_acceptance|fs_watch|fs_watch_ui|recovery|recovery_controller|monitor_customization|monitor_customization_ui|monitor_output|directory_model)$' --output-on-failure }
```

```text
 1/10 Test  #7: viewer_acceptance ................   Passed    9.96 sec
 2/10 Test #16: directory_model ..................   Passed   20.71 sec
 3/10 Test #23: fs_watch .........................   Passed   85.49 sec
 4/10 Test #25: fs_watch_ui ......................   Passed    3.36 sec
 5/10 Test #30: cache ............................   Passed    5.17 sec
 6/10 Test #37: monitor_output ...................   Passed    2.50 sec
 7/10 Test #38: recovery .........................   Passed   14.16 sec
 8/10 Test #40: recovery_controller ..............   Passed   14.48 sec
 9/10 Test #42: monitor_customization ............   Passed    8.41 sec
10/10 Test #43: monitor_customization_ui .........   Passed   24.95 sec

100% tests passed out of 10
Total Test time (real) = 189.71 sec
```

No rerun was needed. Another reviewer was building EN-04 at the same time, which is why the timings are longer than on an idle machine.

## Not verified here

- `fs_watch_budget` and the full suite: these were left out on purpose because of the gate lock. The integrator's gate covers them.
- The Docker build.
- The Linux results: the three baseline failures were not reproduced here. This review accepts them from the handoff's base-commit evidence and the code reading above.
