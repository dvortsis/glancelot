# FS-WATCH-01 Claude independent review R12

Reviewer: Claude (reviewer lane; not the Codex implementer). Candidate: `1b6c031ea84efb6d21b741b4c4305ee1f280c631` (`git rev-parse HEAD` in this tree, clean). Delta reviewed: `0cab319..HEAD` (R11 report preserved at `docs/verification/fs-watch-01/claude-r11-review.md`; R11 was not treated as final approval).

## Verdict

**Source acceptable, subject to the pending full gates** (Windows 32-target gate, Docker full gate, integrated-commit checks). One new Low finding (L1) is recorded; it does not block. No claim of any completed gate is made here.

## F1 disposition (R11 medium): CLOSED in source, runtime evidence not re-run by me

`src/app/io_job_lease.hpp:22-48` (c48b77c):
- **Idempotence**: one `Retirement{lease,disposed}` is shared by all three connections (`finished`, `stateChanged`, `errorOccurred`); `std::exchange(disposed,true)` gates lease release, `deleteLater` and the release callback. Each lambda copy holds the same `shared_ptr`, so exactly-once holds regardless of signal order or repetition.
- **Release condition**: `stateChanged` fires only on `NotRunning`; `errorOccurred` re-checks `process->state()==NotRunning`. A Starting process that fails to exec (the R11 repro) now releases; a still-running process cannot release early (an error such as `Crashed` with state not yet NotRunning is ignored, then `finished`/`stateChanged` follows).
- **Process/lease lifetime**: connections use `process` as context, so they die with it; if the process is destroyed without firing, the lambda copies are destroyed and the shared `Retirement` releases the lease via `~IoJobLease` (no leak path). `deleteLater` (not `delete`) inside the process's own signal is safe. Lease reset precedes `deleteLater`.
- **Owner independence / callback safety**: the lease release no longer depends on the owner (closed-owner test). The `released` callback is queued with a `QPointer<QObject>` guard and is dropped if the owner is gone, matching the previous behaviour of the owner-context connection. Callers (`thumbnail_service.cpp:89`, `viewer_controller.cpp:547,564`) capture `this` whose lifetime equals the owner `q`, so the guard also protects `this`.
- **Reentrancy**: `IoScheduler::release` (`io_scheduler.cpp:141-151`) drops its mutex before `notifyAdmissionAvailable()`; dispose runs no user code inline now (callback is queued), so reentry into the controller from the signal handler is removed (an improvement over 0cab319, where `released` ran inside `finished`).
- The `!process` path still queues the callback with a non-null owner (all three callers pass `q`).
- Evidence read (not re-executed): `lease-probe/*`, `linux-retired-worker-red.txt` / `-green.txt` (green: Starting at retirement, process deleted, lease released, callbacks 1, jobs 0, bytes 0). I did not run the Linux probe; Windows missing-start is synchronous NotRunning, so Windows results cannot qualify the async edge, as the task states.

## Findings

**L1 (Low, unverified at runtime) - stale null `QPointer` entries in ThumbnailService `retiring`**
`src/app/thumbnail_service.cpp:88-89`. The callback `retiring.removeAll(process)` is now queued via a zero-timer *after* `deleteLater()` in `dispose` (`io_job_lease.hpp:30-32`). If the deferred delete is processed before the timer, the `QPointer<QProcess>` entry is already null and no longer compares equal to the raw pointer, so the entry is not removed. Before c48b77c the callback ran synchronously in `finished` (before deletion). Impact: one pointer-sized null entry per cancelled-running worker accumulates until `stopWorkers(wait)` (`:96-104` skips nulls and clears the list), contravening the "bound allocation" rule only marginally; no correctness or admission effect. Suggested fix (lead/owner): `retiring.removeIf([](auto& p){return !p;})` in the callback, or invoke the callback before `deleteLater`. I did not run a test to confirm the event ordering.

**Test note (informational)**: `retiredWorkerReleasesAdmissionExactlyOnce` (`tests/fs_watch_tests.cpp`, ~318-347) covers failed-start (asserts `Starting` on Unix only), running and closed-owner, and checks lease expiry, callback count after a 25 ms settle, and scheduler counters. It does not exercise the `retiring` bookkeeping (L1) or repeated-signal idempotence directly beyond the callback count of 1.

## Test-split assessment: ACCEPTABLE

Personally executed comparison: the removed `hundredThousandEntriesSustainedWriterKeepsGuiResponsive` body in `c48b77c:tests/fs_watch_tests.cpp` (lines 681-801, 121 lines) versus the same-length block starting at line 54 of `tests/fs_watch_budget_tests.cpp`: `Compare-Object` line-for-line, **0 differences** (byte-identical per line). All limits (100k files, 250 ms, 30 s) and the `QSKIP` on missing native identity are retained; the QSKIP semantics are the same as before, so a skip must still be treated as unqualified by the gate (the evidence reports zero skips on Windows). The budget file keeps the needed helpers (`write`, `hash`, `png`), `initTestCase` config dir, includes (`<atomic>`, `<thread>`, `QSaveFile`, ...) and `QTEST_MAIN` + `.moc`; the `fs_watch_tests.cpp` diff for this part is pure deletion (only the new lease test and one include were added). No other case was omitted from the main file in the diff. No production code changed apart from `io_job_lease.hpp` (`git diff 0cab319..HEAD --name-only -- src ui CMakeLists.txt` lists only that file; no QML change in this delta).

## CMake (`cmake/FSWatchTests.cmake`, `CMakeLists.txt:174`)
- Included unconditionally inside `if(BUILD_TESTING)`; both `fs_watch` and `fs_watch_budget` have `QT_QPA_PLATFORM=offscreen` and `TIMEOUT 180` (`set_tests_properties` on both).
- **Test-only hooks exclusion**: `GLANCELOT_FS_WATCH_TESTING` is defined only via `target_compile_definitions` on `fs_watch_tests` and `fs_watch_budget_tests`, which recompile the backend sources from a shared list; `glancelot_core`/app targets do not define it, and a grep shows the macro only inside `#ifdef` blocks in `src/app` files (directory_entry_model, viewer_controller, fs_monitor, fs_revision). Product binaries are therefore built without the hooks. Not verified by inspecting built artifacts.
- `fs_watch_budget_tests` correctly does not depend on `fs_watch_decoder_fixture`; it does depend on `glancelot-decoder` (it opens a real PNG). Dual `-o file,txt -o -,txt` sinks on all three QtTest targets; `${CMAKE_CURRENT_BINARY_DIR}` resolves to the top-level build dir because the file is `include()`d (not a subdirectory).
- Minor: three result files are written into the build dir and overwritten per run; no cleanup needed.

## Commands personally executed (read-only)
`git rev-parse HEAD`; `git diff --stat/--name-only 0cab319..HEAD`; `git diff 0cab319..HEAD` for `io_job_lease.hpp`, `cmake/FSWatchTests.cmake`, `tests/fs_watch_tests.cpp`, `docs/FS-WATCH-01-PLAN.md`; PowerShell `git show c48b77c:tests/fs_watch_tests.cpp` + `Compare-Object` against `tests/fs_watch_budget_tests.cpp` (0 differences); `Select-String` greps for `retireIoProcess`, `GLANCELOT_FS_WATCH_TESTING`, CMake wiring, `retiring`; file reads of `io_scheduler.cpp`, `thumbnail_service.cpp`, `viewer_controller.cpp` excerpts, the green probe log.

## Not executed / unverified
No build, no Qt test, no Windows suite, no Linux probe re-run, no Docker, no GPU/hardware/network checks, no QML review beyond confirming no QML delta. The Windows 32-target gate, Docker full gate and integrated-commit checks remain pending; this report does not claim them. Root `.cache` was not read.
