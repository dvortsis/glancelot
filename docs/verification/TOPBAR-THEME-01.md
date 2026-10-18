# TOPBAR-THEME-01 verification

Branch `agent/titlebar-finish` (continues `agent/topbar-theme`), worktree `.worktrees/titlebar-finish`, build `build-titlebar`, base `ef34492`, with `agent/continuation` (FILE-GRID-01, `b63e931`) merged in so the evidence below is on the combined tree. Requirements: UX-18, UX-01 and UX-11. State: implemented and awaiting independent review.

The top toolbar and the application-drawn title bar take every color from the existing `Theme` roles, and they follow live theme changes. On Windows, the native OS caption of a decorated window is matched to the same roles.

## What changed

- **Window controls.** Frameless mode, or full screen, now has Minimize, Maximize/Restore and Close controls. They are `UiButton`s on the shared 16-unit, 1.5-stroke icon grid; three glyphs were added: `minimize`, `maximize` and `restore`.
  - Close uses a new quiet `danger` variant. While hovered or pressed it fills with `Theme.danger`, and its glyph switches to `Theme.background`.
  - Full screen keeps Minimize and Close and hides Maximize.
- **Moving the frameless window.** Empty bar space is the title bar. A `DragHandler` calls `startSystemMove()`, and a `TapHandler` double-tap toggles maximize. Before this change, a `MouseArea` sat underneath the `ScrollView` and started a system move on every press.
- **Active and inactive window.** In an inactive window, the title label and the window-control glyphs switch from `text` to `muted`, the same as a native caption. Both roles reach 4.5:1 on `surface`.
- **UX-01 safeguard.** In frameless mode the top bar stays visible even if its pane is hidden in the pane settings. Otherwise a frameless window could not be moved or closed with the pointer.
- **Narrow windows.** Below 720 px the "Show frame" button gives up its space, so the window controls are never clipped. The command remains available in Settings, in command search and through its shortcut. In the same narrow horizontal bar the "Layout locked" indicator collapses to its lock icon; its name stays the tooltip and accessible name. Without this, a locked layout at 540 px pushed Close past the bar edge.
- **Selected marker while pressed.** A checked or selected quiet button that is held down draws its marker in `Theme.text` instead of `Theme.primary`. The pressed fill replaces the selection fill, and `primary` on `pressed` fell to 2.74:1 in Olive light and Moss light. `text` on `pressed` is already required at 4.5:1 or more, so the marker stays visible in every variant; the change lasts only for the press, and the marker shape keeps the "on" state readable without relying on hue.
- **Native caption (Windows).** `src/app/native_title_bar.hpp` is header-only, so no CMake change was needed. It sets the following attributes:
  - `DWMWA_USE_IMMERSIVE_DARK_MODE` (attribute 20, or 19 on builds before 20H1).
  - On Windows 11: `DWMWA_CAPTION_COLOR` = `surface`, `DWMWA_TEXT_COLOR` = `text` (or `muted` while the window is inactive) and `DWMWA_BORDER_COLOR` = `border`.
  - The colors are re-applied after any of these: a theme change, activation, an OS color-scheme change (which Qt answers by resetting its own dark frame), toggling frameless mode, and native surface recreation.
  - `dwmapi` is linked through `#pragma comment(lib, "dwmapi")`.
- **Report and test options.** `--report` now records `frameless`, `window_active` and the `native_title_bar` attribute results. `--frameless` starts in frameless mode, for test automation.

There are no shared-header, CMake or catalog changes. The audit found no hard-coded color in the top bar. The brand mark already switches between `icons/app/dark` and `icons/app/light` on `Theme.dark`, and the tests now cover that switch.

## Acceptance coverage

| Item | Evidence |
| --- | --- |
| Tokens drive the surface, text, icons, separators and window background, live, in all 80 variants | `grid_ui_tests::topBarFollowsEveryThemeVariantLive` (bar, edge, divider, title, button foreground, control glyphs, brand-mark variant, window color and disabled opacity, checked after each live switch) |
| Hover, pressed, focus and danger-hover states follow a live switch while the state is held | `topBarStatesFollowLiveThemeChanges` (pointer held on Close during the mode switch; focus ring color and width) |
| Contrast of the top-bar roles, all 80 variants | `theme_tests::topBarRolesMeetContrastInEveryVariant`: text/surface, muted/surface, text/hover and muted/hover at 4.5:1 or more; icons, focus ring, primary marker, close glyph on danger and danger/surface at 3:1 or more |
| Contrast of the derived roles from the live `Theme` singleton | `topBarFollowsEveryThemeVariantLive`: text/pressed and text/selection at 4.5:1 or more; focus ring and on-marker/selection at 3:1 or more; the real `selectionMarker` of a selected, held Settings button against its pressed background at 3:1 or more (asserted, minimum 7.10:1), returning to `primary` on release |
| Native caption colors map to the roles; COLORREF byte order; failure on a missing or destroyed handle | `theme_tests::nativeTitleBarColorsFollowTokens`, `nativeTitleBarAppliesOnWindows` (a real HWND: Windows 11 build 26200 accepts all four attributes, and the dark-mode attribute reads back) |
| Window controls perform their actions; double-click maximizes; minimize, full screen, hidden pane, Show frame, close; 540 px fit, also with the layout lock indicator shown | `grid_ui_tests::framelessWindowControlsDriveTheWindow` (offscreen) and the native run below |
| Inactive window mutes the title and glyphs, and a live switch while inactive leaves no stale color | `inactiveWindowMutesTitleBar` |
| Dark/light, 540/1180, framed/frameless, active/inactive captures | `topBarReviewCaptures` (opt-in, client area, 16 files) and the native screen captures (16 files, including the OS caption) |
| Drag, resize, maximize, restore, minimize and close on a real window | `tests/topbar_native_check.ps1 -Interact` (output below) |

## Commands and actual output

Full suite on the merged tree at `7c826f3` (Windows 11 build 26200, Qt 6.8.3, MSVC 2022):

```text
./scripts/build.ps1 -BuildDir build-titlebar -QtRoot C:\src\Github\Glancelot\.tools\Qt\6.8.3\msvc2022_64 -Test
 1/11 Test  #1: navigation .......................   Passed    1.50 sec
 2/11 Test  #2: directory_listing ................   Passed    5.11 sec
 3/11 Test  #3: viewer_acceptance ................   Passed    4.40 sec
 4/11 Test  #4: thumbnails .......................   Passed    5.49 sec
 5/11 Test  #5: folder_tree ......................   Passed    6.92 sec
 6/11 Test  #6: pane_layout ......................   Passed    0.19 sec
 7/11 Test  #7: window_slots .....................   Passed   20.48 sec
 8/11 Test  #8: folder_grid ......................   Passed   63.46 sec
 9/11 Test  #9: directory_model ..................   Passed   18.67 sec
10/11 Test #10: viewport .........................   Passed    0.06 sec
11/11 Test #11: themes ...........................   Passed    0.14 sec
100% tests passed, 0 tests failed out of 11
Total Test time (real) = 126.41 sec
```

The previous full run (before the merge) failed in `folder_grid`. Run directly, the cause was `framelessWindowControlsDriveTheWindow`: `'rightEdge(close) <= bar->width() - 8' returned FALSE` once the layout was locked at 540 px. The lock indicator fix above resolves it. A mutation check that put the marker back to `Theme.primary` fails as intended: `'markerOnPressed >= 3' returned FALSE. (olive light on-marker/pressed)`.

Theme tests (`theme_tests.exe -o theme-out.txt,txt`, `QT_QPA_PLATFORM=offscreen`, merged build), relevant lines:

```text
QINFO  : ThemeTests::topBarRolesMeetContrastInEveryVariant() border/surface separator contrast across 80 variants: min 1.27, max 2.01
PASS   : ThemeTests::topBarRolesMeetContrastInEveryVariant()
PASS   : ThemeTests::nativeTitleBarColorsFollowTokens()
QINFO  : ThemeTests::nativeTitleBarAppliesOnWindows() Windows build 26200: dark=1 caption=1 text=1 border=1
PASS   : ThemeTests::nativeTitleBarAppliesOnWindows()
Totals: 12 passed, 0 failed, 0 skipped, 0 blacklisted
```

UI tests (`grid_ui_tests.exe -o out.txt,txt`, offscreen software backend, merged build), top-bar lines and totals:

```text
QINFO  : GridUiTests::topBarFollowsEveryThemeVariantLive() on-marker/pressed contrast across 80 variants: min 7.10
PASS   : GridUiTests::topBarFollowsEveryThemeVariantLive()
PASS   : GridUiTests::topBarStatesFollowLiveThemeChanges()
PASS   : GridUiTests::framelessWindowControlsDriveTheWindow()
PASS   : GridUiTests::inactiveWindowMutesTitleBar()
Totals: 42 passed, 0 failed, 2 skipped, 0 blacklisted, 62796ms
```

The two skips are the opt-in capture tests (`topBarReviewCaptures`, `reviewCapturesMixedFolder`); they were not re-run on the merged tree.

The native results and captures below come from the earlier session's build of `bc99e80`. They were not re-run after the marker, lock-indicator and merge changes, because the script moves the real pointer and takes focus on the desktop.

Native run on a real desktop (2560 x 1440 at 100%, with the work area excluding the taskbar). The run used `tests/topbar_native_check.ps1 -Exe build-topbar-theme/glancelot.exe -OutDir build-topbar-theme/.cache/topbar-native -Interact`, with generated media, an isolated config directory and `--no-resume`:

```text
frameless drag on empty title-bar space: frame 682,275,1878,1094 -> 790,329,1986,1148
frameless right-edge resize: width 1196 -> 1116
frameless bottom-edge resize: height 819 -> 759
frameless double-click: frame 790,329,1906,1088 -> 0,0,2560,1392 (work area 0,0,2560,1392, IsZoomed=False)
frameless second double-click: frame -> 790,329,1906,1088 (restored: True)
frameless maximize button: frame -> 0,0,2560,1392 (work area 0,0,2560,1392)
frameless restore button: frame -> 790,329,1906,1088 (restored: True)
frameless minimize button: IsIconic=True
frameless restored from taskbar state: IsIconic=False
frameless close button: process exited=True code=0
framed drag on empty title-bar space: frame 689,275,1871,1087 -> 809,335,1991,1147
framed double-click: frame 809,335,1991,1147 -> 0,0,2560,1392 (work area 0,0,2560,1392, IsZoomed=True)
framed second double-click: frame -> 809,335,1991,1147 (restored: True)
```

Without `-Interact`, the script took 16 screen captures (`native-{dark,light}-{540,1180}-{framed,frameless}-{active,inactive}.png`). Every launch reported `inactive state confirmed: True` and exit 0. The reports show `"native_title_bar": {"dark_mode": true, "caption_color": true, "text_color": true, "border_color": true}`.

## Capture review

I inspected the captures myself.

- **Framed captures.** The native caption has the toolbar's `surface` color, so caption and bar read as one band. The caption buttons follow the light or dark frame.
  - Inactive dark windows show the caption title and the in-app title in `muted`.
  - On inactive light windows, Windows dims the caption glyphs itself.
- **Frameless captures.** The 540 px window shows Minimize, Maximize and Close inside the bar, and the title is elided in the middle. At 1180 px the bar also shows "Show frame".
- **Close hover.** It shows the `danger` fill with a `background`-colored glyph and the "Close window" tooltip.
- **Fixed during review.** The first 540 px frameless capture clipped Maximize and Close off the bar. "Show frame" now gives up its space below 720 px, and a regression assertion covers this.

## Limits and findings for the lead

- **Catalog finding (not changed here).** `scripts/generate-themes.py` `enrich()` gives every family the same slate-tinted `hover` (`#334253` dark, `#E6EDF4` light) and `muted` (`#B5C0CC`, `#526071`). Hover and pressed fills are therefore slate-blue in Graphite, Ruby, Amber and the other non-slate families. That weakens "consistently match the selected theme". The catalog is shared, and regenerating it re-opens THEME-01 evidence and affects the grid and tree, so I left it alone.
  - Proposal: derive both roles from the family hue, as the other roles are:
    - `hover = color(h, saturation, .20 if dark else .935)`
    - `muted = color(h, .12 if not neutral else 0, .76 if dark else .38)`
  - Keep Slate & Ice's current values, then re-run `theme_tests` (which already asserts text/hover and muted/hover at 4.5:1 or more).
- **On-marker against pressed (resolved).** `primary` on `pressed` was 2.74:1 in Olive light and Moss light. The held marker now uses `text`, and the test asserts at least 3:1 in all 80 variants (minimum 7.10:1). At rest the marker is still `primary`, at 3:1 or more against `selection` and `surface` in every variant.
- **Separators** (`border` on `surface`) measure 1.27 to 2.01:1. They are decorative dividers, which WCAG 1.4.11 exempts. The test asserts only that they differ from the surface, and the locked Slate & Ice border is unchanged.
- **Windows 10 (unverified).** The caption, text and border color attributes need Windows 11 build 22000. On Windows 10 only the light/dark frame follows the theme, and the caption keeps the system color. The helper reports those attributes as not applied (`false`), which the unit test expects on non-Windows 11. No Windows 10 machine was available.
- **Linux, X11/Wayland and macOS decorations** are drawn by the window manager or compositor and are unchanged. On those platforms the helper is a no-op, and the theme reaches only the application-drawn frameless title bar. No native Linux GUI run was done.
- **Frameless maximize.** On Windows, Qt implements it by geometry: the frame equals the work area, so the taskbar stays visible, but `IsZoomed` is false. Behavior is correct, and the app's Restore and double-click both restore the previous geometry.
- **Toggling frameless at runtime.** Qt keeps the outer frame size, so the client area grows by the old frame (for example 1180 x 780 becomes 1196 x 819). This was already Qt's behavior before this task and is unchanged.
- **Full screen, framed and native-caption fidelity.**
  - Full screen was checked offscreen, not natively.
  - The opt-in `grabWindow` captures are client-area only. The OS caption appears only in the native screen captures.
  - Fractional DPI, high contrast mode and screen-reader output of the new controls were not exercised. The controls carry accessible names.
- **Offscreen captures.** The photo canvas is blank because the offscreen software backend has no RHI. The native captures show the photo.
