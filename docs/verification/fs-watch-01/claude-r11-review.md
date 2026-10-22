# FS-WATCH-01 / Cache / IO independent review R11 (Claude lane)

Reviewer: Claude Code lane (not the implementing Codex lane), static review only.
Candidate: `0cab319f0f7de2c9ebd0ca8856ad38669d8a3265` (`codex/fs-watch-01`), reviewed in
`claude/review-fs-watch-01-r11`. Baseline: main `a652799` (merge-base). Current `main` is
`6cc87ee`, which differs from `a652799` only in `docs/WORK-QUEUE.yaml`, so the integration
merge should be trivial.

Earlier opinions (R9 accepted FS-only `825f7f2`; R10 `ec649b4` rejected `68c3731`) were not
treated as approval of this head. The diff against `a652799` was reviewed afresh.

## Verdict

**Source acceptable subject to uncompleted gates, with one narrow followup recommended (F1).**
Static reading found no high-severity defect and no confirmed regression. This is not a
test pass and not integration approval. The full Windows `scripts/build.ps1 -Test`, the Docker
build and the integrated-commit checks have not been seen by me. Do not treat them as passed
until their logs exist for this exact head.

## What I executed

Only read-only commands, all in this review worktree:

- `git rev-parse HEAD`
- `git diff --stat`, `git diff --name-only`, `git diff a652799 HEAD -- <paths>`
- `git log --oneline a652799..HEAD`, `git merge-base HEAD main`
- `Select-String` and file reads over source, docs and evidence logs.

I did **not** build, run CTest or fs_watch_tests, run Docker, or touch the implementation
worktree, the root `.cache` or any Explorer data. Every pass/fail count below is quoted from
the committed evidence logs and is **not** independently reproduced by me.

## Scope reviewed (complete reads unless noted)

- `src/app/fs_revision.hpp`, `fs_source_token.hpp`, `fs_monitor.hpp`, `io_job_lease.hpp`
- `src/app/io_scheduler.cpp` and `thumbnail_service.cpp` diffs
- `src/worker/main.cpp` diff
- `src/app/directory_entry_model.cpp` diff
- `src/app/viewer_controller.cpp` full diff: decode, revision-only probe, prefetch, lookahead,
  metadata, mutation adapter, reconcile, open/cancel/teardown
- `ui/Main.qml` diff
- `CMakeLists.txt` and the modified existing tests
- `tests/fs_watch_tests.cpp`: test inventory and the IPC and adapter cases. I did not read all
  1283 lines.
- `docs/FS-WATCH-01-PLAN.md` including the private amendments, and the tail of
  `docs/verification/FS-WATCH-01.md`
- Evidence logs `windows-current-main-native-adapters.txt`, `windows-current-main-adapters.txt`
  and `windows-ipc-focused.txt`. I did not open the remaining logs.

I read no UI skill documents. The product QML delta is a single allow-list token, and I did not
review UI behaviour.

## Checks that held up under static review

- **Product QML:** the only change is `"refresh"` added to `gridAllowsCommand` (`ui/Main.qml:330`).
  The non-test file list is limited to the expected private sources, one `CMakeLists.txt`
  include of `cmake/FSWatchTests.cmake`, and tests. No public header was touched.
- **Source tokens (`fs_source_token.hpp`):**
  - The token covers volume, object, birth, size, mtime, ctime and link count.
  - It is empty unless the file is regular, unambiguous and has a nonzero ctime.
  - `validSourceToken` is strict: `r1:` plus 64 lowercase hex characters.
  - Controller `responseSourceToken` rejects a missing, non-string or malformed token and treats
    an empty token as a miss.
  - Foreground and prefetch admission both refuse to cache an empty token.
  - A revision-only reply that is empty or mismatched falls back to `decode(..., fresh=true)`.
- **Decoder source validation (`src/worker/main.cpp`):** the worker probes before decoding and
  again after the pixel copy, and fails on any change. The RAW and normal paths are symmetric.
  The lock-free transfer and ack rule is preserved and comment-documented.
- **Worker leases (`io_job_lease.hpp`, `retireIoProcess`):**
  - The lease is a RAII `shared_ptr` held until the retired process emits `finished`.
  - The release connection belongs to the process, not the UI owner.
  - Teardown is safe in `~Private`: `monitor.reset()`, then `cancel()`, with queued
    notifications dropped when `q` dies.
  - For the thumbnail service, `breakWorker`'s copied `Job` shares the lease with the retire path
    and is released at function exit, which is correct.
  - The `stopWorkers` leftover path keeps the process-owned release connection.
- **Scheduler (`io_scheduler.cpp`):**
  - It rejects zero and duplicate IDs, negative estimates and overflow.
  - The admission-available wakeup is emitted outside the mutex and coalesced with an atomic
    flag. I found no lost-wakeup window.
  - The thumbnail pump is coalesced through `pumpQueued`.
- **Monitor lifetime:**
  - Worker-side state is a `shared_ptr<State>`. `closed` and `receiver` are accessed under the
    mutex, and `~Monitor` sets `closed` before the 250 ms bounded wait.
  - A worker abandoned in a blocked native call cannot reach a destroyed controller, because
    `invokeMethod` is issued only while `!closed` under the lock.
- **Generation guards:**
  - `value.scope != monitor->generation()` and a monotonic `revision` filter stale observations.
  - `decode`, `startPrefetch` and the lookahead callbacks recheck `token`, `process` and
    `generation` after admission.
  - Lookahead discovery is one running plus one latest pending request.
  - Metadata parsing is one active plus one latest pending job, value-owned, with a
    selection-generation check on completion.
- **Model:** identity matching and pruning run on the listing worker. Truncated or inaccessible
  snapshots neither prune selection nor replace the last complete baseline. The listing is moved
  out of the future instead of copied.
- **Existing-test edits:** `directory_model_tests.cpp` and `grid_ui_tests.cpp` change only the
  selection-after-rename expectations. The new behaviour is that a proven renamed identity stays
  selected, as required by plan item 6.

## Findings

### F1 (Medium, PLAUSIBLE): a worker that never starts leaks its lease and QProcess forever

- **Locations:** `src/app/io_job_lease.hpp:22-33`. Callers are `viewer_controller.cpp:555`
  (`cancel`), `cancelLookahead` and `thumbnail_service.cpp:89` (`retire`).
- **Cause:** `retireIoProcess` releases the reservation only from `QProcess::finished`.
  - If the process is in `Starting` when it is retired (a cancel that races an asynchronous
    start), `kill()` is called and only `finished` is connected.
  - If that start then fails (`FailedToStart`: missing or unexecutable decoder, or a
    `QProcess::start` failure), Qt emits `errorOccurred` only. `finished` is not emitted.
  - `retireIoProcess` has already disconnected the owner's `errorOccurred` handler.
- **Consequence:**
  - The `IoJobLease` and the parentless `QProcess` are never released.
  - A leaked Foreground lease keeps `activeForeground_ > 0`, which makes every later
    `Prefetch` admission fail (`io_scheduler.cpp` `tryAdmit`).
  - A leaked Thumbnail lease permanently consumes one of the 1-2 background slots and its bytes.
  - Before this change the failure mode was a leaked QProcess object. The scheduler leak is new
    and comes from the lease design.
- **Reachability:** a packaging error (decoder missing) combined with rapid navigation on Linux,
  where Qt start-up is asynchronous. On Windows `CreateProcess` fails synchronously, so the state
  is already `NotRunning`.
- **Test gap:** every IPC or timeout test uses a running fixture (`held->state()==Running`).
  None exercises a worker retired while Starting or one that fails to start.
- **Followup (narrow, FS/IO lease helper only):** in `retireIoProcess`, also connect
  `errorOccurred`. When the state is `NotRunning`, run `dispose` and `released`. Make dispose
  idempotent. Add a test that retires a deliberately unstartable program while Starting and
  asserts `activeJobCount()==0`.
- **Not executed:** I could not run this. It is a source inference about Qt behaviour.

### F2 (Low-Medium, evidence gap, not a defect): the final source has no complete `fs_watch_tests` run

- **Evidence on file:**
  - `windows-ipc-focused.txt`: 18/18, 13.492 s, on `6af2858`, before the merge of main.
  - `windows-current-main-adapters.txt`: 20/20, 15.810 s, before the final reentrancy guards
    (the `gen!=generation` early-outs in `loadMetadata`/`decode`, and the lease/`process!=worker`
    rechecks).
  - `windows-current-main-native-adapters.txt`: 6/6, 2.567 s, on the final source, but it covers
    only RAW unchanged/replaced plus the two adapter tests.
- **Gap:** I see no full `fs_watch_tests` run (about 71 cases) and no Linux run on
  `0cab319`/`deef3fb`.
- **Closure:** the externally running Windows gate and the Docker gate must include the full
  `fs_watch_tests` and `fs_watch_ui_tests` targets with zero skips.
- **Preserved fixture failures:** the labelled fixture failures in `fs-watch-01/*-failure.txt`
  are properly kept. They are not counted as passes.

### F3 (Low): cache reuse and rename following depend on a nonzero birth time and a single hard link

- **Locations:** `fs_revision.hpp:59-62` (`identity()`), `fs_source_token.hpp:9`,
  `fs_revision.hpp:95-98` (Linux `statx` birth time).
- **Effect:**
  - Where `STATX_BTIME` is unavailable (some network, FUSE, overlay or older filesystems),
    `born==0` makes `identity()` and `sourceToken()` empty.
  - Every navigation then decodes fresh (no cache hit), and rename identity following is disabled.
  - Hard-linked files behave the same way.
  - The behaviour is conservative and safe, but "genuine hits for unchanged sources" is then not
    delivered on those filesystems.
- **Test coverage:** the modified model and grid tests guard the new rename expectation with
  `QFileInfo::birthTime().isValid()`.
  - On such filesystems the old expectation silently applies.
  - The conditional can mask a regression in environments where birth time exists but is
    misreported.
- **Evidence gap:** physical, network, ReFS and non-btime behaviour is unverified, as the evidence
  doc already says.

### F4 (Low): bounded but multiplied identity memory

- **Locations:** `fs_revision.hpp:114` (16 MiB per snapshot), `fs_monitor.hpp` State and
  Observation, and the model's `identities`.
- **Detail:**
  - The budget is per snapshot.
  - One window can retain, at the same time:
    - the monitor's `accepted` and `acceptedParent`;
    - one coalesced publication, which carries `before`, `after`, `parentBefore` and
      `parentAfter`;
    - the controller's `pendingObservation`;
    - the model's `identities`;
    - the by-value `prior` copy inside an in-flight listing job.
  - Several of these share the same underlying hash through `shared_ptr` or Qt COW, but the
    distinct-snapshot worst case is a multiple of 16 MiB per window. A few tens of MiB would
    typically be shared; more than 100 MiB is possible.
- **Next step:** the plan asks for "bounded revision/identity storage". It is bounded per
  snapshot, and the cap should be stated as an aggregate in the evidence. This is not a
  correctness bug.

### F5 (Low, known and acknowledged): periodic poll cost

- **Evidence:** with 20k entries in the scope and 20k in the parent, a poll cycle measured
  1.1-1.6 s (`local-cost-scope-and-parent.json`).
- **Mechanism:** with polling every 2 s after completion (`fs_monitor.hpp:120,174`), that is
  sustained background metadata I/O for large folders while a window is open. Each scan probes
  every entry.
- **Status:**
  - This is design-driven: the plan requires a bounded fallback because Qt cannot report every
    overflow.
  - The evidence doc already says it is not proof of power or network acceptability.
  - Network and HDD behaviour is unverified.
- **Followup:** consider a slower steady-state period when coverage is complete. That is a policy
  choice for the lead, not an FS defect.

### F6 (Low, behaviour change, intended): the mutation adapter reopens instead of patching state

- **Location:** `viewer_controller.cpp:1740-1763` (`reconcileRenamedPath`).
- **Behaviour:** a rename of the current image or folder now goes through
  `open()`/`navigateDirectory()`, so the image is re-decoded. The previous code patched
  `d->path` in place.
- **Test coverage:** `mutationAdapterRearmsMonitorAndPreservesNewestOpen` asserts the new path,
  a re-decode that sees changed pixels, and that a later `open(c)` wins. This matches the
  plan's amendment "route rename reconciliation through existing asynchronous controller
  navigation". The re-decode or flash is a UX trade-off worth a lead decision.
- **Root handling:** `open()` re-applies `oldRoot` through `setRoot`, which requires the root to
  be a valid directory containing the new location. If the renamed object was the traversal root
  or an ancestor of it, the stale root path is rejected and the root narrows to the new parent.
  It is not widened. This matches pre-existing `Navigator::open` behaviour, which also reset the
  root to the parent. I found no regression, but the "follow the same renamed root object" claim
  covers only renames the watcher can prove.

## Inherited main issues vs FS regressions

I found no inherited main defect affecting qualification in the code I read. The F1 mechanism
(finished-only release) is in the FS/IO lease helper, which is FS-lane code. The followup
belongs to the FS packet or the IO owner for `io_job_lease.hpp` only. Nothing needs a separate
main fix. The Cache and IO owners' original suites cannot be assessed from this static review.

## Plan conformance checked

- Three native handles at most: directory, parent and current image.
- Registration, snapshots and probes run on the monitor worker, not the GUI thread.
- Hot-file rule: the only `ui/Main.qml` edit is the allow-list entry.
- New tests are in `tests/fs_watch_tests.cpp` and `tests/fs_watch_ui_tests.cpp`, not
  `grid_ui_tests.cpp`. The `grid_ui_tests.cpp` edit modifies one existing expectation.
- The testing hooks are guarded by `GLANCELOT_FS_WATCH_TESTING`.
- Production deadlines remain 15 s foreground and prefetch, with a 100 ms baseline fallback.
- I did not verify that the production `glancelot-viewer` binary excludes the hooks. That depends
  on the CMake wiring in `cmake/FSWatchTests.cmake`, which I did not read.

## Required before done (not claimed here)

1. Full Windows `scripts/build.ps1 -Test` on `0cab319` with fs_watch and fs_watch_ui, zero skips.
2. `docker build -t glancelot-browser:main .` and the full Linux test list.
3. Integration under the lock, merging `main` (currently `6cc87ee`), then integrated-commit
   checks. Task state moves to done only after those.
4. A decision on F1: fix before merge, or record it as a tracked followup.
5. Physical hardware, network share and CANVAS/DECODE gates remain unverified.
