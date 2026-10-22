**ACCEPT** for source and diff. I found no required changes.

I read AGENTS.md, the plan, `.cache/watcher-harness/review.patch`, the actual `tests/fs_watch_budget_tests.cpp` and `cmake/FSWatchTests.cmake`. I made no writes or builds, and I used no network or subagents. I compared every hunk in the patch to the working file, and each one matches. I did not run `git diff` against `55122d2`, so I could not independently check that nothing outside the patch hunks changed. I also did not re-run the equivalence check.

**Verified**
- **Scope:** the patch touches only `tests/fs_watch_budget_tests.cpp`. `cmake/FSWatchTests.cmake` is unchanged: `fs_watch_budget` still has `TIMEOUT 180` and `QT_QPA_PLATFORM=offscreen`. The `FS_WATCH_PHASE/PROGRESS/LIFETIME` strings appear nowhere under `src/`, so no production code changed.
- **Workload and assertions:** all of these are unchanged.
  - Both 99,999-iteration loops (writes and removals) keep identical bodies; the only change is added braces.
  - The 100k row counts, the `"generated"` content, the native identity and the rename are untouched.
  - Writer cadence (25 ms) and durations (8000 ms and 2000 ms), the `"0"` filter, and every `QVERIFY`/`QCOMPARE`/`QSKIP` are untouched.
  - The 250 ms checks and the 10 s and 30 s `QTRY` timeouts are untouched.
  - The original complete-baseline and rename retention is intact.
- **RAII ordering:**
  - `lifetime`, `phase`, `phaseLog` and `lifetimeLog` are declared before `QTemporaryDir`, `restoreTiming`, the viewer, the grid and `reportPublications`.
  - Locals are destroyed in reverse order, so `lifetimeLog` fires last, after fixture destruction and after any `QVERIFY` early return.
  - A `QSKIP` also hits the guard, so you get a lifetime line with the last phase.
- **Lifetimes and captures:**
  - `phase` is a `const char*` and is only ever assigned string literals, so there is no dangling pointer.
  - The `[&]` captures refer to locals that outlive the guard.
  - The log output carries no paths or content.
- **Logging outside measured intervals:**
  - Every `qInfo` call runs while the heartbeat is stopped or not yet started: lines 59, 84 and 92 are before it starts, line 119 is after `heartbeat.stop()`, and so are 137, 163, 177, 181, 186 and 194.
  - Inside the running-heartbeat windows, only plain `phase=` assignments occur (lines 98, 115, 159), so no synchronous output lands in the 250 ms measurements.
- **Bounded output:** there are 3 generated and 3 removed progress lines (at 25k, 50k and 75k, since the loops stop at 99,999). Together with about 10 phase lines and 1 lifetime line, that is a fixed count of roughly 17 lines.
- **Equivalence audit:** `source-equivalence.json` only holds the single assertion `equal_after_only_logging_and_braces_removed: true`. The generator script and the baseline and candidate inputs are not in the file. It's consistent with the diff I read, but it isn't independent evidence.

**Advisory timing risk (not blocking, not a scope issue)**
1. The packet adds no timeout fix. The 180 s whole-process bound is unchanged, and a slow-disk host can still fail it. The plan states this explicitly.
2. The three generated-progress `qInfo` calls run inside the `setup` timer, so the reported `setupMs` includes three small synchronous writes. Nothing asserts on `setupMs`. The same applies to the removal loop and the 180 s bound, negligibly.
3. The `baseline` phase includes `viewer.open`, the image load and the native-identity probe, which is slightly broader than "baseline" suggests. That is a labelling matter only.
4. If a failure happens inside a `QTRY` in the measured region, the phase name is whichever assignment ran last. That is the intended coarse granularity.

**Pending before done (process, not source)**
- The real targeted `fs_watch_budget` run is still underway. Its output should be inspected for the ordering and count of phase lines and for the final lifetime line.
- The full `scripts/build.ps1 -Test` and `docker build` still have to go through the main checkout's `gate-lock.ps1`. The user's test-app full gate currently holds the lock.
- Integration still needs `integration_lock`, a merge of `main` into the branch, and an exact integrated-tree comparison.
