# MONITOR-CUSTOM-01 at 2f73c84: CHANGES REQUIRED

This is a source and evidence review only. I read the code, tests, screenshots and result files. I ran no builds or tests and edited nothing. Windows and Docker gates are unverified: the output I saw is the focused offscreen/software run plus one single-display native D3D11 UI run. The advisor call timed out, so none of this was cross-checked.

## Blocking

**1. Pane freeze has two owners that can disagree.**
- The coordinator keeps its own `frozen` flag in the monitor slot and mirrors it into the dock (`src/app/monitor_customization.cpp:329`).
- `paneRoutes()` reports frozen if either flag is set (`:252`), and `editable()` refuses if either is set (`:91`).
- The existing UI still calls `paneLayout.freeze` and `paneLayout.reset()` directly (`ui/SettingsDialog.qml:430`, `ui/SettingsDialog.qml:450`, `ui/PaneArrangement.qml:85`, `ui/Main.qml:614`). Those only clear the layout flag.
- Reproduce:
  1. Run `pane-freeze-grid`, or press Freeze in Monitor settings.
  2. Open Settings > Panes and switch off "Freeze this pane", or press Space in arrangement mode, or press "Reset workspace layout".
  3. The layout now says unfrozen, but `paneRoutes.grid.frozen` is still true. Detach, re-dock and geometry changes fail with "Unfreeze the pane before moving it", and the Monitor tab still shows "Unfreeze". The stuck flag persists across restart.
- Nothing tests this, including the key/pointer/palette parity test. Make the layout the only owner of `frozen`, or route every freeze and reset change through the coordinator.

**2. The shipped native screenshots show a visible UI defect.**
- On the Panes page, `dark-1280-page2.png` and `dark-720-page2.png` show the "Lock workspace layout" label overlapped by the switch ("Loc…rkspace layout").
- `ui/MonitorSettings.qml:125` uses `UiSwitch` without `Layout.fillWidth: true`. `UiSwitch.qml` is built as a full-width row, and `SettingsDialog.qml:425` sets fillWidth. That omission is the likely cause.
- The three switches on the Presentation page (`ui/MonitorSettings.qml:113`) have the same omission. They sit below the fold in every captured screenshot, so they are unverified.
- The same pages also fail the visible-labels requirement (see item 4).

**3. Required acceptance tests are missing, and some paths have never run.**
The plan requires all coordinator methods plus one, two and three mocked displays.
- `swapControllerAndOutput` has only the negative case (`tests/monitor_customization_tests.cpp:124`).
- `moveOutput` is only called with the first display or an absent one.
- Neither positive path has run on any test display. Coordinator tests read `QGuiApplication::screens()` directly. The native run saw only "Display 1 · P27h-20".
- Topology coverage tests only the pure `place()` and `clamp()` helpers (`tests/monitor_customization_tests.cpp:213`). There is no real-coordinator unplug/replug, weak-identity or recovery test.
- Windowed output has no test. `DisplayManager::openPresentation` sets the screen-sized geometry (`src/app/display_manager.cpp:443`), then `applyCustomization` calls `showNormal()` (`src/render/presentation_window.cpp:121`). I expect a screen-sized client area with an off-screen title bar, but that is from reading, not running.
- Detach tests assert selection and the decoded image only. Plan §3 also names focus subject, scroll and content state, and the tests check none of them.
- The renderer-incarnation test still exercises only the counter class (`tests/monitor_customization_tests.cpp:225`).

## Important

4. **Missing visible labels.** The pane destination combo (`ui/MonitorSettings.qml:127`), the saved-preset combo (`:161`) and the preset-type combo (`:163`) have none. `light-1280-page3.png` shows a blank unlabeled dropdown. "Choose a display above" points at a control on another tab. Combos also show raw tokens such as `shrink` and `independent`.
5. **Dragging a detached host to another display probably snaps back (from reading, not run).**
   - `updatePaneGeometry` records only the geometry.
   - `paneRoutes()` clamps `effectiveGeometry` into the work area of the stored `displayId` (`src/app/monitor_customization.cpp:249-251`).
   - The host re-applies that geometry on release (`ui/DetachedPaneHost.qml:40-42`).
   - Related: `commit` is single-flight, so a second drag during a pending save is rejected and the window snaps back.
6. **Default output appearance changes.**
   - Default `background=theme` makes `applyOutput` colour the output with the controller theme (`:96`).
   - Before this change the output was pure black (`presentation_window.cpp:65`). In the light theme that is now a white letterbox on a reference display.
   - Default `fullscreen` now calls `showFullScreen()` instead of a borderless window at screen geometry.
   - This may conflict with "preserve existing monitor behavior". Check it against `monitor_output_tests` when the gate runs, or default to solid `#000000`.
7. **Locking and freezing are blocked while the slot is invalid or newer.**
   - `commit` refuses whenever the slot is `invalid`, `unsupported` or `pending` (`:120-122`).
   - `setLayoutLocked` returns void, so an unlock fails with feedback only.
   - The existing PaneLayout lock still works, so this is not a trap, but it is a gap.

## Minor and design questions

- **Folder hold.** After a folder change, the output stays held even if the user returns to the held folder (`display_manager.cpp:525-534`). Only a policy change, a mode change or close clears it. Confirm that is intended.
- **Pinning while held.** `pinCurrentImage` pairs the displayed older image with the advanced `currentGeneration` (`:576`).
- **Partial view-state change.** `ImageCanvas::setViewState` applies rotation before it validates the rest and returns (`image_canvas.cpp:318-331`). The coordinator pre-validates, so this is latent.
- **Viewport echo.** The controller viewport is echoed back to the output after the `syncing` guard drops. It is harmless because `m_userViewport` is false.
- **Display identity cost.** `computeDisplayId` does O(n²) `EnumDisplayDevicesW` calls on the GUI thread (`display_manager.cpp:344`, `src/app/monitor_customization.cpp:34`). It is bounded and the route cache helps, but it caused the earlier native hangs.
- **Display picker.** "Connected" is hard-coded, and there is no connection type (`ui/MonitorSettings.qml:65`). Pinned-disabled controls lack a per-control explanation. `targetPane` is set but never used.
- **Import limit.** Merging more than 16 `outputsByDisplay` rejects the whole import, with an explicit message (`settings_file.cpp:1102`).
- **Test logging.** `qInfo` step prints remain in `nativeUiEvidence`. A grep of product sources is clean of debug logging.

## Prior early-review items (079a)

| Item | Status at 2f73 |
|---|---|
| B1 | Fixed: the include is at the top of the test files. |
| B2 | Fixed by reading: `applyOutput` no longer closes a presentation. |
| B3 | Fixed and tested: close clears `folderHeld` and `followFolder`. |
| B4 | Fixed at coordinator level by reading (`:99-116`). Only the negative path is tested; the positive path is unverified on multi-display (blocking item 3). |
| B5 | Fixed: `commit` always plans from the current dock. |
| B6 | Fixed and tested: invalid data blocks edits until an explicit reset, which does erase that slot's valid presets. |
| I1 | Fixed: generation fence plus comparison rotation copy, tested. |
| I2 | Partly fixed: Escape now matches the controller, and a frozen host's close shows feedback. |
| I3 | Fixed: explicit z-order, and the test asserts it. |
| I4 | Fixed by design (strong identity restores, weak or ambiguous recovers), but only unit-tested on the pure helper. |
| I5 | Fixed: visibility guard at `:193`. |
| I6 | Fixed: no recover on close, and a position assertion exists. |
| I7 | Fixed: the dock is no longer duplicated in the slot, and the test checks it. |
| I8 | Fixed: policy changes publish the pending frame or request the current image. |
| I9 | Partial: retries are bounded, but the test is still counter-only. |

Your specific recent fixes check out by reading:
- `notify()` clears the route cache before emitting.
- `DisplayManager::notifyDisplays` marks the cache dirty before emitting.
- Only the active host or the controller registers the shortcut table (`ui/Main.qml:501`, `ui/DetachedPaneHost.qml:95`).
- Pinned mode skips the follow, folder and timing policy.

## Evidence notes

- The 26 coordinator tests and the 9 UI tests with 1 skip were run offscreen with the software backend. The log shows "No QRhi found", so the real RHI path was not exercised there.
- The native 10/10 run covers only the UI suite on one display.
- Physical colour, HDR, refresh and mixed-DPI behaviour is untouched and stays with MONITOR-VERIFY.

APPROVE SOURCE needs fixes for items 1 and 2, positive-path swap, move and multi-display coverage plus a windowed-geometry test, then re-captured screenshots, and the gates run afterwards.

Plan mode was active, so I wrote no plan file. This reply is the deliverable.