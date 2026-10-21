# INPUT and monitor correction round

User requested independent reviews, fixes and commits, then explicitly requested implementation of the missing monitor viewport/color/scale core. Both correction candidates are now independently source-accepted by actual Claude Sonnet 5.5. Product source remains on isolated Codex correction branches; documentation evidence is imported to main. Neither packet is marked done or integrated.

## INPUT-02

Candidate `claude/input-02` at `0cc2a38` registered the tree Trash row but still used a separate blocked-row predicate and exempted it from parity tests. Codex `953adf9` unified the predicate across keyboard/menu/explicit routes, retained root protection and exact targets, and restored strict parity tests. Test-only `330d050` waits for both asynchronous failure delegates while preserving exact count and required messages.

Branch `codex/input-02-r3`, evidence tip `2e1585d`; worktree `.worktrees/codex-input-02-r3`. Sonnet accepted source and an explicit follow-up read all nine required native UI skills and accepted the UI. Focused input regressions: 5 passed. Final Linux Docker: 15/15, 136.64 seconds, exit 0. Windows full: 14/15, 245.88 seconds, exit 1; the older branch still fails the previously recorded invalid-path terminal assertion. Awaited direct output confirms `openTerminal("C:/nonexistent_dir_path_12345")` returned true. No Windows qualification waiver.

[Review, commands and actual outputs](../verification/INPUT-02-0cc2a38-CODEX-REVIEW.md). Earlier failed/canceled runs and unedited reviews remain archived.

## MONITOR-OUTPUT-01

Gemini candidate `0e32d16`, source `27bc47c`, fixed four concrete defects but deferred core viewport/color/scale contrary to main's retained acceptance. User explicitly assigned that missing core to Codex. Lead contract and amendments are in [MONITOR-OUTPUT-01-CORE-CORRECTION.md](../MONITOR-OUTPUT-01-CORE-CORRECTION.md).

Final product/test source: `9b03718a59533bacc7b1f66b1536af409ffa3d28`, branch `codex/monitor-output-r2`, evidence tip `6b54375`; worktree `.worktrees/codex-monitor-output-r2`. Per-display viewport defaults/restoration, physical zoom, linked/independent comparison, keyboard/pointer commands and bounded worker-generated SDR color lookup are implemented. Mirror/Pinned gives the primary the full 256 MiB texture budget; comparison splits it. Closed navigation keeps no previous-image history. No full-size per-output color raster or duplicate decode is created.

Actual Sonnet review first required display defaults, mode-aware texture admission and closed history release. Those were fixed; confirmation session `9b6ceafa-6ec9-4f52-8be0-f2bc7c5b3670` accepted with no required changes and read all nine UI skills.

Fresh full gates on the accepted source, through one main gate-lock block:

```text
Windows: 100% tests passed, 0 tests failed out of 33
Total Test time (real) = 450.91 sec
exit=0
Linux Docker: 100% tests passed, 0 tests failed out of 33
Total Test time (real) = 232.74 sec
exit=0
```

Targeted suite: 40 passed, 0 failed, two native opt-in skips. Separate native tests: 4 passed; generated Display-P3 shader patch matched Qt's reference, first-visit defaults and exact saved viewport restoration passed across attached DPR 1 and 2 displays. Windows profile reported `P27h-20`. These do not prove physical ICC calibration, HDR or hotplug/suspend/GPU-loss recovery. Other platforms currently report an explicit sRGB fallback when a native profile adapter is unavailable.

[Full review](../verification/MONITOR-OUTPUT-01-0e32d16-CODEX-REVIEW.md), [raw evidence and command index](../verification/monitor-output-r2/README.md). Failed/interrupted authoring and qualification attempts are retained. Forty-eight archived evidence files were checked byte-identical to staged Git blobs before the evidence commit.

## Next integration work

Synchronize the accepted INPUT correction with current main/rehearsal, resolve conflicts in its lane, obtain confirmation and run integrated Windows/Docker gates. Propagate the INPUT fixes through the stacked Claude packets before final review counts. Then integrate monitor serially under the integration lock, with current-main synchronization and fresh integrated checks. Branch acceptance above does not replace these checks. STARTUP-WINDOW-01 remains the separate ready Codex packet for SAFE's offscreen-startup blocker.

No requested correction remains uncommitted. The integration lock is released in the closeout commit; the machine gate lock is free. No product-source merge occurred this round.
