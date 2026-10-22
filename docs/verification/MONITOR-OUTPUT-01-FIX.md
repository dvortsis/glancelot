# MONITOR-OUTPUT-01 fix pass (Claude lane)

Branch `claude/monitor-output-fix`, based on main a395ca5 plus the review doc (77f67ab). Build dir `build-claude-monitor-output-fix`.
No edits to docs/WORK-QUEUE.yaml or root CMake. `ImageCanvas` (shared header) gained `textureBudget()/setTextureBudget()` (additive, default unchanged 256 MiB) and `IPresentationWindow` gained a pure virtual `setFitMode` (only implementer is in-tree).

## Findings status

| # | Finding | Status |
|---|---|---|
| 1 | Display identity | FIXED in code, hardware UNVERIFIED. `DisplayManager::assignDisplayIds` derives ids from manufacturer/model/serial plus a Windows adapter (`EnumDisplayDevices` device interface path, hardware id segment such as DEL40F4 = EDID manufacturer+product code). `\\.\DISPLAYn` is never used. Identical monitors get a disambiguating ordinal (list order); serial-bearing monitors are order independent. Last resort without any hardware naming: pixel size + physical size + ordinal. Per-unit EDID serial is not read; identical serial-less monitors can swap ids if the OS reorders them. colorSpace is no longer hardcoded: Qt 6.8 public QScreen has no color space API, so it reports "Unknown" (COLOR-01 needs an ICC adapter). |
| 2 | Comparison mode | FIXED. `presentation-compare` calls `compareWithPinnedOrPrevious()`: secondary = explicit comparison image, else pinned, else previously shown image. `setComparisonImage` and `pinImage` reject stale generations. |
| 3 | Per-output viewport/fit | PARTIAL. New command `presentation-fit` (cycles shrink/fit/fill) and `ViewerController::setPresentationFitMode`; fit is stored per display id and restored when that output reopens. Per-output zoom/pan/color/scale are NOT implemented (pan/zoom sync in comparison remains absent). |
| 4 | Bounded memory | FIXED for CPU and GPU admission. Aggregate decoded-image cap (256 MiB default, shared QImage counted once; sheds previous, comparison, pinned; current never dropped). Presentation canvases split a 256 MiB texture budget. Close drops window, canvases, previous and comparison images. `releaseCachedImages()` is the low-memory hook; Qt 6 has no desktop low-memory signal, so nothing calls it automatically. |
| 5 | Hotplug/failed decode/repeat | TESTED offscreen: screenRemoved of the output screen closes exactly once, unrelated removal ignored, screenAdded notifies, failed decode (null image), 50 open/close cycles with window count unchanged. screenAdded still does not re-bind a remembered output (not implemented). Suspend/GPU loss not implemented. |
| 6 | Escape/F8 | FIXED. Escape stays the reserved dismiss key; the toggle key is read from the live `presentation-toggle` binding via a provider, so rebinding works and F8 no longer hard-coded. Window no longer calls `close()` after emitting, so the owner closes once (spy asserts 1). INPUT-02 note: that branch (not on main) may add scopes to `Command`; this change only adds a registry row and a provider and should merge trivially, but the presentation window's key path bypasses the QML Shortcut layer by design (separate OS window), so INPUT-02 scope gating does not apply to it. |

## Tests (tests/monitor_output_tests.cpp, 24 slots, 14 new)
identity duplicates/serial reorder/reconnect, colorSpace not hardcoded, compare via command path, stale pin/comparison, fit command + per-output restore, image budget + close release + texture budget, screenRemoved/Added, failed decode + 50 cycles, Escape once + rebindable toggle key.

## Commands and results
`scripts/build.ps1 -BuildDir build-claude-monitor-output-fix` -> exit 0.
```
monitor_output_tests: Totals: 24 passed, 0 failed
ctest -R "monitor_output|viewer_acceptance|shortcuts":
  viewer_acceptance Passed 7.95 sec; shortcuts_help Passed; monitor_output Passed
  100% tests passed out of 3
```
Note: monitor_output_tests is a GUI-subsystem exe; run it with `-o file,txt` to see output, and with the Qt bin on PATH.

## UNVERIFIED
Physical multi-monitor, EDID on real hardware (Windows adapter compiled only), HDR/DPI, suspend/resume, GPU device loss, low-memory behaviour, on-screen appearance. Full suite and docker gate not run (integrator).
