# F040 independent Gemini review: Reference window

- **Packet:** F040 (Reference window)
- **SHA pair reviewed:** `26f8a89` → `75dd2d7` (`claude/f040`)
- **Reviewer:** Gemini lane (`agent/*`), isolated worktree `.worktrees/review-f040`, branch `agent/review-f040`
- **Disposition:** **APPROVED**

## Scope and Diff Summary

The review evaluated only `git diff 26f8a89 75dd2d7`:
- `docs/F040-PLAN.md`: Contract specification and delivery notes.
- `include/glancelot/viewer_controller.hpp`: Declared properties `referenceImage`, `referenceName`, invokables `pinReference()`, `clearReference()`, and signal `referenceChanged`.
- `src/app/viewer_controller.cpp`: Implemented reference image storage using Qt implicit data sharing (`QImage` copy without pixel duplication); registered commands `reference-pin` and `reference-close`.
- `ui/Main.qml`: Added separate `referenceWindow` `Window` item with dedicated `ImageCanvas`, drag pan, wheel zoom, `F` fit, `1` 100% zoom, and Escape/window-close dismissal.
- `tests/input_ui_tests.cpp`: Added test case `referenceWindowKeepsItsPhoto` verifying persistence across subsequent browsing, replacement on re-pinning, and closing via command or OS close button.

## UI Design & Craft Evaluation

Evaluated against `application-interface-design`, multi-window ergonomics, and Qt/QML design guidelines:
1. **Window Hierarchy & Dimensions**:
   - Clean secondary utility window sizing (720x520 default, 320x240 minimum).
   - Clear and informative title bar: `"Reference — " + viewer.referenceName`.
   - Adheres to `Theme.background` and dark palette styling.
2. **Independent Canvas Interaction**:
   - The reference window embeds its own `ImageCanvas`, allowing independent zooming (smooth wheel scaling `1.0015^delta`), dragging/panning via `MouseArea`, and double-click refitting to viewport bounds (`fit("shrink")`).
   - Fast keyboard shortcuts: `F` to fit, `1` for 100% actual size, and Escape to dismiss.
3. **Isolation & Lifecycle**:
   - Browsing, deleting, filtering, or navigating in the primary window leaves the pinned reference intact.
   - Closing the window (either through native OS close button, Escape key, or `reference-close` command) cleanly clears `viewer.clearReference()`.
   - Pinning when no image is ready fails gracefully, invoking `deleteFlow.showNotice("Open a photo first to pin it as a reference.")` without opening an empty window.
4. **Zero Allocation Overhead**:
   - Leverages `QImage` implicit sharing, requiring no duplicate image decoding or memory footprint beyond the existing decoded buffer.
5. **Accessibility**:
   - Direct screen-reader annotation: `Accessible.name: "Reference photo " + viewer.referenceName`.

## Findings

1. **Scope Note (Synchronized pan/zoom)**:
   - As documented in `docs/F040-PLAN.md` ("Not done"), linked pan/zoom between the reference and main windows belongs to the F026 comparison packet. The independent reference viewing satisfies all requirements.

## Targeted Test Output

Executed in `.worktrees/review-f040` (`build-review-f040`):
```
Test project C:/src/Github/Glancelot/.worktrees/review-f040/build-review-f040
    Start 10: input_ui
1/1 Test #10: input_ui .........................   Passed   42.69 sec

100% tests passed out of 1
Total Test time (real) =  42.77 sec
```
Direct run:
- `input_ui_tests.exe`: All 35 test cases passed, including `referenceWindowKeepsItsPhoto`.
