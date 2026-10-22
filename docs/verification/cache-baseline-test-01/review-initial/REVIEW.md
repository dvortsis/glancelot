**APPROVED** for exact pin `4fd1ee5159068a157d084ce03a6c7638befd68cd`, subject to the checks listed at the end. I found no blocking findings.

I read the worktree files at HEAD with Read, Glob and Grep only. I could not run `git diff a83e43e..4fd1ee5`, so I can't confirm that the diff touches only the two files you named. I also can't confirm that every cache assertion is byte-identical to the base. I read `tests/cache_tests.cpp` in full. All 8 slots and their `QVERIFY`/`QCOMPARE` assertions are present, including the cache-miss ones.

**Assessment**

1. **Hook lifetime and restoration: sound.**
   - The controller copies the hooks by value when its private state is constructed (`viewer_controller.cpp:149`). The pointer only needs to be valid during construction.
   - `hooks` is a stack object in `main()` and outlives `QTest::qExec`.
   - The pointer is `thread_local`, and the controllers are built on the main thread inside the test slots.
   - `previousHooks` is captured before the install, and the `qScopeGuard` restores it on exit.
   - The guard is destroyed before `hooks` and `app`, and no controller is alive by then. The restore is effectively redundant but harmless.
   - It is the same pattern as `HookScope` in `fs_watch_tests.cpp:55-59`.

2. **Target-only define and ODR: clean.**
   - `GLANCELOT_FS_WATCH_TESTING` is set `PRIVATE` on `cache_tests` only (`CMakeLists.txt:229`).
   - `viewer_controller.cpp` and `directory_entry_model.cpp` are compiled directly into that target, not taken from a library.
   - `glancelot_core` and `glancelot_platform` include no `fs_*` headers, so no TU with the other macro state exists.
   - Public headers under `include/` have zero uses of the macro, so no class layout differs between TUs. `fs_monitor.hpp` and `fs_revision.hpp` are private `src/app` headers, already on the include path.
   - The extra macro-gated code is inert with the defaults. The `decoderProgram` and `decoderArguments` overrides are empty, so the real decoder is used. `registerNative` stays `true`. The model publication timing hook is empty.

3. **No weakening of stale-pixel or membership checks.**
   - The only change is `baselineDeadlineMs=5000`, which lengthens the fallback `QTimer` interval (`viewer_controller.cpp:320-323`).
   - The cache-hit path (`revisionOnly = !fresh && cacheReuseAllowed && cache.contains(key)`, line 845) is untouched.
   - `revisionInvalidationOnDiskChange` and `clearDecodedCache` still assert misses.

4. **Deadline and fallback coverage is preserved.**
   - Production still defaults to 100 ms (line 320). `controllerLateCoalescedBaselineDecodesOnce` (`fs_watch_tests.cpp:747-776`) uses `baselineDeadlineMs=0`, so it exercises the 100 ms fallback.
   - It asserts fresh pixels, the newer-open-wins case, and the `decode:begin` counts of 2 and 3.
   - Raising the deadline to 5000 for IPC and cache-hit fixtures already has precedent (`fs_watch_tests.cpp:74`, `392`, `669`).

5. **This isolates cache-hit semantics rather than masking a bug.** The observed failures match designed behavior, not a cache defect:
   - If the 100 ms fallback fires before the baseline, `decodeStartedWithoutBaseline` is set.
   - When the late baseline arrives (lines 339-341), the controller bumps `generation`, sets `cacheReuseAllowed=false`, and does a fresh decode.
   - That forces `isLastDecodeCacheHit == false` and produces the 6-8 `decode:begin` entries against 5 navigation steps, as your traces show.
   - With the baseline awaited, cache reuse and revision-only validation are evaluated as intended. The two files correctly separate "cache hit" from "late baseline restarts decode".

**Non-blocking notes**
- **Possible follow-up for the lead, outside this patch.** Every navigation creates a new monitor scope and so needs a fresh baseline. That costs one 50 ms poll tick plus two directory snapshots (the folder and its parent). On a slow or large folder, the 100 ms fallback loses cache hits in production too. That is an intended trade-off today, but it may be worth a separate perf ticket.
- **Timeout equality.** The default `QTRY_*` timeout is 5000 ms, the same as the hook. If a baseline never arrives, the test fails at the `QTRY` timeout before the fallback fires. It still fails, so this is only a cosmetic point.
- **Lead ownership.** The `CMakeLists.txt` change is lead-owned, so it should be applied under the integration lock.

**Remaining checks before integration**
1. Run `git diff --stat a83e43e..4fd1ee5` and confirm exactly two files change. Confirm the diff adds no changed or removed `QVERIFY`/`QCOMPARE` lines in `cache_tests.cpp`.
2. Do the targeted build, then ten consecutive `ctest -R cache` runs. These should be run under load, ideally overlapping the other targeted suites, since the original failures only appeared under load. Pass criterion: 10/10, with `decode:begin` equal to the navigation steps in the traces.
3. Run `ctest -R fs_watch` (including both `controllerLateCoalescedBaselineDecodesOnce` rows) and `fs_watch_budget`, to show the production fallback coverage still passes.
4. Run the full `scripts/build.ps1 -Test` gate under the gate lock, then `docker build -t glancelot-browser:main .`, then the exact-commit integration after merging `main`. All three are still pending.

No code was edited or executed during this review.