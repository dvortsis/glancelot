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
