# Phase 1 integration and application audit

Audit date: October 18, 2026. Integrated baseline `8eb2d2f`; navigation follow-up `015b664` is under independent review. This record supplements UI-POLISH-01.md; it does not qualify untested hardware or future roadmap features.

## Scope and method

The requested specialized Bugbot service was unavailable. An independent reviewer performed a Bugbot-style source review, reproduced shortcut failures with Qt, built and tested separately, and inspected the refreshed native captures. Native Qt/QML UI design principles were applied; no web framework was introduced.

The visual/interaction inventory covers the empty state, image canvas, filmstrip, pinned/overlay tree, thumbnail grid, top and bottom bars, status feedback, unified Settings (Appearance, Viewing, Browsing, Controls and About), and every Settings choice menu (theme family, theme mode, fit, wheel and folder boundary). Fifty-two refreshed captures exercise representative dark/light themes at compact and wide sizes, with actual branding and canvas shaders in the test executable. Theme catalog token checks cover the remaining variants. Keyboard tests cover palette search and remapping, palette cursor scrolling, dismissal, tree Enter/chevrons, grid focus, wheel modifiers and focused toolbar visibility.

The actual Windows Open dialog and file-type menu were inspected through Win32 controls: cancel, accepted generated image, 48x32 decoded dimensions and unchanged source hash passed. Source evidence is in ignored `.cache/native-dialog/` in the phase1-audit worktree; generated media is disposable.

## Findings and resolutions

- Reserved Escape shortcuts could disable dismissal; exact and chord-prefix conflicts could make bindings ambiguous. Reject these conflicts during editing, loading and concurrent settings merges; explicit user bindings win over new defaults. Acceptance tests cover the failure modes.
- Cached thumbnails could survive edits made while a different folder was watched. Folder changes now invalidate the thumbnail generation while same-folder stepping preserves reuse.
- Palette keyboard movement could leave the selected command outside the viewport. Reveal the keyboard cursor in the enclosing settings scroll area.
- Shift+wheel did not override configured base zoom behavior. Modifier precedence is now explicit and tested.
- Auto-hide could remove keyboard-focused toolbar controls. Focus descendants keep the HUD visible.
- Grid mode exposed an ineffective backdrop action. Disable it with a reason.
- The UI test executable omitted real shaders and branding. Include the production resources so captures exercise the native canvas.
- Linux exposed a child-folder publication race: filePath could identify the child while the tree still marked the parent current. Populate the worker Navigator before publishing the destination, preserving an existing traversal root for child visits. Suppress publication if reopening the found image fails. The regression observes the first stateChanged exposing the destination. Keyboard tree actions also query the model independently of virtualized delegate lifetime.
- Removed an accidentally tracked temporary-settings artifact named `{ not json`.

## Verification evidence

Windows baseline/integration `8eb2d2f`: `scripts/build.ps1 -BuildDir build -Test -Package`, 8/8 tests passed in 59.21s. The portable package and ZIP were refreshed at that revision. Independent reviewer: 8/8 in 61.27s and targeted native cases 6 passed in 18.165s. Lead native capture run: 6 passed in 63.552s, 52 captures.

Real viewer grid verification passed compact dark/light and wide cases, plus 10,001 entries with 33 live cells. All 10,001 original source hashes remained unchanged. Thumbnail limits remained two workers, 256 queued requests and 64 MiB cache.

Follow-up `015b664`: Windows full suite 8/8 passed in 73.30s using the explicit project-local QtRoot. Initial worktree invocation omitted that runtime path and failed DLL lookup; it is not application failure evidence. Linux targeted folder-publication regression: 3 passed in 181ms; tree activation plus resize: 4 passed in 2.479s. Full Linux suite: 8/8 passed in 54.20s in the isolated diagnostic build. The production Docker build with its unchanged full-test gate remains pending in this record.

## Limits and unresolved evidence

The initial Linux delivery gate failed tree Enter and once crashed in resizeTreeAndEmpty. The tree failure now has a reproduced cause and targeted passing fix. The separate crash has no captured backtrace and did not recur in repeated full/debugger runs or the independent ASAN run (leak detection disabled). Do not label its cause resolved or attribute it to Qt without evidence. Software-scenegraph tests warn that QQuickRhiItem has no QRhi; these tests do not prove GPU rendering. Actual native captures exercise the RHI path separately.

Physical multi-monitor, per-monitor ICC/HDR, hardware-specific GPU recovery, Linux Wayland/X11 desktop interaction and live screen-reader operation remain unverified. Second-monitor support is a roadmap item, not shipped functionality. Qt packaging reports unavailable translations from the developer distribution. Browser delivery and final refreshed native package must be rechecked after the follow-up is integrated.
