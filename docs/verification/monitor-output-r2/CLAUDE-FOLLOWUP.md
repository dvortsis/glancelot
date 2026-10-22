**ACCEPT** (source only). Findings 1–3 from `CLAUDE-REVIEW.md` are closed, and I found no new required defects. I ran nothing, so this comes from reading the source and tests. It isn't a full-gate or hardware qualification.

**Skills read:** all nine (application-interface-design, layout-grids, layout-spacing, typography-ui, component-geometry, iconography-ui, motion-ui, responsive-layout, design-review). The output is a bare native canvas with no chrome, so only keyboard recovery, state fidelity, Theme consistency and honest status text apply. Nothing in `ui/` changed.

## Required findings: all closed

1. **First-visit display inherits the old view: closed.**
   - `display_manager.cpp:397-406`: `setFitMode(fitModes.value(newId,"shrink"))` runs first, resetting both canvases. Then `restoreViewport(viewports.value(id, defaults))` runs, where `defaults` is `{primary/secondary fit, linked:true, activeSecondary:false}`. This uses a direct `fitModes` lookup, not the live-window `outputFitMode`.
   - `presentation_window.cpp:111`: `restoreViewport({})` also resets linked, the active pane and the shrink fit.
   - `ImageCanvas::setViewState` (`image_canvas.cpp:284-287`) handles a fit-only map correctly, so the defaults map works.
   - The native test now checks shrink, linked and the primary pane **before** the second screen is edited (`tests/monitor_output_tests.cpp:101-103`). It is opt-in and skipped by default.
2. **Texture budget: closed.**
   - The constructor sets A to 256 MiB and B to 0 (`presentation_window.cpp:28,33`).
   - `setComparisonMode` (`:59-61`) applies 128 MiB each in comparison and 256/0 otherwise. It runs before the early return and on every `applyImagesToWindow`, so Mirror, Pinned and Comparison transitions all land on the right budgets.
   - `testTextureBudgetTracksPresentationMode` checks the budgets in the constructor and across both mode transitions. Its 150 MiB generated image would fail the old 128 MiB cap.
3. **Pixels retained while closed: closed.**
   - `updateImage` (`display_manager.cpp:474`) clears `previous*` while inactive.
   - `testClosedOutputDoesNotRetainNavigationHistory` closes, calls `updateImage` twice, and asserts `retainedImageBytes()` equals pinned + current. It then reopens and checks that compare picks up the pinned image.
   - The contract amendment at `docs/MONITOR-OUTPUT-01-CORE-CORRECTION.md:15` documents all three.

**Other checks:**
- `presentation-fit` defaults to `Ctrl+Alt+F` and doesn't collide with other defaults (`viewer_controller.cpp:157`; the others are `Ctrl+Alt+R/T/C`). It remains remappable.
- The 16 MiB color reservation is documented at `:15` and used consistently by the window and tests.
- The one shared selection-generation counter is documented in the `display_manager.hpp` comment.
- The poll skip while in flight and the equal-atlas/status dedupe are in `presentation_window.cpp:42,49`.
- No factory topology API was added.
- `windows-sonnet-fixes-cases.txt` shows 40 passed, 0 failed, 2 skipped (the two native opt-in tests). The existing budget, pin, close and fit tests still pass with the new budgets.

## Non-blocking

- **Manager defaults map isn't headless-tested.** The headless reset test calls `window.restoreViewport({})`, but production never passes an empty map because it always passes `defaults`. The manager's defaults-map path is only covered by the opt-in native test. A test would need a second `QScreen`, so I'm not asking for one.
- **Budget test name.** The 150 MiB test checks `sizeInBytes() <= textureBudget()`, not the renderer's admission check (`image_canvas.cpp:84`). It does fail on the old code, but the name overstates what it exercises.
- **Comparison with a large image.** A 128–256 MiB image in A drops over budget when you enter Comparison, and the canvas shows its limit error. This matches the 128 MiB-each contract.
- **Closed-state comparison image.** `setComparisonImage` while closed can still retain a comparison image until the next close or release. It has no production caller (tests and the header only).
- **Still open from before, as advisories you've already placed outside this scope:**
  - Unbounded `panBy`.
  - The chord timeout runs from the first key.
  - `QHash::begin()` eviction is arbitrary rather than LRU.
  - Pin and comparison generations reset to 0 on unpin or shed.
- **`Ctrl+Alt+F` and AltGr layouts.** On layouts where AltGr is Ctrl+Alt, this default could collide with typed characters. The output has no text input, so it's low risk.

## Evidence limits (not defects)

- The prior `51fb` full run was stopped and isn't counted, and Docker exited 130 (canceled, not a pass). Both are covered by your statement that fresh final gates will run on the latest source, and ACCEPT here doesn't substitute for them.
- GPU readback, ICC accuracy, HDR and mixed-DPI hotplug are unverified without hardware. The two native tests remain opt-in.