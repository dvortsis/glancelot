# PANES-01 verification

Branch `agent/panes`, worktree `.worktrees/panes`, build `build-panes`. State: implementation complete; Linux checks passed; independent review pending. Contract: docs/PANES-PLAN.md.

Adds the validated `PaneLayout` model (six pane IDs, dock region/order/extent/visible/frozen, center split, global lock) with per-slot persistence via field-level merges under the settings writer lock. It also adds native region hosts with responsive recovery that never persists, pointer and keyboard arrangement, Settings controls, and the remappable commands Arrange panes, Lock or unlock workspace layout, and Reset workspace layout. These commands start unbound so they cannot collide with stored shortcuts, and they are discoverable through command search. While locked, the top bar shows a "Layout locked" indicator that unlocks when activated. Command search marks Arrange panes "Layout locked" while the layout is locked.

## Acceptance coverage

| Item | Evidence |
| --- | --- |
| Restart round-trip per slot; slot 0 session-only | `pane_layout_tests::persistenceAndConcurrentFieldMerges` |
| Concurrent windows do not clobber each other (stale same-slot instance, different slots) | `persistenceAndConcurrentFieldMerges`, `centerContractAndWriteFailure`, `newerVersionsCorruptionAndNoOps` |
| Failed atomic write keeps session change with feedback | `centerContractAndWriteFailure` (held writer lock) |
| Corrupt file: defaults with feedback, never overwritten | `corruptSettingsFileFallsBackWithFeedback` |
| Invalid known fields fall back individually; unknown panes/fields and newer versions preserved | `invalidAndUnknownFields`, `newerVersionsCorruptionAndNoOps` |
| Freeze blocks geometry, keeps visible and interactive; global lock blocks freeze changes; reset | model tests plus `grid_ui_tests::paneGeometryMovesFreezesAndResets` |
| Every pane in every legal region, compact and wide, dark and light, within window (104 placements) | `everyPaneDockRegionStaysInWindow` |
| Center reserved for preview/grid with a reason; no-usable-content layouts recover without persisting | `move` rejection; `paneArrangementBoundsAndGestures` (`_recovered`, extent stays 4096) |
| Keyboard move/resize/order/freeze with focus kept on the pane grip | `paneArrangementKeyboardCommands`, `paneCommandsLockIndicatorAndDragCancellation` |
| Pointer resize commits on release | `paneArrangementBoundsAndGestures` |
| Drag cancelled by Escape and by locking mid-gesture; a completed drag commits once | `paneCommandsLockIndicatorAndDragCancellation` |
| Lock visible outside Settings; arrange refused while locked; reset/lock reachable from command search | `paneCommandsLockIndicatorAndDragCancellation` |

## Independent review round 1 (CHANGES REQUESTED at 5c3f092) and fixes

| Finding | Fix | Regression test |
| --- | --- | --- |
| B1: focus stayed on a hidden grip; keys kept editing | Leaving arrangement focuses content; grip handlers require a visible overlay | `paneCommandsLockIndicatorAndDragCancellation` (Escape, then Left/Space change nothing) |
| B2: edits during recovery persisted fallback geometry | Arrangement is immutable while recovered, with a visible explanation; edits read saved preferences | `recoveredLayoutIsNotEditableAndKeepsPreferences` |
| B3: canvas rescaled whenever the HUD hid or showed | Center derives from docked panes only; auto-hiding bars overlay the canvas, as on main | `hudAutoHideNeverReflowsCanvas` |
| B4: compact tree grip hidden under the preview grip | Center grips sit in the middle of the uncovered content | `arrange-*-540` capture |
| B5: Done button covered Settings | Done button and note sit in the content area; a scrim dims content | `arrange-*` captures |
| I1: resize started from the preferred extent | Starts from the displayed extent; a click without movement commits nothing | `paneArrangementBoundsAndGestures` |
| I2: window Escape skipped the address-field revert | Window Escape is disabled while a text field has focus | existing address tests |
| I3: malformed version blocked saving forever | Only integer versions above 1 are preserved; malformed ones are replaced | `malformedVersionIsReplaceableAndOrdersRenumber` |
| I4: order ties and no-op "earlier" | Moves renumber the region 0..n-1 | same test |
| I5: plan deviation; blank center | Plan amended; the single content pane fills the center | matrix test |
| I6: frozen shown as "locked"; raw IDs; misleading press action | Labels say "layout locked", "frozen" or "window too small"; display names; press action removed; grips rounded | captures |
| I7: silent locked arrange shortcut | Arrangement opens and explains the lock inline | `paneCommandsLockIndicatorAndDragCancellation` |
| I8: timing-dependent captures | Matrix keeps the HUD awake; native capture run passes | native run below |

Polish items left for UI-SETTINGS-01: Settings layout section grouping, "Center" offered for edge panes, preferred size committing only on Enter, vertical toolbar alignment, vertical filmstrip insets, toast wrapping in narrow content, and per-keypress synchronous settings writes.

## Evidence

- Windows, branch with main merged: `scripts/build.ps1 -BuildDir build-panes -QtRoot C:/src/Github/Glancelot/.tools/Qt/6.8.3/msvc2022_64 -Test` passed 9/9 in 67.48s.
- Linux `5c3f092`: `docker build -t glancelot-browser:panes .` in WSL Ubuntu (Qt 6.8.3, full CTest inside the image build) passed 9/9 in 55.46s, including `pane_layout` and `folder_grid` with the offscreen software scene graph. This verifies interaction logic, not GPU output.
- New UI case run directly: `grid_ui_tests paneCommandsLockIndicatorAndDragCancellation` passed 3/3 (with init/cleanup) in 448ms.
- Opt-in captures (`GLANCELOT_REVIEW_CAPTURE=1`) land in `build-panes/.cache/panes-matrix`, `panes-ui/tree-*` and `panes-ui/locked-*`. The lead checked the locked indicator in dark and light at 1180×780.

## Limits

- No UI text-scale or reduced-motion setting exists yet (UI-SETTINGS-01). Pane arrangement uses no animation, so reduced motion is not affected. Large-label behavior is verified only through toolbar reflow into vertical regions.
- Detached floating windows are excluded (MONITOR-CUSTOM-01).
- Physical mixed-DPI, multi-monitor hardware and live assistive technology are unverified.
