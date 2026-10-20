# FS-WATCH-01 Claude-lane independent final-candidate review R7

Candidate: 5e2b0dc (fixed), branch claude/review-fs-watch-01-r7, own worktree. The R6 report
(e2e10cf) is preserved in git history. Reviewed the whole packet against the frozen plan
(docs/FS-WATCH-01-PLAN.md), the Filter integration handoff and the current-main scaffold
baseline f708068, plus the delta 2bd898d..5e2b0dc. Author counts and the author's red/green
logs are not approval; the results below are mine. No subagents, no other worktree, no
product/shared/CMake/queue edits, no merge or push, no Docker, no grid/QML/input/full suite.

## Disposition: NOT APPROVED as a final packet. R6-1 and R6-2 are resolved; native/Docker/F5 runtime gates are still outstanding.

### Disposition split (updated after the Linux F5 release)
- Source/implementation review: no blocking source or test finding remains in the 5e2b0dc packet
  (R6-1 and R6-2 resolved; G1-G3 below are non-blocking test gaps). Later author commits through
  eff0aee are documentation only (`git diff 5e2b0dc eff0aee` over src/include/ui/tests/cmake/CMake/Dockerfile is empty).
- Final packet approval: NOT given. Full current Windows `scripts/build.ps1 -Test` and the serial
  integrated checks are still pending and are not waived.

### Resolved R6 findings
- R6-1 (phase cost): `ModelPublicationStats` exists only under GLANCELOT_FS_WATCH_TESTING, which
  cmake/FSWatchTests.cmake defines for `fs_watch_tests` only. `directory_entry_model.cpp` is
  compiled separately per executable (`glancelot_core`/`platform` do not include fs_revision.hpp),
  so `Private` has one layout per binary: no ODR risk. The hook is `thread_local`, copied at
  construction on the GUI thread, with a guarded restore. The phases are measured around the
  real `apply`/`merge`/`reset` code. No public header, runtime option or product instrumentation.
- R6-2 (budget regression): the 100k test now establishes a real complete one-entry baseline and
  selection, publishes 100,000 rows, and asserts `!complete && observed==0 && accepted==1`
  (no partial prefix, last complete baseline kept). After the extras are removed it renames the
  selected original and requires selection, cursor and the source hash to follow. It uses the
  public model API only. Native-identity absence skips explicitly, never as a pass.
- I re-derived the early-admission equivalence: every real entry costs more than `entryBytes(base)`,
  so `n > B/m` implies the per-entry loop would also trip. Only the probes and retained prefix are
  skipped. Nothing reads an incomplete `identities.entries` (`apply` and `renames()` ignore them).
  The takeResult change is unchanged from R6 and still correct.

### Independent results (my build: scripts/build.ps1 -BuildDir build-claude-review-fs-watch-01-r7, no -Test, 265/265)
Run directly with QT_QPA_PLATFORM=offscreen and project .tools Qt. Outputs are in `.cache/`
(git-ignored). The host was loaded by other lanes' ctest/grid jobs, so timings are not isolated.

| Run | Result | setup | initial gap | sustained/recovery gap | publications (n, max) |
| --- | --- | --- | --- | --- | --- |
| whole fs_watch_tests | 52 passed, 0 failed, 0 skipped, 172.2 s | 82.1 s | 12 ms | 150 ms | 13, 149 ms |
| 100k only #1 | 3 passed, 0 skipped, 108.0 s | 73.9 s | 26 ms | 151 ms | 13, 149 ms |
| 100k only #2 | 3 passed, 0 skipped, 113.1 s | 78.1 s | 12 ms | 190 ms | 12, 189 ms |

All 100k callbacks reported observed/accepted identities 0/1, reset 0, identity and selection 0 ms.
Merge was 93-128 ms (run 1) with merge scan 59-80 ms and raw-mirror projection 7-20 ms. So the
merge scan (an O(rows) hash probe pass) is the largest bounded phase; the 250 ms oracle has
about 60-100 ms of headroom on this loaded host. That is measured evidence, not a proof for
slower hardware, and I invent no extra margin. The 250 ms heartbeat, 250 ms publication and 30 s
recovery assertions are intact (final-recovery publications are also asserted <250 ms, though
those are 1-row publications and trivially pass).

### Main-integration compatibility (static, whole source)
- Public headers and CMake: `include/` is byte-identical to f708068; the only CMake change is
  `include(cmake/FSWatchTests.cmake)`; Main.qml differs from the baseline by exactly the `"refresh"`
  allow-list token. Cache owner implementation is not imported.
- `apply()` reconciles identities from the complete physical listing before Filter projection,
  prunes selection/anchor/cursor against `rawListing.index` (not visible rows) and only when
  `accessible && !truncated`, and keeps the last complete baseline otherwise. Hidden-selected rename
  and current-rename filter retention are covered by new tests that passed in my 52/52 run.
- Controller keeps `observe()` before the temporary-filter clear on successful crossing.
- Lifetime: Monitor teardown is bounded (250 ms wait, by-value shared state); `observationRevision`
  is monotonic across scopes (worker `revision` is never reset), so no new scope is dropped.

### Static F5 route (no runtime run; native slot not released)
Read application-interface-design (the UI change is a keyboard route only, no new visuals, tokens or
branding touched). `viewer.commands` instantiates one `Shortcut` per binding, disabled when
`modalOpen || editingText`, and in grid scope only when `gridAllowsCommand(id)`; "refresh" is now
in that list, `invoke()` applies the same check, and no second F5 binding exists in src/ui/include.
Text-input and settings-modal precedence therefore hold by construction. `tests/fs_watch_ui_tests.cpp`
exercises preview/grid/combined, text input and settings modal with real key clicks, but I did not run
it. Static review does not establish the runtime result.

### Concrete items remaining
- G1 (test gap, moderate): no 100k publication measurement with an ACTIVE filter. With a filter the
  model takes `rawListing = std::move(listing); applyFilter()` (full reset, per-row match, row copy)
  on the GUI thread; the handoff asked for the 250 ms oracle on 100k rows including monitor-driven
  refreshes. This path is inherited from main, but it is unmeasured under FS-WATCH refresh load, and
  every refresh under an active filter resets the model (loses scroll/delegates) rather than merging.
- G2 (test gap, minor): the budget regression asserts the instrumented outcome (observed 0, accepted
  1), which the author's red run proves discriminates the early admission. The final
  rename-follow assertions were never shown red against a source that replaces the baseline with a
  partial one (the red run stopped before them). I did not mutate product source to check this.
- G3 (minor, unchanged R6-3): a stale/cancelled `Listing` is destroyed on the GUI thread.
- Bound, not a bug: roughly 35k+ entries per folder never get rename tracking (16 MiB budget).

### Linux real-QML F5 runtime (my own execution, released isolated container only)
Read application-interface-design (dialog/keyboard/focus rules); the change adds no visuals, tokens or
branding. Command: `wsl -d Ubuntu -- docker build --target build -t glancelot-review-fs-watch-r7-tests .`
from this worktree (exit 0, 491.7 s; context layers #3,#8-#19 CACHED, but the configure/build stage #21,
271 s, and #22, 215.7 s, actually executed, so the fs_watch_ui_tests binary was compiled by me from
5e2b0dc source; image sha256:1be68ac28ec4d78f3a0b54facf97b928b8c6416ad2110eefac2446cefc6f3fba).
Then `docker run --rm --network none -e QT_QPA_PLATFORM=offscreen -e QT_QUICK_BACKEND=software
glancelot-review-fs-watch-r7-tests /build/fs_watch_ui_tests -o -,txt`: **3 passed, 0 failed, 0 skipped,
922 ms**, exit 0 (`workspaceRefreshAndTextModalPrecedence` PASS: real F5 key clicks in preview, grid
and combined raise `filesInvalidated`; F5 with a focused TextInput and with the settings modal open does
not). No host mounts, no network, generated media only. This is Linux offscreen/software-scenegraph
command-route evidence, not GPU/DPI or Windows native key qualification; the log notes "No QRhi found".
Output: `.cache/r7-linux-ui.txt`, build log `.cache/r7-docker-build.log` (git-ignored).

Non-FS-WATCH observation from that run (files identical to f708068/main, so not this packet's code):
~40 `ShortcutCheatSheet.qml:327: ReferenceError: keys is not defined` warnings (the `Repeater { model: keys }`
resolves `keys` declared on the parent RowLayout) and one `PhotoGrid.qml:298:25: Unable to assign
[undefined] to int`. Reproduce: run `/build/fs_watch_ui_tests` as above. Owners: SHORTCUTS-HELP-01 / FILTER-01.
The test passes despite them; I recommend those owners fix them.

### Author's Linux full gate (integrator-run evidence, content-reviewed, not my execution)
From eff0aee `linux-r8-gate-summary.txt` and `linux-r8-fs-details.json`: terminal 0, 25/25 CTest,
206.78 s, fs_watch 76.11 s (53 passed/0 failed/0 skipped; 53 vs my Windows 52 is the POSIX-only
alias-to-file case), fs_watch_ui 2.63 s (3/0/0), image sha256:c1457164...abb43; source/product inputs
identical to 5e2b0dc (my diff check). The build-stage log was CACHED, so it does not show new compilation.
Linux 100k: gap 79 ms, publications max 68 ms, observed/accepted identities 0/1. Consistent with my
Windows runs; it is not a Windows gate.

### Not run / still required (exact remaining gates)
1. Full current-source Windows `scripts/build.ps1 -Test` (25 targets) in an exclusive native slot,
   including `fs_watch_ui` and the grid/input/QML suites. The author's attempt was cancelled after 9
   targets; I did not run it.
2. Full `docker build -t glancelot-browser:main .` on the integrated source. The old 2bd898d Linux
   21/21 is not a gate for this source.
3. Real F5 runtime in preview, grid and combined plus text-input and settings-modal precedence.
4. Serial integration under the lock (merge main, which now includes Cache scaffold work, then
   re-check on the integrated commit).
5. Unverified by anyone: physical/network/coarse-timestamp/ReFS/cloud filesystems, registration ABA,
   GPU hardware (software scene graph results make no GPU claim).

Filtered or static passes, and the 52/52 backend, are not full-packet approval.
