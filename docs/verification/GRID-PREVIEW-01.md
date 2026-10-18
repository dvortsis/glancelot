# GRID-PREVIEW-01 verification

Original implementation: branch `agent/grid-preview`, worktree `.worktrees/grid-preview`, build directory `build-grid-preview`. Continuation: branch `agent/preview-finish`, worktree `.worktrees/preview-finish`, build directory `build-preview-finish`. State: review; independent approval and integrated checks remain required. Contract: docs/GRID-PREVIEW-PLAN.md. Requirements: UX-15, IN-02, PERF-04, SYS-02.

The window gains a third content mode, **Both** (`"combined"`): the folder grid and the photo preview share the center. The two existing modes, **Photo** (preview only) and **Grid** (grid only), are unchanged. The mode, orientation and ratio are stored per window slot.

## Interfaces

- `PaneLayout` gained the pre-approved `contentMode` property, getter and `setContentMode()` exactly as frozen, plus a private `mode_` member. The mode is stored as `center.mode` and written through the existing field-level `_center` merge. Slot 0 keeps it for the session only. An unknown or non-string stored mode falls back to `"preview"` on its own, without touching the other center fields. `reset()` restores `"preview"`. The mode may change under the global lock and with frozen center panes; the split may not.
- No other shared header, root CMake file or `docs/WORK-QUEUE.yaml` was changed. The new commands live in the command registry in `src/app/viewer_controller.cpp`. That is a source file, not a header, but it is the shared command table, so the lead should review it.
- `ui/Main.qml` holds all combined-view state and items, in two clearly fenced blocks ("GRID-PREVIEW-01 …") to ease the DELETE-01 merge. No new QML files were added, because that would have needed a root `CMakeLists.txt` change. Smaller UI edits:
  - `ui/PhotoGrid.qml`: a `tabHandler` hook, start-up activation and the column count;
  - `ui/PaneArrangement.qml`: center frames in the combined view;
  - `ui/UiButton.qml`: a `cornerRadius` property;
  - `ui/UiIcon.qml`: `image`, `grid` and `split` glyphs.

## Behaviour and decisions

- **Commands:** `view-preview`, `view-grid`, `view-combined` and `combined-orientation` are remappable and are listed in command search and in Settings → Controls. Ctrl+Shift+G was free, so it is the default for `view-combined`. If a saved binding map already uses that chord, the existing loader leaves `view-combined` unbound and the user's binding wins (tested). `grid` keeps its meaning: from Both it goes to Grid, and from Grid back to Photo.
- **Top bar:** one outlined, segmented choice (Photo · Grid · Both). Its segments use a concentric 6 px radius inside the 8 px outline. Below 760 px, or when the top bar is docked vertically, it compacts to one button that opens a menu, showing each mode's name and shortcut.
- **Split:**
  - Uses the stored `orientation`. `ratio` is the grid's share of the split, so moving the grid to the other edge keeps its size.
  - The grid's saved edge decides its side: `right`/`bottom` put it at the trailing side or end. `left`/`top`, and `center` (the default), put it leading, giving a master–detail reading order.
  - Minimum extents are 280×200 for the grid and 240×160 for the preview.
  - Dragging the 8 px divider previews the change live and commits once on release, with the ratio as shown. Double-click centres it.
  - The keyboard equivalent is the focused divider. Arrows move it by 5 % and Home centres it. It overrides the window's Left/Right shortcuts while focused.
  - The divider is disabled, with an explanation, while the layout is locked, a center pane is frozen, or a fallback is shown.
- **Compact fallback (display-only, never persisted):**
  - When the saved orientation does not fit, the other orientation is tried. A notice says "Stacked to fit…" or "Placed side by side to fit…".
  - When neither fits, only the focused pane is shown, with a notice; Tab or F6 switches panes.
  - The notice shows when a fallback starts or the shown pane changes, then hides after 5 s. The PANES-01 recovery notice, by contrast, stays visible. The fallback notice is timed because it sits over content in a window that is already small. Lead: confirm.
- **Preview follows the grid cursor:**
  - A cursor move made in the grid (keys, pointer, or entering a folder) opens the cursor photo through `viewer.open()`, after a trailing 120 ms debounce. Focus does not move.
  - The controller's decode generation drops superseded results.
  - Changes that leave the cursor in place (Ctrl+A, Ctrl+Space, clearing the selection) never change the preview. Shift+arrow and Ctrl+click move the cursor (`DirectoryEntryModel::select` sets it), so the preview follows them. This is how "multiselection does not change the preview" was read.
  - Navigating from the preview itself (arrow keys, the wheel, palette Next/Previous) shows the viewer's photo until the grid cursor moves again. The opened photo carries the grid's "Viewing" tag, separately from the cursor and the selection.
- **Explicit states:** these cover the canvas, so a stale photo is never shown as the cursor's item.
  - **Folder:** folder icon, name, "Folder" and "Press Enter to open". No item count is shown: the grid model lists only the current folder, so it does not know a subfolder's count.
  - **Unsupported file:** file icon, extension, name and "Glancelot can't preview this file type". It is never decoded.
  - **Other states:** nothing selected, empty folder, inaccessible folder, loading folder, decode failure (with the controller's message), and no folder (an empty preview beside the grid's own "No folder open").
  - **Pending photo:** the previous photo stays visible. After 200 ms it is dimmed, with a "Loading name…" chip, so quick steps never flash.
  - **Folder change:** while a folder change is pending (`directoryPending`), the preview shows "Loading folder…" and the debounce is cancelled.
- **Focus and precedence:** modal, then text, then the focused pane.
  - Tab cycles grid → preview → divider → grid. F6 toggles between grid and preview. Escape on the preview returns to the grid; in full screen it still leaves full screen first. Escape in the grid (with no multi-selection) and Enter on a photo both move to the preview without leaving Both.
  - The focused pane draws a 2 px focus ring.
  - Window shortcuts are scoped by the focused pane (`gridScope`), so R, F and zoom act only when the preview has focus. Explicit routes (toolbar buttons, command search) run canvas commands whichever pane has focus; only grid-only mode refuses them, as before.
- **Bounds:**
  - No new thumbnail, queue or decode limits. In Both, the grid's normal thumbnail requests and the single current decode share the existing services.
  - The filmstrip is hidden in Both, because the grid already shows the folder.
  - Preview-only still deactivates the grid model. When the single-pane fallback hides the grid, its listing is released.
  - No decode is issued from resize handlers (tested).
  - Top and status bars are docked in Both, as in grid mode, so they never overlay the split.
- **Restoration:**
  - Mode, orientation and ratio restore per slot.
  - A grid that starts visible is now activated on completion. Before this fix, a restored Grid or Both view stayed inactive and showed "This folder is empty"; the real-app smoke run below found it, and `restartRestoresContentMode` now covers it.
  - If the photo opened at start-up arrives while the grid is still placing its first cursor, it becomes the cursor's target.
- **Column count:** the grid now measures columns on the full view width (`grid.width / pitch`, previously `(grid.width - 24) / pitch`). A half-width Both pane of about 360 px therefore gets two columns, not one. Grid-only column counts at the tested sizes are unchanged: 6 at 1180 px, and 3 at 540 px.

## Acceptance coverage

| Plan item | Evidence |
| --- | --- |
| Mode round-trips per slot; slot 0 session-only | `pane_layout_tests::contentModeRoundTripsPerSlot` |
| Unknown mode falls back individually | `unknownContentModeFallsBackIndividually` (string and numeric) |
| Mode allowed under global lock and frozen panes; ratio and orientation are not | `contentModeAllowedUnderLockAndFreeze`; `grid_ui_tests::splitOrientationDragAndKeyboard` (locked part) |
| Reset restores preview; concurrent merges keep the other window's mode | `resetRestoresPreviewAndMergesKeepOtherWindowsMode` |
| All three modes; switching by command, by keyboard and by pointer (segmented control and compact menu) | `combinedModesSwitchByCommandAndPointer` |
| Ctrl+Shift+G conflict check | `combinedShortcutYieldsToSavedBinding` |
| Debounced cursor preview; bounded decodes for rapid moves; the newest wins; the grid keeps focus | `cursorMoveUpdatesPreviewAfterDebounceAndNewestWins` (see the output below) |
| Folder and unsupported cursors show explicit states and never decode | `folderAndUnsupportedCursorShowStatesAndNeverDecode` |
| Corrupt image and inaccessible directory have explicit preview states | `failedAndInaccessiblePreviewStates`; generated invalid PNG and nonexistent directory |
| Multiselection does not change the preview | `multiSelectionDoesNotChangePreview` |
| Tab/F6 focus, focus ring; precedence with a modal open and with text focus; scoped and explicit canvas commands | `cursorMoveUpdatesPreviewAfterDebounceAndNewestWins`, `combinedCommandPrecedenceModalTextAndFocus` |
| Horizontal and vertical splits; ratio drag committing once on release; keyboard equivalent; grid edge placement | `splitOrientationDragAndKeyboard` |
| A folder change while a preview decode is pending discards the stale result | `folderChangeDiscardsPendingPreview`. A 6000×4000 PNG decode is pending when `navigateDirectory` is called. The preview never reports `""` for it, and the newest photo wins. |
| Restart restores the mode (and orientation and ratio); no-folder state; a grid that starts visible lists its folder | `restartRestoresContentMode` |
| Compact recovery does not persist | `compactRecoveryDoesNotPersist`. Reoriented and single-pane fallbacks; no `changed` signal; the slot's saved center object is byte-identical before and after. |
| Fit and physical zoom survive a split resize; no decode from resize | `fitAndPhysicalZoomSurviveSplitResize` |
| Dark/light, compact/wide, horizontal/vertical captures | `reviewCapturesCombined` and `compactRecoveryDoesNotPersist` behind `GLANCELOT_REVIEW_CAPTURE` |

The existing test `everyPaneDockRegionStaysInWindow` was changed by one line. It now sets `gridMode` after `layout.reset()`, because a reset now restores the Photo mode, as the contract requires.

## Commands and actual output (Windows 11, MSVC 2022, Qt 6.8.3)

```text
> ./scripts/build.ps1 -BuildDir build-grid-preview -QtRoot C:\src\Github\Glancelot\.tools\Qt\6.8.3\msvc2022_64 -Test
 1/11 Test  #1: navigation .......................   Passed    2.86 sec
 2/11 Test  #2: directory_listing ................   Passed   15.89 sec
 3/11 Test  #3: viewer_acceptance ................   Passed    4.71 sec
 4/11 Test  #4: thumbnails .......................   Passed    5.81 sec
 5/11 Test  #5: folder_tree ......................   Passed    7.17 sec
 6/11 Test  #6: pane_layout ......................   Passed    0.59 sec
 7/11 Test  #7: window_slots .....................   Passed   25.15 sec
 8/11 Test  #8: folder_grid ......................   Passed   74.18 sec
 9/11 Test  #9: directory_model ..................   Passed   49.60 sec
10/11 Test #10: viewport .........................   Passed    0.27 sec
11/11 Test #11: themes ...........................   Passed    0.31 sec
100% tests passed, 0 tests failed out of 11
Total Test time (real) = 186.57 sec
```

CONTINUATION_WINDOWS_RESULTS

```text
> build-grid-preview\pane_layout_tests.exe
PASS   : PaneLayoutTests::contentModeRoundTripsPerSlot()
PASS   : PaneLayoutTests::unknownContentModeFallsBackIndividually()
PASS   : PaneLayoutTests::contentModeAllowedUnderLockAndFreeze()
PASS   : PaneLayoutTests::resetRestoresPreviewAndMergesKeepOtherWindowsMode()
Totals: 12 passed, 0 failed, 0 skipped, 0 blacklisted, 257ms

> build-grid-preview\grid_ui_tests.exe   (full case list; provider-failure warnings filtered)
QINFO  : GridUiTests::cursorMoveUpdatesPreviewAfterDebounceAndNewestWins() rapid moves: debounce fires 1 decodes 1
QINFO  : GridUiTests::splitOrientationDragAndKeyboard() dragged ratio 0.627
QWARN  : GridUiTests::compactRecoveryDoesNotPersist() DelegateModel::cancel: index out range 24 0
SKIP   : GridUiTests::auditTallGridQueueBound() Native window manager clamped the window; rerun with QT_QPA_PLATFORM=offscreen
SKIP   : GridUiTests::reviewCapturesMixedFolder() Set GLANCELOT_REVIEW_CAPTURE to write review captures
SKIP   : GridUiTests::reviewCapturesCombined() Set GLANCELOT_REVIEW_CAPTURE to write review captures
Totals: 49 passed, 0 failed, 3 skipped, 0 blacklisted, 69038ms
```

The `DelegateModel::cancel` warning comes from Qt. It appears when the single-pane fallback hides the grid while delegates are still incubating: the hidden grid drops its model, as it does in Photo mode. It has no functional effect.

Real application smoke run (`glancelot.exe`, a generated folder, slot 1 saved as Both at a 0.45 ratio):

```text
> glancelot.exe <tmp>\gp-media\photo2.png --quit-after 4000 --report r.json --screenshot s.png --window-size 1180x780
exit 0 (no stderr)
window_slot 1, grid_live_cells 6, image_path …/photo2.png, thumbnail_pending 0, thumbnail_cache_bytes 983040
```

The screenshot shows the restored Both view: the grid at 45 % with real ThumbnailService previews and the cursor on the opened photo, the preview showing that photo, and the grid pane focus ring.

CONTINUATION_LINUX_RESULTS

## Review captures (self-inspected)

`GLANCELOT_REVIEW_CAPTURE=1` writes these to `build-grid-preview/.cache/grid-preview/`:

- `combined-{horizontal,vertical}-{dark,light}-{1180,540}.png`
- `state-folder-{dark,light}.png`, `state-unsupported-{dark,light}.png` and `state-folder-dark-540.png`
- `handle-focus-{dark,light}.png`
- `combined-light-760.png` and `combined-light-759.png` (the segmented-to-menu threshold)
- `mode-menu-light-540.png`
- `fallback-single-preview-540.png`

### Self design review (design-review skill, against layout-spacing, layout-grids, typography-ui, component-geometry, iconography-ui, motion-ui and responsive-layout)

- **Fixed after the first capture pass**
  - The compact mode menu was narrower than its longest row and elided "Grid and photo together", and the button's tooltip covered the first row. The menu is now 344 px wide, kept inside the window by 8 px margins, and the tooltip is suppressed while the menu is open.
  - In the single-pane fallback, the status bar showed grid items ("1 of 0") while the grid was hidden. Grid status items now follow `_gridShown`.
  - The focused divider was a barely visible 4×32 px grip. Focus now draws a 6×48 px grip in the focus-ring colour, and the divider line turns primary.
  - A half-width grid pane at 760 px had one huge column. The column formula now gives two.
- **Remaining (polish, not blocking)**
  - The 2 px pane ring around the grid sits alongside the cursor tile's own focus ring. Together they read clearly in both themes, but they are a little heavy in dark mode.
  - In Both, the status bar's primary "Open" button competes slightly with the zoom controls at 1180 px.
  - At 540×360 the grid pane shows one column of compact tiles beside a 260 px preview. That is usable, but the composition is tight.
- **Strengths**
  - Spacing, radii (8/12/4 px, plus the concentric 6 px segments) and the type ramp are token-only.
  - One outline icon family is used, with the three new glyphs on the same 16-unit grid and 1.5 stroke.
  - Every non-photo state has one message and, for folders, one action.
  - Loading feedback is delayed by 200 ms, so stepping never flashes. The only continuous motion is the existing spinner.

## Unverified

- Physical high-DPI or mixed-DPI monitors, touch input, and live assistive technology (accessible names, roles and descriptions are set, but no screen reader was run).
- Physical Linux windowing. Docker runs the UI tests on the offscreen platform.
- Reduced motion: no new animation was added. The delayed scrim is a visibility switch, not a transition.
- Debounce timing under heavy system load. The bound is asserted as at most two decodes; one was measured.
