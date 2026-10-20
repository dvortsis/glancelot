# MONITOR-OUTPUT-01 independent review (Claude lane)

Reviewed main 76b1979 (impl 58c5a59). Reviewer: Claude lane. Verdict: **CHANGES REQUIRED**.

Files read: include/glancelot/display_manager.hpp, src/app/display_manager.cpp, src/render/presentation_window.{hpp,cpp}, tests/monitor_output_tests.cpp, ViewerController presentation hooks and command table. No QML was touched, so the ui-design-skills lens applies only to the (code-only) frameless black presentation surface, which is acceptable.

## Findings (ranked)

1. HIGH - Identity is not hardware-derived; "native display capabilities" are not delivered. `computeDisplayId` hashes Qt `QScreen` manufacturer/model/serial/physicalSize (SHA-256, truncated to 16 hex). Those are Qt fields, which on Windows are frequently empty or generic; then the fallback hashes `name` + geometry WxH + physical size. `name` on Windows is `\\.\DISPLAYn` and changes with topology; the fallback is therefore NOT stable across reconnect or reorder, and two identical monitors without serials collide into one id. The hash is real SHA-256 but of invented/Qt-derived strings, with no EDID or DXGI/Win32 device-path source. `colorSpace` is hardcoded "sRGB" and `colorDepth` is `QScreen::depth()`: no native color/HDR/capability data. Unverified-on-hardware and likely wrong for COLOR-01.
2. HIGH - Output-specific viewport/color/scale is absent. Presentation window uses a fixed fit mode (`m_fitMode`), no per-output zoom/pan, no per-output color profile or scale; `setFitMode` has no caller. Queue acceptance item not met.
3. HIGH - Bounded aggregate GPU/CPU ownership not enforced. The presentation window owns two extra `ImageCanvas` objects, each with its own 256 MiB texture budget (`image_canvas.cpp` textureBudget); the controller canvas has another. `DisplayManager` holds three full `QImage` references (current, pinned, comparison). QImage is implicitly shared so CPU pixels are not duplicated while shared, but pinned/comparison images pin decoded memory with no cap, and the GPU upload is per canvas (duplicated texture per window). No aggregate budget, no low-memory release, no test. PERF-04 not demonstrated.
4. HIGH - Comparison mode is only half implemented. `presentation-compare` merely sets mode; nothing calls `setComparisonImage`, so the secondary pane shows the pinned image or blank. `setComparisonImage` ignores its generation (no stale check; `comparisonGeneration` is stored but never compared). Layout is a fixed 50/50 split with no synchronized zoom/pan. The test only checks canvas image sizes after calling the setter directly, not the command path.
5. MEDIUM - Disconnect/hotplug is coded (`screenRemoved` closes presentation if `presentationScreen == screen`) but untested; offscreen tests have one screen and never emit screenRemoved. Suspend/resume, GPU loss, device-lost and low-memory handling are not implemented or tested at all. Claims in the implementer doc for these are unsupported. `screenAdded` does not re-bind a previously used display id (no remembered output).
6. MEDIUM - Stale rejection is real but narrow: `updateImage` drops `generation < current` (equal accepted; fine). Tested (gen 100 vs 50). But pin/comparison set paths have no generation check, failed decode (empty image path at ViewerController L1000 passes generation) is only covered by use, and no test covers failed decode, repeated open/close loops, or controller usable after output closes beyond a trivial toggle.
7. MEDIUM - Input matrix: F8 is bound in the command table and the presentation window hard-codes Escape/F8 in `keyPressEvent`. No INPUT-matrix precedence test; `grep F8` shows no conflict with existing bindings, but the window-local hard-coded keys bypass the user-rebindable binding (`setBinding("presentation-toggle", ...)` does not affect the presentation window). Escape path emits `escapePressed` then `close()` which emits `closedByUser`, so `closePresentation` runs twice (second is a no-op by the early return; safe but sloppy).
8. LOW - UI/render never decodes: `updateImage` receives an already decoded QImage from the controller decode path; no decoding or directory scanning in presentation code. Verified by read. PASS.
9. LOW - `static PresentationWindowFactory` global plus static auto-registration in render TU; test also calls register. Works, but couples through global state.

## Test quality
`monitor_output_tests.cpp` (10 slots) is real, not vacuous, for: enumeration, id determinism (same input twice only; does not test stability under change), open/close, toggle, mode routing, stale rejection, pin isolation, escape key, command registration. Weak/missing: hotplug, comparison via command, memory bounds, failed decode, GPU loss, identity collision/fallback.

## Commands run (worktree .worktrees/claude-review-monitor-output, build dir build-claude-review-monitor-output)
- `scripts/build.ps1 -BuildDir build-claude-review-monitor-output` -> exit 0, 342/342 steps, monitor_output_tests.exe linked.
- `ctest --test-dir build-claude-review-monitor-output -R "monitor_output|cache_tests|acceptance" --output-on-failure` ->
```
1/2 Test  #5: viewer_acceptance ................   Passed   19.59 sec
2/2 Test #33: monitor_output ...................   Passed    0.21 sec
100% tests passed out of 2
```
(cache_tests did not match a ctest name; full suite not run.)

## Unverified
Physical multi-monitor, hotplug, real EDID identity, DPI/HDR, suspend/resume, GPU loss, low-memory behavior, on-screen look. Offscreen platform only.

## Required changes
Items 1-4 (native/stable identity incl. duplicate-monitor collision handling or honest documentation, per-output viewport/color/scale, aggregate texture/image budget with release on close, real comparison command path with generation checks) and tests for 5-6. Reclassify the implementer doc claims accordingly.
