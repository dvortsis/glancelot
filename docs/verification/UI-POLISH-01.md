# UI-POLISH-01 — UI/UX review, shared tokens and unified Settings

Branch `agent/ui-polish`, implementation commit `e6b20c6`, base `main` `5cd39fe`. Windows 11, Qt 6.8.3 MSVC2022, project-local toolchain.

## Scope

- Qt Quick Controls stays on the **Basic** style. `ui/Theme.qml` (singleton) holds every design token: catalog palette roles plus derived `pressed`, `selection`, `focusRing`, `scrim`; spacing ramp 2/4/8/12/16/24/32; radii 16 (dialogs), 12 (cards, toasts), 8 (controls, rows), 4 (tags, swatches); control 32, row 28, bar 48, icon 16; type 11/12/13/15/20 plus display 36; motion 150/200 ms.
- Canonical primitives restyle only the controls the app uses: `UiButton` (quiet / primary / outline, checkable, icon-only with tooltip and accessible name), `UiTextField`, `UiComboBox`, `UiSwitch`, `UiDialog`, `UiScrollBar`, `UiIcon` (one outline family, 16-unit grid, 1.5 stroke) and `SettingRow`.
- Folder tree rebuilt as a standard tree on the unchanged flat `FolderTreeModel` contract.
- Top bar: **Themes** and **Commands** removed. Settings now has Appearance (mode and palettes), Viewing, Browsing, Controls (the command palette: search, Enter runs, remap or clear shortcuts, grid-scoped commands dimmed with an explanation) and About.
- Typography: Geist and Geist Mono (SIL OFL 1.1) are bundled per `typography-ui`, with a system fallback chain.

## Design review (ui-design-skills: design-review and subject skills)

Findings from the pre-change UI, by severity, and how each was resolved.

**Blocking**
- Light-theme empty state: the title and copy used the theme text colour on the always-dark `#191919` photo backdrop, which made them unreadable in every light family. *Fix:* with no photo open, the empty state sits on `Theme.background`.
- Command palette and settings duplicated responsibilities (palette dialog, Controls section, separate Themes dialog), and two top-bar entries opened overlapping surfaces. *Fix:* one Settings dialog with sections. `palette` opens Controls with search focused; `--show-themes` opens Appearance.

**Important**
- Geometry: radius 4 everywhere, no container/control/tag distinction. *Fix:* 16/12/8/4 tokens.
- Spacing one-offs (14, 6, 58, 52). *Fix:* ramp tokens; bars are 48.
- Every control restyled inline with repeated recipes (three copies of the tool-button background). *Fix:* single primitives.
- Focus indication was inconsistent: primary border on buttons, text ring on the tree and filmstrip. *Fix:* `focusRing` is the text colour everywhere, distinct from primary, which marks selection and the current item.
- Glyph icons (`✓`, `↑↓`, text "Pin"/"Parent") were mixed with a one-off Shape chevron. *Fix:* one outline icon set; icon-only buttons carry tooltip and accessible name.
- Tree: no folder glyphs or indent guides; the 16 px indent elided deep names to fragments; no way to widen the panel. *Fix:* standard tree anatomy, 12 px indent, guides, drag-to-resize edge (double-click resets), full-name tooltip when truncated, double-click expands, PageUp/PageDown.
- Fit-mode setting always showed "Shrink only" whatever the canvas state. *Fix:* bound to `canvas.fitMode`, "Custom zoom" after manual zoom, plus "Actual size".
- The status toast duplicated the empty-state message and could cover an unpinned folder panel. *Fix:* the unchanged start-up status is suppressed while the empty state shows, and the toast centres on the uncovered canvas.
- Disabled commands gave no reason (IN-04). *Fix:* dimmed rows with tooltip, accessible description and a visible note in grid view.

**Polish**
- Uppercase tracked section labels replaced by sentence-case small semibold labels.
- Tabular numerals for counters and zoom.
- Sort direction shown with an icon rather than an arrow glyph.
- Settings strip at narrow widths drops icons and the duplicate page heading. Dialog margins tighten in short windows.

**Defects found during implementation and fixed**
- A QML property named `onPrimary` is parsed as a signal handler and read as black. The token is renamed `textOnPrimary`.
- `Shape.CurveRenderer` icons ignored the ListView clip and leaked over the header and bottom bar. Switched to `GeometryRenderer` with antialiasing.

**Strengths kept:** scoped keyboard maps, HUD keep-open logic, virtualization and hidden-model detachment, non-colour state cues in the tree (current marker, bold name, state words).

## Verification

Full build and tests, worktree `build-ui`:

```
PS> .\scripts\build.ps1 -BuildDir build-ui -QtRoot C:\src\Github\Glancelot\.tools\Qt\6.8.3\msvc2022_64 -Test
1/8 Test #1: navigation .......................   Passed    2.06 sec
2/8 Test #2: viewer_acceptance ................   Passed    4.60 sec
3/8 Test #3: thumbnails .......................   Passed    6.23 sec
4/8 Test #4: folder_tree ......................   Passed    5.97 sec
5/8 Test #5: window_slots .....................   Passed   19.89 sec
6/8 Test #6: folder_grid ......................   Passed    9.95 sec
7/8 Test #7: viewport .........................   Passed    0.04 sec
8/8 Test #8: themes ...........................   Passed    0.12 sec
100% tests passed out of 8
```

`grid_ui_tests` (folder_grid): `Totals: 17 passed, 0 failed`. No QML reference, type or binding errors in the log; remaining warnings are harness noise (qrc brand art absent from the test binary, no RHI under the software backend, deliberately failing thumbnail provider).

New tests:
- `settingsMergesThemesAndCommands`
  - The top bar has no Commands, Themes or Menu buttons.
  - `palette` opens Settings > Controls with the search field focused; typing "rotat" and pressing Enter runs `rotate` and closes the dialog.
  - `openThemes()` opens Appearance; Escape closes it.
  - In grid view the same search does not run `rotate` and the dialog stays open; Escape then returns focus to the grid.
  - Saves 20 section captures (wide and compact, dark and light) to `.cache/review/`.
- `treeRowClickEnterAndChevron`
  - A chevron click expands a folder without opening it.
  - A name click opens the folder.
  - Enter on the keyboard cursor opens the selected folder.
  - A double-click toggles expansion once.

Native screenshots (`glancelot.exe --no-resume --window-size … --theme-mode … --theme … --screenshot …`, scratch `GLANCELOT_CONFIG_DIR`, generated media) were inspected:
- 1180×780: empty state, dark and light.
- 1180×780, dark: pinned tree.
- 1180×780, light, `forest`: pinned tree.
- 1180×780, dark: Appearance.
- 1920×1080: grid, light `ocean` and dark `amber`.
- 540×360, light, `terracotta`: Settings.
- 540×360, dark, `rose`: tree.

## Limitations

- Windows only; the Linux GUI and Docker image were not rebuilt for this change.
- Reduced-motion preference: Qt 6.8 exposes no platform reduced-motion hint to QML. Motion is limited to 150–200 ms state changes and the tree's loading spinner, and nothing waits on an animation.
- The tree panel width set by dragging is per session and not persisted.
- Screen-reader output was checked by reading roles and names, not with a live screen reader.
- The independent review result is recorded in WORK-QUEUE when it lands.
