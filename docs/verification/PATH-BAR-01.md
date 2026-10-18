# PATH-BAR-01 verification

Implementation merged into main at `60ebe87`, final branch `e0bca53`. Adds a native editable current-folder field above the tree, full-path Copy, paste/Enter navigation, Escape restoration/cancellation and inline errors. Exact visits include empty folders, synchronize tree/grid/filmstrip and clear the old image safely. Empty locations persist in enabled window slots. Text editing suppresses image command shortcuts.

Independent Bugbot-style review found and verified fixes for early address cancellation/rejection interrupting image scans, empty-folder session persistence, and first-signal snapshot coherence. The original clipboard test was racing deferred tree focus; it now waits for the initial tree focus before editing. Screenshot export is opt-in so functional tests support read-only source mounts.

## Evidence

- Final Windows branch: `scripts/build.ps1 -BuildDir build-path-bar -QtRoot C:/src/Github/Glancelot/.tools/Qt/6.8.3/msvc2022_64 -Test`: 8/8 passed, 69.34s.
- Independent Windows `7067365`: 8/8 passed, 69.93s; native editing test 3/3 in 1.044s; controller tests 3/3 in 1.008s. Final `e0bca53` changes only optional screenshot export.
- Final Linux generated core navigation tests passed. Controller exact-directory/error/cancellation/persistence/race regression: 3/3 in 820ms. Native-QML keyboard/clipboard regression: 3/3 in 959ms using offscreen software scenegraph. This verifies interaction logic, not GPU output; QRhi warnings are expected in that backend.
- Independent native compact/wide dark/light screenshots reviewed: address field and Copy control fit; no blocking path-bar visual defect.
- Integrated main checks passed 8/8 in 60.04s using scripts/build.ps1 -BuildDir build -Test. Task is done after independent review and integrated checks.

Tests cover Unicode/spaces, empty and image folders, invalid/missing/file targets, no navigation merely on paste, exact folder scope, stale visits, first-signal coherence, early image-open cancellation/failure, previous-folder races, Escape, copy and text command precedence. Existing natural ordering and folder traversal tests remain green.

Real disconnected UNC shares, physical mixed DPI and live assistive technology remain unverified. Linux tests use generated local paths; no physical/network performance claim follows from them. Very long network paths need additional platform fixtures before claiming exhaustive qualification.
