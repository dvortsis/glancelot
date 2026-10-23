# STACK-F049 follow-up handoff (review findings R1–R4)

Written for: the lead and the fresh-context reviewer. Implementer: a Claude Code (Opus) subagent on the lead's Windows PC. It did not review this packet.
Findings: `docs/verification/STACK-F049-PRESETS-REVIEW.md` on `origin/claude/review-stack-f049` (`dbc9353`), items R1–R4.

| | |
| --- | --- |
| Branch | `claude/stack-f054-part2` (carries this packet and STACK-F054-COVERAGE part 2) |
| Parent (exact) | `a425ed1` = `origin/lead/integrate-cycle1-c2` (`8943463`, EN-04 + F054 part 1 on main) merged with `origin/claude/stack-f049-presets` (`b7add92`). The merge took the lead's resolution from `origin/lead/integrate-cycle1-c` `6762b51` file for file: both commits have the same parents and `git diff 6762b51 a425ed1` is empty. |
| Commits | `747b926` "fix(f049): review follow-ups R1-R4 …" (code and tests); `2c2c4cc` (a refusal edited away stays out of the general note; R4 test order); `1938752` (refusal capture scrolling, refreshed captures); then the `fix(f049):` commit that adds this file |
| Packet tip | the commit that adds this file, directly before `i18n(f054)` part 2 on the branch |
| Review diff | `git diff a425ed1 <packet tip>` |

The ui-design-skills were loaded from `~/.agents/skills/ui-design-skills` before any QML edit: application-interface-design, layout-grids, layout-spacing, typography-ui, responsive-layout and design-review.

## What changed, by finding

### R1: the "transaction superseded the preset" order is forced, not timed

- New test seam `settings_detail::attemptPendingSettingsNow(QObject* owner)` in `src/app/settings_retry.hpp` (internal header, not `include/glancelot`) and `src/app/settings_file.cpp`. It stops the owner's 25 ms retry timer and runs one retry attempt at once, synchronously, so the completion runs inline. Nothing in the product calls it.
- `layoutPresetDuringPendingTransactionStaysCoherent` now runs four forced cases and asserts one exact outcome for each: transaction queued first and landing first (superseded, "replaced" feedback), transaction queued first but the preset landing first (refused), preset first and transaction landing first (both apply), preset first and landing first (refused). The old OR-acceptance is gone.
- `supersededFeedbackSurvivesNewPresentationSession` releases the lock and calls the seam on the coordinator before the event loop can run either timer. The superseded branch therefore runs every time on every platform. It asserts that the preset's pending write was cancelled, that the runtime shows the transaction's dock, and that the feedback starts with "Pane layout changes made while saving were replaced" and keeps the session note.

### R2: refusals appear beside the control that was used

`ui/SettingsDialog.qml`, Browsing page, pane layout preset block:
- Save (button or Enter) and Apply go through `savePanePreset()` and `applyPanePreset()`. When `paneLayout` refuses, its feedback is kept in `presetSaveRefusal` or `presetApplyRefusal`. The text shows in a `Note` in `Theme.danger`, right under the name field (`layoutPresetSaveRefusal`) or right under the preset row (`layoutPresetApplyRefusal`, in the slot of the existing blocked-reason note; the muted blocked reason still wins while Apply is disabled).
- `revealItem()` scrolls the page just enough to show the note. When the user has scrolled so that Save is the last row in view (the 560 px case), the refusal therefore still appears without further scrolling.
- The refusal is also the field's or Apply's `Accessible.description`. Editing the name, choosing another preset, Delete, a successful save or apply, and closing the dialog clear it. The entered name is kept for correction.
- The general `paneLayout.feedback` note at the end of "Pane placement" (`layoutFeedback`) hides when it repeats an inline refusal, so the text is said once.

### R3: one right edge for the control rows

- The preset row (choice, Apply, Delete) no longer has `Layout.maximumWidth: measure`. It fills the column, so Delete ends at the same x as the lock switch and the "Pane placement" controls. Only the prose `Note`s keep the 70-character measure.
- "Save pane layout as" is stacked like the preset row: a body-size label, then the name field (fills) and Save. The field's left edge equals the preset choice's, and Save ends with Delete.

### R4: the main window shows only its own write's outcome

`ui/Main.qml`, inside the F049 regions only (the property block near line 25, the `Connections` next to `splitMessageTimer`, and the `layout-preset-next` block):
- Each press of `layout-preset-next` is a request (`presetRequest`). It arms (`presetPendingRequest`) only when the apply returned with the "Saving layout preferences…" text, that is, when its own write is waiting. A newer press replaces an older armed one.
- While armed, the watcher accepts only that write's terminal outcomes: empty (saved, no message), "Layout changed for this session; settings could not be saved." and "…; a newer saved layout was preserved." These are matched with `qsTranslate("Layout", …)`, the same context and source text as `pane_layout.cpp`, so the match survives translation. Any other layout message (a refusal from Settings, say) means the write's own completion will stay silent (`pane_layout.cpp` drops a deferred outcome once the feedback changed), so the request disarms without showing anything.
- A 6 s timer, longer than the 4 s retry budget, disarms a request that never ended.
- An immediate outcome (a newer saved layout preserved) is shown at once with the preset name, "Layout: Review. Layout changed for this session; a newer saved layout was preserved.", and arms nothing. Before this fix it armed the watcher with that text, and the next different message, however unrelated, became the split message.

## Tests added or changed

- `monitor_customization_tests`: `layoutPresetDuringPendingTransactionStaysCoherent` (four forced cases, exact outcome each) and `supersededFeedbackSurvivesNewPresentationSession` (superseded order forced).
- `layout_presets_ui_tests`:
  - New `presetRefusalsShowBesideTheirControls` (560×640): with Save the last row in view, a name used by a display workspace preset is refused. The refusal must show inside the page's viewport, directly under the field (within 24 px), exactly once, as the field's accessible description, and must clear on typing. A newer-version preset record is then refused on Apply, and that refusal must show directly under the preset row.
  - New `presetControlRowsShareTheColumnEdges` (560 and 1180): the right edges of `layoutLock`, `layoutPresetDelete`, `layoutPresetSave` and `layoutPaneChoice` agree within 1 px, and the name field starts where the preset choice does.
  - New `presetCommandShowsOnlyItsOwnWriteOutcome`: an immediate outcome is shown with the preset and a later refusal never becomes the split message; then, while a write waits, an unrelated refusal is not shown, the request disarms, and nothing appears late after the file is released.
  - `presetCommandWaitsForDisplaySaveAndReportsOutcome`: checks `presetPendingRequest` is 0 at the end (the renamed property).
  - `settingsPresetEvidence` and the refusal test write the captures below.

## Fail before, pass after

**R1, fix reverted (review mutation M6).** `src/app/monitor_customization.cpp` was changed back to the old order: `if(superseded)feedback=…replaced…` before the presentation-session branch, and the new composition removed. `monitor_customization_tests` was rebuilt (under `build-slot.ps1`) and both forced tests were run directly. The superseded test now fails on Windows, which it could not do before (the review's M6 passed there). The file was restored with `git checkout -- src/app/monitor_customization.cpp` and everything rebuilt.

```
> monitor_customization_tests.exe supersededFeedbackSurvivesNewPresentationSession layoutPresetDuringPendingTransactionStaysCoherent   # M6 applied
QINFO  : MonitorCustomizationTests::supersededFeedbackSurvivesNewPresentationSession() transaction superseded the preset: Preferences saved; the newer presentation session was left unchanged.
FAIL!  : MonitorCustomizationTests::supersededFeedbackSurvivesNewPresentationSession() 'feedback.startsWith("Pane layout changes made while saving were replaced")&&feedback.contains("newer presentation session")' returned FALSE. (Preferences saved; the newer presentation session was left unchanged.)
C:\src\Github\Glancelot\.worktrees\claude-stack-ui-p2\tests\monitor_customization_tests.cpp(186) : failure location
QINFO  : MonitorCustomizationTests::layoutPresetDuringPendingTransactionStaysCoherent() transaction queued first and lands first: it supersedes the preset: feedback "Pane layout changes made while saving were replaced by the saved monitor layout." frozen 1 preset 0
QINFO  : MonitorCustomizationTests::layoutPresetDuringPendingTransactionStaysCoherent() transaction queued first, preset lands first: transaction refused: feedback "The pane layout changed while saving. Retry with its current state." frozen 0 preset 1
QINFO  : MonitorCustomizationTests::layoutPresetDuringPendingTransactionStaysCoherent() preset applied first, transaction lands first: both apply in order: feedback "" frozen 1 preset 1
QINFO  : MonitorCustomizationTests::layoutPresetDuringPendingTransactionStaysCoherent() preset applied and lands first: transaction refused: feedback "The pane layout changed while saving. Retry with its current state." frozen 0 preset 1
PASS   : MonitorCustomizationTests::layoutPresetDuringPendingTransactionStaysCoherent()
Totals: 3 passed, 1 failed, 0 skipped, 0 blacklisted, 239ms
```

The coherence test still passes under M6, as it should: without a presentation session the old order also produced "replaced". Its job is to prove all four orders run; the log above shows each of them on Windows, where the review saw only two. After the restore:

```
> monitor_customization_tests.exe layoutPresetDuringPendingTransactionStaysCoherent supersededFeedbackSurvivesNewPresentationSession
QINFO  : …supersededFeedbackSurvivesNewPresentationSession() transaction superseded the preset: Pane layout changes made while saving were replaced by the saved monitor layout. Preferences saved; the newer presentation session was left unchanged.
PASS   : MonitorCustomizationTests::supersededFeedbackSurvivesNewPresentationSession()
Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 186ms
```

**R2, R3 and R4, fixes reverted.** `ui/Main.qml` and `ui/SettingsDialog.qml` were restored from `a425ed1` (`git restore --source=a425ed1 …`), and the new UI tests were run against them (the tests load the QML from the source tree, so no rebuild is needed). The files were restored afterwards and `git status` was clean.

```
> layout_presets_ui_tests.exe presetRefusalsShowBesideTheirControls presetControlRowsShareTheColumnEdges presetCommandShowsOnlyItsOwnWriteOutcome   # a425ed1 QML
FAIL!  : LayoutPresetsUiTests::presetRefusalsShowBesideTheirControls() 'besideControl(h,collision,field).isEmpty()' returned FALSE. ("A display workspace preset already uses that name. Choose another name." is not shown under the control (y=566) inside the viewport (y=144-608); shown at y=850-873)
FAIL!  : LayoutPresetsUiTests::presetControlRowsShareTheColumnEdges() 'qAbs(fieldLeft-choiceLeft)<=1' returned FALSE. (width 560: name field left 263, preset choice left 32)
FAIL!  : LayoutPresetsUiTests::presetCommandShowsOnlyItsOwnWriteOutcome() '!h.window->property("splitMessage").toString().contains("center is reserved")' returned FALSE. (The center is reserved for image and grid content. Choose an edge for this pane.)
Totals: 2 passed, 3 failed, 0 skipped, 0 blacklisted, 23834ms
```

- R2: at the base, the refusal is drawn only at y=850–873, below the viewport, which ends at y=608.
- R3: at the base, the side-by-side save row puts the field at x=263, not at the preset choice's x=32.
- R4: at the base, an unrelated refusal after an immediate "newer saved layout preserved" outcome becomes the main window's split message, which is exactly the review's scenario.

## Grant check

```
> git diff --name-only a425ed1..1938752
docs/verification/STACK-F049-PRESETS/settings-presets-dark-1180.png
docs/verification/STACK-F049-PRESETS/settings-presets-dark-560.png
docs/verification/STACK-F049-PRESETS/settings-presets-light-1180.png
docs/verification/STACK-F049-PRESETS/settings-presets-light-560.png
docs/verification/STACK-F049-PRESETS/settings-presets-refusal-dark-560.png
docs/verification/STACK-F049-PRESETS/settings-presets-saving-dark-1180.png
src/app/settings_file.cpp
src/app/settings_retry.hpp
tests/layout_presets_ui_tests.cpp
tests/monitor_customization_tests.cpp
ui/Main.qml
ui/SettingsDialog.qml
```

The handoff commit adds `docs/handoff/STACK-F049-FOLLOWUP-HANDOFF.md`.

- `ui/SettingsDialog.qml` (Browsing preset block, the dialog's preset properties and functions, its `onClosed`), `ui/Main.qml` (the three F049 regions only), `tests/layout_presets_ui_tests.cpp`, `tests/monitor_customization_tests.cpp`, `docs/handoff/STACK-F049-FOLLOWUP-HANDOFF.md` and `docs/verification/STACK-F049-PRESETS/*.png` are F049's files.
- **Outside the packet's files:** `src/app/settings_retry.hpp` and `src/app/settings_file.cpp` (the test seam, 9 lines). They are internal `src/app` files, not shared headers under `include/glancelot`. R1 asks for a deterministic hook; see "Decisions taken".
- No shared header changed (`git diff a425ed1 747b926 -- include/` is empty). No CMake change.

## Test results (Windows 11, MSVC, Qt 6.8.3, offscreen and software Quick)

Code under test: the final code tree of `claude/stack-f054-part2` (the STACK-F049 follow-up plus STACK-F054 part 2), built by `build-slot.ps1` into `build-stack-ui-p2`; the handoff commits add documents only. Runs inside `& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch claude/stack-ui-p2 -Run { … }` with PATH prefixed by `.tools\python\Scripts` and `.tools\Qt\6.8.3\msvc2022_64\bin` (queued 06:19, ran 06:40–07:07 on October 23).

Targeted:

```
> ctest --test-dir build-stack-ui-p2 -R "translation_coverage_ui|layout_presets_ui|pane_layout|monitor_customization|ui_settings|input_ui|pathbar_ui|command_routing|folder_grid" --output-on-failure
 1/10 Test #10: pane_layout ......................   Passed    2.33 sec
 2/10 Test #12: folder_grid ......................   Passed  375.18 sec
 3/10 Test #13: input_ui .........................   Passed   74.77 sec
 4/10 Test #14: pathbar_ui .......................   Passed    4.67 sec
 5/10 Test #22: ui_settings ......................   Passed    1.57 sec
 6/10 Test #42: monitor_customization ............   Passed    9.21 sec
 7/10 Test #43: monitor_customization_ui .........   Passed   25.63 sec
 8/10 Test #44: translation_coverage_ui ..........   Passed   23.78 sec
 9/10 Test #45: layout_presets_ui ................   Passed   22.28 sec
10/10 Test #46: command_routing ..................   Passed   10.53 sec
100% tests passed out of 10
Total Test time (real) = 550.47 sec
```

Repeated:

```
> ctest --test-dir build-stack-ui-p2 -R "^(layout_presets_ui|monitor_customization)$" --repeat until-fail:10 --output-on-failure
    Test #42: monitor_customization ............   Passed    9.14 sec
    Test #42: monitor_customization ............   Passed    9.17 sec
    Test #42: monitor_customization ............   Passed    8.86 sec
    Test #42: monitor_customization ............   Passed    8.93 sec
    Test #42: monitor_customization ............   Passed    9.11 sec
    Test #42: monitor_customization ............   Passed    8.68 sec
    Test #42: monitor_customization ............   Passed    8.88 sec
    Test #42: monitor_customization ............   Passed    8.63 sec
    Test #42: monitor_customization ............   Passed    8.85 sec
1/2 Test #42: monitor_customization ............   Passed    8.72 sec
    Test #45: layout_presets_ui ................   Passed   20.92 sec
    Test #45: layout_presets_ui ................   Passed   25.99 sec
    Test #45: layout_presets_ui ................   Passed   18.77 sec
    Test #45: layout_presets_ui ................   Passed   18.79 sec
    Test #45: layout_presets_ui ................   Passed   19.22 sec
    Test #45: layout_presets_ui ................   Passed   22.11 sec
    Test #45: layout_presets_ui ................   Passed   25.05 sec
    Test #45: layout_presets_ui ................   Passed   22.77 sec
    Test #45: layout_presets_ui ................   Passed   23.20 sec
2/2 Test #45: layout_presets_ui ................   Passed   21.30 sec
100% tests passed out of 2
Total Test time (real) = 307.09 sec
```

Full offscreen suite (no baseline failures on this machine):

```
> ctest --test-dir build-stack-ui-p2 --output-on-failure
 1/46 Test  #1: selection_export .................   Passed    0.41 sec
 2/46 Test  #2: perf_trace .......................   Passed    0.08 sec
 3/46 Test  #3: navigation .......................   Passed    2.78 sec
 4/46 Test  #4: directory_listing ................   Passed   16.51 sec
 5/46 Test  #5: file_operations ..................   Passed    0.21 sec
 6/46 Test  #6: startup_window_geometry ..........   Passed    0.07 sec
 7/46 Test  #7: viewer_acceptance ................   Passed   10.57 sec
 8/46 Test  #8: thumbnails .......................   Passed    6.19 sec
 9/46 Test  #9: folder_tree ......................   Passed    9.87 sec
10/46 Test #10: pane_layout ......................   Passed    1.57 sec
11/46 Test #11: window_slots .....................   Passed   28.60 sec
12/46 Test #12: folder_grid ......................   Passed  219.43 sec
13/46 Test #13: input_ui .........................   Passed   76.81 sec
14/46 Test #14: pathbar_ui .......................   Passed    3.24 sec
15/46 Test #15: file_operation_service ...........   Passed    1.25 sec
16/46 Test #16: directory_model ..................   Passed   26.62 sec
17/46 Test #17: viewport .........................   Passed    0.13 sec
18/46 Test #18: themes ...........................   Passed    0.91 sec
19/46 Test #19: platform_integration .............   Passed    1.33 sec
20/46 Test #20: grid_search ......................   Passed    0.38 sec
21/46 Test #21: grid_density .....................   Passed    0.28 sec
22/46 Test #22: ui_settings ......................   Passed    1.83 sec
23/46 Test #23: fs_watch .........................   Passed   89.05 sec
24/46 Test #24: fs_watch_budget ..................   Passed   97.78 sec
25/46 Test #25: fs_watch_ui ......................   Passed    3.64 sec
26/46 Test #26: favorites ........................   Passed    0.45 sec
27/46 Test #27: folder_history ...................   Passed    0.08 sec
28/46 Test #28: shortcuts_help ...................   Passed    0.80 sec
29/46 Test #29: filter ...........................   Passed    3.24 sec
30/46 Test #30: cache ............................   Passed    5.75 sec
31/46 Test #31: io_scheduler .....................   Passed    0.24 sec
32/46 Test #32: metadata .........................   Passed    0.17 sec
33/46 Test #33: animation ........................   Passed    0.48 sec
34/46 Test #34: mutation .........................   Passed    0.21 sec
35/46 Test #35: copy_move ........................   Passed    0.26 sec
36/46 Test #36: format ...........................   Passed    0.17 sec
37/46 Test #37: monitor_output ...................   Passed    2.53 sec
38/46 Test #38: recovery .........................   Passed   14.42 sec
39/46 Test #39: recovery_startup .................   Passed    0.51 sec
40/46 Test #40: recovery_controller ..............   Passed   14.98 sec
41/46 Test #41: recovery_process .................   Passed   29.72 sec
42/46 Test #42: monitor_customization ............   Passed    9.02 sec
43/46 Test #43: monitor_customization_ui .........   Passed   24.72 sec
44/46 Test #44: translation_coverage_ui ..........   Passed   21.48 sec
45/46 Test #45: layout_presets_ui ................   Passed   19.97 sec
46/46 Test #46: command_routing ..................   Passed    8.46 sec
100% tests passed out of 46
Total Test time (real) = 758.89 sec
```

## Captures (`docs/verification/STACK-F049-PRESETS/`)

Regenerated by `settingsPresetEvidence` and `presetRefusalsShowBesideTheirControls` with `GLANCELOT_F049_UI_EVIDENCE` set (offscreen, software backend):
- `settings-presets-{dark,light}-{560,1180}.png`: a frozen tree pane. The preset row and the stacked "Save pane layout as" row end at the lock switch's right edge (x=954 at 1180 px). The name field starts under the preset choice.
- `settings-presets-saving-dark-1180.png`: a display save pending, with the lock, freeze and Reset reasons.
- New `settings-presets-refusal-dark-560.png`: both refusals inline at 560 px, in view. "A newer version of Glancelot saved that preset; it was preserved and not applied." sits under the preset row; "A display workspace preset already uses that name. Choose another name." sits under the name field, with the entered name kept.
- `navigator-detached-preview.png` is unchanged.

Design review of the captures (application-interface-design, layout-grids, layout-spacing, typography-ui, responsive-layout, design-review):
- **Blocking:** none.
- **Important:** none open. Feedback is next to the action (R2), there is one right edge (R3), and prose keeps a measure of about 70 characters while controls fill the column.
- **Polish (open):**
  - The R5 items from the review are untouched. `paneNames` still duplicates `paneChoice.model`, the gap under the Reset note is about 15 px against 8 px elsewhere, and Apply has no primary treatment. The refusal capture now shows Apply enabled: it uses the standard, quiet button, beside the outlined Delete.
  - At 1180 px the preset choice is about 350 px wide, which is generous for a short name. That is the cost of one shared edge.

## Decisions taken

- **R1 hook: a synchronous retry seam rather than cancelling the pane write in the test.** Alternatives: (a) `cancelPendingSettings(&h.panes,…)` before the release, which needs no product change but forces only the superseded order and removes the very pending write the coordinator is meant to cancel; (b) a separate settings directory for the pane write, which tests a different scenario. The seam keeps both queued writes real and fixes their order, so all four cases run on every platform. It is nine lines in an internal header, and the product never calls it.
- **R2: danger colour plus text, inline, scrolled into view.** The refusal is a sentence, so it does not rely on colour. Muted text stays for disabled-state explanations, which are not errors. The general feedback note keeps showing other layout messages (busy writes, newer layout preserved).
- **R2 scope: refusals only.** A successful apply whose write waits ("Saving layout preferences…") still reports through the general note, as before. Following that outcome inline would need the same request matching as R4 inside Settings; the cost is not justified while the dialog is modal.
- **R3: the controls fill the column; the combo is not capped.** Capping the combo but not the buttons would leave a gap between them. At 1180 px the choice is wide, but it aligns with every other control row on the page.
- **R4: outcome matching by request and by outcome kind.** The frozen `PaneLayout` header exposes only `feedback` and `changed()`, so no per-write generation can reach QML without a header change. Matching the three terminal texts through `qsTranslate("Layout", …)` and a request counter gives the same guarantee within the frozen API.
- The new `Layout: %1` and `Layout: %1. %2` split messages are wrapped in `qsTr` here, because this packet rewrote that line.

## Open questions for the lead

- Formalize `attemptPendingSettingsNow` as test-only (for example behind a test define) at the next contract freeze, if you prefer that to an always-compiled internal function.
- A per-write generation on `PaneLayout` (a `feedbackRevision` property, say) would let QML match outcomes without comparing text. It needs a shared-header change, so it is left for a contract freeze.

## Not covered / unverified

- No physical display, multi-monitor or assistive-technology run. Offscreen with the software backend proves layout and logic only.
- `translation_coverage_ui` is expected to fail on this packet's tip alone. F049's Settings strings ("Pane layout presets", the scope note and the rest) entered the catalog only when STACK-F054 part 2 regenerated it, in the next commits on this branch. The part 2 handoff's fail-before run lists them as untranslated against the older catalog. The branch tip passes.
