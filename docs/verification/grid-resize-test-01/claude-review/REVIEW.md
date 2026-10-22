**Verdict: APPROVED** for the test-only synchronization change, with three non-blocking findings.

**Pin:** `498d92279a356d064595c2781f84bad89c3624c3`.

**Scope:**
- I reviewed `tests/grid_ui_tests.cpp:270-298` (`reviewCompactCellsSnapToWholeRows`).
- I read `ui/PhotoGrid.qml` for the GridView and delegate lifecycle (lines 23-61, 470-487, 650-709).
- I read the `.cache/review/baseline/` outputs.
- I did not review CSV, new UI behavior or hardware. I ran nothing, so none of this was run.

## Assessment

**1. This is cleanup synchronization, not a hidden bug.**
- `liveDelegateCount` goes up on `Component.onCompleted` and down on `Component.onDestruction` (`PhotoGrid.qml:665-666`). It therefore counts delegates that are still alive, including ones the GridView has released but not yet destroyed.
- Both failing baseline runs (2 and 7) fail at the same place: `w 300 h 420 delegates 9 columns 1`.
- `compact` is `height < 420` (`PhotoGrid.qml:59`), so `ph=420` is the iteration where compact switches to regular layout. Both `cellHeight` and `columns` change at that point.
- Roughly 4-5 old compact delegates plus the new regular ones would give about 9 for a moment. That fits a transient old-plus-new overlap, not a leak.
- The other nine failure-free iterations pass the same assertion at the settled count, and 8 of 10 runs pass overall. That points to a race, not a deterministic overshoot.
- I could not confirm from Qt source that the delegate destruction is deferred. I'm inferring it from the pattern. `QTest::qWait` (used inside `QTRY_*`) is known to flush `DeferredDelete`, which a bare `processEvents()` may not. The 10 corrected repetitions are what will confirm it.

**2. The wait and assertion stay bounded and meaningful.**
- `QTRY_VERIFY2_WITH_TIMEOUT(..., 2000)` is limited to 2 s per iteration.
- A failure returns from the test immediately, so the worst case is one 2 s stall, not 697 of them.
- When the count is already within the limit, the wait returns on the first check, so the timing-budget tests are not slowed.
- The limit is unchanged at `columns * 8`.
- A real leak, a steady count above the limit, or a release that takes more than 2 s still fails, with the same diagnostic text.
- Binding-loop `failOnWarning` stays in force during the longer event-loop turns.

**3. Sampling geometry before the wait is low risk.**
- `compact`, `gh`, `cell` and `columns` are read before the wait, but they are all synchronous bindings:
  - `compact` follows panel height.
  - `columns` follows grid width.
  - `cellHeight` follows `height` and `width`.
  - The GridView height comes from anchors.
- The only deferred work I found is `Qt.callLater(reveal/restoreViewport)`, which moves the viewport, not the cell size. The pre-wait and post-wait values should therefore be identical.
- If something did change them, the geometry assertions would run against stale values and could pass vacuously.

## Non-blocking findings

1. **Move the geometry reads after the wait.** Moving `compact`, `gh`, `cell` and `columns` below the `QTRY` costs nothing and removes the vacuous-pass risk. The predicate would then need to read `columns` fresh, or you accept one extra read.
2. **The test now bounds the settled count, not the peak.** `QTRY` passes at the first poll where the count is within the limit, so the old-plus-new overlap of roughly 2× is tolerated. `worstDelegates` and the "bounded" comment now mean "settled". Please reword them. If a transient bound matters for thumbnail requests under the AGENTS.md "bound allocation" rule, that is a separate decision for the lead. This change doesn't make it worse, since the baseline also sampled at an arbitrary instant.
3. **The second assertion at line 290 can't fail.** No events run between the `QTRY` pass and the re-read, so `live <= columns * 8` there is redundant. It is harmless, and keeping it matches the "all previous assertions remain" claim.

## Baseline provenance

The baseline output paths point to `.worktrees/codex-integrate-export-csv-01`, not this worktree. The failing line is 284 there, and the same assertion is at 290 here. That is consistent with the described six-line diff, but I can't confirm that tree was exactly at `75175d5`. The `PhotoGrid.qml:706` thumb `Image` matches.

## Remaining checks, none of which I verified

- 10 corrected repetitions of the focused test. A pass here is expected, and one fail is enough to block.
- The full Windows gate (`scripts/build.ps1 -Test`) and `docker build -t glancelot-browser:main .` under the gate lock.
- The exact merge check.
- The corrected test must also not be slower than the baseline passes, which took about 3.0-4.0 s.