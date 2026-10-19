# FS-WATCH-01 Claude-lane independent corrective review (STATIC phase)

Candidate: e4c3d3d (fixed, detached). Reviewer: Claude Code, branch
claude/review-fs-watch-01-r2, a new worktree. Prior candidate 5b658a4 and its
review worktree were not accessed or modified.
Phase: static only. Nothing was configured, built, run or probed (the root owns the
fixture/Docker gate and the native slot). Every claim below comes from reading
`git diff main...HEAD` and `git diff 0dafc6c..HEAD`. No runtime evidence exists in this
report. The author's red/green logs and counts were not re-examined and are not
treated as approval.

This report supersedes the 0dafc6c report (candidate 5b658a4). That report's B1, N1,
N2 and N3 are re-assessed in the disposition table below.

## Disposition: CHANGES REQUESTED (static), no new blocking defect

The original B1, N1 and N2 are fixed in the source by my reading. I found no new
blocking defect, but four new non-blocking findings (N4-N7) need author action or an
explicit lead waiver. Full approval is withheld because:
- No native build, CTest or Docker run exists for e4c3d3d. The old Docker 20/20
  predates the product changes, and the new linked-scope test (ac40da2) has never run.
- N3 (20k-entry I/O cost) is unmeasured.
- The `fs_watch` and `fs_watch_ui` gates must be re-run in full after these fixes.

## Prior findings, re-assessed

| Id | Disposition | Basis |
|----|-------------|-------|
| B1 first baseline lost while controller busy | Fixed (static) | `fs_monitor.hpp:127-134` seeds `accepted*` on the first worker scan of a manual-baseline scope. `before` stays null for that first observation. `acceptedScope==scope` holds from then on, so later scans get `before` = the first snapshot. `renamedImage`/`renamedDirectory` are computed against it (`:141-144`). The controller back-fill (`viewer_controller.cpp:~236`) is consistent, since `pending->before ?: pending->after` is the same snapshot. `acknowledge()` still advances only on reconciliation, and a stale ack is rejected by `value.scope!=scope`. The worker `imageBefore` is the seeded image, which is also correct. A new scope always has a new number, so there is no A-B-A reuse of `acceptedScope`. |
| N1 linked scope permanently incomplete | Fixed (static) with caveats N5-N7 | `directoryProbe` (`fs_revision.hpp:93`) is worker-only. `snapshot`, the model listing and coverage now use the resolved target. Ordinary entries still use no-follow `probe`, so links never get an identity. Alias spelling is kept in rows, callbacks and watch paths. Handles are still at most 3. No GUI-thread I/O was added: `observe()` uses lexical `absolutePath`. |
| N2 incomplete identity map replaced last complete | Fixed in the model (static) | `directory_entry_model.cpp` `apply()` assigns `identities` only when `complete`, and clears it only if the stored directory differs from the current one. Not fixed in the monitor baseline: see N4. |
| N3 steady-state I/O | Open | Needs the runtime measurement. Alias scopes add one `canonical()` plus two `lstat` per probe, which is negligible. |

## Findings

### N4 (non-blocking, should fix before approval) - `!value.before` no longer identifies the first observation, so the late-baseline re-decode can be skipped
- `viewer_controller.cpp` observation callback uses `!value.before` as "first
  observation of this scope". That drives `decodeAwaitingBaseline` (start decode) and
  `decodeStartedWithoutBaseline` (re-decode after a late baseline, which closes the
  open-to-watch race).
- Before 0cc49bf, `before` stayed null until the controller acknowledged, so every
  pre-ack observation matched. Now the worker seeds `accepted` on its first scan
  (`fs_monitor.hpp:127`). If a second scan finishes before the GUI thread consumes the
  first publication, the single coalesced publication slot (`:150-154`) holds
  observation 2 with `before`=snapshot 1, so `!value.before` is false.
- Trigger: open an image, press F5 within a few ms (`forced` bypasses debounce at
  `:88`), with the GUI thread busy for more than one scan. Observation 1 is
  overwritten. The 100 ms timer starts a decode with `decodeStartedWithoutBaseline=true`,
  and observation 2 never re-decodes. A write to the file between decode start and
  scan 1 is then never compared. Only later changes are detected. A stale image can
  stay on screen.
- Likelihood is low, but the fix changed the meaning of a gating field and no test
  covers it. `firstUnacknowledgedBaselineRetainsRenameIdentity` runs a bare Monitor
  with no controller and no coalescing.
- Proposed fix (private `Observation` only, no shared header): a worker-set
  `baseline` flag on the first publication of a scope, OR-merged when the coalescing
  code (`:150-153`) replaces the slot, used by the controller instead of `!before`.
  Regression: delay the receiver event loop, publish two scans, assert one
  post-baseline decode.

### N5 (non-blocking) - model treats alias to target navigation as a directory rename
- `directory_entry_model.cpp` `buildListing`: `directoryIdentity` is taken from
  `result.identities.revision`, which is now the resolved target's revision. For alias
  L to A, and for L1 to L2 on one target, identity equals `prior.revision.identity()`
  while the keys differ, so `renamedDirectory` is true. `apply()` then restores
  `previousSelection`, anchor and cursor, and maps paths through `renamed`.
- Ordinary navigation is meant to start with a new selection, and the lead
  clarification says links cannot establish an alias-directory rename. The worker side
  obeys this because parent entries are no-follow probed. The model uses the
  target identity.
- Proposed fix: derive directory-rename identity from `directoryEntryRevision` (the
  no-follow entry revision) and require that it is not a link. Keep the target
  revision for the membership-stability check only. Regression: select a file in L,
  navigate to the real target spelling, assert the selection is cleared.

### N6 (non-blocking) - monitor baseline can be replaced by incomplete snapshots (N2 parity)
- `Monitor::acknowledge` (`:214`) and the non-manual accept path (`:166`) assign
  `after` regardless of `after->complete`. The controller acknowledges before
  inspecting completeness (`reconcileObservation`).
- Trigger: complete, then an incomplete snapshot (a directory revision change during a
  large copy), then a rename of the current image, then a complete snapshot.
  `renames()` requires both snapshots complete (`:145`), so `renamedImage` is empty.
  The path is absent from `current.entries`, so `reconcileRemovedPaths({path})` steps
  to a neighbour. This is conservative, but it is the same loss the model fix removes.
- The first seeded baseline has the same weakness: an incomplete first scan stays the
  baseline for all later pre-ack scans.
- Proposed fix: advance `accepted*` only to a complete `after`, or keep a separate
  last-complete pair for rename matching. Add a monitor test for
  complete, incomplete, rename, complete.

### N7 (non-blocking) - linked-scope test does not prove the N1 symptom is gone
- `linkedScopeMonitorsTargetAndPreservesRenameSelection` checks `snapshot().complete`
  and `after->complete`, but never `values.last().coverage`. Monitor error state,
  retry backoff and repeated `filesInvalidated` were the observable N1 symptoms.
- The rename and selection assertions run only `if(!identity.isEmpty())`, so a
  filesystem without identities passes silently.
- Not covered: alias retarget and A-B-A mid-enumeration (the `stableAlias` and
  `finalRevision` guards), a dangling link, a link to a regular file, parent or alias
  deletion, and a controller-level case with an alias scope.
- Windows: `probe` sets `link` for any reparse point. A scope directory that is a
  cloud placeholder or a non-junction reparse still fails `readable`
  (`fs_revision.hpp:121`) and is permanently incomplete. Only junctions are tested.
- Proposed tests: assert `coverage` and `maxHandles<=3`; add dangling and file-target
  links; retarget the alias during an injected `read` and assert `complete==false`;
  record Windows junction and POSIX symlink results separately in the evidence.

## Checked statically, no defect found
- Alias/target ABA: `snapshot` compares the link entry (`sourceRevision`) and the
  target directory revision across enumeration (`:135-137`). A directory swapped for a
  link, or the reverse, fails one of the two comparisons. Retargeting changes the
  link's creation or change time, and an A-B-A swap is caught unless it changes
  nothing observable. Unresolvable or non-directory targets give unknown or
  non-directory, which is incomplete.
- Watch handles stay at most 3: directory (alias spelling), lexical parent, current
  image (`:99-110`). Registration, `canonical` and probes run only on the worker.
- Parent for an alias is the lexical parent via `cleanPath`, so an alias rename is
  excluded by the link identity refusal, and a missing target gives the existing
  "missing" error path.
- Generation guards: `value.scope!=monitor->generation()` and monotonic revision. A
  stale acknowledgement is rejected. Pending observations merge only within the same
  scope. `open()` resets pending and restarts the scope. F5 goes
  `observe(); monitor->refresh()` with `forced` merged through coalescing.
- Bounds: 20,000 entries and 16 MiB per snapshot, with the model sharing the byte
  cap. Peak retention is several snapshots (accepted, parent, publication, pending,
  worker current), so roughly 100 MiB worst case by my estimate, unmeasured.
- Teardown and close behaviour are unchanged from the earlier review.
- QML: the only product QML edit is the `"refresh"` token in `gridAllowsCommand`
  (`ui/Main.qml:324`). No visual, layout, token or branding change, and the existing
  dispatcher's text/modal precedence is reused. `fs_watch_ui_tests.cpp` runs real F5
  in preview, grid and combined, with a `TextInput` and the settings modal as negative
  cases. Weaknesses: negatives use a fixed 250 ms wait, the folder is empty, and the
  modal case is not repeated in grid mode. The ui-design-skills were not available in
  this environment, so the design-skill review is limited to this diff reading.
- Original media: probes open read-attributes only. Linked-scope work adds no writes.

## Limitations
- No build, CTest, Docker, UI or filesystem probe was run. Everything above is
  unverified at runtime, including the new linked-scope test, the junction `mklink`
  quoting in `QProcess`, and the B1 regression logs.
- Physical, network-share, ReFS, OneDrive and cloud-placeholder behaviour is untested.
- The author's narrow passes and old Docker 20/20 are not accepted as approval.
  A full native `scripts/build.ps1 -Test` and Docker pass on the final source remain
  required, with `fs_watch` and `fs_watch_ui` in the list.
- The runtime phase will follow when the root releases the slot. It should include the
  N3 timing of a 20k-entry folder, with and without the probes.
