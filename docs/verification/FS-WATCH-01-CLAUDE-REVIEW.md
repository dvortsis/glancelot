# FS-WATCH-01 Claude-lane independent corrective review R6

Candidate: 2bd898d (fixed), branch claude/review-fs-watch-01-r6, own worktree. Reviewed
`9bc891f..2bd898d` (src, tests, docs, generated evidence) plus the resulting whole
`buildListing`/`Private::pump`/`apply`/`merge`/`reset` in directory_entry_model.cpp, the identity
helpers in fs_revision.hpp, cmake/FSWatchTests.cmake and the installed Qt 6.8.3 headers. The R5
report (1398061) is preserved in git history. Author counts, the retained 9bc891f Docker pass and
the author's pending full gates are not approval. No `ui/` or QML file is in the delta, so no UI
behavior was reached and no visual review is claimed. I used no subagents and touched no other
worktree.

## Disposition: NOT APPROVED. N17 (100k responsiveness margin) stays open; full gates outstanding.

### Static findings: both performance fixes are correct as written

1. Impossible-identity early admission (directory_entry_model.cpp:102-107). Every real
   entry costs `entryBytes(row.path)` with `row.path = base + name`, name at least 1 char, so
   `entryBytes(base)` is a strict lower bound per entry. `n > B/m` (integer floor) is exactly
   `n*m > B`, which implies the per-entry loop would have tripped `cost > budget - bytes` at
   some entry anyway. So `identities.complete=false` is the same final verdict the old code
   reached; only the native `probe()` calls and the retained prefix map are skipped. Rows,
   index, sort, `truncated`, the `directoryProbe` re-check and `renames()` (returns empty when
   either side is incomplete, fs_revision.hpp:161) are unchanged, and `apply()` keeps the last
   complete baseline for the same scope (lines 226-229). Per-entry budget stays for listings
   that might fit. Cancellation is still checked in the loop.
   Consequence to state plainly: with the 16 MiB budget and about 500 B/entry in a long path,
   folders above roughly 35k entries never get rename tracking, and the 100k stress folder
   never does. That is a bound, not a bug, but "rename-safe selection" does not extend there.
2. `takeResult()` (lines 190-194). Verified in the installed headers:
   `QFutureInterface::takeResult` (qfutureinterface.h:423-440) does `waitForResult(-1)`, takes the
   mutex, binds a non-const `ResultIteratorBase`, so `value<T>()` is `T&` (qresultstore.h:75) and
   `move_if_noexcept` really moves; then `reset()` and `clear<T>()` free the stored object.
   `QFuture::result()` (qfuture.h:309) returns by value from `resultReference(0)`: a deep copy of
   the row vector plus a second copy retained until `deleteLater`. `finished` implies the
   result is stored, so `waitForResult(-1)` does not block the GUI. One consumer, no
   `future.cancel()` anywhere, the worker always returns one `Listing` (also when stop was
   requested: `cancelled=true`), so result count is exactly 1 and `Q_ASSERT(isValid())` /
   `Q_ASSERT(!hasException())` hold. A thrown worker exception would rethrow in both the old and
   new code (same exposure). `static_assert(is_nothrow_move_constructible_v<Listing>)` compiles
   and is the condition `move_if_noexcept` needs. Generation/stale/cancel flags, `inFlight`,
   `pump()` and `apply(std::move)` are unchanged; stale results are still discarded by token.
3. Test-only hook. `modelPublicationTiming` is `inline thread_local`, copied into
   `Private::publicationTiming` at construction on the constructing (GUI) thread; both the
   member and the timer are inside `GLANCELOT_FS_WATCH_TESTING`, defined only for
   `fs_watch_tests` (cmake/FSWatchTests.cmake:11); `Private` lives in one TU per target so layout
   cannot diverge. Lifetime is sound: `publications` outlives the model, `restoreTiming` is
   declared before `viewer`/`grid`, the reporting guard after them. The new
   `maxPublication<250` assert is additive; the 250 ms heartbeat, 30 s recovery and
   exact-row checks are unchanged (no weakened timeout, no skip).

### Independent runtime (backend only, own build dir; no Docker, no grid/QML/input/full suite)

Build: `scripts/build.ps1 -BuildDir build-claude-review-fs-watch-01-r6` (no `-Test`)
completed 237/237 in 5 min 24 s. Run: `fs_watch_tests.exe -o ...,txt`, QT_QPA_PLATFORM=offscreen,
project `.tools` Qt. Outputs are `.cache/r6-full.txt` and `.cache/r6-100k-{1,2,3}.txt` (ignored,
not committed). Host was shared: VS Code, cpptools and a Logitech agent were consuming CPU.

| Run | Result | setup | initial gap | writer/recovery gap | publications (n, max) |
| --- | --- | --- | --- | --- | --- |
| whole file | 49 passed, 0 failed, 0 skipped, 137.6 s | 41.8 s | 12 ms | 180 ms | 5, 179 ms |
| 100k only #1 | 3 passed, 0 skipped | 68.2 s | 15 ms | 181 ms | 2, 170 ms |
| 100k only #2 | 3 passed, 0 skipped | 33.5 s | 12 ms | 129 ms | 6, 128 ms |
| 100k only #3 | 3 passed, 0 skipped | 43.3 s | 16 ms | 232 ms | 3, 232 ms |

4/4 executions passed here, versus 1/3 at 9bc891f in R5 (385 ms gap and a 30 s recovery
timeout). That is real improvement evidence but not proof of the acceptance criterion:

- The worst sustained gap equals the worst single publication callback in run #3 (232 ms vs
  232 ms), i.e. the remaining GUI stall IS one apply, and it sits 18 ms under the 250 ms oracle.
  Observed maxima 128/170/179/232 ms plus the author's 208 ms are a 128-232 ms spread; a
  slower or more loaded host crosses 250 ms with no source change.
- The author's pre-takeResult instrumented number (208 ms on 5e4fe9e, one run) and my
  post-takeResult numbers (128-232 ms, four runs) do not show a measurable GUI-side gain from
  the move. No A/B of copy vs move on the same host exists. The copy removal is correct and
  verified statically; its effect on the callback is unquantified, and the remaining cost (merge
  scan/`reset`, destruction of the replaced rows, `deleteLater`) is not itself bounded or sliced.
- I did not weaken or skip anything and do not claim the passes are representative.

### Concrete unresolved items

- R6-1 (open, acceptance, N17): the 100k GUI publication is 128-232 ms against a 250 ms hard
  oracle with no margin and no proof that `apply`/`merge`/`reset` cost is bounded for 100k rows.
  Needs either a measured bound (per-phase timing of merge scan vs reset vs destruction, or
  chunked/moved destruction off the GUI thread) or repeated runs on an isolated host
  demonstrating headroom. The 30 s recovery oracle did not recur here but is not closed either.
- R6-2 (test gap): the impossible-budget path has no committed regression test. The 34,380-file
  before/after diagnostic is two one-line text files (`identity-budget-{before,after}.txt`) whose
  harness is ignored/not in the repo, so I could not re-run it. The invariants it should pin are
  rows unchanged, `identities.complete=false`, no retained prefix and last-complete baseline kept.
  I verified them by reasoning above, not by running the author's diagnostic. The reported 1604 ms
  vs 940 ms is uncontrolled-cache timing; only the 32,102 vs 0 retained-identities counts are a
  stronger signal and I did not reproduce even those.
- R6-3 (minor, not required): a stale or cancelled `Listing` is still destroyed on the GUI thread
  (up to 100k rows). Not reached by the passing runs and not asked for in this packet.

### Not run by me
Docker; the full Windows `scripts/build.ps1 -Test`; grid/QML/input suites (native slot not
released, as instructed); real F5 preview/grid/combined runtime; junction/symlink native probes;
the filtered-main (Filter/History/Help) merge, which needs its own future review. I do not claim
the author's running 2bd898d backend (36723) or Docker (24467) results, nor the retained
9bc891f Docker 21/21 (204.40 s) as a gate for this source; the earlier 20/21 grid-delegate failure
and its isolated passes remain as the author recorded them. The Filter integration doc's
unfiltered identity-baseline preservation is described, not implemented, and is out of scope here.

### Residual (unverified, hardware/platform)
Registration ABA, coarse-timestamp identity collisions and physical/network/ReFS/cloud behavior
remain unverified.

### Required before approval
1. Resolve R6-1 with measured per-phase GUI cost or isolated repeated runs showing headroom
   under 250 ms, and keep the 250 ms heartbeat, 250 ms publication and 30 s recovery checks.
2. Add (or point to) a committed regression for the impossible-budget path (R6-2).
3. Full Docker and full Windows `scripts/build.ps1 -Test` on 2bd898d (or its successor) with
   exact output; then independent real F5 runtime review once the native slot is released.
4. Serial integration under the lock, merging current main (Filter/History/Help) first, with
   checks on the integrated commit.
