# FS-WATCH-01 Claude R13 independent review

Reviewer: actual Claude (claude/review-fs-watch-r13). Candidate: `116dac30e4bcc32bdb0ce86b0617be6a509027cd`.
Scope: source review only. No binaries, tests, Docker, launchers or old fixture were run or restored. Native hardware remains unverified.

## Methods actually used
- `git rev-parse HEAD`, `git diff --stat 58fa807..116dac3`, `git diff 58fa807..116dac3 -- . ':!docs'`.
- Read `docs/verification/fs-watch-01/R12-L1-CORRECTION.md`, `src/app/io_job_lease.hpp` (`retireIoProcess`) and `src/app/thumbnail_service.cpp` (`retire`, `stopWorkers`, `retiring`, destructor).

## Diff scope
`58fa807..116dac3` touches exactly two files: `src/app/thumbnail_service.cpp` (+5/-1, one callback) and `R12-L1-CORRECTION.md` (new). No header, lease, CMake, queue, UI or test changes. `io_job_lease.hpp` is unchanged, so the lease/admission contracts reviewed in R12 are unchanged.

## Analysis
- **Lifetime/ordering:** `dispose()` runs `lease.reset(); process->deleteLater();` and only then queues `released` with `QTimer::singleShot(0, guard, ...)`. DeferredDelete events are processed before the zero timer, so the QPointer is normally already null when the callback runs. The old `removeAll(rawProcess)` compared against a raw address and left that null entry (R12 L1). The new predicate `!entry || entry==retired` removes the null entry and any entry matching the live weak pointer, so L1 is fixed for both orderings.
- **Address reuse:** The capture is now a QPointer, so a new QProcess allocated at a freed address cannot be mistaken for the retired one. If `retired` is null, `entry==retired` matches only null entries, so a live entry is never removed wrongly. Pruning other already-null entries is benign, because null entries have no process left to wait on in `stopWorkers`.
- **Owner closure:** `this` is captured, but delivery is guarded by `QPointer<QObject>(owner=q)` in the singleShot context. `Private` is reached through q, and `~ThumbnailService` calls `shutdown()` first, so a callback cannot run on a destroyed Private. The `!process` early path and the `wait && waitForFinished` path never touch `retiring`, so they are unaffected.
- **Shutdown interaction:** `stopWorkers(wait)` copies and clears `retiring`, skips null entries, and deletes the rest. Deleting a process destroys its connected `dispose` lambdas and releases the Retirement lease through shared_ptr, so no admission leaks. `released` simply never fires then, which is acceptable at shutdown.
- **Admission invariants:** The lease is released only inside `dispose()`, which requires NotRunning (finished, stateChanged or errorOccurred), unchanged. The callback only prunes bookkeeping and calls `schedulePump()`. It does not release or take admissions.
- **Build compatibility:** `QList::removeIf` needs Qt 6.1 or later; the project uses 6.8.3. `<QPointer>` is already included.

## Findings
None blocking. 
- **Info (non-blocking):** No regression test covers the null-entry pruning. The correction doc states this openly, and it is an accepted gap. The existing cancellation, failure, shutdown and FS lease tests must still pass on the combined candidate.

## Disposition
- **L1: closed in source.** No source-level regressions or blockers found in `116dac3`.
- **Source disposition: ACCEPT** for the narrow R12-L1 fix, subject to these still-pending gates: full combined FS Windows (32) and Linux (32) runs after the lead lock is released and the integrated fixture gate is complete, plus Docker. The earlier Defender-failed 31/32 run was not a pass. Earlier backend 81, budget 3 and UI 3 passes relate to `1b6c031` and are not transferred to this source.
- Full-scope original FS + Cache/IO closure remains pending. Native hardware stays unverified. I executed no tests.
- No integration, lock release, queue or config edits were made.
