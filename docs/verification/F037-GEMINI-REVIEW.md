# F037 independent Gemini review: Zoom presets, zoom lock and navigator

- **Packet:** F037 (Loupe, zoom presets, navigator overview and zoom lock on canvas UI)
- **SHA pair reviewed:** `b1b82e4` → `319cdf4` (`claude/f037`)
- **Reviewer:** Gemini lane (`agent/*`), isolated worktree `.worktrees/review-f037`, branch `agent/review-f037`
- **Disposition:** **APPROVED**

## Scope and Diff Summary

The review evaluated only `git diff b1b82e4 319cdf4`:
- `docs/F037-PLAN.md`: Contract specification and delivery notes.
- `src/app/viewer_controller.cpp`: Added command registry entries: `zoom-50`, `zoom-200`, `zoom-400`, `zoom-lock`, `navigator` (scope `canvas`).
- `src/render/image_canvas.hpp` & `src/render/image_canvas.cpp`: Added `zoomLock`, `zoomTo(scale)`, `visibleImageRect()`, `centerOn(x, y)`; preserved manual zoom on photo change when `zoomLock` is enabled.
- `ui/Main.qml`: Handled F037 commands; added `zoomNavigator` overlay rectangle with scaled `navigatorViewport` indicator and click/drag interaction.
- `tests/canvas_tests.cpp`: Unit test `presetsZoomLockAndNavigator` covering preset scaling, zoom lock retention across `setImage()`, `visibleImageRect()`, and `centerOn()`.
- `tests/input_ui_tests.cpp`: Targeted UI tests `zoomPresetsZoomLockAndNavigator` verifying command triggering, navigator overlay visibility, mouse drag recentering, and zoom lock release upon fit commands.

## UI Design & Craft Evaluation

Evaluated against `application-interface-design`, component layout, and Qt/QML design guidelines:
1. **Visual System & Theme Integration**:
   - The zoom navigator adheres to design tokens: `Theme.surface` (90% opacity), `Theme.border`, `Theme.radiusSmall`, and `Theme.primary` for the active viewport frame and fill tint (`alpha: 0.18`).
   - Placed non-destructively in the bottom-right corner of the canvas with appropriate breathing room (`Theme.gapMd`, `Theme.gapLg + 2 * Theme.controlHeight`), avoiding overlap with canvas controls.
2. **Affordance & States**:
   - Intelligent visibility: The navigator panel only appears when `app.navigatorOn` is true AND the image is actually zoomed in beyond whole-image bounds (`zoomedIn`), preventing unnecessary screen clutter when viewing fitted images.
   - The viewport box enforces a minimum size of 2x2 px (`Math.max(2, ...)`) so the location indicator remains perceivable even under extreme zoom levels.
3. **Motion & Interaction**:
   - Both single clicks and continuous drags on the navigator `MouseArea` recalculate source image coordinates and invoke `canvas.centerOn()`, yielding responsive pan feedback.
   - Zoom presets (`zoom-50`, `zoom-200`, `zoom-400`) anchor cleanly around the viewport center (`width()/2, height()/2`).
   - Zoom lock maintains consistent magnification and center across consecutive image navigations, releasing automatically when returning to fit modes (`fit`, `fill`, `shrink`), preventing jarring user disorientation.
4. **Accessibility**:
   - The navigator panel declares `Accessible.role: Accessible.Pane`, `Accessible.name: "Zoom navigator"`, and `Accessible.description: "Click or drag to move the visible area of the photo"`.

## Findings

1. **Scope Note (Navigator internal thumbnail)**:
   - As documented in `docs/F037-PLAN.md`, the navigator provides a geometric map with translucent surface backing rather than a secondary rendered thumbnail image (which would require a separate derived decode/canvas path). This is clean, lightweight, and performant.
2. **Scope Note (Keyboard panning within navigator)**:
   - Keyboard panning is handled through the standard canvas arrow navigation rather than distinct navigator focus keys. Meets requirements.

## Targeted Test Output

Executed in `.worktrees/review-f037` (`build-review-f037`):
```
Test project C:/src/Github/Glancelot/.worktrees/review-f037/build-review-f037
    Start 10: input_ui
1/2 Test #10: input_ui .........................   Passed  141.95 sec
    Start 14: viewport
2/2 Test #14: viewport .........................   Passed    0.08 sec

100% tests passed out of 2
Total Test time (real) = 142.05 sec
```
Direct runs:
- `viewport` (`canvas_tests`): All test cases passed (`fitModes`, `portraitAndResize`, `cursorZoomAndPan`, `presetsZoomLockAndNavigator`, `rotationAndPan`, `emptyAndInvalidInputs`).
- `input_ui` (`input_ui_tests`): All 33 test cases passed, including `zoomPresetsZoomLockAndNavigator`.
