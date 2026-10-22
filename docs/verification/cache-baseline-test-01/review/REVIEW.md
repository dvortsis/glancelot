**APPROVED** for exact pin `d63cd9d5f36f0134a4b42bcddeaa4b62ddad198f`. I found no blocking findings. This was read-only (Read, Glob and Grep). I couldn't run `git` or the build, so everything below comes from reading the patch and source, not from running it.

**1. Namespace and declarations: fixed.**
- `fs_monitor.hpp:18` declares `namespace fs_watch`, and `ControllerTestHooks` (line 36) and `inline thread_local controllerTestHooks` (line 49) sit inside it under `GLANCELOT_FS_WATCH_TESTING`.
- `cache_tests.cpp:298-303` now uses `fs_watch::ControllerTestHooks` and `fs_watch::controllerTestHooks`. That matches `viewer_controller.cpp:149` and the `HookScope` in `fs_watch_tests.cpp:56-58`.
- The earlier errors in `.cache/build-attempt-1.txt` (C3083, C2039, lines 288-293) were all caused by the `glancelot::fs_watch` spelling, so they should be gone.
- `src/app` is on the target's include path. `fs_monitor.hpp` pulls in `fs_revision.hpp`, which has the `<filesystem>` and `<stop_token>` includes it needs.
- `QScopeGuard`, `QTemporaryDir` and `QByteArray` are all included, and `qScopeGuard` captures by value.
- The compiler flags are the same as the failed attempt apart from the source, so I don't expect new compile problems.
- `/W4` applies only to `glancelot_core`, and I found no `/WX` anywhere.

**2. Scoped hook lifetime: sound.**
- The controller copies the hooks by value when its private state is built (`viewer_controller.cpp:149-150`). The pointer only needs to be valid during construction.
- It is `thread_local`, and every controller in `cache_tests.cpp` is built on the main thread inside a slot.
- `hooks` is a stack object in `main()` and outlives `qExec`. `previousHooks` is captured before the install and restored by a guard.
- A default-constructed `ControllerTestHooks` is inert apart from `baselineDeadlineMs`. `registerNative` stays true, `read` is the default snapshot, and `decoderProgram` and the other overrides are empty. So the real native watcher and decoder are exercised.

**3. Scoped config lifetime and cleanup: sound.**
- `GLANCELOT_CONFIG_DIR` is read at call time in `configDirectory()` (`viewer_controller.cpp:94`), `settings_file.cpp:71` and `theme_manager.cpp:18`. Setting it before the first controller therefore isolates all three readers.
- The prior value is captured with `hadConfig` and `previousConfig`, then restored or unset by a guard. This is a more careful pattern than the other tests in the repo, which use bare `qunsetenv`.
- `config` is declared before the guards, so destruction runs in this order: `tests`, then the hook guard, then `hooks`, then the config guard, then the `QTemporaryDir` removal, then `app`. The environment is restored before the directory is deleted, and no controller is alive at that point.
- The early `return 1` on an invalid temp directory happens before anything is installed.
- Non-blocking: all slots share one config directory for the run. Persisted state such as boundary mode could carry between slots. The slots that depend on it (`crossFolderLookaheadPrefetch`, `lookaheadCancellationOnReversal`, `lookaheadCancellationOnFilterChange`) call `setBoundaryMode("continue")` themselves, and `cacheDefaults` runs first. Before this patch the state leaked into the real user preferences, so this is a strict improvement.

**4. Target-only macro and ODR: clean.**
- `GLANCELOT_FS_WATCH_TESTING` is `PRIVATE` on `cache_tests` only (`CMakeLists.txt:229`).
- `viewer_controller.cpp` and `directory_entry_model.cpp` are compiled directly into that target. They are the only non-test sources that include `fs_*` headers, so every TU that sees those headers has the macro defined. `glancelot_core` and `glancelot_platform` include none of them.
- Nothing under `include/` references the macro, so no class layout differs between TUs.
- `FSWatchTests.cmake:17` already uses the same pattern for the fs_watch targets.
- The `add_test` change follows the `fs-watch-results.txt` precedent in `FSWatchTests.cmake:23`. The path is quoted, and the second `-o -,txt` keeps stdout output.

**5. Original assertions: unchanged.**
- `source.patch` shows only added lines in `tests/cache_tests.cpp`. The only removed line in the whole patch is the old `add_test` in `CMakeLists.txt`.
- I read the full file. All eight slots and their assertions are present, including the cache-miss ones in `revisionInvalidationOnDiskChange` (line 149) and `clearDecodedCache` (line 180), and the cache-hit ones in `cacheHitOnRevisit`, `bidirectionalNavigation` and `crossFolderLookaheadPrefetch`.

**6. Stale-pixel protection: not weakened.**
- The only behavioural change is `decodeStart.setInterval(5000)` (`viewer_controller.cpp:322`), which lengthens the fallback timer.
- The production default stays 100 ms (line 320). The late-baseline path (lines 339-341) still bumps `generation`, calls `cancel()`, sets `cacheReuseAllowed=false` and does a fresh decode.
- Revision validation and the cache-hit decision are untouched. Because the tests now wait for registration, the cache-hit assertions check real cache behaviour instead of racing the fallback.
- The 100 ms fallback, fresh-pixel and newer-open-wins cases stay covered by `controllerLateCoalescedBaselineDecodesOnce` in `fs_watch_tests.cpp`. That test uses `baselineDeadlineMs=0`, i.e. the production default. I read it at `4fd1ee5`, and the patch since then doesn't touch `fs_watch_tests.cpp`.

**Still pending (yours, as stated):**
- The targeted build and the 10 `ctest -R cache` repeats, including under parallel load, with `decode:begin` equal to the navigation steps.
- The complete `scripts/build.ps1 -Test` gate with raw Qt case output for `fs_watch` and `fs_watch_budget`, then `docker build`, then the exact merge checkpoint.
- A compile success of this pin, which I could not observe. Please confirm the previous C3083 and C2039 errors are gone in the next build log.

No UI or hardware review was done.