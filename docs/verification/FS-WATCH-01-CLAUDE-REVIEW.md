# FS-WATCH-01 Claude-lane independent corrective review R3 (STATIC phase)

Candidate: b4cfd20 (fixed). Reviewer: Claude Code, branch claude/review-fs-watch-01-r3,
own worktree. Reviewed range: `08bb4b1..b4cfd20` (source, tests, plan wording), plus the
full current `fs_monitor.hpp`, `fs_revision.hpp`, the observation/reconcile/open/trigger
paths of `viewer_controller.cpp` and `buildListing`/`apply` in `directory_entry_model.cpp`.
Prior report: bb3d271 (N4-N7, B1, N1, N2, N3), superseded by this file.

Phase: static only. Nothing was configured, built, run or probed, and no process or
window was touched. No runtime evidence is claimed. I read the author's two logs
(`.cache/author-corrective-{red,green}.txt`) only as a description of what the author ran.
They are not approval. The green log is a 12-case filtered subset (`Totals: 12 passed`),
not the 32-method `fs_watch` file, and not the full Windows or Docker gate.

UI: no `ui/`, QML, token or visual file is in `08bb4b1..b4cfd20` (checked with
`git diff --stat`). The native UI skills were therefore not needed for this round. The
earlier one-token `Main.qml` F5 change is unchanged and is not re-reviewed here.

## Disposition: CHANGES REQUESTED (static). No new blocking defect found.

The source fixes for N4, N5 and N6 are correct in my reading, for the cases they target.
I found four new non-blocking findings (N8-N11) and several unclosed coverage gaps.
Full approval is withheld because:
1. No native build, CTest or Docker run exists for b4cfd20. The author's 12-case run is narrow.
2. N3 (20k-entry I/O cost) is still unmeasured.
3. The N7 mid-enumeration retarget test and a controller-level N4 test do not exist.
4. The branch lacks the latest `main` (UI, settings, favorites, platform 32cf4f4). The frozen
   public contracts and source need an integration review again after that merge.
FS-WATCH is not integrated and nothing here says otherwise.

## Prior findings, re-assessed on b4cfd20

| Id | Disposition | Basis |
|----|-------------|-------|
| B1 first baseline lost while controller busy | Fixed (static) | `fs_monitor.hpp:136-143` seeds the first complete `accepted`/`acceptedParent` and `acceptedImage` under the mutex, so later unacknowledged scans carry the earliest baseline. Unchanged in intent. |
| N1 linked scope permanently incomplete | Fixed (static), see N9, N10 | Resolved-target probing, alias row spelling and at most 3 handles are intact. |
| N2 incomplete identity map replaced the last complete | Fixed in model and now in monitor | Model: `apply()` `:204-205`. Monitor: see N6. |
| N3 steady-state I/O | Open | Needs the runtime measurement. Static estimate below. |
| N4 `!before` stopped meaning "first observation" | Fixed (static), controller path untested | `Observation.baseline` is set on the worker's first scan of a scope (`:87`, `:147`), OR-merged on the publication slot (`:162`) and in the controller pending merge (`viewer_controller.cpp:240`). The decode gate at `:228` now tests `value.baseline`. The gate runs on arrival, before the merge. A flag carried by a merged pending value is not reused, so there is no double re-decode. Open/restart always bumps the scope (`restart=true`), so a re-open of the same path always gets a new baseline. |
| N5 alias to target navigation read as a directory rename | Fixed (static) for a link leaf | `buildListing:121-123` uses the no-follow `directoryEntryRevision.identity()` against `prior.entryRevision.identity()`. `identity()` is empty for a link, so alias to target, target to alias and L1 to L2 are never renames. A real rename of an ordinary directory still matches. Ancestor aliasing: see N11. |
| N6 partial snapshots replaced the monitor baseline | Fixed (static) | `remember()` advances `accepted`/`acceptedParent` only to a complete snapshot, and `acceptedImage` independently. `establishScope()` clears all three on a scope change, in the worker, in `acknowledge()` and in the automatic path. The automatic path is guarded by `value->scope==shared->request.scope` (`:175`), and `acknowledge()` by `value.scope!=scope` (GUI thread, serialized with `setScope`). I could not construct an interleaving where `acceptedScope` runs ahead of the worker's scope or an old scope's identities survive. |
| N7 linked-scope coverage | Partly addressed | Added: coverage and zero-invalidation steady state, viewer rename following, a dangling target, a regular-file target (POSIX only), and ordinary navigation reset. Still missing: the retarget A-B-A guard test (below). |

## New findings

### N8 (non-blocking, should fix) - controller pending merge can pin an incomplete `before` over the worker's complete seed
- `viewer_controller.cpp:236`: `value.before = pending->before ? pending->before : pending->after`.
  `pending->after` can be incomplete, and the merge result then becomes the next
  `pending->before`.
- Trigger: the first scan of a scope is incomplete (a busy or large folder, or a change
  during the scan) while navigation or reconciliation is busy. Scan 2 is complete. The worker
  seeds S2 but publishes `before=null`. The controller assigns `before=S1` (incomplete). Scan 3
  arrives with the worker's correct `before=S2`, but `pending->before` is non-null, so S1 wins
  again and stays until the controller acknowledges.
- Effect, all conservative: `value.before->entries != current.entries` is true, so
  `filesInvalidated` plus a navigation refresh job fire without a real change (`:402-404`,
  `:426-432`). `:382` also refuses to prove a deleted folder because `before->complete` is false.
  Worker `renamedImage`/`renamedDirectory` use the worker's own `before` and stay correct.
  No wrong deletion or rename follows.
- Proposed fix: keep a pending `before` only when it is complete, else prefer the worker's
  `value.before`, else a complete `pending->after`. Add a controller-level test that runs this
  sequence with a busy navigation. The Monitor-level partial tests cannot see it.

### N9 (non-blocking, needs a lead decision) - folders the monitor can never snapshot completely no longer auto-refresh the grid
- `Observation.before` is now non-null only after a complete snapshot, because partial
  snapshots no longer seed or advance `accepted` (N6). The incomplete branch of
  `reconcileObservation` emits `filesInvalidated` only `if(value.before||value.forced)` (`:393`).
- Trigger: a folder with more than 20,000 entries or more than 16 MiB of identity (so
  `snapshot()` is always incomplete), but within the model's own 100k listing limit. Before
  this round, `accepted` followed the incomplete snapshot, `before` was non-null and every
  observation invalidated. That also meant a full relist on every retry, up to every 30 s,
  which is the N3 cost. Now only F5 (`forced`) invalidates. A file added by another program
  does not appear in the grid. The "could not obtain a complete snapshot, press F5" status is
  shown, so the user is told, but this is a behaviour change the plan did not state.
- The plan says this packet does not claim large-directory publication. Either record this as
  an accepted limitation, or gate the invalidation on `!value.baseline` instead of `value.before`
  so a truncated folder still refreshes on events with the existing backoff. Either way, add a
  test with an injected `read` that is permanently incomplete and make the policy explicit.

### N10 (low, unverified) - alias retarget does not re-arm the native registration
- `:121` registers only paths missing from `watcher.files()+directories()` by string. When the
  alias link is retargeted from A to B, the string `alias` stays registered, but the OS handle
  or inotify watch belongs to the old target A.
- `coverage` stays true and the next scan sees B's membership through the periodic path (every
  2 s when coverage and completeness hold). Native events from B are never delivered, so
  latency falls from event-driven to the poll interval without any degraded indicator.
- Not a correctness loss. Propose: remove and re-add the alias path when
  `current->entryRevision` (the link entry) differs from the revision seen at registration, or
  document the 2 s bound. Linux inotify and the Windows change handle behaviour are untested.

### N11 (low) - test-quality gaps in the new cases
1. `firstBaselineMarkerSurvivesPublicationCoalescing` asserts the `baseline` flag on a bare
   Monitor. The thing N4 protects is the controller's single post-baseline re-decode, which has
   no test. Needed: delay the receiver event loop, publish two scans, assert exactly one
   re-decode after a late baseline and no stale image (hash of decoded pixels after a write
   between decode start and scan 1).
2. `automatic-first-partial` passed in the author's red run (`Totals: 4 passed, 5 failed`), so
   it does not discriminate the fix. The manual cases and middle-partial cases do.
3. `viewer.image().pixelColor(0,0)==red` after the rename cannot distinguish a reload from
   retained pixels, because the image is the same colour. The `filePath()==b` and `imageReady`
   checks carry the evidence. Use a distinct colour per file, or compare the decoded hash.
4. A leaf alias is covered. A non-link directory reached through a link ancestor (for example
   `junction\sub` against `real\sub`) has a non-link entry identity equal to the prior, so
   `renamedDirectory` is true and the selection follows to the other spelling. That is the
   same object, so it is arguably right, but it contradicts "ordinary navigation starts with a
   new selection" and has no test or stated decision.
5. Identity cases `QSKIP` or `QWARN` on filesystems without a usable native identity. A skipped
   result is not a pass.

## Required remaining coverage (concrete)
- **N7 retarget during enumeration.** `snapshot()` has no seam between `directory_iterator`
  and the `finalRevision`/`stableAlias` checks (`fs_revision.hpp:135-139`). Add a private
  test-only hook, for example an optional callback invoked once after enumeration, or a
  `std::function` parameter with a default. Then: retarget alias to B and back to A inside
  the hook and assert `complete` is false when the link entry revision changed; swap to B and
  leave it, assert `complete==false` and that the controller does not report B's membership
  as A's rename. Record `coverage` and `maxHandles<=3` in the same test. Without a seam, the
  guard is only exercised by chance. Residual hole to state: a swap that leaves inode, ctime
  and birth time identical within one kernel tick on a coarse-timestamp filesystem.
- **Controller N4 and N8 tests** (above), including cancellation of the re-decode by a newer
  open.
- **N9 policy test** with a permanently incomplete injected `read`.
- **POSIX symlink and regular-file-target tests** (`directoryAliasToRegularFile...` is
  `#ifndef Q_OS_WIN` and has never run). They need the Docker/Linux run.
- **Windows reparse variants** beyond a junction: `mklink /D` directory symlink and a cloud
  placeholder. `probe` sets `link` for any reparse point, so those scopes are incomplete
  (`readable` false) and have coverage false. That is conservative, not a regression, but it
  is unmeasured.
- Original-media immutability: the changed code adds no writes (probes use read-attributes
  only, as before). Hash assertions exist in the rename tests and need the full run.

## Checked statically, no defect found
- Coalescing: one publication slot, `publicationQueued` single flight, `baseline`/`forced`/
  `fileEvent` ORed only inside the same scope. An old-scope publication is replaced, never
  merged into a new one. `observationRevision` is monotonic across scopes because the worker
  counter is not reset.
- Races: `before` is read under the mutex, then rename maps are computed outside it. A
  concurrent acknowledge can only make that `before` older, never from another scope, so a
  match remains an identity relation against a real earlier complete snapshot.
- Receiver startup and teardown: the worker invokes the receiver only while holding the mutex
  with `closed==false`, and `~Monitor` sets `closed` under the same mutex before waiting 250
  ms. A blocked filesystem call holds only `shared_ptr` state. The leaked-thread path after a
  timed-out wait is unchanged and is bounded by the abandon design.
- Rename with missing old pathname or name reuse: `renamedDirectory` and `renamedImage` come
  from identity maps keyed by the old path, so a new object created at the old name does not
  mask a proven rename, and ambiguous (hard link, reparse, unknown) identities are refused by
  `identity()`. The worker uses the lexical parent, and the parent entry of an alias is a link,
  so an alias directory rename is never inferred.
- Stale acknowledgement: `value.scope!=scope` on the GUI thread, plus `establishScope` clearing.
- Bounds: snapshots stay at 20,000 entries and 16 MiB of identity cost. The change adds no
  retained snapshot (the seed shares the same `shared_ptr`s). Peak retention is still the
  accepted pair, the current pair, one publication and one controller pending observation.
  The 100k model limit is unchanged and untouched by this diff.
- Off-GUI calls: the diff adds no filesystem call on the GUI thread. `observe()` uses lexical
  `QFileInfo` paths, and all probes, `canonical` and registration remain in the worker or the
  listing/navigation pools.
- Test-file integrity: every slot present at e4c3d3d, 301674a and 08bb4b1 is present at HEAD.
  The mid-series removal at ed85f5e (22 slots) was restored by 3506e08. HEAD has 32 slot names
  including one `_data`. The author's "30 originals" count refers to 301674a.
- Plan wording change in `FS-WATCH-01-PLAN.md` ("at each membership scan boundary") matches
  the code (`directoryProbe` at the start and again at the end of `snapshot`).

## N3 static estimate (not a measurement)
Per scan the monitor probes every entry (Windows: open, `GetFileInformationByHandle`,
`GetFileInformationByHandleEx`, close), then probes the parent listing too, and repeats every
2 s while complete and covered. At 20k entries that is on the order of 80k handle operations
for the folder plus the parent's, on a worker. The model repeats it per listing. This is
off-GUI and bounded, but the steady cost is unproven. Alias scopes add two `canonical()` and
one more `probe` per snapshot, which is negligible. The prepared 20k three-round probe still
needs the runtime phase: actual model, bare listing and snapshot, measured with and without
the probes, for both folder and parent.

## Remaining gates (none waived, none counted as pass)
1. Merge of latest `main` (32cf4f4 and later) into this branch, then a fresh review of the
   shared-contract and source diff.
2. Author fixes or a lead waiver for N8, N9, N10, N11 and the N7 seam test.
3. Full `scripts/build.ps1 -Test` on Windows with `fs_watch` and `fs_watch_ui` in the list.
4. `docker build -t glancelot-browser:main .`, which also runs the POSIX symlink and
   file-target cases.
5. Independent runtime review: B1/N2 historical probes re-run on b4cfd20, N3 timing, the real
   junction and symlink behaviour, F5 in preview/grid/combined.
6. Serial integration under the lock, then checks on the integrated commit.

## Limitations
- No build, CTest, Docker, UI or filesystem probe was run in this phase. Every statement above
  is from reading source and diffs, including the claim that the tests would pass or fail.
- Physical, network-share, ReFS, OneDrive and cloud-placeholder behaviour is unverified.
- The author's red/green counts are accepted only as a description of what was run.
