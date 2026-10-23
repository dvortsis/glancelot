# STACK-F049-PRESETS handoff

Written for: the lead and the fresh-context reviewer of this packet. Session: S6 (stack UI), Phase 0 cycle 1, cloud (Linux).
Contract: docs/handoff/LEAD-WRAPUP-2026-10-22.md (first frozen resolution) and LEAD-TAKEOVER-2026-10-22.md §4 row STACK-F049-PRESETS.

| | |
| --- | --- |
| Branch | `claude/stack-f049-presets` |
| Base SHA | `afeac1a5d5b1da088d7b35447a933a2f64400f49` (`origin/main`, "chore(lead): freeze Phase 0 cycle 1 contracts") |
| Tip SHA | the commit that adds this handoff; the code tip it describes is `bfb2885` (review diff: `git diff afeac1a <tip>`) |
| Review | fresh session, `docs/verification/STACK-F049-PRESETS-REVIEW.md` on `claude/review-stack-f049-presets` |

## What changed, by contract clause

1. **Presets obey the canonical lock/freeze authority; no second live owner.** `PaneLayout` (`layouts[slot]`) stays the only live lock/freeze state.
   - `applyPreset` is still refused while the layout is locked or any pane is frozen (no runtime or disk change). It now copies only `region`, `order`, `extent` and `visible` per pane plus the center split and content mode. It never writes `locked` or `frozen`: `persistWhole()` used to rewrite both.
   - `savePreset` stores no `frozen` flags (the old code stored them and only stripped them on apply) and never `locked`.
   - `persistWhole()` no longer calls `updateSettings` directly. It goes through the layout's own value-owned `saveWithRetry` channel (`layout/<slot>/_preset/...`), the same one `MonitorCustomization` cancels and reloads. A busy file now queues the write after earlier edits and shows "Saving layout preferences…". It used to fail silently while the session showed the preset.
   - `MonitorCustomization::commit`: when a dock transaction finishes after pane edits made while it was saving (a layout preset, for example), the committed transaction still wins, because those pending writes are cancelled and the saved dock restored. The coordinator now reports this ("Pane layout changes made while saving were replaced by the saved monitor layout.") instead of dropping the edit silently. If the pane write lands first, the coordinator refuses as before ("The pane layout changed while saving…"). Either way the runtime equals `layouts[slot]` afterwards; the test checks both orders.
   - Settings disables Apply, the lock switch and the freeze switch while a display transaction is saving (`monitorCustomization.saving`). Apply shows the reason in visible text (and as `Accessible.description`): locked, frozen, or saving.
2. **Per-slot placement isolation.** Applying a preset touches only `layouts[<this slot>]`. Other slots' layouts, lock and freeze, and the coordinator's per-slot routes and presets are unchanged. A deferred preset write that completes after the window changed slot reports nothing into the new slot.
3. **`snapshotExtras_` and unknown or newer data survive `persistWhole()`.**
   - Known fields merge into the stored layout. Unknown root, pane and center fields and unknown pane ids stay.
   - `snapshotExtras_` is written when no current-version layout is stored. A malformed layout (non-integer version) is replaceable, as for every other layout command.
   - A newer layout version is never rewritten: the preset applies for the session, with "a newer saved layout was preserved".
   - At runtime, unknown pane and center fields from a restored snapshot survive an apply.
   - Preset records: a newer-version record is never overwritten ("A newer version of Glancelot saved…") and is not applied. Replacing a current record keeps its unknown fields. A non-object `layoutPresets` value is preserved: saving is refused with feedback, and delete finds nothing.
4. **Distinct names and scopes.**
   - Settings > Browsing shows a "Pane layout presets" heading and a "Pane layout" row ("Docked panes, shared by all windows. Never changes lock, freeze or displays."), then "Save pane layout as" and a separate "Pane placement" heading. The display workspace presets stay behind "Displays and workspaces…" (per window slot).
   - Names cannot collide, case-insensitively, in either direction. `PaneLayout::savePreset` refuses a name used by a display workspace preset in any slot. `MonitorCustomization::savePreset`/`renamePreset` refuse a pane layout preset name, built-ins included.
   - MonitorSettings.qml is outside this packet's grant, so its heading is renamed in STACK-F054-COVERAGE ("Display workspace presets for this window", plus a sentence on scope).
5. **Navigator follows `paneAvailable("preview")`.** In `ui/Main.qml`, the zoom navigator's `visible` uses `paneAvailable("preview")`, and its `parent` follows the detached preview host's `paneContent`. It shows in, and moves with, a detached preview window and returns on re-dock.

## Files

`src/app/pane_layout.cpp`, `src/app/monitor_customization.cpp`, `ui/SettingsDialog.qml` (Browsing page: preset block and the two switches), `tests/pane_layout_tests.cpp`, `tests/monitor_customization_tests.cpp`, new `tests/layout_presets_ui_tests.cpp`, `docs/verification/STACK-F049-PRESETS/*.png`.

The lead should look at two edits outside the packet's file list. Each is in its own commit so it can be taken or dropped:
- `9c28226` changes `ui/Main.qml`: the navigator block only (2 lines and a comment, around old line 1293). It is far from EN-06's routing region (655–867) and from the merge leftovers at 1327 and 2051. The contract's navigator clause cannot be met without it.
- `aa2a9a4`, "cmake: provisional target for STACK-F049-PRESETS", adds `layout_presets_ui` to the MONITOR-CUSTOM-01 suite loop in `cmake/MonitorCustomization.cmake` (sources, environment `QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software`, timeout 240, same as `monitor_customization_ui`). The lead may replace it.

No shared header changed (`git diff afeac1a -- include/` is empty).

## Tests added (failure and cancellation cases)

- `pane_layout_tests`:
  - `presetWritePreservesUnknownAndNewerData`: unknown root, pane and center data; newer version; malformed replaceable; `snapshotExtras_`; runtime unknown fields; no `locked` or `frozen` written.
  - `presetWriteRetriesAndKeepsOtherSlotsAndLock`: busy file; FIFO after an earlier resize; a lock set by another window meanwhile survives; other slot byte-identical; a stale completion after a slot change.
  - `presetsObeyLockAndFreezeAuthority`: refused while locked or frozen with no change; saved preset carries neither flag; applying in another slot imports neither.
  - `presetStoreMalformedNewerAndNameScopes`: newer record preserved and not applied; unknown fields kept on replace; non-object store preserved; name collision with a display workspace preset.
- `monitor_customization_tests`:
  - `layoutPresetsObeyCanonicalLockAndFreeze`: coordinator lock and freeze block presets; derived `paneRoutes.frozen`; the workspace snapshot captures the canonical result; distinct names in both directions; another slot untouched.
  - `layoutPresetDuringPendingTransactionStaysCoherent`: a preset applied while a busy coordinator freeze transaction is pending. Afterwards runtime equals disk and the outcome has explicit feedback. In 3 local runs the coordinator landed first ("replaced" path).
- `layout_presets_ui_tests` (shipped `Main.qml`):
  - `navigatorFollowsDetachedPreview`: navigator in the detached host window, inside the canvas bounds, back on re-dock.
  - `settingsPresetRowObeysCanonicalLockAndFreeze`: Apply disabled with a reason under a coordinator lock, a freeze, and a pending transaction.
  - `settingsPresetEvidence`: screenshots, skipped unless `GLANCELOT_F049_UI_EVIDENCE` is set.

All four new `pane_layout` cases fail on `afeac1a`'s `pane_layout.cpp`. The navigator case fails on `afeac1a`'s `Main.qml` ("Compared QObject pointers are not the same … viewerWindow").

```
$ build-claude-stack-f049-presets/pane_layout_tests        # with afeac1a's src/app/pane_layout.cpp
FAIL!  : PaneLayoutTests::presetWritePreservesUnknownAndNewerData() 'layout["future"].toBool()' returned FALSE. ()
FAIL!  : PaneLayoutTests::presetWriteRetriesAndKeepsOtherSlotsAndLock() '!one.feedback().isEmpty()' returned FALSE. ()
FAIL!  : PaneLayoutTests::presetsObeyLockAndFreezeAuthority() '!pane.toObject().contains("frozen")' returned FALSE. ()
FAIL!  : PaneLayoutTests::presetStoreMalformedNewerAndNameScopes() 'model.feedback().contains("newer")' returned FALSE. ()
Totals: 13 passed, 4 failed, 0 skipped, 0 blacklisted, 85ms
```

## Build and test environment (cloud, unverified against the gate)

- Ubuntu 24.04, 4 vCPU, `QT_QPA_PLATFORM=offscreen`.
- `download.qt.io` is denied by this session's network policy (proxy 403), so aqt could not install Qt. Qt 6.8.3 came from conda-forge (`qt6-main 6.8.3 hb82b983_5`, installed with py-rattler into `/opt/qtconda`; GCC 14 build). It was used with system GCC 13 and `LDFLAGS=-L/opt/qtconda/lib -Wl,-rpath,/opt/qtconda/lib`. Configure: `cmake -S . -B build-claude-stack-f049-presets -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=/opt/qtconda` with `/opt/qtconda/lib/qt6/bin` on PATH (for `lrelease`).
- Software Quick backend: the RHI canvas does not paint ("No QRhi found"), so screenshots show the canvas area empty. The navigator overlay and all chrome render.

### Targeted (`ctest -R "pane_layout|monitor_customization|layout_presets_ui"`, final tip)

```
1/4 Test #10: pane_layout ......................   Passed    0.51 sec
2/4 Test #42: monitor_customization ............   Passed    6.63 sec
3/4 Test #43: monitor_customization_ui .........   Passed   15.99 sec
4/4 Test #44: layout_presets_ui ................   Passed    1.78 sec
100% tests passed, 0 tests failed out of 4
Total Test time (real) =  24.92 sec
```

Also `ui_settings` passed, and `input_ui_tests layoutPresetsFromCommandAndSettings` passed (3 passed, 0 failed) after the last QML commit.

### Full offscreen suite (`ctest --test-dir build-claude-stack-f049-presets --output-on-failure`, at `aa2a9a4`; the later `bfb2885` only wraps two accessible names in `qsTr`)

```
 1/44 Test  #1: selection_export .................   Passed    0.04 sec
 2/44 Test  #2: perf_trace .......................   Passed    0.03 sec
 3/44 Test  #3: navigation .......................   Passed    0.24 sec
 4/44 Test  #4: directory_listing ................   Passed    0.99 sec
 5/44 Test  #5: file_operations ..................   Passed    0.02 sec
 6/44 Test  #6: startup_window_geometry ..........   Passed    0.01 sec
 7/44 Test  #7: viewer_acceptance ................   Passed    5.44 sec
 8/44 Test  #8: thumbnails .......................   Passed    3.92 sec
 9/44 Test  #9: folder_tree ......................   Passed    6.17 sec
10/44 Test #10: pane_layout ......................   Passed    0.56 sec
11/44 Test #11: window_slots .....................   Passed   15.73 sec
12/44 Test #12: folder_grid ......................***Failed  181.37 sec
13/44 Test #13: input_ui .........................***Failed   71.21 sec
14/44 Test #14: pathbar_ui .......................   Passed    2.62 sec
15/44 Test #15: file_operation_service ...........   Passed    0.98 sec
16/44 Test #16: directory_model ..................***Failed    4.63 sec
17/44 Test #17: viewport .........................   Passed    0.04 sec
18/44 Test #18: themes ...........................   Passed    0.23 sec
19/44 Test #19: platform_integration .............   Passed    0.49 sec
20/44 Test #20: grid_search ......................   Passed    0.13 sec
21/44 Test #21: grid_density .....................   Passed    0.08 sec
22/44 Test #22: ui_settings ......................   Passed    0.62 sec
23/44 Test #23: fs_watch .........................   Passed   70.49 sec
24/44 Test #24: fs_watch_budget ..................   Passed   20.25 sec
25/44 Test #25: fs_watch_ui ......................   Passed    2.09 sec
26/44 Test #26: favorites ........................   Passed    0.07 sec
27/44 Test #27: folder_history ...................   Passed    0.02 sec
28/44 Test #28: shortcuts_help ...................   Passed    0.27 sec
29/44 Test #29: filter ...........................   Passed    1.64 sec
30/44 Test #30: cache ............................   Passed    2.18 sec
31/44 Test #31: io_scheduler .....................   Passed    0.06 sec
32/44 Test #32: metadata .........................   Passed    0.02 sec
33/44 Test #33: animation ........................   Passed    0.38 sec
34/44 Test #34: mutation .........................   Passed    0.06 sec
35/44 Test #35: copy_move ........................   Passed    0.10 sec
36/44 Test #36: format ...........................   Passed    0.02 sec
37/44 Test #37: monitor_output ...................   Passed    2.07 sec
38/44 Test #38: recovery .........................   Passed   11.09 sec
39/44 Test #39: recovery_startup .................   Passed    0.24 sec
40/44 Test #40: recovery_controller ..............   Passed   12.44 sec
41/44 Test #41: recovery_process .................   Passed   28.65 sec
42/44 Test #42: monitor_customization ............   Passed    6.92 sec
43/44 Test #43: monitor_customization_ui .........   Passed   16.55 sec
44/44 Test #44: layout_presets_ui ................   Passed    2.11 sec
93% tests passed, 3 tests failed out of 44
Total Test time (real) = 473.32 sec
```

The three failures are environment failures, not this packet. They were rerun once, with the same results:

```
1/3 Test #12: folder_grid ......................***Failed  174.86 sec
FAIL!  : GridUiTests::externalRenameAndDeleteRefreshKeepSelection() Compared lists have different sizes.
2/3 Test #13: input_ui .........................***Failed   68.68 sec
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()
3/3 Test #16: directory_model ..................***Failed    3.49 sec
FAIL!  : DirectoryModelTests::selectionSurvivesRelistSortAndDropsRemovedKeys() Compared lists have different sizes.
```

- `directory_model_tests` builds only `directory_entry_model.cpp` and the core library, none of which this packet touches. `folder_grid`'s failing case fails the same way with `afeac1a`'s `Main.qml` and `SettingsDialog.qml` restored (`externalRenameAndDeleteRefreshKeepSelection`: 2 passed, 1 failed). Both assert the same selection-after-relist size (2 vs 1), which points at this container's filesystem.
- `input_ui` `treeRowAndPathBarMenusOpenAndMatch` fails identically with the packet's changes stashed (`Totals: 31 passed, 1 failed`), so it exists on `afeac1a` in this environment.
- `folder_grid` and `input_ui` are budget tests under AGENTS.md. The authoritative gate decides all three.

## Screenshots (docs/verification/STACK-F049-PRESETS/)

- `settings-presets-{dark,light}-{560,1180}.png`: compact and wide Settings > Browsing with a frozen tree pane. They show the "Pane layout presets" heading, the scoped description, the disabled Apply with its visible reason, and the separate "Pane placement" group.
- `navigator-detached-preview.png`: the zoom navigator inside the detached Preview window (software backend; the canvas does not paint).

## Design-review note

The ui-design-skills (application-interface-design, layout, spacing, typography, design-review) are **not available in this cloud session**. Their principles were applied from AGENTS.md, using existing theme tokens and components only (`SectionLabel`, `SettingRow`, `Note`, `UiButton`):

- Two clear groups replace one long list: "Pane layout presets" and "Pane placement".
- A disabled action always has its reason beside it in muted text, not only a greyed button. The description is two short sentences that fit three lines at 560 px.
- The compact (560 px) capture has no horizontal overflow; controls keep their widths and the description wraps.
- Keyboard and screen-reader names: the combo is "Pane layout preset" and the field "Name for the new pane layout preset". Apply's `Accessible.description` carries the blocked reason.
- Open item for the reviewer: the preset row's description column wraps to three or four lines at wide sizes, because the trailing combo and two buttons take the width. It is acceptable, but a stacked layout (controls under the label) would read better if the row grows.

## Unverified

- The Windows gate and Docker were not run (no Docker in this session); the authoritative `lead-windows-gate` decides.
- No physical or multi-monitor hardware: the detached-preview navigator test uses the offscreen platform's single virtual screen.
- No native RHI rendering.
