**CHANGES REQUIRED** (source only). I found two required fixes and one that should be done with them. This review is read-only: I ran no tests, and every finding comes from reading the source. It is not full-integration or hardware qualification.

**Skills read:** application-interface-design, layout-grids, layout-spacing, typography-ui, component-geometry, iconography-ui, motion-ui, responsive-layout, design-review. Application: the output surface is native Qt with no chrome, and nothing in `ui/` references presentation. Spacing, typography, geometry, icons and motion don't apply to a bare canvas. The design-review points that do apply are keyboard operation, recovery, state fidelity and honest status text.

## Required findings

**1. P1: Retargeting copies the previous output's viewport onto a display that has no saved state.**
- Cause: `src/app/display_manager.cpp:387` sets `activeId` to the new id before `:398` calls `outputFitMode(d->activeId)`.
- At `:557-562` that returns the live window's fit, which belongs to the old display. If the old view was manual, it returns `"custom"`, which `setFitMode` rejects at `presentation_window.cpp:97`.
- `setPrimaryImage` (`presentation_window.cpp:71-77`) then keeps the old canvas `viewState`, and `restoreViewport` returns early on empty state (`:108`).
- Result: a first-visit display inherits the old display's manual zoom and pan, and an old fit mode, instead of the default. That breaks the per-display-id contract.
- The native restore test only revisits displays that already have saved state, so it can't catch this.
- Fix:
  - On retarget, always apply the stored state for the target id, or defaults: `{primary:{fit: fitModes.value(id,"shrink")}, secondary:{…}, linked:true, activeSecondary:false}`.
  - Make `restoreViewport` reset to those defaults on empty input instead of returning.
  - Read `fitModes` directly for any id other than the one the window currently shows.
- Test: use a fake `IPresentationWindow` via the factory, since offscreen has only one screen. Open on A, go manual, retarget to a never-seen B, and expect B at shrink.

**2. P2: The 256 MiB texture cap is halved per canvas in Mirror and Pinned.**
- `presentation_window.cpp:28,34` give each canvas 128 MiB, and the contract says the 256 MiB caps are unchanged.
- A decoded image of 128–256 MiB, such as 6000×8000 RGBA at about 192 MiB, is accepted by the controller canvas. The presentation canvas rejects it ("Image exceeds this canvas's GPU limits") even though the secondary canvas is hidden and empty.
- Fix: in `setComparisonMode`, set the budgets to 128 MiB each in comparison, and 256 MiB for A and 0 for B otherwise.
- Test: assert the budget values per mode, plus a 150 MiB QImage accepted as primary in Mirror.

**3. P2 (fix alongside): the manager keeps pixels after the presentation closes.**
- `updateImage` (`display_manager.cpp:468-479`) runs unconditionally, because `viewer_controller.cpp:918` and `:966` don't gate it. `closePresentation` clears `previousImage` once, but the next navigation while closed refills it.
- The manager can then hold a decoded raster the controller's cache has evicted, so the "close releases pixels" claim only holds momentarily.
- Fix: when `!d->active`, don't set `previous*`. Keep only current and pinned.
- Test: close, call `updateImage` twice, and expect `retainedImageBytes()` to cover the current and pinned images only.

## Non-blocking findings

- **P3, color refresh churn** (`presentation_window.cpp:42,45-52`): every 5 s, each visible window rebuilds the atlas and replaces it with a new QImage, which causes a GPU re-upload and an `outputColorChanged` signal even when nothing changed. The bump of `m_colorRevision` also discards a build that is still in flight. Fix: skip the set and the emit when the atlas and status are equal (`QImage==` on 144 KB), and don't bump the revision while a build is in flight.
- **P3, color accounting** (`output_color.hpp:55-58`, `presentation_window.hpp:28`): a worker can transiently hold a 4 MiB file buffer plus a `QColorSpace` ICC copy, which is more than the 4 MiB reservation. `colorResourceBytes()` returns a constant, not a measurement. Document it as a reservation, or raise it to 16 MiB.
- **P3, generation spaces** (`display_manager.cpp:478,512`): `updateImage` raises the comparison high-water mark, so a comparison producer with its own counter would be rejected forever. Pin and comparison generations also reset to 0 on unpin or shed (`:176,506`), which re-admits stale results. Document one shared selection-generation space. No production async comparison producer exists today.
- **P3, recovery** (`image_canvas.cpp:276`, controller table `:157`): `panBy` is unbounded and `presentation-fit` has no default key. Wheel-panning can lose the image, and from the output only Escape works. Clamp pan to keep part of the image visible, or give fit a default binding.
- **P3, chord timing** (`presentation_window.cpp:219,233`): the 1500 ms timeout runs from the first key, not the last. I didn't compare this against the controller's chord semantics.
- **P3, polish:**
  - `viewports` and `fitModes` evict `begin()` of a `QHash` (`display_manager.cpp:368,417,538`), which is arbitrary rather than LRU.
  - `image_canvas.cpp:62-66` resets the SRB without resetting the pipeline on a LUT size change. That is layout-compatible, but I couldn't verify it on a GPU.

## Checked, no defect found

- **Pixel handling:**
  - Pixels are shared sRGB QImages with no per-output full-raster conversion (`cacheKey` is asserted).
  - The shader's slice math keeps samples inside each slice (`p ≤ 32`, centers at `+0.5`).
  - The LUT is applied to straight alpha before blending.
  - Format and size are validated (1089×33 RGBA8888).
- **Worker lifetime:**
  - The worker captures values only, and the permit is released by RAII even if the window dies.
  - The watcher destructor doesn't block, and retries are a 50 ms bounded poll.
  - A stale revision is discarded and rerun.
  - There is no GUI file I/O.
  - The Windows path checks for a fixed drive, caps the profile at 4 MiB and 32 KB of path, and loads dynamically. Other platforms report an sRGB fallback.
- **Viewport math:** source-center save and restore is consistent with `imagePointAt` for rotation and DPR, and pan is rescaled on a DPR change. Independent comparison state survives a temporarily missing image.
- **Keys:** autorepeat is ignored, Escape cancels a pending chord, a binding change clears it, and focus-out and hide clear it.
- **Command access:** all `presentation-view-*` commands act on the output through `trigger`, and the controller canvas isn't touched.

## Test quality

- `testGeneratedColorAtlasMatchesSdrReferencePatches` uses Qt's own transform on both sides. It is a consistency check, not independent evidence.
- The GLSL interpolation is covered only by the opt-in native readback (one patch, tolerance 4). The default suite skips it, which accounts for the two skips.
- Nothing tests retarget-to-a-new-display (finding 1) or the per-mode texture budget (finding 2).

## Evidence and platform limits (not defects)

- `windows-edge-final-cases.txt` has one failure (fit `shrink` vs `fit`, at `monitor_output_tests.cpp:527`). `windows-restored-fit-cases.txt` after the latest fix shows 37 passed, 0 failed, 2 skipped.
- `windows-final-full.txt` has no result lines, so the final full run is still pending.
- Docker exited 130 (canceled), which is not a pass.
- `GetICMProfileW` returns only the device's default profile, so no WCS or per-user association and no HDR. Physical ICC accuracy, mixed-DPI hotplug and GPU readback are unverified without hardware.

Raw logs are in `docs/verification/monitor-output-r2/`. Findings 1–3 need fixing and re-verifying on a fresh gate before ACCEPT.