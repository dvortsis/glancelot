# STACK-F054-COVERAGE part 2 handoff (`ui/Main.qml`, F049 Settings strings, full coverage)

Written for: the lead and the fresh-context reviewer. Implementer: a Claude Code (Opus) subagent on the lead's Windows PC. It did not review this packet.
Contract: the F054 bullet in `docs/handoff/LEAD-WRAPUP-2026-10-22.md`, the part 1 review (`docs/verification/STACK-F054-COVERAGE-REVIEW.md` on `origin/claude/review-stack-f054`, `a7fa4ee`, integration notes 2–4) and `docs/handoff/STACK-F054-COVERAGE-HANDOFF.md` ("Not covered": `ui/Main.qml`).

| | |
| --- | --- |
| Branch | `claude/stack-f054-part2` |
| Base | `8943463` = `origin/lead/integrate-cycle1-c2` (EN-04 and F054 part 1 on main), merged with `origin/claude/stack-f049-presets` `b7add92` as `a425ed1` (the lead's `6762b51` resolution) |
| Parent (exact) | `04aef7c`, the tip of the STACK-F049 follow-up packet on this branch |
| Commits | `e1d584b` "i18n(f054): part 2, Main.qml strings, F049 Settings strings and full coverage check"; `f686b45` (status bar found by its pane rectangle); `7450392` (media folder masked in the report; captures); then the commit that adds this file |
| Review diff | `git diff 04aef7c <this packet's tip>` |

The ui-design-skills were loaded from `~/.agents/skills/ui-design-skills` before any QML edit (application-interface-design, layout-grids, layout-spacing, typography-ui, responsive-layout, design-review). The QML change is strings only: no layout, spacing, colour or component changed.

## What changed

### `ui/Main.qml`: every user-facing string through `qsTr` (strings only)

- `qsTr` calls went from 44 to 149. The newly wrapped strings cover the following:
  - the window title (`%1 — Glancelot`) and the reference window title and name;
  - the routing reasons ("Not in grid view", "Needs the folder grid", "Needs the folder tree") and the pane-command reasons;
  - the delete reasons and notices, the context-menu "Move “%1” to the %2" labels and the reference-pin notice;
  - the combined-view split, fallback and layout-recovery notices, and the orientation messages;
  - the preview state titles and details, and "Loading %1…";
  - the canvas, navigator, split handle, mode group, sort, tab, lock, slideshow and chord accessible names and descriptions;
  - every tooltip;
  - the status bar ("%1 selected", "%1 of %2", "No selection", the key hint, the zoom percentage, the image size, the grid Open button);
  - the maximize/restore label and the file dialog's filter labels.
- **No concatenation.** Interpolation uses `qsTr("…%1…").arg()`.
  - Tooltips with a shortcut use one helper, `withShortcut(text, id)`, built on `qsTr("%1 (%2)")`. It replaces `shortcutHint()`, whose only callers were in this file.
  - Sentences that used to splice in a word are now whole sentences. Examples: "Showing the grid. Tab switches." against "Showing the photo. Tab switches.", the split-handle descriptions, and "File order: %1, ascending" against "File order: %1, descending".
- **Without a translator the English text is unchanged.** All other suites still compare against it ("Layout: Review", "Needs the folder grid", "100%", and so on).
- **Kept untranslated on purpose,** from a scripted scan of the remaining literals (`scan_literals.py`, below):
  - `"Glancelot"`: the product name, used as the window title with no file open.
  - `fixedKeys` ("Enter", "Backspace", "Ctrl+A") and `sequence: "Escape"`: key sequences, which are data like every command binding.
  - `viewer.status === "Open a JPEG or PNG to begin."`: a comparison against the C++ controller's text, not a displayed string.
  - The file dialog's patterns (`*.jpg … *.PNG`, `*`): only the labels around them are translated.
  - Command ids, `objectName`s, pane ids, region and mode keys and icon names. Their sets are unchanged.

```
> python -I scan_literals.py ui/Main.qml        # string literals with words, not inside qsTr/qsTranslate, not identifiers
16: 'Glancelot'  <<  title: viewer.fileName ? qsTr("%1 — Glancelot").arg(viewer.fileName) : "Glancelot" // the product name stays as is
542: 'Enter' / 'Backspace' / 'Ctrl+A' / 'Enter'  <<  readonly property var fixedKeys: ({ "grid-open": "Enter", … })
929: 'Escape'  <<  sequence: "Escape"
1673: '/glancelot-64.png'  <<  source: "qrc:/brand/icons/app/" + (Theme.dark ? "dark" : "light") + "/glancelot-64.png"
2037: 'Open a JPEG or PNG to begin.'  <<  readonly property bool defaultHint: viewer.status === "Open a JPEG or PNG to begin."
2093: '*.jpg *.jpeg *.png *.JPG *.JPEG *.PNG'  <<  nameFilters: [qsTr("Photographs (%1)").arg("*.jpg …"), qsTr("All files (%1)").arg("*")]
```

### STACK-F049's Settings strings

F049 already wrapped its new Settings > Browsing strings in `qsTr`, so this packet adds them to the catalog. They are: the `layoutReset` text and its accessible description (the Reset note), "Pane layout presets", the scope note, "Pane layout preset", "Save pane layout as", "Name for the new pane layout preset", "Pane layout name", "Pane placement", and the saving, frozen and locked reasons. The R2 follow-up adds no new strings: its notes show `paneLayout` feedback. MonitorSettings' F049 heading and scope note are in the catalog too. The obsolete wordings were removed (see "Catalog").

### `tests/translation_coverage_ui_tests.cpp`

- `deferredToMainQml` and `deferredToF049` are deleted, so every surface the walk visits is checked without allowances.
- New `walkMainWindow`, run in every pass, in the photo view and the grid view:
  - It asserts that the top bar (`topBar`) and the status bar have no plain-English text, accessible name, description, tooltip or placeholder. The status bar is found as the Rectangle laid out on `paneRect("statusbar")`, because Main.qml edits here are strings only, so no `objectName` was added.
  - Data on those bars is allowed: the C++ status line, file and folder names, and tab titles.
  - The rest of the window (folder tree, path bar, grid, filmstrip components outside the grant) is reported in `coverage-report.txt`, not asserted. The generated media folder is masked as `<folder>`.
- Settings > Browsing also captures the F049 preset block scrolled into view (`settings-browsing-presets-*`).
- New pass `pseudoLocaleRightToLeftCompactLight` (RTL at 560×640, light).
- `catalogCoversEveryInterfaceMessage` spot-checks a Main message and a SettingsDialog F049 message.

## Catalog (`translations/glancelot_en_XA.ts`)

Qt Linguist tools are not in the Windows `.tools/Qt`, so they were installed in WSL (Ubuntu 24.04) into a user prefix. `/tmp` did not survive a WSL restart, so the tools went under the WSL home directory. The `icu` archive is needed for `lupdate` to start:

```
$ python3 -m venv ~/glancelot-aqt/venv
$ ~/glancelot-aqt/venv/bin/pip install -q aqtinstall==3.2.1
$ ~/glancelot-aqt/venv/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 --archives qttools qtbase qtdeclarative icu -O ~/glancelot-aqt/qt
INFO    : Finished installation
$ ~/glancelot-aqt/qt/6.8.3/gcc_64/bin/lupdate -version
lupdate version 6.8.3
```

Regeneration, from the worktree (`/mnt/c/src/Github/Glancelot/.worktrees/claude-stack-ui-p2`), with the README's commands:

```
$ ~/glancelot-aqt/qt/6.8.3/gcc_64/bin/lupdate ui src app -locations none -no-obsolete -ts translations/glancelot_en_XA.ts
/mnt/c/…/src/app/viewer_controller.cpp:114: Ignoring definition of undeclared qualified class
Updating 'translations/glancelot_en_XA.ts'...
    Found 582 source text(s) (113 new and 469 already existing)
    Removed 6 obsolete entries
    Same-text heuristic provided 2 translation(s)
$ python3 -I translations/pseudolocalize.py translations/glancelot_en_XA.ts
translations/glancelot_en_XA.ts: 113 message(s) pseudo-localized
```

The second run is byte-identical (SHA-256 compared before and after), and `lrelease` finishes every message:

```
    Found 582 source text(s) (0 new and 582 already existing)
translations/glancelot_en_XA.ts: 0 message(s) pseudo-localized
    Generated 582 translation(s) (582 finished and 0 unfinished)
identical: True
```

- **The diff is limited to the intended changes.** `git diff 04aef7c e1d584b -- translations/glancelot_en_XA.ts` gives 448 lines added and 20 removed.
  - Added: a new `Main` context; the F049 `SettingsDialog`, `MonitorSettings` and `Layout` messages (`qsTranslate("Layout", …)` in Main.qml shares the C++ context); and "%1 (%2)" and the other new Main messages.
  - Removed, as obsolete: the 6 old wordings. They are MonitorSettings' "Named presets for this window slot" and its old scope note, plus SettingsDialog's "Layout preset", "Review, Browse and Default are built in; …", "Save current layout as" and "Preset name".
- **Line endings.** `git ls-files --eol translations/glancelot_en_XA.ts` gives `i/lf w/lf` (the file holds no CR). Before regeneration it was `i/lf w/crlf`, the working copy of an autocrlf checkout, so the index stays LF.
- **README.** `translations/README.md` now gives the new message count (582).

## Fail before, pass after

The new test was run with `ui/Main.qml` and the catalog from `2c2c4cc` (identical to the parent `04aef7c` for both files), using `git restore --source=2c2c4cc ui/Main.qml translations/glancelot_en_XA.ts`. Both files were restored afterwards.

```
> translation_coverage_ui_tests.exe catalogCoversEveryInterfaceMessage pseudoLocaleLeftToRightDark     # Main.qml and catalog before part 2
FAIL!  : TranslationCoverageUiTests::catalogCoversEveryInterfaceMessage() Compared values are not the same      (translation_coverage_ui_tests.cpp(386) at f686b45, line 391 at the tip: the Main spot check)
QINFO  : …pseudoLocaleLeftToRightDark() untranslated main-photo-top-bar: "Open a photo (Ctrl+O) (tooltip)" at …/topBar/…/openButton
QINFO  : …pseudoLocaleLeftToRightDark() untranslated main-photo-top-bar: "Show the folder tree (accessible description)" at …/foldersToggle
QINFO  : …pseudoLocaleLeftToRightDark() untranslated main-photo-top-bar: "View (accessible name)" at …/contentModeSegments
QINFO  : …pseudoLocaleLeftToRightDark() untranslated main-photo-top-bar: "Cycle file order (S); reverse with Shift+S (tooltip)" at …/sortButton
QINFO  : …pseudoLocaleLeftToRightDark() untranslated main-photo-top-bar: "File order: name, ascending (accessible name)" at …/sortButton
QINFO  : …pseudoLocaleLeftToRightDark() untranslated main-photo-top-bar: "Settings, palettes and shortcuts (Ctrl+,). Find a command: Ctrl+Shift+P (tooltip)" at …/settingsButton
FAIL!  : TranslationCoverageUiTests::pseudoLocaleLeftToRightDark() 'plain.isEmpty()' returned FALSE. (Open a photo (Ctrl+O) (tooltip) at main-photo-top-bar/topBar/…/openButton …)
QINFO  : …pseudoLocaleLeftToRightDark() untranslated settings-browsing: "Restores the default pane placement, sizes and view, and turns off the lock and every freeze." at …/Note
QINFO  : …pseudoLocaleLeftToRightDark() untranslated settings-browsing: "Pane layout presets" at …/layoutPresetHeading
QINFO  : …pseudoLocaleLeftToRightDark() untranslated settings-browsing: "Saved for every window; applies to this window's docked panes. Never changes lock, freeze or displays." at …/layoutPresetScope
QINFO  : …pseudoLocaleLeftToRightDark() untranslated settings-browsing: "Pane layout preset (accessible name)" at …/layoutPresetChoice
QINFO  : …pseudoLocaleLeftToRightDark() untranslated settings-browsing: "Save pane layout as" at …/layoutPresetSaveLabel
QINFO  : …pseudoLocaleLeftToRightDark() untranslated settings-browsing: "Name for the new pane layout preset (accessible name)" at …/layoutPresetName
QINFO  : …pseudoLocaleLeftToRightDark() untranslated settings-browsing: "Pane layout name (placeholderText)" at …/layoutPresetName
QINFO  : …pseudoLocaleLeftToRightDark() untranslated settings-browsing: "Pane placement" at …/SectionLabel
FAIL!  : TranslationCoverageUiTests::pseudoLocaleLeftToRightDark() 'plain.isEmpty()' returned FALSE. (Restores the default pane placement, … at settings-browsing/…/layoutReset …)
Totals: 2 passed, 2 failed, 0 skipped, 0 blacklisted, 5352ms
```

At the tip, all 10 functions pass (`Totals: 10 passed, 0 failed, 0 skipped`). The full ctest results are below.

## Grant check

```
> git diff --name-only 04aef7c..7450392
docs/verification/STACK-F054-COVERAGE/part2/coverage-report.txt
docs/verification/STACK-F054-COVERAGE/part2/main-{grid,photo}-{ltr-dark-1180,ltr-light-560,rtl-dark-1180,rtl-light-560}.png (+ -expanded for the LTR four)
docs/verification/STACK-F054-COVERAGE/part2/settings-browsing{,-presets}-{ltr-dark-1180,ltr-light-560,rtl-dark-1180,rtl-light-560}.png (+ -expanded for the LTR four)
tests/translation_coverage_ui_tests.cpp
translations/README.md
translations/glancelot_en_XA.ts
ui/Main.qml
```

The handoff commit adds this file. Everything is inside the packet's grant: `ui/Main.qml` (strings only, under the brief's grant), the coverage test, `translations/` and the evidence folder. The grant's "Settings > Browsing strings" needed no QML edit. No shared header, CMake file, C++ source or other QML file changed (`git diff --stat 04aef7c..7450392 -- include src cmake CMakeLists.txt ui/SettingsDialog.qml` is empty).

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

## Captures (`docs/verification/STACK-F054-COVERAGE/part2/`)

Written by the coverage test with `GLANCELOT_F054_UI_EVIDENCE` pointing at the folder. Only the requested surfaces were kept: Settings > Browsing and the main window. All are in the `en_XA` pseudo-locale.

| Pass | Settings > Browsing | F049 preset block (scrolled) | Main window, photo and grid |
| --- | --- | --- | --- |
| LTR dark 1180 | `settings-browsing-ltr-dark-1180.png` | `settings-browsing-presets-ltr-dark-1180.png` | `main-{photo,grid}-ltr-dark-1180.png` |
| RTL dark 1180 | `settings-browsing-rtl-dark-1180.png` | `settings-browsing-presets-rtl-dark-1180.png` | `main-{photo,grid}-rtl-dark-1180.png` |
| LTR light 560 | `settings-browsing-ltr-light-560.png` | `settings-browsing-presets-ltr-light-560.png` | `main-{photo,grid}-ltr-light-560.png` |
| RTL light 560 | `settings-browsing-rtl-light-560.png` | `settings-browsing-presets-rtl-light-560.png` | `main-{photo,grid}-rtl-light-560.png` |
| Clipping (+40 %) | `…-ltr-dark-1180-expanded.png`, `…-ltr-light-560-expanded.png` | same | same |

`coverage-report.txt` records 582 catalog messages and no untranslated text on the named surfaces.
- **Clipping candidates:**
  - the elided C++ command label "Folder grid: details or thumbnails" (part 1, unchanged);
  - the metadata title in the 40 % pass (part 1);
  - the file name in the top-bar title in the 40 % compact pass. It elides by design (`ElideMiddle`) and is data.
- **Reported for follow-up, outside this packet's files:** `ui/PhotoGrid.qml` (or its components) still shows plain English. The affected text is "Folder grid", "Folder %1", "Filter files in folder (Ctrl+F)", "Filter files", "Move to Recycle Bin" and its description, "Items in %1", "%1, current photo", "Press Enter or double-click to open." and the "PNG" type badge.

Design review of the captures (application-interface-design, typography-ui, layout-spacing, responsive-layout, design-review):
- **Blocking:** none.
- **Important:** none.
  - At 560 px with 40 % longer text, the top bar adapts as designed: the view segments become their menu, the sort button yields its room, and Open, Folders and Settings stay whole.
  - No status-bar text is clipped. Rotate hides below 680 px, as before.
- **Polish (follow-ups):**
  - `DeleteFlow.trashName` ("Recycle Bin", "Trash") is not translated in `ui/DeleteFlow.qml`. It therefore appears in English inside the translated delete reasons and menu labels.
  - The canvas toast ("Untagged source assumed sRGB · display profile not yet managed") and the other status lines are C++ (`viewer.status`). They belong to the C++ extraction packet.
  - In RTL the combo box's text stays left-aligned and `UiSwitch` does not mirror. Both are part 1 follow-ups, unchanged here.

## Decisions taken

- **`withShortcut(text, id)` with one pattern, `"%1 (%2)"`.** The alternatives were a translatable `" (%1)"` suffix, which is still concatenation, or one message per tooltip including its key, which would put bindings into translations. One pattern keeps the bindings as data and lets a language reorder them.
- **Whole sentences instead of spliced words.** The fallback notices, the split-handle descriptions and the sort order name are full sentences per variant. A translator never sees "Showing the %1" with an English "grid" or "photo" substituted.
- **No plural forms (`%n`).** The lead waived plurals for F054 (part 1, E6 TODO). "%1 selected" and "%1 of %2" follow the existing "%1 of %2" message.
- **The status bar is found by its pane rectangle** rather than through a new `objectName`, because the Main.qml grant is strings only.
- **Report-only outside Main.qml.** The tree, path bar, grid and filmstrip components are not in this packet's grant. Their text is listed in the report for a follow-up packet, so the walk does not fail on files this packet may not edit.
- **Only the requested captures are committed.** The other surfaces' captures from the same run (appearance, monitor pages, metadata, rename, detached) belong to part 1 and are unchanged in substance.

## Open questions for the lead

- Schedule a follow-up packet for `ui/PhotoGrid.qml` (and the other grid components), `ui/DeleteFlow.qml`'s `trashName` and concatenated reasons (part 1 note), and the C++ status and command strings (part 1's C++ extraction list).
- `PaneLayout`'s C++ refusals "Unlock the layout before changing panes." and "Unfreeze this pane before changing its layout." (`pane_layout.cpp` ~165 and ~179) are plain literals, not `QCoreApplication::translate`. They belong on the C++ extraction list.

## Unverified

- No real RTL catalog: RTL is evidenced by layout direction with the pseudo-locale, as in part 1.
- No assistive-technology run (NVDA). Accessible names and descriptions are checked as properties only.
- During one evidence run (05:18), `pseudoLocaleRightToLeftDark` hit the 300 s QTest function timeout (`QFATAL … Test function timed out`), right after the LTR pass. Another session's gate started on this machine at about the same time. It did not recur: the function alone, LTR then RTL, the full binary twice with evidence and the ctest runs below all passed. If the gate sees it, it is a hang in the shared harness (an image open or a window grab), not in a string change.
