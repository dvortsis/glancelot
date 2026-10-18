# GRID-01 verification — virtualized folder grid

Scope: Windows only. Requirements IN-02, UX-02, PERF-05, SYS-02.

## What landed
- `ui/PhotoGrid.qml`: GridView over the existing `FilmstripModel`; the model is `null` while the grid is hidden, so delegates and thumbnail requests disappear with it.
- Browsing cursor (`selectedIndex`) is separate from the opened photo. Arrows/Home/End/PageUp/PageDown move it; Enter, double-click or "Open selected" open it; single click only selects; Escape or "Back to photo" returns to the canvas.
- Grid navigation keys use `Keys.onShortcutOverride`, so a remapped window shortcut cannot steal them while the grid has focus. While in grid mode only `open, settings, palette, quit, grid, tree, fullscreen, frameless` are accepted; canvas commands are ignored.
- Columns are capped at 12 and cells are at least 160 px tall, which bounds delegates and thumbnail requests below the 256-entry service queue on very large displays.
- Activating the already-open photo does not reopen it. Status messages show in the grid's bottom bar. Hiding a focused folder tree returns focus to the visible content.
- `--show-grid`, `--window-size` and grid/thumbnail counters in `--report` support `scripts/verify-folder-grid.py`.

## Evidence
- Integrated `main` (`scripts/build.ps1 -Test -Package`): 8/8 CTest targets pass, including `folder_grid` (7 cases: 10000-row virtualization, hidden-grid release, keyboard and remap, modal focus, activation and click, remapped-shortcut scope, reveal of the current item, resize/tree/theme/empty).
- `python scripts/verify-folder-grid.py build\glancelot.exe <out>`: compact dark/light, wide dark and a 10,001-file folder all PASS (live cells 6/6/32/32; active workers 0; pending 0; cache ≤ 64 MiB; sources unchanged).
- Portable smoke with Qt removed from PATH: packaged `glancelot.exe --show-grid` exits 0 and reports the grid visible with 5 cells.
- Independent review (separate agent, adversarial probes and 12 QML mutations): APPROVE WITH FIXES. Fixes applied and covered by new tests: shortcut scope (major), delegate/request growth with window area (major), reveal-assertion gap (major), grid status feedback, palette bypass, focus after hiding the tree, redundant reopen, badge size. Fix verification was by the new tests and native script; the reviewer did not re-run after the initial fixes. REVIEW-ALL-01 subsequently obtained an independent full-suite and native-script rerun; see CLAUDE-AUDIT.md.

## Known limits
- Linux GUI and physical displays, screen readers, touch and native FileDialog-from-grid timing are unverified.
- The earlier tall-window queue overflow was reproduced and fixed by REVIEW-ALL-01: the independent 7680x5890 test now observes 132 cells. Physical display qualification remains open.
- A background change of the viewed photo (e.g. deletion) moves the cursor to the new current photo.
- Thumbnails are requested at 256 px, which is soft at device pixel ratios of 2 or more.
- Hover and selection share a fill; only the border distinguishes selection.
