# MONITOR-CUSTOM-01 at 6e22804: APPROVE SOURCE / FULL GATES PENDING

I found no blocking source or functional-evidence problems. I read the source, tests, patches, logs and screenshots. I ran no builds or tests and edited nothing. The Windows and Docker gates are unverified, and this is not an integration approval.

Plan mode is active and you asked for no plan file, so this reply is the deliverable.

## Pre-integration requirements (none block source approval)

1. **Clean complete native run.** Make one complete Windows/D3D11 native UI run on the integrated commit and record it. See the native-evidence verdict below.
2. **Windowed frame-fit test.** Add a pure-helper test of `monitor_topology::windowedClient` with realistic frame margins (for example 8/31/8/8) on a negative-origin work area. Today nothing proves the native frame fits, by test or by native run.

## Prior blocking and important findings (review-2f73)

| 2f73 item | Status at 6e22804 | Evidence |
|---|---|---|
| 1. Freeze had two owners | **Fixed.** `paneRoutes().frozen` comes from the layout (`monitor_customization.cpp:88-90`). `editable()` checks only the layout (`:91-96`). `known()` drops legacy route `frozen` from owned writes (`monitor_customization_state.hpp:145-149`). SAFE policy and reset no longer own it (`settings_policy.cpp:108`, `settings_file.cpp:393`). | `canonicalFreezeIsSharedBySettingsArrangementAndReset` (`tests:177`) covers Settings, arrangement and reset. |
| 2. Switch overlapped its label | **Fixed.** Switches now use `Layout.fillWidth`. | `native-ui/dark-720-page2.png` and `dark-720-page1-bottom.png` show clean rows. |
| 3. Missing tests | **Fixed, one gap.** Positive move and swap run on a real coordinator with one, two and three synthetic QPA screens (`tests:194-214`). They cover pinned transform retention, strong and weak unplug/replug, and user-drag rebinding. The `screenChanged` bookkeeping is exercised at `:205`. Native focus, scroll and content-mode retention is covered by `realGridRetainsFocusScrollAndContentAcrossMigration`. Windowed output is fixed by reading only (see below). | Offscreen only. |
| 4. Missing labels | **Fixed.** Pane destination, saved preset and preset type have visible labels. Enum values show text such as "Shrink to fit". | `light-1280-page3.png`. |
| 5. Drag snaps back | **Fixed.** `updatePaneGeometry` rebinds `displayId` and `boundScreens` (`:344-350`). The host ignores route changes while `saving` and restores on failure (`DetachedPaneHost.qml:23-43`). | `busyDragFailureRestoresTheSavedHostGeometry`. |
| 6. Output default | **Resolved.** Default is `solid #000000` (`monitor_customization_state.hpp:14`), explicitly frozen by lead. | |
| 7. Layout-only blocked when invalid or newer | **Fixed.** `layoutOnly` bypasses the `unsupported` and `invalid` guards and never rewrites the monitor subtree (`:128-131,149`). | `layoutUnlockAndFreezePreserveUnsupportedMonitorData`. It covers the newer-version case only. The `invalid` case uses the same condition but is untested. |

Fixes from the second batch that I confirmed by reading:
- **Pin uses displayed generation:** `display_manager.cpp:588`.
- **Canvas view-state validation is atomic:** `image_canvas.cpp:317-339`. Rotation is assigned only after the other fields validate.
- **`synchronizePresentationScreen`** (`display_manager.cpp:576-585`) is guarded by active state, the screen list and an id change. The coordinator's `QPointer` guard (`monitor_customization.cpp:118-121`) ignores stale callbacks. It does not rewrite the saved preferred display.
- **Workspace-preset guard** checks every canonical pane, including controller-only ones (`:374-375`).

## Earlier 079a items and open minors

| Item | Status |
|---|---|
| B1–B6, I1, I3, I5–I8 | Unchanged from the 2f73 review: fixed. |
| I2 (Escape and frozen close) | Still partial. |
| I4 (weak versus strong identity) | Now covered on a real coordinator with synthetic screens. |
| I9 (bounded retries) | Still counter-only. `rendererIncarnationsShareAdmissionUntilOldTextureReleases` (`tests:264`) tests only `CanvasTextureAdmission`. |
| Connection type | Still missing. `ui/MonitorSettings.qml:79` hard-codes "Connected", and the "Unknown" after it is the colour space. Plan §1 lists connection. |
| `finishEnable` | Still string-matches "Saving monitor" in `ui/MonitorSettings.qml` even though a `saving` property now exists. |
| Display identity cost | `computeDisplayId` still does O(n²) Windows device enumeration on the GUI thread. It is bounded and cached. |
| Test logging | `qInfo` prints remain in `nativeUiEvidence`. |

## Important, non-blocking

1. **Emergency recovery does not survive a restart.** `recoverToController()` (`monitor_customization.cpp:395-406`) clears `detached` only in `d->data["panes"]`. It makes no commit and leaves `persisted` untouched. The plan doesn't strictly require it to be durable.
   - Repro:
     1. Detach a pane on a strong-identity display.
     2. Recover.
     3. Restart. `setSlot` reloads `detached:true` and `syncDetachedPanes` recreates the host.
   - The redock is written silently by the next unrelated save, because it takes its owned panes from `data`.
2. **Windowed native-frame fit is fixed by reading only.** The logic is right (add margins, clamp, remove margins), and the margins are read after `applyCustomization` (`:123-124`). The only assertions are offscreen (`tests:201,204`), where frame margins are zero, so they pass trivially. No native test opens a windowed presentation.
3. **Option changes un-maximize the output window.** In windowed mode, any option change restores a maximized or minimized output window (`presentation_window.cpp:123`). A controller theme change does the same through `colorChanged` (`monitor_customization.cpp:213`).

## Native-evidence verdict

The evidence is sufficient for source review. It is not a complete native pass at 6e22804.
- Attempt 7 passed 12 methods and failed `nativeUiEvidence`. It failed at its first setup assertion, `h.window->isExposed()` (`ui_tests.cpp:95`), before any feature code ran. The same method passes in isolation (attempt 8) and the suite passed in attempt 6.
- That looks environmental, but I can't prove it. Attempt 8 supplies the 25 screenshots, six migrations, red pixels, admission bounds and old-renderer retirement.
- Nothing in the archive ties either binary to 6e22804. "Unchanged source" is the author's statement, not something I could verify.
- Gated runs skip `nativeUiEvidence`, so renderer retirement has no automated regression protection.

## Other limits and polish

- **Reset path:** `resetPreferences()` clears locks and freezes silently (`:391-394`). The QML confirmation dialog states that locks are cleared (`ui/MonitorSettings.qml:207-214`), so plan §4 is met.
- **Controller clamp:** every `displaysChanged` event re-clamps the controller window (`:204-207`). That can slightly shrink a window that already exceeds the work area.
- **Command parity gap:** `layout-lock` (`Main.qml:643`) isn't gated by `saving`. The commit fence refuses a stale write (`:172`), so state stays consistent.
- **Freeze behaviour differs:** `PaneLayout::freeze` forces the pane visible (`pane_layout.cpp:192`); `setPaneFrozen` doesn't.
- **Accessible names repeat:** each pane card has identical "Detach to selected display", "Freeze" and "Re-dock" names.
- **Compact layout:** at 720 px with 150% text, the Displays page help text peeks under the footer. The recovery action is not clipped.
- **Button affordance:** buttons render as plain text, so primary actions such as "Create preset" are not distinguished.
- **Not verified:** the hardware swap and move of a fullscreen controller. The dev machine has a single display. Physical colour, HDR, refresh and mixed-DPI remain with MONITOR-VERIFY.

**Strengths:**
- A single freeze and lock owner.
- A transactional commit with a layout fence.
- Test-only QPA screens that stay offscreen.
- Honest retention of the failed native attempt.

**Highest-leverage next step:** make one clean complete native run on the integrated commit, then run the locked Windows and Docker gates.
