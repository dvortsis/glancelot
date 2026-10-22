# CACHE-01 / IO-01 Claude-lane independent review (current main a395ca5)

Reviewer: Claude lane, branch `claude/review-cache-io` (worktree `.worktrees/claude-review-cache-io`, build `build-claude-review-cache-io`). Source reviewed is unmodified `main` a395ca5 (CACHE-01 8b28060, IO-01 1f97893, FS-WATCH-01 1bd4461). No product or test source was changed.

## Verdicts

- CACHE-01: **APPROVED** for the recorded follow-up (Codex 16ef41f stale-pixel defect is fixed and tested on main). Residual items below are non-blocking and stay open as scope notes.
- IO-01: **APPROVED** for the recorded follow-up (duplicate-ID overwrite, negative estimate, ownership and allocator linkage are fixed and tested on main). Non-blocking residuals below.

Neither verdict covers physical storage behaviour (see Unverified).

## Follow-up defect status on current main

### CACHE-01: same-size / restored-mtime noncurrent navigation shows stale pixels -- FIXED

- Cache entries now carry a source token (`CachedImage{pixels, sourceToken}`, `src/app/viewer_controller.cpp:235`). A navigation cache hit does not display pixels directly: `decode()` (line 846) starts a `--source-revision` revision-only worker, and the hit is accepted only if the returned token equals the stored token (lines 888-899, compare at 892). Mismatch removes the entry and falls through to a fresh decode (`decode(file,token,true)`).
- Token (`src/app/fs_source_token.hpp:8-18`) is SHA-256 over volume, object id, birth time, size, mtime, change time (ctime), link count and flags, so restoring size and mtime does not reproduce it (change time / object id differ). Empty/invalid tokens (non-regular, directory, stream) are never cached (lines 653, 945) and a malformed reply fails the decode (lines 890, 935).
- Decoded and prefetch results also carry a worker-verified token taken before and after decode (`src/worker/main.cpp:164-265`), covered by `decodeRejectsSameTupleEditAfterAdmissionProbe`.
- Explicit open bypasses the cache (`fresh`/`cacheReuseAllowed`), covered by `AcceptanceTests::explicitOpenDoesNotReuseSameSizeTimestampPixels`.
- Direct regression for the Codex scenario exists on main: `FSWatchTests::noncurrentSameTupleReplacementInvalidatesCache` (`tests/fs_watch_tests.cpp:664`). Run output: `noncurrent replacement: source QColor(ARGB 1, 0, 0, 1) display QColor(ARGB 1, 0, 0, 1) cache hit false` (displayed pixel equals new source; the Codex red log showed source blue / displayed red / cache hit true). Also `sameSizeRestoredMtimeStillHasChangedRevision` (line 1169) and `revisionOnlyWorkerDistinguishesAtomicReplacement` (line 412).

### IO-01: duplicate-ID overwrite and negative-estimate acceptance -- FIXED

- `tryAdmit` (`src/app/io_scheduler.cpp:98-101`) rejects id 0, negative estimate, an id already in `activeJobs_`, and `estimatedBytes > INT64_MAX - admittedBytes_`, all before touching the ledger and before any signal. `release` of an unknown id is a no-op and the byte decrement is clamped.
- `allocateIoJobId()` (`io_scheduler.cpp:22-30`) is a process-wide atomic starting at 1<<63, nonzero, not reset by `reset()`, returns 0 on exhaustion (declared in `include/glancelot/io_scheduler.hpp`, linkable from the tests). All controller and thumbnail admissions use it via `detail::admitIoJob` (`src/app/io_job_lease.hpp`).
- Ownership: each admission is an `IoJobLease` shared_ptr whose destructor releases exactly its own id; `retireIoProcess` holds the lease until the killed process is actually `NotRunning` (finished, stateChanged or errorOccurred) with an exactly-once `disposed` guard. Failed-start, running and closed-owner cases are tested.
- Tests on main: `admissionIdentityCannotOverwriteAnotherJob` (`tests/fs_watch_tests.cpp:323`, log "duplicate admitted false bytes 1024 jobs 1"), `admissionRejectsInvalidEstimates` (365: -1, id 0, INT64_MAX), `controllerCannotReleaseForeignAdmissions`, `retiredWorkerReleasesAdmissionExactlyOnce`, `thumbnailServicesOwnDistinctAdmissions`, `thumbnailMemoryReleaseWakesPendingWork`, `admissionAvailabilityIsQueuedAndCoalesced`.
- Stale generation / cancellation: every worker callback checks `token==generation && process==worker` (decode) or `token==lookaheadGeneration && process==prefetchWorker` (prefetch); admission results are re-checked after `admitIoJob` (viewer_controller.cpp:861, 924, 628). The probe-phase lease replaces the 0-byte initial lease only after the new one is admitted, so no gap or double count.

## Findings (ranked, none blocking)

1. **Medium (CACHE-01 acceptance wording): no test for eviction pressure or aggregate headroom.** `docs/CACHE-01-PLAN.md` requires LRU eviction under pressure and "total admitted bytes (current + worker + staging + cache) never exceeds headroom". Implementation: cache is a `QCache` capped at 128 MiB (`viewer_controller.cpp:236`); headroom is a local formula `bytes*4 + image + cache.totalCost()*1024` against min(1 GiB, 40% available physical RAM) (lines 921-922 foreground, 624-625 prefetch), and on overflow the foreground path clears the whole cache (922) while prefetch cancels (625). There is no test of the over-cost-insert refusal (653, 945), LRU eviction, or the `cache.clear()` pressure branch; `cache_tests.cpp` has 9 cases and none cover them. Also the scheduler ledger (256 MiB default, `memoryLimit_`) counts only transfer bytes of active jobs, not decoded-cache bytes, so "shared admission counts cache" is satisfied by the controller-local formula, not the shared ledger. FS-WATCH INTEGRATED.md already lists this as owner scope. Not a correctness defect, a coverage gap. A real pressure test needs either a new test target (root CMake, lead change) or a test hook for the allowance; I did not add one because no defect was demonstrated and registering a new target requires a root CMake edit.
2. **Low (IO-01): foreground admission over the limit when ledger is empty.** `io_scheduler.cpp:104` admits a foreground job whose estimate exceeds `memoryLimit_` when `admittedBytes_ == 0`. Intended (a single image must always be viewable) and bounded by the 256 MiB per-image check at viewer_controller.cpp:914, but it is a documented-by-code only soft limit. Background jobs are hard-limited.
3. **Low: initial 0-byte foreground admission** (viewer_controller.cpp:860) counts toward `activeForeground_` and blocks prefetch/background but reserves no bytes until the probe. Valid use of the "zero estimate" contract; noted only so the 0-byte semantics are explicit.
4. **Low: `IoScheduler::reset()` (io_scheduler.cpp, ~line 141) clears live leases.** Test-only; later lease release is a safe no-op because ids are never reused.
5. **Residual (inherent, documented in fs_source_token.hpp):** the token hashes metadata, not content; a write that preserves object id, size, mtime AND change time is undetectable. Cache-hit revalidation is a point-in-time probe; later external writes rely on watch/poll reconciliation.

## Commands and output

Build (targets only, Windows MSVC 2022, Qt 6.8.3, Ninja, Release, same configuration as `scripts/build.ps1`): configure + `cmake --build build-claude-review-cache-io --parallel 4 --target cache_tests io_scheduler_tests fs_watch_tests fs_watch_budget_tests fs_watch_ui_tests thumbnail_tests glancelot-decoder` exit 0 (90/90).

```
ctest --test-dir build-claude-review-cache-io -R "^(cache|io_scheduler|thumbnails|fs_watch|fs_watch_budget|fs_watch_ui)$" --output-on-failure -j1   (QT_QPA_PLATFORM=offscreen)
1/6 Test  #6: thumbnails .......  Passed    6.35 sec
2/6 Test #19: fs_watch .........  Passed   83.90 sec
3/6 Test #20: fs_watch_budget ..  Passed  154.12 sec
4/6 Test #21: fs_watch_ui ......  Passed    4.59 sec
5/6 Test #26: cache ............  Passed    5.00 sec
6/6 Test #27: io_scheduler .....  Passed    0.32 sec
100% tests passed, 0 tests failed out of 6; Total Test time 254.31 sec
```

Direct runs with QtTest logs (zero skips):
- `fs_watch_tests.exe` selected cases (admission identity/estimates/ownership/retirement, noncurrent same-tuple, restored-mtime, post-probe edit, atomic replacement, prefetch supersede/deadline, rapid navigation): `Totals: 18 passed, 0 failed, 0 skipped`.
- `cache_tests.exe`: `Totals: 10 passed, 0 failed, 0 skipped` (hit on revisit, bidirectional, invalidation on disk change, clear, cross-folder lookahead, cancellation on reversal and filter change).
- `io_scheduler_tests.exe`: `Totals: 12 passed, 0 failed, 0 skipped`.

The full `scripts/build.ps1 -Test` suite and Docker build were not run here (to avoid contention with other lanes); the FS-WATCH-01 integrated record (1bd4461: Windows 32/32, Linux Docker fresh 32/32) covers the integrated main commit before the later SAFE-MODE contract commits.

## Unverified

- Physical SSD/HDD/SMB behaviour: storage-type detection (`DeviceIoControl` seek penalty, `GetDriveTypeW`, Linux `statvfs` f_type) and rotational/network concurrency and prefetch gating were exercised only through `setPathOverride`/`setProfile` on a generated local drive. Linux rotational detection is not implemented (non-Network Linux paths default to SSD).
- Real low-memory behaviour of the 40%-of-available-RAM allowance (only the Windows branch computes it; Linux uses the fixed 1 GiB).
- Linux/Docker execution on this reviewed branch (not re-run; relies on the FS-WATCH-01 integrated record).
- Timing/latency claims (cache hit speed) are unmeasured by this review.
