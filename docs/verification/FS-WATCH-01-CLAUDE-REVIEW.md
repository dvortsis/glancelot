# FS-WATCH-01 Claude-lane independent corrective review R5

Candidate: 9bc891f (fixed), branch claude/review-fs-watch-01-r5, own worktree. Reviewed
`9177b15..9bc891f` (src, tests, docs, generated evidence) plus the whole resulting paths:
`directoryProbe`/`buildListing` (directory_entry_model.cpp, fs_revision.hpp), the worker
publication/merge in fs_monitor.hpp and the observation merge/invalidation in
viewer_controller.cpp. R4 report (a19a431) is preserved in git history. The author's handoff
is not approval. Public headers unchanged; no `ui/` file in the diff (the one-token `refresh`
allow-list is the earlier delta), so no UI behavior was reached and no visual review is claimed.

## Disposition: NOT APPROVED. One unresolved test/acceptance defect; gates remain open.

### Static findings
- N12 fixed in source. `buildListing` now compares the prior directory via `directoryProbe`
  (resolves the leaf alias). Missing old name -> empty identity -> rename; different object ->
  differing identity -> rename; old name aliasing the moved object -> equal identity -> no
  rename, so selection is correctly dropped. A prior alias gives an empty `entryRevision`
  identity, so no rename is inferred. Reads only; no original is written.
- N14 fixed in source. `subsequent = !baselinePending` is set per scan and OR-merged in the worker
  (fs_monitor.hpp:196) and the controller (viewer_controller.cpp:257); `baseline` stays the
  OR-merged first marker. Invalidation uses `subsequent||forced`, so an initial plus later
  incomplete scan coalesced together still invalidates, while the pure first baseline does
  not. Stale scopes are still dropped by the scope/revision check (generation preserved).
- Bounds, lifetime, macro isolation, cancellation and symlink/junction guards: unchanged from the
  R4 analysis; the delta adds one bool and one probe call per rename candidate. Nothing new.

### Independent runtime (backend only, my own build dir; no Docker, no grid/QML/native-input suite)
Build: `scripts/build.ps1 -BuildDir build-claude-review-fs-watch-01-r5` completed 237/237.
Run: `fs_watch_tests.exe -o ...,txt`, QT_QPA_PLATFORM=offscreen, Qt from main .tools.
(`.cache/r5-fswatch-full.txt`, `.cache/r5-100k-run1.txt`, `r5-100k-run2.txt`; ignored, not committed.)

1. Whole `fs_watch_tests` file: `Totals: 48 passed, 1 failed, 0 skipped, 230979ms`. The one
   failure is `hundredThousandEntriesSustainedWriterKeepsGuiResponsive`:
   `100000 entries: generated setup 124786 ms; initial GUI gap 179 ms; sustained-writer/recovery
   GUI gap 385 ms; writes 256 invalidations 4` -> `'worst<250' returned FALSE` (fs_watch_tests.cpp:185).
   All 9 new/changed R5 rows (3 N12, deferred-incomplete, 3 native rearm) passed in that run.
2. Filtered 100k rerun #1: PASS. setup 58204 ms, initial gap 12 ms, writer/recovery gap 129 ms,
   writes 274, invalidations 13; `3 passed, 0 failed, 0 skipped, 90237ms`.
3. Filtered 100k rerun #2: FAIL at fs_watch_tests.cpp:178, `!grid.loading()&&metadata.count()>0`
   timed out (30000 ms; "30250 ms would have been sufficient"); `2 passed, 1 failed`, 138073 ms.

So 1 of 3 independent executions of the new test passed. The author's 140 ms pass reproduces
(129 ms), but it is not stable here. Other lanes/IDE/Java/node processes were active (CPU about
20% at sampling) and setup ran 1.4x-3x slower than the author's 40.9 s, so host contention is
plausible, but I cannot prove it is the only cause. I did not weaken the 250 ms gap or the 30 s
recovery oracle, and I did not claim the pass is representative.

Classification: this is a failing acceptance test on the candidate in a shared, loaded host. I
cannot separate hardware/load limitation from a source defect. The recovery wait is close to
the oracle margin (30.25 s needed), and the gap in run 1 (385 ms) is above the oracle with
the worker (not GUI) doing the scans, which points at GUI-thread work after publication (model
apply of 100k rows or dataChanged) rather than raw I/O. This is a concrete finding (N17): the
author must either show the GUI-thread cost is bounded (e.g. per-slice apply timing) or
demonstrate stability over repeated runs on an isolated host. A test that passes only when the
machine is quiet is not evidence of the responsiveness acceptance criterion.

### Not run by me
Docker (author's Linux gate running), grid/QML/input/full Windows suite (native slot not
released), real F5 preview/grid/combined runtime, junction/symlink native probes, the
paired 20k cost JSON (reviewed as data only; the 1.13-1.61 s worker I/O per pair is material,
warm uncontrolled cache, no power/network claim). I do not claim the author's current 9bc891f
Docker or Windows gate results.

### Residual (unverified, hardware/platform, not source bugs)
Registration ABA, coarse-timestamp identity collisions and physical/network/ReFS/cloud behavior
remain unverified; periodic recovery stays bounded.

### Required before approval
1. Resolve or explain N17 (100k test 1/3 pass here) with repeated isolated runs, or bound the GUI cost.
2. Current-source full Docker and full Windows `scripts/build.ps1 -Test` on 9bc891f.
3. Independent real F5 preview/grid/combined runtime review once the native slot is released.
4. Serial integration under the lock with checks on the integrated commit.
