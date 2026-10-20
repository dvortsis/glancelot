# Bug, UX, performance and visual audit of main 6cc87ee (Claude lane, read-only)

Pinned commit `6cc87ee`, worktree `claude-audit-ux`, build `build-claude-audit-ux`. No product, test, queue or lock file was
changed. Windows `scripts/build.ps1 -Test` on this commit: 29 of 29 passed (229 s). Design basis: ui-design-skills
(design-review, application-interface-design; layout, typography and geometry applied by inspection of screenshots).

## Method and limits
- Disposable media (7 files: landscape 4000x2667, portrait, 32x32 PNG, 12000x2000 panorama, 12000x8000, a 14-byte "corrupt.jpg",
  an 80-character file name) opened at 540x360, 900x600 and 1920x1080 with `--no-resume --screenshot` (offscreen, software backend).
- Static review of GUI-thread file access, decoding paths and thread-pool use in `src/app`.
- **Not verified:** the GPU canvas (the software backend shows "No QRhi found", so the photo area renders blank in every
  screenshot even though the status bar reports the decoded size and zoom), real DPI/scaling, colour accuracy, keyboard
  and pointer input on hardware, screen readers. Timings were not used: other agents were building on the same machine.

## Findings (ranked)

### Major
1. **Animation decoding breaks the "UI never decodes" and bounded-allocation rules** (`src/app/animation_controller.cpp`).
   `jumpToFrame` decodes missing frames with `QImageReader` on the GUI thread (line 203). `load()` decodes in-process on
   `QThreadPool::globalInstance()` (line 282), not in the sandboxed decoder worker, and pre-decodes up to 64 frames with no
   pixel or byte cap (lines 303-316). Large animated WebP/APNG/GIF files can therefore stall the UI and allocate
   frames x width x height x 4 bytes. Fix: decode frames in the worker or a bounded service, cap total frame bytes, and never
   decode in `jumpToFrame`.
2. **A corrupt or non-image file is reported as an unsupported format** (`src/worker/main.cpp:222`). `corrupt.jpg`
   (junk bytes) shows "This alpha supports JPEG, PNG, GIF, WebP, TIFF, BMP, and TGA images." even though the file is named
   .jpg. Say the file could not be read or is damaged, and keep the format list for genuinely unknown formats. See
   `docs/verification/audit-ux/h.png`.
3. **Images over 64 megapixels cannot be viewed at all** (`src/worker/main.cpp:226`, 256 MiB RGBA cap). The 12000x8000
   file shows "Image exceeds the 256 MiB decoded-image limit." with no fallback. The message is honest, but 60-100 MP
   cameras exist. Consider decoding at reduced scale for oversize files. See `g.png`.

### Moderate
4. **Synchronous filesystem calls on the GUI thread in `MutationService`** (`mutation_service.cpp:117-242`): `renamePath`,
   `validateTargetName` and `createFolder` return a value synchronously and call `QFileInfo::exists`, `QDir::exists`
   and `QFile::rename`. On a slow or unavailable network share the UI freezes. Also `AnimationController::load` calls
   `QFileInfo::exists` on the GUI thread (line 272) and `ViewerController::observe` calls `isDir` (line 617, superseded
   by FS-WATCH-01 if that merges).
5. **Paused copy/move jobs hold a global thread-pool thread** (`copy_move_service.cpp:331,454`, `msleep(50)` loops on
   `QThreadPool::globalInstance()`). The animation loader shares that pool, so several paused batches can starve it.
6. **Persistent developer wording in the status toast** (`viewer_controller.cpp:632,712`): "Untagged source assumed sRGB .
   display profile not yet managed" shows for every untagged image. It is jargon and noise for most users. Show it only
   on request (Info) or reword.
7. **A failed open leaves a blank canvas with only a transient toast** (`h.png`, `g.png`). There is no lasting error state
   in the photo area explaining what failed and what to do.

### Minor
8. At the 540x360 minimum the filmstrip takes about 112 px of 360 px and the photo area about 152 px (`a.png`). Consider
   hiding or shrinking the filmstrip below roughly 400 px of height.
9. The worker messages at `worker/main.cpp:179,222,226` are plain English strings that bypass the translation contexts.

### Seen working
- No overlap or clipping at 540x360, 900x600 and 1920x1080. The 80-character file name is middle-ellipsised.
- Controls that do not apply (zoom, fit, rotate) are disabled when no image is shown. Top bar collapses to a compact
  Photo control at the minimum width.

## Suggested routing
Items 1, 4, 5 and 9 are engine/service work for the Codex lane; 2, 3 and 7 touch the worker and `ui/Main.qml`
(Claude owns the QML packet); 6 and 8 are Claude UI. Nothing here was fixed in this audit.

---

# Addendum: second test pass (goal "test anything not yet tested")

Same pinned commit `6cc87ee`. A throwaway QtTest (`docs/verification/audit-ux/audit_ui_tests.cpp.txt`; needs a CMake target,
not added because root CMake is lead-owned) loaded the real `ui/Main.qml` and swept 3 view modes x 3 window sizes plus text
scale 1.5 and 2.0 (12 matrix cases) and 5 dialogs x 2 sizes. 15 of 15 cases ran with no failure; they check nothing by
themselves, so the findings below come from screenshots and a scene probe (items outside the window, clipped text).
Probe hits for grid rows below the viewport and the pane-resize handle at the window edge were false positives
(virtualised rows, intentional edge handle). Software backend: the photo canvas does not render.

## New findings
10. **Moderate - Shortcuts dialog footer text collides at 540x360** (`dialog-shortcuts-help-540x360.png`): the hint
    "Press ? ..." overlaps "Customize shortcuts in Settings..." and the result list shows only three rows.
11. **Moderate - "Arrange panes" overlay labels overlap and truncate at 540x360** (`dialog-arrange-panes-540x360.png`):
    "Top toolbar . move" covers the logo and Open button; "Image preview . ..." and "Status toolbar . m..." are cut off.
12. **Minor - Settings tab strip is cut mid-label at 540x360** (`dialog-palette-540x360.png`, which opens Settings > Controls):
    the first tab shows as "ce" with no scroll cue.
13. **Moderate (dev-visible) - `ShortcutCheatSheet.qml:327: ReferenceError: keys is not defined`** fires 814 times across
    the 15 cases (about 40 per window creation). Together with `PhotoGrid.qml:298: Unable to assign [undefined] to int`
    (22 times) it means a binding evaluates before its data exists. Both are pre-existing (also seen in the R9 FS-WATCH
    review). The cheat sheet is the screen where item 10 appears, so check them together.
14. **Minor - at text scale 1.5 and 2.0 the top bar is tight**: "Pho...jpg" is truncated and the view switcher stays
    readable, but there is no room left (`grid-900x600-def-def-s2.png`, `both-540x360-def-def-s1.5.png`). Nothing overlaps.
15. **Minor - "Viewing" badge covers part of the thumbnail** at larger text scales
    (`both-540x360-def-def-s1.5.png`).

## Code and startup robustness (no defects unless listed)
- **Decoder worker fuzz (probe phase):** 155 inputs (24 random mutations each of JPEG, PNG, GIF, BMP and TIFF, truncations at
  4/16/64/200 bytes, 0xFF headers, empty and random files). 97 accepted at probe, 58 rejected, 0 crashes, 0 hangs
  (4 s limit). The pixel-transfer phase was not fuzzed (it needs the parent's shared-memory handshake).
- **Command line:** missing file, directory, empty file, 300-character path, non-ASCII name, two files, unknown option
  (exit 1) and non-numeric/negative `--quit-after` (exit 2) all behave sensibly.
16. **Minor - `--window-size 99999x99999` hangs for more than 20 s** (and ignores `--quit-after`). The size is not clamped. This
    is a test-automation option only.
17. **Moderate - an unreachable UNC path stalls startup**: `\\nonexistent-host\share\a.jpg` with `--quit-after 2500`
    took 8.9 s and exited 3, so roughly 6 s of the stall is on the startup thread. This matches finding 4 (filesystem calls
    on the GUI thread) and matters for network shares.

Seen working: Move-to-Recycle-Bin dialog (`dialog-delete-current-file-540x360.png`) is clear at the minimum size, with a
destructive-styled primary action, a Cancel button and the file name shown.

## Theme catalog contrast (all 40 families, dark and light, computed from `assets/themes/catalog.json`)
- Text pairs pass in all 80 palettes: text/background and text/surface, muted/background and muted/surface at 4.5:1 or better,
  onPrimary/primary at 4.5:1, primary/background and danger/surface at 3:1.
18. **Minor - light-theme borders are very faint**: border/background is only 1.18 to 1.3:1 in every one of the 40 light
    palettes (dark palettes are above 1.5:1). WCAG 1.4.11 asks 3:1 for the boundary of a control when the border is what
    identifies it (inputs, outlined buttons). Whether that applies depends on whether fills or labels also mark the control,
    so this is a design call; the screenshots were all taken in the default dark theme, so light-theme layouts were not viewed.

## Third pass: light themes, full decoder protocol, startup traces
- **Light and dark themes rendered** (5 families x light/dark x grid/photo = 20 screenshots; two committed). The layouts hold in
  light themes: no overlap, readable text, "Viewing" badge and the selected cell are clear
  (`theme-parchment-light-grid.png`, `theme-slate-ice-light-photo.png`). The theme-mode switch applied in all 20 cases.
19. **Minor - "Preview unavailable" placeholder colour ignores the theme family**: it is a fixed cool blue-grey, so on warm
    families (parchment, amber) the tiles clash with the palette. Take it from a theme token.
20. **Minor - filmstrip labels break mid-word** when no thumbnail is available ("Photo 0.j / pg" in
    `theme-slate-ice-light-photo.png`). Elide or break at word boundaries.
- **Decoder worker, full protocol fuzz** (probe, shared-memory handshake, decoded, ok): 400 mutated, truncated, header-corrupted and
  byte-inserted PNG/JPEG/BMP/TIFF files, 42 decoded end to end, 358 rejected cleanly, 0 crashes, 0 hangs (8 s limit).
  GIF was not seeded because this Qt build cannot write GIF.
- **`--window-size` trace:** cost scales with window area. 20000x20000 takes about 1 s, 32768x32768 1.6 s, 50000x50000 3.5 s
  and 99999x99999 exceeds 25 s with no file (22.9 s and exit 3 with a file). With a file and a window of 32768x32768 or more the
  load is still pending at the timed quit (exit code 3 means "still loading"). Only the software backend was tried.
  Clamp the size to the screen.
- **UNC trace (refines finding 17):** `\\nonexistent-host\share` as a directory argument exits in 0.9 s, but the file form
  `\\nonexistent-host\share\a.jpg` takes 8.7 s and exits 3 (load still pending at the 0.5 s timed quit). `app/main.cpp:129`
  calls `QFileInfo::exists(filePath)` synchronously before `viewer.open`. I could not separate time spent before the window
  from time spent at shutdown.

## Suite re-run on the audit branch (`claude/audit-ux`, only docs differ from main 6cc87ee)
- Windows `scripts/build.ps1 -Test`: first run 28 of 29, with `folder_grid` timing out at 300 s (it normally takes about 150 s). A
  second run alone took 156 s and passed (74 passed, 4 skipped), and a full second run passed 29 of 29 in 240 s
  (`folder_grid` 153.7 s). No stray processes were left from my startup tests. The cause of the single timeout is not
  established: machine load from other agents is the likely explanation, but the rare UI hang seen earlier in this project
  cannot be ruled out from one occurrence.
