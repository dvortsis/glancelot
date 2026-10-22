Status: I've read all the requested evidence and source, and I'm writing up findings now. I ran nothing and wrote nothing.

## Source-provable facts

1. **The product never swapped a live frame.** `frames` increments only on `QQuickWindow::frameSwapped` (`app/main.cpp:242`). `grabWindow()` runs first (`main.cpp:252`) and does not touch that counter.
   - In the failing run, `frames=0` and `first_image_frame_ms=-1` are therefore real, not a reporting artifact.
   - The decode did complete, at +151 ms after `exec()` started (`decoded_ms` 4313 − `quit_timer_started_ms` 4162). The earlier passing runs decoded about 150–170 ms after the same point.
   - The quit callback fired on time: 6671 − 4162 = 2509 ms.
   - So the event loop was alive for the full 2.5 s with a visible window and still produced no swap.

2. **`window.exposed=false` is the proximate state.** With `visible=true`, `visibility=2` and `state=0`, the window was windowed and not minimized, but Qt never delivered or kept an expose. The product only reads this state (`main.cpp:268`); it never sets it. `window_active=true` alongside `exposed=false` means the window was activated but had no exposed paint region.

3. **Nothing in the product sets 2360x1560.** `ui/Main.qml:11-12` sets 1180x780. `--window-size` isn't passed. `restoreWindow()` returns immediately in safe mode (`viewer_controller.cpp:1182`). A grep of `app`, `src`, `ui`, `scripts` and the CMake files found no DPI, scale, `setWidth`/`setHeight` or manifest code. The size is exactly 2× in both axes and 1560 exceeds the 1440 screen height. That points to a DPI or scale transition, from Qt or the OS, applied to the native window. The source cannot explain it.

4. **Startup was about 3 s slower before the loop.** `quit_timer_started_ms` was 4162, against 1041–1064 in the passing runs. That time is spent synchronously in `engine.loadFromModule`, which creates and shows the window (`visible: true`).
   - The native window therefore existed about 4 s before `exec()`.
   - That is a window in which an OS-side DPI or move event could land without being processed.
   - This is consistent with the evidence. It is not proven.

5. **The harness filters few variables.** `scripts/verify-recovery.py:123-125` strips only `GLANCELOT_*`, `QSG_*` and five named `QT_*` variables. Every other inherited variable passes through, including `QT_SCALE_FACTOR`, `QT_SCREEN_SCALE_FACTORS`, `QT_ENABLE_HIGHDPI_SCALING`, `QT_FONT_DPI` and `QT_QPA_PLATFORM` options.
   - The child's actual environment in that run was not recorded. That is the historical unknown behind your clean shell check.

6. **`startup_environment.cpp` doesn't touch windowing.** It sets software-render and D3D11 selection only. Safe-mode `QT_OPENGL=software` and `AA_UseSoftwareOpenGL` are irrelevant to the D3D11 path. The device was identical in the passing run (Microsoft Basic Render Driver, CPU device type 5).

7. **`NativeTitleBarSync` doesn't explain it.** It only calls `DwmSetWindowAttribute` on `Show`, `SurfaceCreated`, `activeChanged` and theme change, using the same values as the passing runs (all four results true). It sets no geometry or flags. I found no way for it to change size or exposure.

## Does report/grab ordering explain the evidence?

It explains one counter, but not the failure.

- **What it likely explains:** `render_callbacks=1`, `scenegraph_invalidations=1` and `observed=false` together. A `grabWindow()` on an unexposed window has to create the RHI, render once and release it. That would produce exactly one `beforeRendering`, then `sceneGraphInvalidated`, which resets `observed`. This is plausible but unproven. It is also consistent with the valid generated RGB.
- **Why it isn't the cause:** `grabWindow` runs after the 2.5 s window has already elapsed. The missing swaps happened earlier.
- **Hazard:** because the grab runs before the evidence read, a grab can erase or fabricate `observed` and the callback counts. Those fields cannot tell live rendering from a screenshot-only render.
- **Slow rendering is excluded:** a slow WARP frame at 3.7 MP would still show swaps. The invalidation after the single callback also rules out a frame still in flight.

## Ranked

**Required**
- **R1.** Treat the failure as a real product-visible state: a visible, unexposed, mis-scaled window with zero swaps. Don't clear it on a later pass.
- **R2.** Capture evidence that distinguishes the OS/Qt DPI-or-expose path from the product, before any source change. The minimal route is below.

**Non-blocking**
- **N1.** Snapshot the RHI and window evidence before `grabWindow()` runs, so a screenshot can't mask live-render evidence. This is a reporting-order change only; every assertion stays.
- **N2.** The harness could record the child's `QT_*` environment. It would add no assertion.

## Minimal next diagnostic

1. **No code change:** rerun the identical process oracle with the same renderer, deadlines and assertions, and keep the full artifacts from any failure.
2. **If it fails again, add logging only, with no new assertions.** The target is a timeline of first `Expose`, first `beforeRendering`, first `frameSwapped`, `screenChanged`, `devicePixelRatioChanged`, and width and height changes. Each entry gets a timestamp relative to `elapsed`. Add a pre-grab snapshot of `isExposed` and the geometry, plus the child's `QT_*` environment from the harness.
3. **Source-fix invariant, only if the timeline shows a product-side cause:** the window must be shown at its declared logical size and must receive an expose before the quit timer starts. I have no source evidence for that yet, so I'd make no fix now.

I'm not claiming a cause. The older zero-swap failures remain unexplained.
