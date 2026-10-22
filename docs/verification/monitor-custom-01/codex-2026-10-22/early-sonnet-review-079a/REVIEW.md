# MONITOR-CUSTOM-01 early source review at 079a3a9

**Verdict: CHANGES REQUIRED.** This is source review only. The focused build is still running, and I ran no tests or gates. Nothing here is a claim of passing tests, delivery or hardware qualification.

**Provenance caveat.** The session's git status showed `M src/app/viewer_controller.cpp`, and `source.patch` has no hunk for that file. The three `emit configurationReloaded()` calls I found (`src/app/viewer_controller.cpp:1770/1784/1798`) are working-tree content. I couldn't show they exist at 079a3a9. I therefore mark the import/reset → `reloadPreferences()` wiring as unverified. If the emits are missing at the commit, the next edit after an import is refused as "changed in another window", because nothing reloads. The existing test calls `reloadPreferences()` directly, so it doesn't cover the wiring.

## Blocking

**B1. An existing regression target cannot compile.**
- `tests/fs_watch_ui_tests.cpp:35` constructs `glancelot::MonitorCustomization`, but the header is only included at `:76`, after `QTEST_MAIN`. Nothing included earlier pulls it in.
- CMake attaches the coordinator to `fs_watch_ui_tests` (`cmake/MonitorCustomization.cmake:62`).
- Trigger: build `fs_watch_ui_tests`. Move the include to the top.

**B2. A stored but absent `displayId` closes any presentation, including F8.**
- `src/app/monitor_customization.cpp:92-93`: `applyOutput()` runs on every `presentationActiveChanged`, every `setOutputOptions` commit and every controller `colorChanged`. If the stored `displayId` is non-empty and unavailable, it calls `closePresentation()`.
- Repro 1: set `displayId=X` via Enable output, then disconnect X or hand-edit the settings. Press F8 (`presentation-toggle`). Presentation opens on the remaining display and immediately closes.
- Repro 2: while output is active elsewhere, call `setOutputOptions({guides:true})`. Output closes.
- It is also a dead end. `""` can never be set back (`:230` calls `screenFor("")`, which returns null). Only reset or choosing another display clears it.
- This contradicts "missing displays remain recorded as preferences" and "preserve existing monitor command behavior".

**B3. Folder hold/disable state survives close and reopen.**
- `display_manager.cpp:434-435`: `closePresentation()` clears only the cadence timer and pending image. `followFolder` and `folderHeld` are reset only in `setRoutingPolicy`, and only when the policy actually changes.
- Repro (disable): set `folderChange=disable` and navigate to folder B, which closes output. Re-enable output and press Next. `followFolder` is still the old folder, so output closes again.
- Repro (hold): set `folderChange=hold`, change folder, then close and reopen output. It shows the stale held frame, because `updateImage` returns early at `:501-503` without updating `currentImage`.

**B4. `moveOutput` and `swapControllerAndOutput` do not preserve viewport or fit.**
- `openPresentation` on an already-active window (`display_manager.cpp:416-422`) resets fit to the per-display default and restores the target display's stored viewport. It also forces `setGeometry(screen->geometry())`, which defeats windowed mode.
- `PresentationWindow::applyCustomization` reapplies `fitMode` only when the option changed (`presentation_window.cpp:107`).
- Repro: with `fitMode=fill`, call `swapControllerAndOutput`. Swap leaves the options unchanged (`monitor_customization.cpp:265`), so the output reverts to `shrink` while `outputOptions` still says `fill`.
- This violates the frozen text: "`moveOutput` preserves current presentation mode, pin, viewport and decoded image".
- Contract question for the lead, separate from the bug: `setOutputOptions` (`:224-227`) replaces all options with defaults when moving to an unseen display. That includes `followSelection`, `folderChange`, `slideshowTiming` and `viewportLink`, not just appearance.

**B5. `swapControllerAndOutput` can revert the pane layout, including the lock.**
- It is the only commit path that doesn't refresh `next["dock"]=d->panes->snapshot()` (`:259-268`).
- `data.dock` goes stale after any PaneLayout-native change (content mode, resize, the existing lock command). It is also stale right after `setSlot`.
- `commit` then sees `dockChanged` (`:113-114`), writes the stale dock into `layouts[N]` (`:141-143`) and calls `restoreSnapshot` (`:152`).
- Repro: unlock via the PaneLayout command, change content mode, then swap. The old mode and lock come back.

**B6. One invalid sub-field deletes all valid presets on the next edit.**
- `setSlot` (`:196-199`) falls back to defaults when `normalize` fails and says "preserved", but `unsupported` stays false.
- The next unrelated edit writes `presets: []` through `:126-128`.
- Repro: store a version-1 slot with several valid presets plus one preset with an unknown `kind` or invalid `fitMode`. Toggle guides. All presets are gone.
- Either salvage per record or refuse writes until an explicit reset.

## Important

**I1. Linked viewport is wrong in timed, held, no-follow and comparison states.**
- The output→controller emit uses `displays->currentGeneration()` (`:101`), not the generation the output canvas is showing.
- In independent timing, hold or no-follow, `currentGeneration` advances while the output still shows an older image (`display_manager.cpp:501-504`). Controller viewport state is then applied to a different image. Plain mirror mode is fine, because both sides read the same counter.
- `viewportLink=linked` is rejected only for pinned and not-follow (`:229`). It is still allowed with `slideshowTiming=independent` and `folderChange=hold`.
- Comparison isn't excluded. `syncControllerViewport` writes `primaryCanvas()->setViewState` directly (`:246`), including rotation, while the reference canvas stays unrotated. That bypasses the comparison linking rules.
- Separately, `ImageCanvas::viewState()` now carries `rotation`. Check the `copyViewport` and per-display `restoreViewport` callers (`presentation_window.cpp:56/159/191/201`) for unintended propagation.

**I2. Detached-host keyboard parity is incomplete.**
- `DetachedPaneHost.qml:1673` makes Escape re-dock the pane. In the controller, Escape (`Main.qml:759-773`) ends the loupe, hides metadata, leaves grid mode or restores focus. In a detached grid or preview, Escape now re-docks instead.
- `Main.qml` has four `Shortcut` blocks (`?`, and others near `:774`). The host duplicates only the keyTable and Escape.
- Closing or Escaping a frozen or locked host does nothing: `:1602` sets `accepted=false`, then `redockPane` is rejected by `editable()`. The close is silently swallowed. Recover-all still works.
- Nothing I could find forces focus into a newly detached pane.
- Popups from a plain `Window` (canvas menu via Menu or Shift+F10, grid context menu) are untested. Windows opened from a detached grid (delete, rename) call `app.requestActivate()` only in some routes. Check whether keys end up in the host while `keysLive` is false.
- `MonitorSettings.openPage` always focuses `displayChoice` and ignores `targetPane`, even when opening the Panes or Presets pages.

**I3. Z-order regression after detach and re-dock.**
- `Main.qml:1768-1772` re-parents items back to `app.contentItem`, which appends them as the last child.
- `previewVisuals`, `photoGrid`, `treePanel` and `metadataPanel` have no explicit `z` (checked via grep of `z:`). After a round trip they paint above topbar, filmstrip and statusbar where those overlap, such as the auto-hiding filmstrip over the canvas.
- Fix with `stackBefore` or explicit `z`, and test the sibling order. The UI test only asserts `item->window()` (`ui_tests:1549-1550`).

**I4. Hotplug and identity restore are weaker than the contract.**
- `paneRoutes()` honors the preferred display only if `boundScreens[id]==currentScreen` (`:208-210`). After a restart there is no binding. After unplug and replug the `QPointer` is null and the new `QScreen` never matches. Even a strong serial identity never returns, until the user re-detaches.
- Identity confidence is never consulted. `screenFor` matches on id only (`:33-35`), and the picker offers only "Serial identity: …" or a generic warning (`MonitorSettings.qml:2678`).
- The picker shows no connection type. Identify labels are numbered by `screens()` order, but the picker lists only `name`, so duplicate-model monitors stay indistinguishable.

**I5. Hotplug clamp on the controller can break fullscreen.**
- `displaysChanged` calls `setGeometry(reachable(...))` unconditionally (`:164-166`). `reachable` clamps to `availableGeometry`.
- A fullscreen or maximized controller uses the full screen geometry, so a taskbar or work-area change could shrink or un-maximize it. It needs a visibility guard. Treat this as a repro to run once the build is up.

**I6. `recoverToController` on every controller close.**
- `Main.qml:1778` calls it on every close, and `:324` always targets `primaryScreen`. A controller on a secondary display is moved to primary on exit.
- `main.cpp:192` runs `viewer.flushWindowPosition()` on `aboutToQuit`, after QML `onClosing`. If that position is the one persisted, every restart opens on primary. Verify.

**I7. Duplicate `dock` copy in the monitor slot.**
- `dock` is persisted both in `monitorCustomization.slots[N].dock` and in `layouts[N]`. `known()` includes it in the stale-write check.
- It is never the source of truth for restore, yet B5 shows it drifts. Prefer a single source.

**I8. Resume after hold or no-follow shows a stale image.**
- `setRoutingPolicy` clears `folderHeld` but publishes nothing. After turning follow back on or releasing hold, the output stays stale until the next navigation.

**I9. Texture admission.**
- Each `ImageCanvas` has its own counter (`image_canvas.hpp: m_gpuAdmission`). It is not an aggregate with the presentation's 256 MiB budget or the detached preview.
- On `reserve` failure the renderer re-queues `update()` every 16 ms (`image_canvas.cpp:108-111`) until the old incarnation releases. If the old window is minimized or occluded and never re-renders, a detached preview could stay black and keep polling. Verify.
- The test exercises only the counter class (`tests:1474-1478`), not a real renderer.

## Minor and contract notes

- `DisplayManager::cadence` isn't single-shot (`display_manager.cpp:106`), so it wakes every 3 s while independent timing is active.
- Import merges `outputsByDisplay` as plain objects (`settings_file.cpp:802`). A merge above 16 entries makes `feature()` fail, which rejects the whole settings import.
- `DetachedPaneHost.qml:1581/1596`: `route` is a fresh object on every `changed`, so `restoreGeometry()` runs on every notification. During a Busy save or any unrelated `changed` it can snap the window back mid-drag.
- `MonitorSettings` shows no disabled or explained state for meaningless combinations (pinned or comparison, Wayland positioning). Rejection is reported only after the click (`:229-231`).
- `MonitorSettings` "Update snapshot" renames the preset to whatever is in the name field, or fails if it is empty (`:2764`).
- `MonitorSettings` dereferences `customization.` without a null guard. Grep for any other `Main.qml` loaders outside the CMake consumer list; those would throw TypeErrors.
- Pointer, key and palette parity for the `pane-*` routes is enforced centrally in C++ via `editable()`, which is good. `commandBlockedReason` mirrors it in QML.

## Missing acceptance coverage

The two new test files have about 12 and 2 tests. None exist yet for:
- `swapControllerAndOutput`, `moveOutput` and `identifyDisplays`
- `updatePaneGeometry` and `redockPane`
- async Busy/WriteFailed/retry and concurrent writes
- the bounds: 64 presets, 16 per-display records, 256 KiB, 1 MiB, 80-character names
- corrupt and invalid slot data (B6)
- real-coordinator hotplug and replug; the topology test calls pure `place()`
- fill, actual and mixed logical origins with DPR
- output→controller viewport and rotation
- stale generation, rapid navigation, a removed pin and low memory
- SAFE backup, import and reset beyond one merge test
- preservation of selection and focus across detach, host close, and controller shutdown
- popups and menus inside a detached host
- the pointer/key/palette parity matrix

## Checked and fine

- `commit` single-flight with slot and revision fencing; stale-write refusal.
- Unknown-field and newer-version preservation in `commit`, import and SAFE reset (`version!=1` is skipped).
- `setSlot` never opens a window; the identify timer is bounded at 3 s with no focus steal.
- `QSignalBlocker` use is sound, and the retry `completed` semantics match the code.
- ComboBox and switch bindings do re-evaluate on rejection, because `fail()` emits `changed`. That is not a bug.

## Not source findings

Physical color, HDR, refresh and mixed-DPI behavior, Wayland positioning, and anything needing the full gate or Docker are unverified. Those belong to MONITOR-VERIFY and the lead's gate.

I made no edits, and I did not run any shell or tests.