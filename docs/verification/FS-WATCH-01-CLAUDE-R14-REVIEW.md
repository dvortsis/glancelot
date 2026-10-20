# FS-WATCH-01 R14 independent Claude review

Candidate: `4a86528` (`codex/fs-watch-01`, "avoid repeated membership scans for metadata-only refresh"). Reviewer: Claude lane, `claude/review-fs-watch-r14`, worktree `.worktrees/claude-review-fs-watch-r14`, build `build-claude-review-fs-watch-r14`. Reviewed source is unchanged by this record.

Disposition: **APPROVED for the narrow publication-budget correction**, with one test-infrastructure risk (below) and one unresolved gate (folder_grid). Not an integration approval: serial combined gates on the merged commit remain required.

## Source review
`DirectoryEntryModel::Private::merge` gains a fast path when row count and every row key match in order. It skips the survivor hashing, order check and run counting, then calls the new `updateMetadata`, which is the previous tail of `merge` extracted unchanged (copy changed rows, move the index, emit one `dataChanged` over the changed span). For equal keys and order the old path computed zero runs, no removals, no insertions and reached the same tail, so behaviour is equivalent and delegates, persistent indexes and selection survive. No defect found. The fast path costs one linear key comparison instead of repeated hash lookups.

New test `metadataOnlyRefreshPreservesPersistentRowsAndSelection` asserts zero `modelReset`, a valid `QPersistentModelIndex`, updated size role, retained selection/bytes and cursor row.

## Independent measurements (Windows, MSVC, Qt 6.8.3; actual output)
- `fs_watch` 84.24 s Passed; `fs_watch_ui` 8.44 s Passed (ctest, from the Qt-enabled environment).
- `fs_watch_budget_tests.exe` run directly: 3 passed, 0 failed, 0 skipped, 180505 ms. 100k case: GUI gap 63 ms, maximum publication 78 ms (limit 250), merge scan 4-6 ms. Before the fix the failing run showed merge scan 177-227 ms and publication 358 ms.
- Linux was not run by this reviewer.

## Findings
1. **P2, test infrastructure.** Generated setup for the 100k case took 147.7 s here (117.6 s in Codex's failing run), so `fs_watch_budget` exceeds ctest's 180 s timeout before it can report (my ctest run timed out at 180.06 s with no result; the same test passed when run directly). The production budget is met; the gate is not reliable on a loaded or slower disk. The timeout or setup cost belongs to the lead/Codex (shared CMake), not to this review. The earlier "dense 180 s timeout" was this mechanism, not a publication regression.
2. **Unresolved.** One earlier full Windows run of this candidate hit a `folder_grid` 299.99 s timeout while another lane's full ctest was running concurrently, and the run was then cut at my own 10-minute tool limit before `fs_watch`. This is load-confounded and not evidence either way; folder_grid must pass in a quiet full gate.

## Limits
Concurrent lane activity means no host isolation is claimed. No physical-hardware or clean-machine claims. First targeted run exited 0xc0000135 (my environment lacked the Qt bin directory on PATH, which showed a system dialog); corrected and rerun.

## Addendum: folder_grid rerun
Finding 2 is resolved for Windows: `folder_grid` alone on `4a86528` (no other Windows test lane running; Codex's Docker build was running concurrently) passed in 168.70 s, `ctest -R ^folder_grid$` exit 0. The earlier 299.99 s timeout was load from another lane's full ctest. 168.7 s is still more than half of the 300 s limit, so the margin is load-sensitive. Linux remains unrun by this reviewer; Codex's concurrent Docker gate is the Linux evidence to collect.
