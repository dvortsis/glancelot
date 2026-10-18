# Whole-code audit of Claude's Glancelot work

Review baseline: `main` at `f529d25`, plus the unintegrated SORT-01 commits `00bacab` and `0950710`. Review date: October 17, 2026. Repository state, source and test results are authoritative; earlier memory/handoff claims were checked against them.

## Scope and result

Reviewed the complete current implementation: navigation and sort rules; viewer async generations and decoder protocol/lifetime; worker image reading and limits; thumbnail scheduling/provider/model; tree loading/reveal; canvas transforms/rendering; all QML views and command/modal routes; settings/themes/window slots; build/verification scripts; tests and build plans. A separate reviewer covered UI/render/model/resource behavior and independently reviewed the backend fixes and sort integration. No claim is made that a review proves the absence of every defect.

The existing implementation is a useful Windows JPEG/PNG alpha. The earlier plans were stale: M2.1 and the grid were still described as future work, GRID-01 was marked done without an independent post-fix rerun, and SORT-01 existed only in a worktree. This audit fixes the defects below, reruns the grid gate, and brings the existing sort implementation through review and integration.

## Reproduced defects and corrections

1. **Settings backup failure could be ignored.** A directory at `settings.json.bak` caused backup creation to fail while the primary edit still returned success. Backups and `.damaged` evidence now use checked atomic replacements; failure preserves the primary. Output is rejected before any write if it exceeds the reader's 1 MiB bound.
2. **A stale slot counter could duplicate ownership.** With live slot 1 and counter 0, another claimant also obtained slot 1. Claims now respect the highest live slot. Unreadable, malformed, oversized or incompatible coordination is retained and refused; only a missing file starts empty. Session writes cannot exceed their own 64 KiB read limit.
3. **Tree cancellation left an unbounded worker queue.** One thousand collapse/re-expand cycles created 1,001 outstanding jobs behind a noncooperative lister. At most two futures/jobs now exist; pending requests are represented once per materialized node and are removed before submission when canceled. Actual running calls keep their slot until completion.
4. **Repeated Parent navigation lost the current-folder marker.** Two root raises while the viewed folder stayed unchanged left currentRow=-1. Root changes now trigger reveal even when the current folder key is unchanged.
5. **Grid size could exceed the thumbnail queue.** A 7680x5890 grid created 344 cells. Both rows and columns are now bounded; the independent test observes 132 cells at that size. Normal compact/wide layouts retain their existing geometry.
6. **Activating a failed current path could not retry it.** The previous no-op checked the requested path rather than successful decode state. The controller now tracks whether the current attempt succeeded and provides a separator/case-aware path comparison without filesystem access. Failed images retry; a valid current image avoids reopening, including Windows slash/case aliases.
7. **Sort persistence failure was hidden by a success message.** Session-only ordering is now reported when the shared settings writer is busy or unavailable. Sort worker generations, natural folder order/explicit landing, deleted anchors, stable ties, missing metadata and visible-order stepping are covered by core/controller tests.

8. **Concurrent sort edits could revert another window's preferences.** A stale window changing only direction replaced the entire sort object, resetting the sort key and dropping unknown nested fields. Sort updates now merge only edited fields under the shared writer lock. The independent regression failed before the fix and passes afterward, retaining the other window's key, direction, seed and unknown nested fields. Selecting shuffle also persists its seed with its mode.

Additional hardening: decoder stderr is discarded instead of accumulating; a failed pixel copy no longer destroys the previous good image; worker request limits are checked before accepting a newline; subtree-search pending storage is bounded as well as visited work; viewer teardown has a one-second navigation cancellation grace instead of waiting indefinitely on a blocked filesystem call. Incomplete UNC server-root validation was corrected, but actual SMB behavior remains unverified.

## Verification evidence

Author worktree full Windows run after the fixes:

```text
navigation 1.82s; viewer_acceptance 4.41s; thumbnails 5.59s;
folder_tree 4.78s; window_slots 23.06s; folder_grid 9.82s;
viewport 0.13s; themes 0.38s
100% tests passed out of 8; total 50.42s
```

Independent reviewer reruns passed all eight targets (41.43s, then 42.72s after the oversized-request regression). Direct Qt logs also exercise the actual new cases: tree reveal/queue, successful-current no-op, failed-current retry and the tall grid (132 cells). The final teardown-change reviewer rerun passed all eight targets in 38.47s on runtime `3dea7bf`; direct tree/grid/parser/persistence regressions also passed. The final sort fix at `bf7f0b8` was then independently approved: all 31 persistence cases and both sort UI cases passed.

Native Windows `verify-folder-grid.py` passed compact dark/light, wide and 10,001-image cases: 6/6/32/32 live cells, no queued/active work at capture, cache 76,000/76,000/441,600/441,600 bytes. All 10,001 source entries are compared by SHA256, including a malformed PNG and a long Unicode filename. The independent reviewer repeated this script and inspected the compact screenshots.

`verify-window-resume.py` passed five real-process checks: reverse close order/full restart, explicit-file override/unopened retention, delayed claim, final-flush ownership, and source byte identity.

Linux core navigation/sort tests compiled with Ubuntu GCC 13.3 and passed. Output:

```text
permission-denial enforcement=1
in-progress cancellation observed=1
link creation: directory=1 cycle=1 file=1
child-folder permission-denial enforcement=1
navigation: all generated fixture tests passed
```

Final integrated `main` runtime `bf7f0b8`: `scripts/build.ps1 -Test -Package` succeeded, all eight CTest targets passed in **38.51s** (navigation 1.50s, viewer 4.24s, thumbnails 5.43s, tree 4.87s, slots 18.48s, grid 3.60s, viewport 0.03s, themes 0.11s). The portable package passed native open/resume/grid checks with Qt development paths removed, zero stderr and unchanged generated source bytes. Packaging warned that the local Qt developer installation has no translations; this does not qualify localized delivery. Root logs: `.cache/audit/integrated-final.log` and `.cache/audit/portable-final.log`.

Local author artifacts: `.worktrees/audit/.cache/audit/`; independent artifacts: `.worktrees/audit-review/.cache/`. Use `scripts/build.ps1 -Test -Package` for the required integrated Windows gate. Qt GUI tests use an offscreen/software backend for interaction semantics; native screenshots use the Windows backend. Neither is physical display/color/HDR/performance qualification.

## Build-plan reconciliation and remaining work

BUILD-PLAN.md, FIRST-MILESTONE.md, AGENT-BUILD-PLAN.md and WORK-QUEUE.yaml now distinguish historical planning, implemented Windows slices, review/integration gates and ready follow-up tasks. The requirement matrix and original specification remain unchanged, including every acceptance criterion.

Remaining work includes Linux Qt GUI/package delivery, physical fractional-DPI/ICC/HDR and high-refresh qualification, reference-hardware performance/soak/accessibility, revision-validated decoded cache/lookahead, shared storage-aware admission, favorites, filters, richer input contexts, metadata HUDs and qualified format expansion. A small retained image cache is not evidence of a validated cache-hit path; two fixed thumbnail workers are not evidence of storage adaptation. Process separation is not yet an OS security sandbox.

Persistent settings I/O failure can prevent saves; transient position writes retry and final teardown makes a bounded attempt. Damaged `sessions.json` now disables position memory rather than guessing ownership. To recover that bookkeeping, close all viewer processes and set that file aside; keep `settings.json` to retain saved locations. Never replace coordination while another viewer is using it. Existing corrupt settings can still recover from a valid backup, or be set aside explicitly with `--reset-settings`.
