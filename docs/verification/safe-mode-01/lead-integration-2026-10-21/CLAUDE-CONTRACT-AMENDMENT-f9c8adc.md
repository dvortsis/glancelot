**ACCEPT.** Both amendments from my earlier contract review are now specified. I found no remaining REQUIRED contract amendments. This is contract acceptance only. It is not approval of the implementation or of integration. I read the files and executed nothing.

**Amendment 1: outer frame vs client rectangle (resolved).**
- `docs/STARTUP-WINDOW-01-PLAN.md:15` and the comments at `src/app/startup_window_geometry.hpp:15-16` both say current and result are outer-frame rectangles in Qt logical coordinates.
- Both say `minimumSize` includes the actual frame margins.
- The plan says the adapter reads `frameGeometry` and `frameMargins`, converts back to client geometry, and does no DPR math.
- The decorated fixture is arithmetically consistent. Client (0,1600,2360,1560) with 8/31/8/8 margins gives outer (-8,1569,2376,1599).
- The zero-margin fixture covers the raw failing rectangle.

**Amendment 2: degenerate, cap and preferred semantics (resolved).**
- `TooManyScreens` is checked first, before any rectangle validation, and returns the geometry unchanged.
- A usable screen has positive width and height, with qint64 edge and center arithmetic.
- An invalid or out-of-range preferred screen falls back to the first usable screen in input order.
- The preferred index is either left unfiltered or remapped explicitly.
- Empty or all-invalid screen sets return `NoUsableScreen`, unchanged.
- An invalid or empty current rectangle counts as not visible. Each `minimumSize` component is normalized to at least 1, and the minimum wins on small work areas.
- Visibility needs positive-area intersection, so touching edges do not count.
- Explicit `--window-size` is clamped only when the window is wholly offscreen, subject to minimums.
- The oracle, renderer flags and all timeouts are untouched.

**CMake move.**
- `include(cmake/StartupWindowGeometryTests.cmake)` at `CMakeLists.txt:64` sits inside `GLANCELOT_BUILD_VIEWER`, after the `glancelot` target and its `target_link_libraries`, so the target exists when the fragment runs.
- `include(CTest)` at line 6 comes earlier, so the `BUILD_TESTING` guard inside the fragment is valid.
- I did not confirm that `Qt6::Test` is found at configure time; I'm assuming that from your note. A missing component would fail loudly at configure rather than silently.
- The recovery wiring is not guarded, and the guard in this fragment is documented as removed at integration.

**Optional notes (not blockers):**
1. The "anchor the title/control area" rule leaves the exact anchor open. Pinning it as "outer-frame top-left at the work-area top-left" would make that fixture unambiguous across implementers.
2. Centering rounding for negative or odd differences is not pinned. If the tests assert exact coordinates, such as the (0,1600) recovery on the screen at (0,-1600), say floor or truncate. Tests that assert only containment need no change.
3. The 64-screen cap is stated as "more than 64". A boundary test at exactly 64 (accepted) and 65 (`TooManyScreens`) would be worthwhile. The test list already says "more-than64 inputs".
4. The status semantics are implied rather than stated. `Unchanged` appears to mean visible, so geometry is returned as is. `Recovered` appears to mean the geometry changed. Stating this would help the adapter's diagnostic handling.

The missing `.cpp` and test source remain the intended next Codex packet.
