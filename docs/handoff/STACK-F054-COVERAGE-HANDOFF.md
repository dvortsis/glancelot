# STACK-F054-COVERAGE handoff (part 1: everything except `ui/Main.qml`)

Written for: the lead and the fresh-context reviewer of this packet. Session: S6 (stack UI), Phase 0 cycle 1, cloud (Linux).
Contract: docs/handoff/LEAD-WRAPUP-2026-10-22.md (F054 resolution), LEAD-TAKEOVER-2026-10-22.md §4 row STACK-F054-COVERAGE, and LEAD-PLAN-2026-10-22.md §5.

**State: partial, by design.** The cycle rule says `ui/Main.qml` strings wait until EN-06 (S3's routing fix) is integrated into `main`. When this part finished, `origin/main` was at `3893c77` and did not contain EN-06 (`claude/en-06-routing` exists unmerged). So this branch has every non-Main.qml file done, and the session stops here as instructed. After EN-06 lands, someone needs to merge `main` into this branch, wrap Main.qml's strings, rerun `lupdate` and `translations/pseudolocalize.py`, and remove the `deferredToMainQml` allowance in the test.

| | |
| --- | --- |
| Branch | `claude/stack-f054-coverage` |
| Base SHA | `afeac1a5d5b1da088d7b35447a933a2f64400f49` |
| Tip SHA | the commit that adds this handoff; the code tip it describes is `8c2c3ea` (review diff: `git diff afeac1a <tip>`) |
| Review | fresh session, `docs/verification/STACK-F054-COVERAGE-REVIEW.md` on `claude/review-stack-f054-coverage` |

## What changed

1. **Strings wrapped** (`qsTr`, with `%1` placeholders instead of concatenation) in:
   - `ui/MetadataHud.qml`, `ui/AnimationBar.qml`, `ui/RenameFlow.qml`, `ui/TransferHud.qml`
   - the settings dialog: `ui/SettingsDialog.qml`, plus `ui/ShortcutRecorder.qml` on its Controls page
   - the presentation/monitor dialog: `ui/MonitorSettings.qml`, with its Displays, Presentation, Panes and Presets pages
   - `ui/DetachedPaneHost.qml`

   Identifiers stay stable: pane ids, metadata filter keys, option values and preset kinds. Translated display names are mapped onto them, for example `paneTitles`, the pill `label`, and `choiceLabel` with `qsTr`. Without a catalog, the English text is unchanged except for two deliberate edits:
   - *(Reverted in review round 1: this packet changed MonitorSettings' preset heading and note; that copy belongs to STACK-F049, so main's wording is back, wrapped in `qsTr`.)*
   - The pane destination label uses fixed per-pane sentences.
   - AnimationBar builds its frame/page counter in QML ("Frame %1 / %2" / "Page %1 / %2", the same text as `AnimationController::frameLabel`) so it can be translated.
2. **Accessibility names are kept and translatable.** New names were added where controls had none or only a glyph:
   - AnimationBar's play/pause, previous, next, speed and loop controls and its scrubber
   - MetadataHud's close button (now icon-only, named "Close metadata inspector", like UiDialog's close), its search field and its filter pills (role Button, checked state)
   - the rename and new-folder fields

   Interpolated names use `arg()`, for example "%1 pane width" and "Shortcut for %1: %2".
3. **RTL.**
   - `DetachedPaneHost` windows mirror with `Qt.application.layoutDirection`, like the controller and reference windows. Their resize grip opts out, because the drag grows from the bottom-right.
   - AnimationBar opts out of mirroring: media and frame-stepping controls keep their order, the usual convention for playback controls.
4. **Clipping fix found by the new test.** MetadataHud's six filter pills sat in a `RowLayout`, which forced the column wider than a 320 px panel and cut off the close button and tag badges, in English as well. They now wrap in a `Flow`, and the title elides before it can overlap the actions.
5. **Catalog.**
   - `translations/glancelot_en_XA.ts` was regenerated with `lupdate ui src app -locations none -no-obsolete`: 468 messages, 250 of them new. `-no-obsolete` removed one `Status` message, "Saved shortcuts conflict or use reserved Escape; default shortcuts are active. Reset shortcuts in Settings to repair them." Its source text did not change: commit `90e63e2` moved it into `src/app/command_registry.cpp` (about line 166) as a plain literal, so `lupdate` no longer extracts it. *(Corrected in review round 1; it is on the C++ follow-up list below.)*
   - New `translations/pseudolocalize.py` is the accenting and bracketing pass the README described. It keeps placeholders and lupdate's escaping.
   - `lrelease` result: "Generated 468 translation(s) (468 finished and 0 unfinished)". README updated.
6. **Test.** New `tests/translation_coverage_ui_tests.cpp` (provisional target in its own commit, below). It installs a `QTranslator` that reads the committed `.ts` directly, then:
   - `catalogCoversEveryInterfaceMessage`: no unfinished or vanished messages; every translation is bracketed and keeps exactly its source's placeholders; spot translations.
   - `pseudoLocaleLeftToRightDark` (1180×780) walks Settings (all five sections), Displays and workspaces (all four pages), the metadata inspector, the rename and new-folder dialogs and a detached pane window. It fails on any visible plain-English interface text. Data is allowed: command labels and key sequences, theme catalog names, preset names, display names, file names, parsed metadata and C++ status lines.
   - `pseudoLocaleRightToLeftDark` and `pseudoLocaleCompactLight` (560×640) repeat the walk for screenshots. They also check mirroring: the detached window's Settings button sits left of the title in RTL and right of it in LTR, and the grip stays bottom-right.
   - `standaloneLeftToRight` and `standaloneRightToLeft` drive AnimationBar and TransferHud from `QQmlPropertyMap` fakes. They check the translated counter, coverage, and that media controls keep their order in RTL.
   - `expandedTextClipping` pads every translation by about 40% and walks again, reporting clipping candidates without asserting. A candidate is text wider than its own box without wrap or elide, elided text, or text laid out past its clipping ancestor or the window.

   Negative check: with the MetadataHud `Flow` reverted to `RowLayout`, the detector reports `"✕ (outside its clip)"` and `"[Sûmmàrý] (outside its clip)"` at 560 px.

## Files

`ui/AnimationBar.qml`, `ui/DetachedPaneHost.qml`, `ui/MetadataHud.qml`, `ui/MonitorSettings.qml`, `ui/RenameFlow.qml`, `ui/SettingsDialog.qml`, `ui/ShortcutRecorder.qml`, `ui/TransferHud.qml`, `translations/glancelot_en_XA.ts`, `translations/pseudolocalize.py`, `translations/README.md`, new `tests/translation_coverage_ui_tests.cpp`, and `docs/verification/STACK-F054-COVERAGE/` (40 PNGs and `coverage-report.txt`).

Notes for the lead:
- `ShortcutRecorder.qml` is not named in the packet. It is part of the settings dialog's Controls page, and the coverage walk found its "Unassigned"/"Press a shortcut…" text untranslated.
- `8c2c3ea`, "cmake: provisional target for STACK-F054-COVERAGE", adds `translation_coverage_ui` to the MONITOR-CUSTOM-01 suite loop in `cmake/MonitorCustomization.cmake` (offscreen and software environment, timeout 240). The lead may replace it.

No shared header changed.

### Overlap with STACK-F049-PRESETS (same session, separate branch from the same base)

- F049 edits SettingsDialog's Browsing preset block. This branch deliberately does not touch those lines; F049 wrapped its own new strings. Git should merge both cleanly, since they touch different hunks of `SettingsDialog.qml`.
- `cmake/MonitorCustomization.cmake` has adjacent one-line edits in both branches (each appends one suite name to the same `foreach`). Resolve by listing both: `monitor_customization monitor_customization_ui layout_presets_ui translation_coverage_ui`.
- After both integrate, regenerate the catalog (the two commands in translations/README.md) to pick up F049's new `Layout` and `SettingsDialog` messages. `catalogCoversEveryInterfaceMessage` cannot see unextracted strings, but `pseudoLocaleLeftToRightDark` will flag F049's new Settings rows until the catalog is regenerated.

## Not covered, and why

- **`ui/Main.qml`.** It is waiting on EN-06, per the cycle rule. Main's routing reasons ("Needs the folder grid", "Needs the folder tree"…) appear on the Settings Controls page; the test allows them through the explicit `deferredToMainQml` list, which the Main.qml step must delete.
- **C++ strings outside this packet's files.** These include command labels (context `Commands`), `MetadataModel` tag labels and categories, `DisplayManager` routing and colour status lines, `FileOperationService` status messages, PaneLayout's built-in preset names (Default, Review, Browse, used as identifiers), `MonitorCustomization` feedback strings (plain literals) and `presentation_window.cpp` (Track D). They are listed in the test as data and reported here as follow-ups for a C++ extraction packet (`QT_TRANSLATE_NOOP` or `tr`).
  - The shortcut-conflict status message in `src/app/command_registry.cpp` (`BindingResolution::error`, about line 166) was translatable through `QCoreApplication::translate("Status", …)` until `90e63e2` made it a plain literal. Wrap it again (`QT_TRANSLATE_NOOP("Status", …)` at the literal, `translate` where `viewer_controller.cpp` shows it).
- **`ShortcutCheatSheet.qml` `ReferenceError: keys is not defined`** (takeover row). It was not reproduced in this environment: the string does not appear in either full-suite log. EN-06's acceptance owns asserting it is gone, so this packet does not edit that file.
- **SettingsDialog `sectionMatches` search keywords** are English literals used only for matching; they were not wrapped, so searching in another language matches visible (translated) row labels but not those hidden keywords.

## Build and test environment

Same as STACK-F049-PRESETS:
- Ubuntu 24.04 cloud container, 4 vCPU, `QT_QPA_PLATFORM=offscreen` (the coverage test also sets `QT_QUICK_BACKEND=software`).
- Qt 6.8.3 from conda-forge, because `download.qt.io` is blocked here; the cycle document now names the same fallback. `lupdate` and `lrelease` came from `/opt/qtconda/lib/qt6/bin`.
- Build directory `build-claude-stack-f054-coverage` in worktree `.worktrees/claude-stack-f054-coverage`.

### Targeted (`ctest -R "translation_coverage_ui|monitor_customization|input_ui|ui_settings|shortcuts_help|pathbar_ui|fs_watch_ui|animation|metadata"`)

```
 1/10 Test #13: input_ui .........................***Failed   69.45 sec
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()
 2/10 Test #14: pathbar_ui .......................   Passed    2.43 sec
 3/10 Test #22: ui_settings ......................   Passed    0.52 sec
 4/10 Test #25: fs_watch_ui ......................   Passed    2.00 sec
 5/10 Test #28: shortcuts_help ...................   Passed    0.25 sec
 6/10 Test #32: metadata .........................   Passed    0.02 sec
 7/10 Test #33: animation ........................   Passed    0.38 sec
 8/10 Test #42: monitor_customization ............   Passed    6.61 sec
 9/10 Test #43: monitor_customization_ui .........   Passed   15.94 sec
10/10 Test #44: translation_coverage_ui ..........   Passed   12.56 sec
90% tests passed, 1 tests failed out of 10
```

The coverage test alone: `Totals: 9 passed, 0 failed, 0 skipped, 0 blacklisted, 23623ms`.

### Full offscreen suite (`ctest --test-dir build-claude-stack-f054-coverage --output-on-failure`, at `8c2c3ea`)

```
 1/44 Test  #1: selection_export .................   Passed    0.03 sec
 2/44 Test  #2: perf_trace .......................   Passed    0.03 sec
 3/44 Test  #3: navigation .......................   Passed    0.18 sec
 4/44 Test  #4: directory_listing ................   Passed    1.56 sec
 5/44 Test  #5: file_operations ..................   Passed    0.01 sec
 6/44 Test  #6: startup_window_geometry ..........   Passed    0.01 sec
 7/44 Test  #7: viewer_acceptance ................   Passed    5.41 sec
 8/44 Test  #8: thumbnails .......................   Passed    3.80 sec
 9/44 Test  #9: folder_tree ......................   Passed    6.00 sec
10/44 Test #10: pane_layout ......................   Passed    0.13 sec
11/44 Test #11: window_slots .....................   Passed   15.76 sec
12/44 Test #12: folder_grid ......................***Failed  170.58 sec
FAIL!  : GridUiTests::externalRenameAndDeleteRefreshKeepSelection() Compared lists have different sizes.
13/44 Test #13: input_ui .........................***Failed   69.27 sec
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()
14/44 Test #14: pathbar_ui .......................   Passed    2.00 sec
15/44 Test #15: file_operation_service ...........   Passed    0.98 sec
16/44 Test #16: directory_model ..................***Failed    7.21 sec
FAIL!  : DirectoryModelTests::selectionSurvivesRelistSortAndDropsRemovedKeys() Compared lists have different sizes.
17/44 Test #17: viewport .........................   Passed    0.03 sec
18/44 Test #18: themes ...........................   Passed    0.19 sec
19/44 Test #19: platform_integration .............   Passed    0.48 sec
20/44 Test #20: grid_search ......................   Passed    0.11 sec
21/44 Test #21: grid_density .....................   Passed    0.08 sec
22/44 Test #22: ui_settings ......................   Passed    0.49 sec
23/44 Test #23: fs_watch .........................   Passed   70.39 sec
24/44 Test #24: fs_watch_budget ..................   Passed   19.89 sec
25/44 Test #25: fs_watch_ui ......................   Passed    1.65 sec
26/44 Test #26: favorites ........................   Passed    0.12 sec
27/44 Test #27: folder_history ...................   Passed    0.02 sec
28/44 Test #28: shortcuts_help ...................   Passed    0.28 sec
29/44 Test #29: filter ...........................   Passed    1.52 sec
30/44 Test #30: cache ............................   Passed    2.01 sec
31/44 Test #31: io_scheduler .....................   Passed    0.06 sec
32/44 Test #32: metadata .........................   Passed    0.03 sec
33/44 Test #33: animation ........................   Passed    0.38 sec
34/44 Test #34: mutation .........................   Passed    0.06 sec
35/44 Test #35: copy_move ........................   Passed    0.08 sec
36/44 Test #36: format ...........................   Passed    0.03 sec
37/44 Test #37: monitor_output ...................   Passed    2.07 sec
38/44 Test #38: recovery .........................   Passed   11.22 sec
39/44 Test #39: recovery_startup .................   Passed    0.21 sec
40/44 Test #40: recovery_controller ..............   Passed   12.35 sec
41/44 Test #41: recovery_process .................   Passed   24.64 sec
42/44 Test #42: monitor_customization ............   Passed    6.90 sec
43/44 Test #43: monitor_customization_ui .........   Passed   16.06 sec
44/44 Test #44: translation_coverage_ui ..........   Passed   12.86 sec
93% tests passed, 3 tests failed out of 44
Total Test time (real) = 467.18 sec
```

The three failures are baseline failures with conda-forge Qt in the cloud container, not regressions. PHASE0-CYCLE1 on current `origin/main` (`3893c77`) names exactly these functions as failing on unmodified `afeac1a`, and STACK-F049-PRESETS-HANDOFF.md reproduces them, rerun once, including with this session's QML changes removed.

## Screenshots and report (docs/verification/STACK-F054-COVERAGE/)

All pseudo-locale (`en_XA`), offscreen, software backend (the RHI canvas does not paint):
- LTR dark 1180: `settings-{appearance,viewing,browsing,controls,about}-ltr-dark-1180.png`, `monitor-page{0,1,2,3}-ltr-dark-1180.png`, `metadata-`, `rename-`, `new-folder-` and `detached-metadata-ltr-dark-1180.png`.
- RTL dark 1180: `settings-browsing-`, `monitor-page2-`, `metadata-`, `rename-`, `new-folder-` and `detached-metadata-rtl-dark-1180.png`, plus `animation-transfer-rtl.png` (and `-ltr`).
- Compact light 560: the same set as `*-ltr-light-560.png`.
- Clipping pass (40% longer text): `*-expanded.png` at dark 1180 and light 560.
- `coverage-report.txt` has the full run's findings:
  - untranslated visible text: none
  - clipping candidates: two C++ command labels elided by design in the Controls list ("Set storage profile to HDD (Serialized background I/O)", "Folder grid: details or thumbnails"), and the metadata title elided only in the 40%-expanded compact pass

## Design-review note

The ui-design-skills (application-interface-design, layout, spacing, typography, design-review) are **not available in this cloud session**. Their principles were applied from AGENTS.md, with existing theme tokens and components only:

- **No new chrome.** The close control reuses the dialog's icon-only close pattern.
- **Wrapping over clipping.** Filter pills now flow onto a second line in a narrow inspector. The title elides only when it has to, and never overlaps the actions.
- **Mirroring follows platform convention.** Panels and detached windows mirror; media playback order and the resize grip do not.
- **Review in the screenshots:**
  - The detached window's title keeps natural (left) text alignment under pseudo-LTR text in RTL layout. Real RTL scripts align right on their own.
  - With 40% longer text, MonitorSettings' Panes page stays readable at 560 px. Buttons wrap through `Flow`, and labels such as "Width"/"Height" stay beside their fields.

## Unverified

- Windows, the gate and Docker were not run here; the authoritative gate decides.
- No real RTL language catalog exists: RTL is evidenced by layout direction with the pseudo-locale.
- No physical display, multi-monitor or assistive-technology checks (NVDA/Orca).
