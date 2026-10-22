# Task Packet: ANIM-01 (F036 Animation & Multipage Controls)

**Milestone**: M3 / M5  
**Requirements**: `ADV-06`, `FMT-03`  
**Owner Lane**: Gemini (`agent/*`)  
**Branch**: `agent/anim-playback`  
**Worktree**: `.worktrees/anim-playback`  
**Build Directory**: `build-anim-playback`  
**Memory Action**: `act_mvg6kcvb_2fd302d57885`  

---

## 1. Objective & Scope

Task `ANIM-01` introduces comprehensive, bounded, and asynchronous multi-frame animation playback and multipage document navigation controls:
1. **Animated Formats (GIF, WebP, APNG)**:
   - Detect multi-frame sequences asynchronously off the UI thread.
   - Respect per-frame durations, disposal modes, and loop semantics.
   - Play/pause (`Space`), step forward (`.`), step backward (`,`), seek scrubber, speed control (0.25x - 2.0x+), and loop toggle.
2. **Multipage Documents (TIFF)**:
   - Recognize multi-page TIFF documents.
   - Honor requirement `ADV-06`: TIFF pages are NOT assigned invented playback timing or automated looping.
   - Provide clean page-stepping navigation (Next Page, Previous Page, Page Scrubber, `Page X / Y`).
3. **Ergonomic Native Floating HUD (`ui/AnimationBar.qml`)**:
   - Floating pill positioned at bottom center of canvas/preview pane.
   - Built with native `Theme.qml` tokens and keyboard/touch accessible buttons.
   - Auto-fades on idle and reveals on mouse proximity or active interaction.
4. **Resilience, Generational Safety & Memory Bounds**:
   - Background worker probes frame count and delays without blocking UI.
   - Pre-decoding and frame caching bounded to 64MB / 64 frames.
   - Every background decode task carries a generation stamp; results from obsolete image selections are cleanly dropped.

---

## 2. Shared Interfaces & Contracts

### `include/glancelot/animation_controller.hpp`
- `AnimationController : public QObject`
- Properties:
  - `bool active`: true if file has > 1 frames or pages.
  - `bool isAnimated`: true for GIF/WebP/APNG, false for multipage TIFF.
  - `bool playing`: true when playback timer is active.
  - `int currentFrame`: 0-indexed frame or page number.
  - `int frameCount`: total frames or pages.
  - `qreal speed`: playback speed multiplier (default 1.0, range 0.25x - 4.0x).
  - `bool loop`: whether playback repeats at the end.
  - `QImage frameImage`: current rendered frame bitmap.
  - `QString frameLabel`: formatted string (e.g. `Frame 14 / 48` or `Page 2 / 5`).
  - `int currentDelayMs`: frame duration in ms for the current frame.
- Methods:
  - `play()`, `pause()`, `togglePlayPause()`, `nextFrame()`, `previousFrame()`, `jumpToFrame(int)`, `setSpeed(qreal)`, `setLoop(bool)`, `load(QString, quint64)`, `clear()`.

### `include/glancelot/viewer_controller.hpp`
- Exposes `animationController` as a `Q_PROPERTY`.
- Integrates commands: `toggle-animation`, `animation-play`, `animation-pause`, `animation-next-frame`, `animation-previous-frame`, `animation-speed-up`, `animation-speed-down`, `animation-toggle-loop`.

---

## 3. Verification & Acceptance Criteria

- `tests/animation_tests.cpp` executes with:
  1. Multi-frame GIF playback, delays, and loop policy.
  2. Multi-frame WebP playback.
  3. Single-frame image behavior (`active == false`).
  4. Multipage TIFF document detection (`isAnimated == false`, page stepping).
  5. Speed scaling using monotonic timer intervals.
  6. Out-of-bounds seek and clamp safety.
  7. Stale generation discard when switching images rapidly.
- Full Windows native suite (`scripts/build.ps1 -Test`) and Linux Docker build pass.
