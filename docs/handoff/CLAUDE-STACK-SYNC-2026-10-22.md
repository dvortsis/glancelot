# Claude stack synchronized onto integrated main, October 22, 2026

Written for: the lead/integrator and the Gemini confirmation reviewer. Claude lane (Sonnet 5.5) work; no queue, root-CMake or `main` edits, no merge into `main`, nothing pushed.

The F002 -> F037 -> F031 -> F040 -> F049 -> F009 -> F014 -> F051 stack is merged forward onto integrated `main` `5424cef`, one predecessor tip at a time. Each branch's worktree is clean. Each pair below is the review pin: `git diff <predecessor> <tip>` is that packet's own change only.

| Branch | Tip | Predecessor | Own diff |
|---|---|---|---|
| claude/f002 | `e05614f` | main `5424cef` | 8 files, +411/-131 |
| claude/f037 | `2891250` | `e05614f` | 7 files, +154/-2 |
| claude/f031 | `53a789b` | `2891250` | 4 files, +85/-4 |
| claude/f040 | `3cb96bf` | `53a789b` | 6 files, +99/-3 |
| claude/f049 | `d4eb4cc` | `3cb96bf` | 4 files, +42/-4 |
| claude/f009 | `f22cbfe` | `d4eb4cc` | 5 files, +73/-4 |
| claude/f014 | `d181d4f` | `f22cbfe` | 4 files, +96/-0 |
| claude/f051 | `cf40fcc` | `d181d4f` | 35 files, +2132/-234 (see below) |

All eight Gemini reviews of the original candidates were APPROVED, so no earlier review fix needed propagating. These are new merge commits; the pre-sync tips the reviews pinned are unchanged ancestors.

## Verification actually run

Per branch: `scripts/build.ps1 -BuildDir build-claude-<f>` (full compile, exit 0) and targeted `ctest -R` (exempt from the gate lock). No full `scripts/build.ps1 -Test`, no `docker build`: those belong to the integrator under the lock. Linux, physical keyboard layouts/IME and trackpads are unverified.

| Branch | Targeted tests, all passed |
|---|---|
| F002 | input_ui 80.6s, pathbar_ui 6.5s |
| F037 | input_ui, pathbar_ui, viewport, monitor_output (4/4) |
| F031 | input_ui, pathbar_ui, viewport, animation, monitor_output (5/5) |
| F040 | same five (5/5) |
| F049 | the five plus metadata (6/6) |
| F009 | folder_grid, input_ui, pathbar_ui, viewport, grid_search, grid_density, ui_settings, monitor_output (8/8) |
| F014 | same eight (8/8), including `gridSearchKeepsPrintableDefaultsWhileChordsCanArm` |
| F051 | 19 targeted tests (window_slots, pane_layout, ui_settings, themes, favorites, folder_history, shortcuts_help, filter, metadata, directory_model, folder_grid, input_ui and the rest): 18/19 first run; input_ui rerun after the fix below 1/1 |

F051 first run: `input_ui` failed 2 of 33 cases. (1) `shortcutRecorderCapturesChordsAndExplainsRefusals`: the test picked F8 as a free key, but `main` binds `presentation-toggle` to F8, so the recorder correctly refused it; the test now uses F7 (`cf40fcc`). (2) `treeRowAndPathBarMenusOpenAndMatch`: `qt.qpa.mime: Unable to obtain clipboard`, Windows clipboard contention; it passed on the rerun and on F002..F014. Treat as an environmental flake, not a product defect. F051 also failed to compile once after the merge (`entry` out of scope in `restoreWindow`, F015 tab restore); fixed in `fb698c0`.

## Conflict resolutions (what the reviewer should look at)

- **Command registry.** `main` moved the registry into `src/app/command_registry.cpp`. Each packet's own commands were carried into it: F037 zoom presets/lock/navigator, F031 slideshow, F040 reference-pin/close, F049 focus-mode, F009 view-details, and for F051 layout-preset-next and tab-new/close/next/previous.
- **F002.** The test harness stays in F002's shared `tests/ui_test_support.hpp`; `main`'s `Harness(TrashBackend)` constructor and includes were ported into it (`HeldTrash` stays in `input_ui_tests.cpp`).
- **F049.** Focus mode also hides `main`'s metadata panel (added after F049 was written).
- **F009.** `PhotoGrid.qml` keeps `main`'s bucket-size column and cell math, with details mode applied on top.
- **F014.** Its tip carried an older `main` merge, a queue note and `aa10549`. The tree is the F009 tree plus F014's own change (type and failed-preview badges `67d3f83`, F013/F042 plan doc). `aa10549` (grid type-to-select yields) is dropped because `main`'s integrated INPUT-02 routing (`PhotoGrid.yields`) already covers it and the old hunk would have preempted it. The queue note (`f884684`) is dropped: `docs/WORK-QUEUE.yaml` is lead-owned.
- **F051** (a stack top, not one packet: F051 recorder, F053 high contrast and large targets, F049 named presets, F052 accessibility audit, F054 pseudo-locale i18n, F015 tabs, F042 type chips). Resolutions: `main`'s async settings, recovery and decode code kept with F054's `translate()` wrapping re-applied; `Private::save` now maps `highContrast` and `largeTargets` (otherwise F053's toggles would silently stop persisting); `DirectoryEntryModel::apply` uses `filtering()` on `main`'s restructured projection; pane layout and theme feedback keep `main`'s deferred-save text, translated; CMake QML list unions `main`'s new components with `ShortcutRecorder.qml`.

## Findings for the lead

1. **F015 tab persistence is not carried through the recovery path.** With a recovery session (the production path), `main` records only folder and image and skips the settings write, so tabs are saved and restored only on the settings-file fallback. Fixing it extends the lead-owned recovery contract; not done here.
2. **F040 overlaps `main`'s pinned-reference output** (MONITOR-OUTPUT-01 `pinPresentationImage`). No code collision; both kept. Product decision for the lead.
3. **`main` moved after this sync.** The monitor-customization contract freeze changed `include/glancelot/pane_layout.hpp` and `include/glancelot/viewer_controller.hpp`. A trial merge of `main` `356caba` into F051 conflicts in exactly those two shared headers (aborted, nothing committed). F049 presets add to `PaneLayout`'s API, so the final sync needs a lead decision on those headers.
4. **F054 coverage.** QML and controller strings that `main` added after F054 was written (MetadataHud, AnimationBar, RenameFlow, TransferHud, presentation) are not wrapped for translation.
5. **Pre-existing on `main`:** `ui/ShortcutCheatSheet.qml` line 327 logs `ReferenceError: keys is not defined` during UI tests. That file is byte-identical to `main`'s.
