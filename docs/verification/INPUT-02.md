# INPUT-02 verification

Branch `claude/input-02`, commits ddd67ae and 01d5daa, base 76da49e (Windows 11, Qt 6.8.3, `scripts/build.ps1 -BuildDir build-claude-input-02 -Test`).

## Measured
- Windows ctest: 14 of 15 passed. `input_ui` (12 cases, 1 opt-in capture skipped) passed in 16 s, `folder_grid` 122 s, `window_slots` (three new INPUT-02 controller cases) 29 s.
- `platform_integration` fails on the base commit as well (`openTerminalInvalidOrMissingPathReturnsFalse`, Gemini lane PLATFORM-OPS-01 stub). It is unrelated to this branch.
- Linux (Docker engine in WSL Ubuntu, debug image without the ctest step): `input_ui_tests` 12/12; `grid_ui_tests` passed 72/72 twice and failed one case in two other runs. The base commit 76da49e showed the same kind of flake (`deleteBatchProgressStopAndPartialFailureResults` failed once in two runs), so these are pre-existing timing flakes.
- Review captures (`GLANCELOT_REVIEW_CAPTURE`): `.cache/input-02/{canvas,grid}-menu-{wide,compact}-{dark,light}.png`.

## Not done / unverified
- The full `docker build -t glancelot-browser:main .` integration check was not run on the final commit (the run was declined).
- Independent cross-lane review: an advisory read-only Claude subagent reviewed ddd67ae (CHANGES REQUESTED; Escape-in-grid/tree, weak wheel assertions, parity test, tree row naming, loupe/hold guard were fixed in 01d5daa). This does not count as the required Codex-lane review.
- Known low items left: bindings on keys the grid or tree claim (arrows, Enter) are accepted but never fire (for the F051 recorder to validate); physical trackpads, real keyboard layouts and IMEs are unverified.

## Integration note (October 19, 2026)

A trial merge of `main` (41bdce5, after GRID-DENSITY-01 and PLATFORM-01) into the top of this stack, `claude/f009`, conflicts in `CMakeLists.txt`, `include/glancelot/viewer_controller.hpp`, `ui/Main.qml` and `ui/PhotoGrid.qml` (command tables, QML_FILES and test-target lists, and the grid delegate). The merge was aborted. Per the lane protocol the integrator merges `main` into the reviewed branch under the lock, so the stack is not rebased while `main` is still moving; each branch merges `main` once it has its independent review, in order: input-02, f002, f037, f031, f040, f049, f009.

## Codex review response (a6bda32, CHANGES REQUIRED)

Fixed in the next commit on `claude/input-02` (Windows: input_ui 16/16, grid_ui 72/72, window_slots 34/34):

1. P1 remaps vs native pane keys: grid and tree yield a key to a registered grid or tree command (including both chord steps) through `ViewerController::keyStep` and `Main.qml remapped()`. Global commands remapped onto a native pane key still lose to the pane, which keeps the older, tested rule that grid keys beat global remaps. Test: grid-up on Right, chord Left, D.
2. P2 pane ownership: canvas and grid keys are not live while the tree has focus; a focus-region change cancels a pending chord. Explicit toolbar/palette routes are unchanged.
3. P2 pointer cancel: `onCanceled` now ends any hold, key or pointer; test cancels the grab and checks the exact fit and zoom.
4. P2 tree Trash: on a drive root the Delete key explains instead of retargeting, matching the disabled menu row; test covers a root.
5. P2 compact menus: the menu list scrolls to the current row on keyboard navigation; test presses Down to the last row in 540x360 and asserts it is inside the window.

Not independently re-run: Linux Docker on this fix, physical input devices. The folder_grid test file is unchanged.
