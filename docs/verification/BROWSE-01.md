# BROWSE-01 — lazy filmstrip and worker-backed thumbnails

Integrated at 3c197a7 (contracts aa35ee2; implementation, code-review fixes, UI polish and design-review fixes rebased onto main). Covers UX-02 (top HUD filmstrip) and the first slice of PERF-05/SYS-02 for thumbnails. Windows only; Linux GUI, physical displays and large-folder/HDD/SMB performance are unverified.

## What exists

- `glancelot-decoder --thumbnail-server`: persistent worker, JSON request line in, JSON header plus raw RGBA out. JPEG/PNG only, edge clamped to 16-256, decode scaled in stored orientation then EXIF-transformed, stored size and mtime captured before opening and compared after decoding, source closed before reply, stdin/stdout binary on Windows.
- `ThumbnailService`: at most 256 queued+active requests, at most 2 workers, per-job deadline, 64 MiB memory-only cache, epoch invalidation, cancel, worker replacement on crash/hang/protocol violation, idle worker shutdown after 30 s, stderr discarded. Thumbnails are never written to disk.
- `ThumbnailProvider` (async image provider, response moves to the GUI thread), `FilmstripModel` (resets only when the folder listing changes), `ui/Filmstrip.qml` (virtualized; no model while hidden so a hidden strip decodes nothing), command/setting `filmstrip` (default `T`, persisted), HUD stays open while the strip is hovered or focused, Tab from the canvas reveals and focuses it.

## Evidence

- `scripts/build.ps1 -Test -Package` on main 3c197a7: five CTest targets passed (navigation 0.60 s, viewer_acceptance 3.87 s, thumbnails 4.87 s, viewport 0.04 s, themes 0.07 s). `thumbnail_tests` has 33 cases: bounded sizes/aspect, no upscaling, all eight EXIF orientations, malformed/missing files then recovery, queue bound, cancellation, stale epochs (failed and successful results), cache until epoch change, killed worker, eight misbehaving-worker modes (hang, garbage, bad geometry, oversize, wrong id, truncated, flood without newline, crash), source directory unchanged, missing worker program, shutdown leaves no processes, model reset behavior, id round trip, provider deliver/cancel/invalid.
- Independent reviewer (separate agent, did not write the code) rebuilt and found: dead file-changed check (reproduced 0/5 detections; now 5/5 for the thumbnail path, same fix applied to the full-decode path), hidden strip still decoding (2 workers with `filmstrip:false`; now 0, and 2 with `true`), HUD auto-hiding under keyboard focus, plus minors. Re-verification at 0a1a44b: APPROVE; thumbnail_tests 5x clean, ctest 5/5, QTest-driven Tab/focus harness in an offscreen window, controller binding-collision harness.
- Portable `dist/Glancelot/bin/glancelot.exe` with Qt removed from PATH opens a generated 61-file folder and shows the strip; exit 0, no stderr. Final package SHA256 34719972BE4742A4B09286FBCA95FF67BDB68660DF18029ACA3D45B7859630F9 (portable smoke was run on 2b8dc3e; later commits changed only Filmstrip.qml).
- Design review under the repo-mandated ui-design-skills (applied to native QML, Slate & Ice tokens and 4 px radius kept): found focus/current ring ambiguity, focus decoupled from current photo after arrow navigation, weak strip edge, edge clipping, ScrollBar overlap, no failure cue. All fixed; re-verified with a QTest key-driven harness (focus follows the current photo; Tab/Return) and screenshots in dark/light and at 540x360. The outset focus ring clip found in re-verification was fixed in 3c197a7 and checked by screenshot only.

## Known limits and follow-ups

- No automated test for the file-changed-during-decode check (needs a deterministic slow-decode hook); verified by a manual append-during-decode repro.
- Transient failures (queue full, timeout) are not retried and failures are not negatively cached; each directory-watcher event bumps the epoch, so visible thumbnails reload.
- Worker count is fixed at 2 with no storage-class awareness, user override, or admission against the current-image decode (PERF-05 is only partly met). Workers have no OS sandbox, same as the existing decoder.
- Main.qml keyboard wiring (HUD timer plus Tab) was exercised by reading and by a QTest harness with a stub canvas, not by a live full-window keyboard session.
- Filmstrip scroll centers instantly (no scroll animation); the 150 ms thumbnail fade does not yet honor a reduced-motion preference because Qt exposes none here.
