# FS-WATCH-01 Claude-lane independent review (STATIC phase)

Candidate: 5b658a4 (fixed). Reviewer: Claude Code, branch claude/review-fs-watch-01.
Phase: static only. Nothing was configured, built, run or probed (root owns a live
Windows gate). Runtime phase pending an explicit slot release.

## Disposition: CHANGES REQUESTED (static)

Not approved. One blocking and three non-blocking findings below. No runtime evidence
exists in this report; every claim is from source reading of `git diff main...HEAD`.

## Findings

### B1 (blocking) - first unacknowledged baseline loses rename identity when the controller is busy
- `src/app/fs_monitor.hpp:125-137`: the worker supplies `before` and computes
  `renamedImage`/`renamedDirectory` only when `state->acceptedScope==scope.scope`.
  With `manualBaseline=true` (controller), `accepted*` is set only by `acknowledge()`
  from `reconcileObservation()` (`src/app/viewer_controller.cpp:~246`).
- `open()` starts a scan/navigation job, so `navigationBusy` is normally true when the
  first (baseline) observation arrives. It is held in `pendingObservation` and not
  acknowledged. The next worker observation again has `before==nullptr`, so no rename
  is computed. The controller then back-fills `value.before` from
  `pendingObservation->after` (`viewer_controller.cpp:236`), but `renamedImage` and
  `renamedDirectory` stay empty.
- Trigger: open image X; before navigation completes (large or slow folder) rename X to
  Y (or rename its folder). After navigation idles, `current.entries` lacks X, so
  `reconcileRemovedPaths({X})` runs. This treats a rename as a deletion and steps to a
  neighbour, contradicting "preserves the earliest unprocessed identity while
  navigation is busy".
- Test gap: `deferredBaselineRetainsOriginalRenameIdentity`
  (`tests/fs_watch_tests.cpp:60-82`) acknowledges the first baseline (line 70) before
  any rename, so it never exercises this path.
- Proposed fix (lead/author decision): in the worker, treat the first publication of a
  scope as the accepted baseline even in manual mode (only later baselines are manual),
  or have the worker keep its own unacknowledged first snapshot for rename matching.
  Regression: manualBaseline monitor, no acknowledge after first publication, rename
  a to b, assert `renamedImage==b` and a controller-level test with a delayed
  navigation job.

### N1 (non-blocking, should fix) - symlinked/junctioned scope directory is permanently "incomplete"
- `fs_revision.hpp:68,81,113`: `probe` uses `lstat`/`FILE_FLAG_OPEN_REPARSE_POINT`, so a
  scope directory reached through a symlink or junction has `link==true`. `snapshot`
  then reports `complete==false` forever, and the controller shows "Folder monitoring
  could not obtain a complete snapshot", backs off to 30 s and invalidates files on
  each retry. `DirectoryEntryModel` does the same in
  `directory_entry_model.cpp` (`!directoryRevision.link`). Nothing in
  `fs_watch_tests.cpp` covers a linked scope directory.
- Regression: generated symlink/junction folder as scope; expect either normal
  monitoring (resolve the directory once) or an explicitly documented degraded state
  with no repeated invalidation.

### N2 (non-blocking) - model replaces last-good identities with an incomplete snapshot
- `directory_entry_model.cpp:200`: `identities` is overwritten whenever
  `accessible&&!truncated`, including `complete==false` (for example a directory
  revision change during enumeration under a copy). The next successful listing then
  has no complete baseline, so a rename in that window drops the selection, anchor
  and cursor. This is conservative but undercuts "preserve selection". Keep the last
  complete snapshot. Regression: incomplete listing between a complete one and a
  rename.

### N3 (non-blocking, measure at runtime) - steady-state I/O cost
- Worker `snapshot` opens a handle per entry (up to 20,000) for the directory and again
  for its parent, on the 2 s fallback poll and each debounce. `buildListing` adds a
  per-entry probe on every folder listing (up to 100,000 entries, cut off at the 16 MiB
  identity budget). Off the GUI thread and bounded, but not measured against REQUIREMENTS
  listing/first-paint budgets, or on network shares. Runtime phase should time a
  20k-entry folder with and without the probes.

## Checked statically, no defect found
- Scope generation guards: the controller callback drops `scope!=generation()` and
  `revision<=observationRevision`; the worker revision is monotonic across scopes; the
  monitor request is latest-only; the publication slot is single and coalesced
  (`publicationQueued`).
- Teardown: `~Monitor` sets `closed`, requests stop and clears `accept` under the
  mutex, waits at most 250 ms and abandons a blocked worker that holds only
  `shared_ptr<State>`; the queued lambda re-checks `closed`. `~Private` resets the
  monitor first. The receiver is never dereferenced after close.
- Decode ordering: 100 ms fallback timer; a late baseline re-decodes once with a new
  generation. `cancel()` stops the timer. Stale pixel restoration is guarded by the
  existing generation.
- Local-delete duplicate suppression: `removedCovers` is checked before stepping, and
  `reconcileRemovedPaths` resets `pendingObservation`. A queued stale observation is
  dropped by the scope change.
- Incomplete or ambiguous evidence: `renames()` requires both snapshots complete and
  unique identities, and excludes hard links (`links!=1`) and links. Truncated
  snapshots never prune selection (`directory_entry_model.cpp` `!truncated` guards).
- Byte and entry caps are enforced in both `snapshot` and `buildListing`. F5 is a
  one-token allowlist addition in `Main.qml:324`; text and modal precedence stay in
  the existing dispatcher. No public header or ABI change; the worker semaphore removal
  is a lock deletion with a justified comment.
- No original-media writes: probes use read-attribute handles only. Windows probes open
  with full share flags.

## Limitations
- No build, test, Docker or UI inspection was performed, so the real-QML F5 tests,
  `fs_watch_ui_tests.cpp` assertions and the design-skill review of the one-line QML
  change are unverified beyond reading the diff (no visual change).
- Earlier author evidence (Docker 20/20, Windows 18/20 failures, narrow passes) was not
  re-examined; no full Windows pass exists for this candidate.
- Windows file-ID uniqueness on ReFS (128-bit) and OneDrive placeholder behaviour are
  untested.
