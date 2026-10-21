**CHANGES REQUIRED.** Two documentation-only amendments. Nothing else in the contract blocks implementation. I executed nothing and read only the files you named, plus `ui/Main.qml` and `viewer_controller.cpp` in the SAFE worktree to check ordering.

## Required amendments

1. **Rectangle space is undefined (`startup_window_geometry.hpp:15-19`, plan lines 15 and 23).**
   - The helper has no frame-margin input, but the plan says to "anchor the title/control area inside" the screen.
   - Qt's `geometry()` (client area) and `frameGeometry()` differ on a decorated Windows window. A client origin at the work-area top puts the native title bar above the work area.
   - Amend with one sentence. Either "`current` and the result are the outer frame rectangle; the adapter converts to and from `geometry()` using `frameMargins()`", or "client rectangle, and the adapter ensures the title bar stays inside the work area". I'd pick the first.
   - Also require that the adapter apply the result through logical `setGeometry` or `setPosition` and `resize`, with no DPR math.

2. **"Usable" and the degenerate inputs are not pinned down (plan lines 23-24).** Without this, the lane's tests and implementation can both be self-consistent yet disagree with the lead's intent. Add:
   - A usable screen is one with `width > 0` and `height > 0`. Its edges are computed in `qint64`.
   - An out-of-range `preferredScreen`, or an unusable preferred rectangle, falls back to the first usable screen in input order.
   - An invalid or empty `current` is treated as not visible. Its recovered size is `minimumSize`, clamped to the work area. The plan doesn't say where the size comes from in this case.
   - A negative or zero `minimumSize` component is clamped to at least 1 (or 0), and never reaches the arithmetic.
   - `TooManyScreens` is checked before any rectangle validation.

## Checked and fine

- **API:**
  - The span, the 64 bound, the value result and the status enum are sufficient.
  - The `[[nodiscard]]` return type is a plain QRect, so no allocation is needed.
  - Statuses are explicit, and `Unchanged` doubles as the "visible" result.
- **Touching vs. intersecting:** touching-edge handling is specified (positive-area intersection only). That matches the failure case, where the window is at y=1600 on a 1440-high primary and entirely outside the secondary.
- **Adapter ordering:**
  - `ui/Main.qml:15` has `visible: true`, so the native window exists before `main.cpp` runs. The recovery is therefore post-creation by necessity.
  - "After actual geometry and after the `--window-size` resize (line 208), before the quit timer (line 246) and the `restoreWindow` single-shot" is implementable.
  - I found no code in `restoreWindow` or `viewer_controller.cpp` that sets window geometry, so the later `restoreWindow` single-shot can't undo the correction. The plan's "no settings writes" requirement stays satisfiable.
- **Wiring (`StartupWindowGeometryTests.cmake`, `CMakeLists.txt:257`):**
  - The include sits after the `BUILD_TESTING` block, inside the Qt scope, so the `glancelot` target exists.
  - A present `.cpp` with a missing test source fails configure, so the lane can't ship the implementation without the test.
  - The test links Core and Test only, and the header uses only QRect, QSize and `<span>`. That matches the pure-value contract.
  - No QML, hot files or public headers are touched.
- **Evidence handling:** the plan states the offscreen mechanism as proven and the earlier cause as unproven. It does not touch the oracle, renderer or timeouts, and it keeps the native process proof separate from the unit test.

## Non-blocking notes

1. If the adapter drops invalid screens before calling the helper, it must keep `preferredScreen` pointing at the same screen. Either pass the list unfiltered or remap the index.
2. A 1 px sliver of overlap counts as visible and is left unchanged, even though it leaves the title unreachable. That matches "preserve valid placements" and the real failure. I'd leave it unless you want a minimum-grab rule, which would be a new product feature.
3. If `--window-size` asks for more than the work area, the explicit size gets clamped. State that this is intended, or the test authors will guess.
4. Add a test where the helper receives the real-failure rectangle (0,1600,2360,1560) against both screen arrangements. The plan lists this already, but make the exact numbers the fixture.
5. Pre-grab diagnostics: also record the screen index chosen and the helper status as report fields. This is cheap and makes the next placement failure diagnosable.
6. The guard (`if(EXISTS …cpp)`) silently skips the target if the lane misnames the file. Your plan line 35, "target must appear in both gates", covers it.
