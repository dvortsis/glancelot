# FS-WATCH-01 Claude-lane independent corrective review R4 (STATIC phase)

Candidate: 9177b15 (fixed). Reviewer: Claude Code, branch claude/review-fs-watch-01-r4,
own worktree. Reviewed range `b0593b8..9177b15` (src, tests, docs) plus the full resulting
`fs_monitor.hpp`, `fs_revision.hpp`, the observation/reconcile/open paths of
`viewer_controller.cpp`, `buildListing`/`apply`/`setDirectory` of `directory_entry_model.cpp`,
`cmake/FSWatchTests.cmake` and the frozen plan including the R3 policy/seam amendment.
The R3 report (ecb9d54, candidate b4cfd20) is preserved unchanged in git history; this file
supersedes it in the working tree. The author's R4 handoff is not approval.

Phase: static only. I did not configure, build, run, probe or use Docker, and touched no
process or window. No runtime result is claimed. `.cache/author-r4-targeted.txt` reads
`Totals: 10 passed, 0 failed, 0 skipped, 15682ms`. I treat that as a description of what the
author ran. It is a 10-case filtered subset, not the full `fs_watch` file, the Windows suite or
Docker.

UI: the diff contains no `ui/` file. The one-token `"refresh"` allow-list in `ui/Main.qml` came
from the earlier routing amendment (`0bb6207..9177b15` shows exactly that token). No UI behavior was
reached, so the native UI skills were not needed and no visual review is claimed.

## Disposition: NOT APPROVED (static phase). No blocking source defect found.

All four R3 findings (N8-N11) are fixed in source as the lead's R3 policy specifies, for the
cases they target. I found five new non-blocking findings (N12-N16). This is not full approval
because these gates are open and none is waived or counted:
1. Full current-source `scripts/build.ps1 -Test` on Windows, including the whole `fs_watch` file
   and `fs_watch_ui`. The author's attempt collided with another lane and was cancelled.
2. `docker build -t glancelot-browser:main .` (Linux runtime of the symlink/regular-file cases).
3. Independent runtime review on 9177b15 (B1/N2 probes, N3 timing, real junction/symlink,
   F5 in preview/grid/combined). Another lane owns the Windows run in this phase.
4. Serial integration under the lock and checks on the integrated commit.
Because of 1-3, the verdict on 9177b15 is "static review clean of blockers, runtime pending",
not APPROVED. Do not merge on this report alone.

## Synchronization and contracts (checked)

- `0bb6207` (root main) is not an ancestor of 9177b15. Its only delta from the common base is
  `docs/WORK-QUEUE.yaml` (one line, lock release). No product delta.
- `include/` is byte-identical to `0bb6207` (empty `git diff --stat`). No shared header changed.
- Against `0bb6207` the root `CMakeLists.txt` differs by one line, `include(cmake/FSWatchTests.cmake)`.
  `favorites_tests` and its `add_test` remain, so both test target sets are kept. Both `refresh`
  (`viewer_controller.cpp:111`, handler `:893`) and `add-to-favorites` (`:113`) commands are present.

## Macro, ODR and lifetime (GLANCELOT_FS_WATCH_TESTING)

- `rg` over the whole tree finds exactly one definition, `cmake/FSWatchTests.cmake:11`, `PRIVATE` on
  `fs_watch_tests`. No root, preset or script defines it. `fs_watch_ui_tests` compiles the same
  `viewer_controller.cpp` without it. Viewer, decoder and other feature binaries therefore do not
  contain `ControllerTestHooks`, the thread-local, the `afterEnumeration` parameter or the
  `deferReconciliation`/`pending` calls. (Static source reasoning; no binary inspection was done.)
- ODR: `fs_revision.hpp`/`fs_monitor.hpp` are included only by `viewer_controller.cpp`,
  `directory_entry_model.cpp` and `tests/fs_watch_tests.cpp`. The libraries `glancelot_core` and
  `glancelot_platform` do not include them. Inside `fs_watch_tests` every TU has the macro, so the
  extended `snapshot()` signature is consistent. In other executables no TU has it. `Private` is
  defined only inside `viewer_controller.cpp`, and the public header is unchanged, so the layout
  difference is not visible across TUs. No mixed-definition hazard found.
- Lifetime: hooks are copied by value at `Private` construction from a thread-local pointer.
  `HookScope` restores the previous pointer in its destructor, so it cannot leak across tests.
  Test lambdas capture `shared_ptr`s or locals declared before the `ViewerController`, which is
  destroyed first. `~Monitor` sets `closed` and clears `accept` under the mutex, so a late `pending`
  hook invocation after destruction is not reachable. In the late-baseline test the gate loop also
  exits on `stop_requested()`, and a `QScopeGuard` releases it, so a failing `QVERIFY` cannot hang
  the worker.
- The tests run the real `ViewerController`, `Monitor::Worker`, `reconcileObservation` and
  `DirectoryEntryModel`. Only the snapshot reader (`read`), native registration and the two
  deterministic gates are substituted. `afterEnumeration` runs inside the real `snapshot()`.
  I judge the tests exercise actual controller logic, not a copy.

## Findings on the stated items

### N8 - pending baseline preference: FIXED (static)
`viewer_controller.cpp:247-261` `earliestComplete` returns pending-before if complete, else the worker's
before if complete, else a complete pending-after, else the worker's before, separately for the
folder and the parent. An incomplete snapshot can no longer become the carried baseline: the final
fallback is the worker's value, which is null or complete. This closes the R3 S1-incomplete pin.
`controllerPendingPrefersCompleteWorkerBaseline` discriminates it. I traced it against the old
`pending->before ? pending->before : pending->after`: scan 2 would carry incomplete S1 as `before`,
and on scan 3 the old code keeps it, so `pending.last().before->complete` fails on the old code and
passes on the new one. Its reconcile deferral is a controller hook, so the real merge runs. Residual:
the worker's `renamedImage`/`renamedDirectory` are not re-derived across merges, but they are computed
from the unacknowledged `accepted` baseline, so they stay cumulative. No defect.

### N9 - automatic invalidation after the first incomplete observation: FIXED (static), see N14, N15
`:425` is now `if(!value.baseline||value.forced) emit filesInvalidated()`. The first scan of a scope
(the baseline, when the model has just listed) is skipped. Every later incomplete observation
invalidates, including a permanent one, at the existing backoff (`retryDelay` doubles from 2 s to
30 s while `coverage&&complete` is false). Incomplete still never proves rename or deletion: the
rename paths require `complete`, and the missing-folder deletion proof requires `before->complete`
and `parentAfter->complete`. `permanentlyIncompleteScopeStillRefreshesAutomatically` has no F5 and
uses `registerNative=false`, so the second periodic scan (about 4 s) is the only possible source,
inside a 10 s timeout. It discriminates the R3 behavior. It asserts the open image stays ready and
that a degraded error is shown.

### N10 - alias and image rebinding: FIXED (static), see N13, N16
`fs_monitor.hpp:130-155`. Registration identity and link-entry revision are stored only if the path
is in `watcher.files()+directories()` after the add (`:150-155`), which is the requested "after actual
add". Rebind triggers on a target identity change, or on a link-entry revision change when the entry
is a link. A directory rebind also drops the image watch, so an image below a retargeted alias is re-added.
A transient unknown identity can only cause a harmless remove and re-add inside the same scan. A
scope change clears all native watches first, so a stale stored identity cannot be used (it is
guarded by `registered.contains`). Handles stay at most three (directory, parent, image).
`retargetRearmsNativeAliasWatch` waits for B's identity and `coverage`, writes into B and requires
the event in under 1.5 s while polling is 2 s. CORRECTION (follow-up commit): this test is not proof of
alias-watch delivery; see N16.1. It can pass on the parent watch or on a late poll.

### N11 - ancestor alias vs real rename: FIXED (static), see N12
`buildListing:121-124` makes a directory rename require a non-empty native directory identity equal to the
prior entry identity AND that the probe of the former spelling no longer yields that identity. The probe
runs on the listing worker, not the GUI thread. Cases traced:
- `alias/sub` to `target/sub`: the former spelling still resolves to the same directory (ancestor
  link), so the identities are equal, `renamedDirectory` is false and `setDirectory` resets the
  selection. Test `ancestorAliasNavigationResetsSelection` passes the right way round.
- Real rename `A` to `B`, with the old name missing: the probe is unknown, so the identity differs
  and the rename is proven.
- Old name reused by a different object: the identity includes volume, object id and birth time,
  so it differs and the rename is still proven.
- Leaf link alias: the prior identity is empty for a link, so the rename is never proven (unchanged).
The controller path for a proven rename (parent identity match in the worker) is unchanged and is
covered by the existing rename tests. Full results depend on the runtime phase.

## New findings (all non-blocking)

### N12 (low) - N11 uses a no-follow probe on the former spelling
`directory_entry_model.cpp:124` compares `probe(prior.directory).identity()`, which is empty for a link
leaf. If someone renames `A` to `B` and then makes `A` a junction/symlink to `B`, the former spelling
still denotes the same object, yet the probe returns "different" and the model treats the move as a
rename, so the selection follows instead of resetting. The plan text says a rename requires "former
spelling no longer denotes that identity". Suggested fix: use
`directoryProbe(nativePath(prior.directory)).identity()`, which resolves leaf links. Also no test covers
the model selection path for old-name reuse by a different object; the existing reuse test
(`renamedDirectorySurvivesOldPathReuse`) covers the viewer's current image only, not grid selection.

### N13 (low, unverified) - residual rebind holes
(a) Registration happens after the scan snapshot. An A-B-A swap that straddles one scan and one add
leaves the stored identity equal to the later identity for an ancestor alias, which has no link entry to
compare. The next scan then sees no change although the handle may be on B. Bounded: the poll still
re-scans every 2 s while covered, so only native latency is lost, never membership.
(b) `identity()` needs a birth time. On a Linux filesystem without `STATX_BTIME`, `born` is 0 and identity is
empty, so identity-based rebinding and rename proof are inert (link-entry rebinding still works for a
leaf symlink). (c) On a coarse-timestamp filesystem a swap with inode reuse inside one tick can leave the
link entry equal. Physical/coarse-timestamp/network variants are unmeasured, as the plan states.

### N14 (low) - merged `baseline` can suppress one invalidation
`baseline` is OR-merged in the controller's pending coalescing and cleared only after reconcile.
If a first incomplete baseline observation and a later incomplete one coalesce while navigation is
busy, the merged observation is treated as the baseline and does not invalidate. The next observation,
within the 30 s backoff cap, does. This is bounded and conservative. It matters only for the claim "after
the first observation"; a deferred-reconcile test with two incomplete observations would pin it.

### N15 (note, policy cost, unmeasured) - permanent relisting
Under the R3 policy a folder that can never snapshot completely (over 20,000 entries or the 16 MiB identity
budget) is relisted by the model on every observation, every 4-30 s when idle and on each native event burst
when active, up to the model's 100k limit. Each relist is a worker job, with at most the model's concurrency
plus one pending request, and a merge or reset on the GUI thread. N3 measured one 20k relist at about
0.5-0.6 s. Nobody measured a 100k folder, a sustained writer or GUI merge/reset cost with this policy. This is
the accepted policy and is not claimed as a large-directory solution. It needs a runtime check against the
250 ms GUI oracle with a sustained external writer before integration.

### N16 (low, test quality)
1. `retargetRearmsNativeAliasWatch` can false-pass and false-fail. (a) `A`, `B` and `alias` are all children
   of `root`, which is the watched parent and is never rebound. Writing into `B` changes B's last-write time,
   and if Qt's Windows directory watch includes a last-write filter that fires `root`, the next scan reads B's
   membership through the alias and the test passes on parent delivery. I could not check the flags: only the
   private header `qfilesystemwatcher_win_p.h` is under `.tools/Qt`, not the `.cpp`, so this is open, not
   asserted either way. (b) The "poll cannot land in the window" argument needs under 500 ms between the last
   `lastProbe` and the write. Publication queueing plus QTRY's stepping under load can exceed that, and the
   2 s poll then lands inside the 1.5 s window. (c) The same load can false-fail the 1.5 s bound (the other
   lane's CTest collided with the author's run). Proposed fix: place `A`, `B` and the alias parent in separate
   directories so a write into `B` cannot touch the watched parent, and assert the event arrived with no poll
   (for example by stopping periodic polling via the hook or checking `stats().snapshots`). Until then, treat
   the native alias-rebind evidence as unproven.
2. The test covers a bare directory alias without an image. The image-below-alias rebind branch
   (`:140-143`) and the parent rebind branch (`:144`) have no test.
3. `controllerLateCoalescedBaselineDecodesOnce` depends on real timers: the second scan needs the 2 s
   retry, then a fixed 100 ms sleep and a 2.3 s wait. Its trace-count assertions (2 and 3) are exact but
   fragile if the retry interval changes. It uses `GLANCELOT_TRACE_FILE` and restores it with a guard.
4. `emptyFolderAndRenamedDirectoryRecover` and `renamedDirectorySurvivesOldPathReuse` still have an `else`
   branch that passes when the filesystem reports no identity. That is not a skip, but it proves nothing.
5. Pre-existing: a skipped or `QWARN` identity test is not a pass.

## N3 local metadata cost - assessment of stated limits

`docs/verification/fs-watch-01/local-cost.json` matches the quoted ranges exactly: bare listing
64.74-81.91 ms, model listing 496.64-587.06 ms, watch snapshot 379.53-442.54 ms, three alternating rounds
of 20,000 one-byte local files, `setup_ms` 6459. The file's own `timing_scope` is "worker construction and
destruction; no GUI publication or decoder", and `cache_state` is "uncontrolled".
- Stated limits are honest. Overhead is about 5-6x the bare listing for a snapshot and 6-9x for the model.
  The snapshot adds one open-and-query per entry, as predicted in R3.
- Limits I would add: n=3 per method with no order randomization (the round-0 model sample, the slowest at
  587 ms, is also the first run); generated 1-byte files are warm after creation; one volume only; the parent
  was not a second 20k folder; a monitor scan probes the folder and its parent every 2 s while healthy, which is
  about 0.4 s of worker I/O per 2 s on this machine (a high steady background load for a 20k folder, never
  compared against the 250 ms GUI gate); nothing was measured past 20k, on a network share, ReFS, OneDrive or
  a spinning disk. It was measured on b4cfd20. The enumeration and probe code in 9177b15 is unchanged apart
  from the test-only parameter, plus one extra `probe` in `buildListing` per directory change, so the numbers
  carry over for the snapshot but not as a re-measurement of this candidate.
- Conclusion: the cost is material and measured locally under those limits. It does not establish acceptable
  first-paint, steady-state CPU or network performance. The current-source and independent runtime
  measurement stays required, and no timeout, 250 ms oracle or original-media rule is changed by the diff.

## Checked, no defect found
- Original media: the diff adds no write. `probe` uses read-attributes on Windows and `lstat`/`statx` on
  POSIX. The new tests write only generated files in temporary folders.
- Off-GUI work: the only new filesystem calls are in the monitor worker and the listing worker. The controller
  hooks run no filesystem call.
- Generation safety: the new code adds no result path that bypasses `value.scope!=monitor->generation()` and the
  monotonic `observationRevision` guard. The late-baseline path re-checks `path` and the `generation`.
- Bounds: still three watch handles, 20,000 entries and 16 MiB identity cost per snapshot, one publication
  slot and one controller pending observation.
- The 250 ms GUI oracle, the 15 s decode deadline and decoder behavior are untouched.

## Remaining gates (none waived, none counted as pass)
1. Full current-source `scripts/build.ps1 -Test` on Windows, with `fs_watch` and `fs_watch_ui` in the list,
   zero skips and no overlapping lane's CTest. Another lane owns it in this phase.
2. `docker build -t glancelot-browser:main .`, which also runs the POSIX symlink and regular-file-target cases.
3. Independent runtime review at 9177b15: N3 re-measurement including 100k and a sustained writer (N15), real
   junction and symlink behavior, ancestor-alias reset, image-below-alias rebind (N16), F5 in preview, grid and
   combined modes. Runtime must be released explicitly; this phase did not run anything.
4. Author decision on N12 (one-line change to `directoryProbe`, plus a model test for old-name reuse) and
   acknowledgement of N13-N16, or a lead waiver recorded in the queue.
5. Serial integration under the lock and checks on the integrated commit.
6. Physical CANVAS/DECODE/color/network hardware gates stay unverified, as does coarse-timestamp, network,
   reparse, ReFS and cloud-placeholder behavior.

## Limitations
- Static reading only. Statements that a test passes or fails come from tracing source, not a run. Macro absence
  from product binaries is inferred from build definitions, not from binary inspection.
- The author's logs are accepted only as descriptions of what was run.
