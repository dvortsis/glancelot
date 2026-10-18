# TREE-01 — collapsible lazy folder tree (M2.1, UX-03)

Integrated at 4a23cf1 (contracts 1cc844e). Windows evidence; Linux GUI, real SMB shares and physical displays are unverified. Favorites and the grid stay in M3.

## What exists

- Core (`src/core/navigation.cpp`): `childFolders()` lists one level in the traversal's natural order with the same link/volume policy, distinguishes unreadable from empty, honors cancellation and stops at a 20,000-entry limit (`truncated`); `firstImageInSubtree()` is a budgeted (500 folders), cancellable preorder search used by folder selection.
- `FolderTreeModel`: flattened tree, one level per expansion on a 2-thread pool, per-node sequence plus root epoch so stale or cancelled listings never apply, caps of 2,000 children per parent and 20,000 rows, current-folder reveal that expands only the ancestors of the current folder, Windows case-insensitive keys, teardown waits at most about one second and then abandons a wedged listing.
- `ViewerController`: `openFolder()` (newest wins, cancels the previous search, rejects folders outside the browsing root, honest messages for empty / unreadable / budget-exhausted), explicit `raiseRoot()`, persisted `tree` and `treePinned`, remappable `tree` command (default Ctrl+B; a stored binding on that key wins and leaves the new command unassigned).
- `ui/FolderTree.qml` + `Main.qml`: left HUD under the top bar/filmstrip, Folders button, Shift+Tab route, `treeShown` focus handoff, keyboard (Up/Down/Home/End/Left/Right/Enter/Space/Escape, window shortcuts suppressed while it has focus), pin (canvas inset only at width >= 720), chevron (QtQuick.Shapes, 150 ms rotation), states in words (`empty`, `no subfolders`, `no access`, `partial`), model detached while hidden so a hidden panel does no work.

## Evidence

- `scripts/build.ps1 -Test -Package` on main 4a23cf1: six CTest targets passed (navigation 1.32 s, viewer_acceptance 3.90 s, thumbnails 5.28 s, folder_tree 4.61 s, viewport 0.04 s, themes 0.10 s). `folder_tree_tests`: 22 cases including natural order, laziness, collapse/re-expand, empty and inaccessible states, reveal, stale loads with cooperative and non-cooperative listers, collapse-while-loading cancellation, truncation (cap and flag), teardown with a cooperative and a wedged lister, root raise limits, Windows case-insensitivity, `openFolder` subtree/none/outside-root/unreadable/500-folder budget/newest-wins, `raiseRoot`, settings persistence and binding collision.
- Core `navigation_tests` (including child listing, limit/truncation, link exclusion and the budgeted subtree search) also pass on Ubuntu 24.04 under WSL with g++ 13, including real permission denial (`child-folder permission-denial enforcement=1`) and symlinks.
- Independent review (separate agent) built the branch and drove harnesses against the real QML and model. First pass: APPROVE WITH FIXES with one blocker (chevron click also navigated), majors (unbounded `openFolder`, GUI-thread teardown freeze on a wedged listing, unthemed current-row tint caused by reading `.r/.g/.b` on a string, Escape ignored without a cursor, HUD hiding under button focus) and minors. All fixed; re-verification at e6af422: APPROVE (chevron clicks toggle only, `openFolder` on a 10,000-empty-folder directory ends in 864 ms with the budget message, a new open cancels it, teardown 1.0 s against a 6 s wedge, tint measured as `#2E4153` dark / `#E2E8EE` light, ctest 6/6, `folder_tree_tests` 5x clean, no QML warnings). Real ACL denial and a junction on Windows give `accessible=false`.
- UI reviewed under the repo's ui-design-skills (native QML, Slate & Ice tokens, 4 px radius kept): contrast on the real current-row tint 9.37:1 name / 5.70:1 muted (dark), 10.53:1 / 5.21:1 (light); 3 px current marker distinct from the 2 px text-colored keyboard ring; compact (540x360) and pinned/unpinned screenshots inspected.
- Portable `dist/Glancelot/bin/glancelot.exe` with Qt off PATH opens a nested folder with the pinned tree: exit 0, no stderr. Package SHA256 9EC1E2B3DB0EE5B9E26C4CE20928AB7F2575F12D0AD833C39737DA9A0BFE28DB.

## Known limits and follow-ups

- UNC/SMB paths: the traversal core's link check treats a `\\server\` prefix as a link, so shares show `no access` (pre-existing policy in `safePath()`); needs a real-share qualification.
- Hidden and system folders at drive roots are listed; two rapid `raiseRoot` calls raise one level (the second is computed from the stale root).
- The default root is the opened file's folder, so a leaf folder shows one row until the user chooses Parent; raising is explicit by design.
- Live-window keyboard sessions of the Main.qml wiring (Shift+Tab, `treeShown` focus, button focus keeping the HUD open) were verified by reading and QTest harnesses with stub viewers, not by real focus in a full window.
- The controller's navigation thread pool still waits for running jobs on destruction (pre-existing); the tree model no longer does.

## Whole-code audit follow-up (October 17, 2026)

REVIEW-ALL-01 supersedes the UNC-prefix and controller-teardown limitations above: incomplete server prefixes are skipped during validation (real SMB remains unverified), and controller shutdown now gives navigation cancellation a bounded one-second grace. It also bounds outstanding tree jobs to two and repairs current-folder reveal after repeated completed Parent navigation. Independent tests passed; see [CLAUDE-AUDIT.md](CLAUDE-AUDIT.md). Rapid overlapping Parent requests still coalesce as documented above.
