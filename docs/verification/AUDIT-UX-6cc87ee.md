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
