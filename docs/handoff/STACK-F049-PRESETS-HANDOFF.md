# STACK-F049-PRESETS handoff

Written for: the lead and the fresh-context reviewer of this packet. Session: S6 (stack UI), Phase 0 cycle 1, cloud (Linux).
Contract: docs/handoff/LEAD-WRAPUP-2026-10-22.md (first frozen resolution) and LEAD-TAKEOVER-2026-10-22.md §4 row STACK-F049-PRESETS.

| | |
| --- | --- |
| Branch | `claude/stack-f049-presets` |
| Base SHA | `afeac1a5d5b1da088d7b35447a933a2f64400f49` (`origin/main`, "chore(lead): freeze Phase 0 cycle 1 contracts") |
| Tip SHA | the commit that updates this handoff. Review round 1 fixes: see "Review round 1 fixes" below (re-review diff: `git diff 4770e99 <tip>`, after the `origin/main` merge `380cf7b`) |
| Review | fresh session, `docs/verification/STACK-F049-PRESETS-REVIEW.md` on `claude/review-stack-f049-presets` |

## What changed, by contract clause

1. **Presets obey the canonical lock/freeze authority; no second live owner.** `PaneLayout` (`layouts[slot]`) stays the only live lock/freeze state.
   - `applyPreset` is still refused while the layout is locked or any pane is frozen (no runtime or disk change). It now copies only `region`, `order`, `extent` and `visible` per pane plus the center split and content mode. It never writes `locked` or `frozen`: `persistWhole()` used to rewrite both.
   - `savePreset` stores no `frozen` flags (the old code stored them and only stripped them on apply) and never `locked`.
   - `persistWhole()` no longer calls `updateSettings` directly. It goes through the layout's own value-owned `saveWithRetry` channel (`layout/<slot>/_preset/...`), the same one `MonitorCustomization` cancels and reloads. A busy file now queues the write after earlier edits and shows "Saving layout preferences…". It used to fail silently while the session showed the preset.
   - `MonitorCustomization::commit`: when a dock transaction finishes after pane edits made while it was saving (a layout preset, for example), the committed transaction still wins, because those pending writes are cancelled and the saved dock restored. The coordinator now reports this ("Pane layout changes made while saving were replaced by the saved monitor layout.") instead of dropping the edit silently. If the pane write lands first, the coordinator refuses as before ("The pane layout changed while saving…"). Either way the runtime equals `layouts[slot]` afterwards; the test checks both orders.
   - Settings disables Apply, the lock switch, the freeze switch and Reset while a display transaction is saving (`monitorCustomization.saving`). Each shows its own reason in visible text: Apply below the preset row (and as `Accessible.description`: locked, frozen with the pane names, or saving), the switches in their descriptions, Reset in its note. The `layout-preset-next` command waits the same way (round 1).
2. **Per-slot placement isolation.** Applying a preset touches only `layouts[<this slot>]`. Other slots' layouts, lock and freeze, and the coordinator's per-slot routes and presets are unchanged. A deferred preset write that completes after the window changed slot reports nothing into the new slot.
3. **`snapshotExtras_` and unknown or newer data survive `persistWhole()`.**
   - Known fields merge into the stored layout. Unknown root, pane and center fields and unknown pane ids stay.
   - `snapshotExtras_` is written when no current-version layout is stored. A malformed layout (non-integer version) is replaceable, as for every other layout command.
   - A newer layout version is never rewritten: the preset applies for the session, with "a newer saved layout was preserved".
   - At runtime, unknown pane and center fields from a restored snapshot survive an apply.
   - Preset records: a newer-version record is never overwritten ("A newer version of Glancelot saved…") and is not applied. Replacing a current record keeps its unknown fields. A non-object `layoutPresets` value is preserved: saving is refused with feedback, and delete finds nothing.
4. **Distinct names and scopes.**
   - Settings > Browsing shows a "Pane layout presets" heading, the scope sentence "Saved for every window; applies to this window's docked panes. Never changes lock, freeze or displays.", the preset controls stacked under it, then "Save pane layout as" and a separate "Pane placement" heading (round 1 layout). The display workspace presets stay behind "Displays and workspaces…" (per window slot).
   - New names cannot collide, case-insensitively, in either direction. `PaneLayout::savePreset` refuses a new name used by a display workspace preset in any slot, including one still queued in this process. `MonitorCustomization::savePreset`/`renamePreset` refuse a new or changed name that is a pane layout preset name, built-ins included, and the queued write refuses it again if another process saved that pane layout preset meanwhile. An unchanged legacy name can still be updated (round 1).
   - MonitorSettings.qml (lead decision, round 1: this packet owns the wording; F054 does not touch it): the heading is "Display workspace presets for this window", and the note starts "Saved for this window only." and says pane layout presets in Settings are saved for every window.
5. **Navigator follows `paneAvailable("preview")`.** In `ui/Main.qml`, the zoom navigator's `visible` uses `paneAvailable("preview")`, and its `parent` follows the detached preview host's `paneContent`. It shows in, and moves with, a detached preview window and returns on re-dock.

## Files

`src/app/pane_layout.cpp`, `src/app/monitor_customization.cpp`, `ui/SettingsDialog.qml` (Browsing page: workspace layout group, preset block and the two switches), `ui/MonitorSettings.qml` (presets heading and scope note only, round 1), `ui/Main.qml` (navigator block; `layout-preset-next` block and its outcome watcher, round 1), `tests/pane_layout_tests.cpp`, `tests/monitor_customization_tests.cpp`, new `tests/layout_presets_ui_tests.cpp`, `docs/verification/STACK-F049-PRESETS/*.png`.

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
  - `layoutPresetDuringPendingTransactionStaysCoherent`: a preset and a busy coordinator freeze transaction pending together, in both orders. Afterwards the runtime equals what `layouts[slot]` reloads as, and the outcome is one of three explicit results: the transaction superseded the preset (with "replaced" feedback), the preset landed first and the transaction was refused, or both applied in order. Locally, order 0 gives "superseded" and order 1 gives "refused".
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

## Hosted Windows feedback (`Build and test`, `viewer` job, MSVC on windows-2022)

- The run on `2419653` (https://github.com/dvortsis/glancelot/actions/runs/39879139768) passed the MSVC build. Tests were 43/44: everything passed, including `folder_grid`, `input_ui`, `directory_model`, `pane_layout` and `layout_presets_ui`, except `monitor_customization`.
- The hosted log carries no QTest output, so the failing assertion is not visible. `monitor_customization` passes on the hosted job for `main` and the other cycle branches, so the failure was in this packet's new coordinator test.
- Likely cause: the first version of `layoutPresetDuringPendingTransactionStaysCoherent` compared raw `layouts[slot]` JSON with runtime pane fields. When the preset write lands first (the "refused" order), the stored panes have no `frozen` key. That comparison failed, whereas the same fields reloaded through `PaneLayout` match.
- The test now compares reloaded state and forces both orders (a second loop starts with the preset). Locally the forced order takes exactly the refused path, and it passes.
- The follow-up commit's hosted run is the check; I could not run Windows here.

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
- (Superseded by round 1: the controls now stack under the text.) Open item for the reviewer: the preset row's description column wraps to three or four lines at wide sizes, because the trailing combo and two buttons take the width. It is acceptable, but a stacked layout (controls under the label) would read better if the row grows.

## Unverified

- The Windows gate and Docker were not run (no Docker in this session); the authoritative `lead-windows-gate` decides.
- No physical or multi-monitor hardware: the detached-preview navigator test uses the offscreen platform's single virtual screen.
- No native RHI rendering.

## Review round 1 fixes (fix session S10, October 23, 2026)

Written for: the lead and the next fresh reviewer. The original implementer was unavailable; fix session S10 made these changes on the same branch.

| | |
| --- | --- |
| Starting tip | `4770e99`, then `git merge --no-edit origin/main` (`f325ef6`) as merge commit `380cf7b` (no conflicts) |
| Code commit | `0d5afb6` "STACK-F049-PRESETS: review round 1 fixes" |
| Tip | the commit that adds this section (evidence and handoff only) |
| Re-review diff | `git diff 380cf7b <tip>` for round 1 only; `git diff afeac1a <tip>` for the whole packet, which also includes main's changes since `afeac1a` |

### What changed, by finding

1. **Refusals are checked against disk** (`tests/pane_layout_tests.cpp`, `presetsObeyLockAndFreezeAuthority`). The test reads `layouts` right before each refused apply (locked, then frozen) and `QCOMPARE`s it with what is on disk afterwards. `Q_UNUSED(layoutBefore)` is gone.
2. **Only a new or changed name is refused.**
   - `PaneLayout::savePreset` skips the display-name check when `layoutPresets` already holds that exact name, because that is an update.
   - `MonitorCustomization::savePreset` and `renamePreset` use `layoutPresetNameTaken(id, name)`, which skips the check when the record keeps its current name. "Update snapshot" (`ui/MonitorSettings.qml`) therefore works again for a display preset named "Review", "Default", "Browse", or the name of a user pane layout preset, from before this packet.
   - The write-time check (item 5) skips a name that is unchanged from the stored record.
3. **Scope copy** (`ui/SettingsDialog.qml`): "Saved for every window; applies to this window's docked panes. Never changes lock, freeze or displays." The `MonitorSettings.qml` note now ends the same way in reverse: "…pane layout presets in Settings are saved for every window."
4. **The outcome shows in the main window.**
   - `layout-preset-next` (`ui/Main.qml`) now waits while `monitorCustomization.saving`, as Settings' Apply does. It shows "Display preferences are saving. Apply the preset when they finish." and applies nothing. That removes the "Layout: X, then a silent revert" path: a coordinator transaction can only supersede a preset if the transaction started first and was still pending when the preset applied, and the command no longer applies during that window.
   - A preset whose write had to wait (busy file) now reports how it ended in the main window. `presetSavingFeedback` holds the "Saving layout preferences…" text, and a `Connections` on `paneLayout` shows the next different, non-empty `paneLayout.feedback` (for example "Layout changed for this session; settings could not be saved.") as the split message, then disarms.
   - `monitor_customization.cpp`: the "replaced" message is composed last and leads. Any other note (the presentation-session note, or the `committed()` or `applyOutput()` text) follows it. Before this fix, the "newer presentation session" branch overwrote it.
5. **Name checks include queued saves.**
   - In this process, `PaneLayout::savePreset` overlays the coordinator's pending settings edits (`settings_detail::overlayPendingSettings`) before it checks display preset names. The coordinator registers itself on the layout through a dynamic property, `_glancelotDisplayPresetQueue`, because no shared header changes. The property is cleared when the coordinator is destroyed.
   - Across processes, the coordinator's queued write re-checks `layoutPresets` on disk when it finally runs. If another window saved a pane layout preset with that new name meanwhile, the write is refused with "A pane layout preset already uses that name. Choose another name." So no collision reaches disk in either order.
   - The coordinator's edit now clears `*refused` at its start, so running it as a read overlay leaves no stale refusal behind.

Design review (see the note below):
- The preset controls stack under their text at a measure of 70 average characters.
- The repeated "Pane layout" row label is gone.
- The freeze reason names the frozen panes ("Frozen: Folder tree. Unfreeze to apply a pane layout preset.").
- The lock and freeze switches give their own reason while saving ("Unavailable while display preferences are saving."). The freeze switch also says "Unlock the workspace layout to freeze or unfreeze a pane." when the layout is locked.
- "Reset workspace layout" moved out of "Pane placement" to sit after the lock switch, with a note saying it also turns off the lock and every freeze. It is disabled while saving, like the lock switch.

Lead decisions applied:
- The `9c28226` navigator edit is kept.
- Main.qml changes in round 1 are limited to the `layout-preset-next` block, one property and one `Connections` next to `splitMessageTimer`.
- `aa2a9a4` (provisional CMake) is untouched, for the lead to re-land.
- MonitorSettings wording: this packet now owns the heading and scope note only (`objectName`s `monitorPresetHeading`, `monitorPresetScope`, as on the F054 branch). Expect a textual conflict with `claude/stack-f054-coverage` on those two lines, and resolve it to this packet's text.
- F054 copy: I checked every `ui/` string change in `git diff afeac1a` and found no pure `qsTr` wrap of a string this packet did not introduce or rename, so there was nothing to remove. The existing `"Layout: "` split message in Main.qml stays unwrapped for F054.

### Tests added or changed

- `pane_layout_tests`:
  - `presetsObeyLockAndFreezeAuthority`: disk comparisons after both refusals.
  - New `legacyCollidingPresetNameStaysUpdatable`: a legacy "Studio" pane layout preset beside a display preset "Studio" updates and keeps its unknown field; "STUDIO" is still refused and not written.
- `monitor_customization_tests`:
  - `legacyCollidingDisplayPresetNameStaysUpdatable`: display presets renamed on disk to "Review" and "studio" (with a pane layout preset "Studio"). Update snapshot and rename-to-self succeed with no feedback. Renaming to "Browse" or "STUDIO" is refused, and the stored names stay as they were.
  - `layoutPresetNameChecksIncludeQueuedSaves`: a display preset save queued behind a busy file blocks the same pane layout name at once. A pane layout preset written by "another process" while a display save waits makes that queued save fail with feedback, and only the earlier display preset is stored.
  - `supersededFeedbackSurvivesNewPresentationSession`: a freeze transaction queued, then a preset, then a new presentation session. When the transaction supersedes the preset, the feedback starts with "replaced" and keeps the session note; when the preset lands first, the transaction's refusal is shown. It logs which order ran (here, always "superseded").
- `layout_presets_ui_tests`:
  - `settingsPresetRowObeysCanonicalLockAndFreeze`: the freeze reason names "Folder tree". While saving, the lock and freeze switches and Reset are disabled and the switch descriptions say why; afterwards they are enabled again.
  - New `settingsPresetBlockReadsAtEveryWidth` (560 and 1180 px):
    - the exact scope copy;
    - at least 40 characters per line and a width no wider than the measure;
    - the controls sit below the text;
    - Reset sits above the preset heading;
    - the MonitorSettings heading and note text.
  - New `presetCommandWaitsForDisplaySaveAndReportsOutcome`:
    - two `layout-preset-next` presses while a display save is pending apply nothing, the cursor stays at -1 and the split message says why;
    - with the file held busy, a press shows "Layout: Review" and then, about four seconds later, "…settings could not be saved."
  - `settingsPresetEvidence` now frames the lock, Reset and preset block, and adds one capture while saving.

### Fail before, pass after

The new tests were built against round 1's tests with `src/` and `ui/` at `380cf7b` (`git stash push -- src ui`):

```
$ QT_QPA_PLATFORM=offscreen ./pane_layout_tests
FAIL!  : PaneLayoutTests::legacyCollidingPresetNameStaysUpdatable() 'model.savePreset("Studio")' returned FALSE. (A display workspace preset already uses that name. Choose another name.)
Totals: 17 passed, 1 failed, 0 skipped, 0 blacklisted, 506ms
$ QT_QPA_PLATFORM=offscreen ./monitor_customization_tests
FAIL!  : MonitorCustomizationTests::legacyCollidingDisplayPresetNameStaysUpdatable() 'h.customization.savePreset(builtInId,"Review","presentation")' returned FALSE. (A pane layout preset already uses that name. Choose another name.)
FAIL!  : MonitorCustomizationTests::layoutPresetNameChecksIncludeQueuedSaves() '!h.panes.savePreset("queued")' returned FALSE. ()
FAIL!  : MonitorCustomizationTests::supersededFeedbackSurvivesNewPresentationSession() 'feedback.startsWith("Pane layout changes made while saving were replaced")&&feedback.contains("newer presentation session")' returned FALSE. (Preferences saved; the newer presentation session was left unchanged.)
Totals: 33 passed, 3 failed, 0 skipped, 0 blacklisted, 6877ms
$ QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./layout_presets_ui_tests
FAIL!  : LayoutPresetsUiTests::settingsPresetRowObeysCanonicalLockAndFreeze() 'blocked->property("text").toString().contains("Folder tree")' returned FALSE. (Unfreeze every pane to apply a pane layout preset.)
FAIL!  : LayoutPresetsUiTests::settingsPresetBlockReadsAtEveryWidth() 'scope' returned FALSE. ()
FAIL!  : LayoutPresetsUiTests::presetCommandWaitsForDisplaySaveAndReportsOutcome() 'h.window->property("splitMessage").toString().contains("Display preferences are saving")' returned FALSE. ()
Totals: 3 passed, 3 failed, 1 skipped, 0 blacklisted, 18190ms
```

Finding 1 (a refusal that writes must fail): `applyPreset` was temporarily mutated to write the preset (`persist("_preset", …)`) before each refusal, one at a time, and then restored:

```
# write before the lock refusal
FAIL!  : PaneLayoutTests::presetsObeyLockAndFreezeAuthority() Compared values are not the same
   Actual   (after["layouts"]): QJsonValue(object, QJsonObject({"1":{"center":{},"locked":true,"panes":{"filmstrip":{"extent":112,"order":0,"region":"bottom","visible":true}, … "topbar":{"extent":48,"order":0,"region":"top","visible":false}, …},"version":1}}))
   Expected (layoutBefore)    : QJsonValue(object, QJsonObject({"1":{"locked":true,"panes":{"tree":{"frozen":true,"visible":true}},"version":1}}))
   Loc: [/home/user/glancelot/tests/pane_layout_tests.cpp(295)]
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 10ms
# write before the freeze refusal
FAIL!  : PaneLayoutTests::presetsObeyLockAndFreezeAuthority() Compared values are not the same
   Loc: [/home/user/glancelot/tests/pane_layout_tests.cpp(299)]
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 7ms
```

After the fixes (`0d5afb6`), targeted:

```
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build-claude-stack-f049-presets -R "pane_layout|monitor_customization|layout_presets_ui|ui_settings|input_ui" --output-on-failure
1/6 Test #10: pane_layout ......................   Passed    0.50 sec
2/6 Test #13: input_ui .........................***Failed   68.40 sec
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()
3/6 Test #22: ui_settings ......................   Passed    0.60 sec
4/6 Test #42: monitor_customization ............   Passed    7.29 sec
5/6 Test #43: monitor_customization_ui .........   Passed   15.62 sec
6/6 Test #44: layout_presets_ui ................   Passed    8.37 sec
83% tests passed, 1 tests failed out of 6
Total Test time (real) = 100.79 sec
```

`input_ui`'s one failure is the documented baseline (`treeRowAndPathBarMenusOpenAndMatch`). Its preset case passed: `PASS   : InputUiTests::layoutPresetsFromCommandAndSettings()`, `Totals: 31 passed, 1 failed, 1 skipped`.

The two timer-ordered suites, repeated:

```
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build-claude-stack-f049-presets -R "^(monitor_customization|layout_presets_ui)$" --repeat until-fail:10 --output-on-failure
    Test #42: monitor_customization ............   Passed    7.09 sec   (runs 1-9: 6.97-7.09 sec, all Passed)
1/2 Test #42: monitor_customization ............   Passed    6.98 sec
    Test #44: layout_presets_ui ................   Passed    8.16 sec   (runs 1-9: 8.08-8.30 sec, all Passed)
2/2 Test #44: layout_presets_ui ................   Passed    8.21 sec
100% tests passed, 0 tests failed out of 2
Total Test time (real) = 152.14 sec
```

Order log from one direct run: `order 0: transaction superseded the preset`, `order 1: preset landed first; transaction refused`, and `transaction superseded the preset: Pane layout changes made while saving were replaced by the saved monitor layout. Preferences saved; the newer presentation session was left unchanged.`

Full offscreen suite, once, at `0d5afb6`:

```
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build-claude-stack-f049-presets --output-on-failure
10/44 Test #10: pane_layout ......................   Passed    0.52 sec
12/44 Test #12: folder_grid ......................***Failed  158.72 sec
FAIL!  : GridUiTests::externalRenameAndDeleteRefreshKeepSelection() Compared lists have different sizes.
13/44 Test #13: input_ui .........................***Failed   68.35 sec
FAIL!  : InputUiTests::treeRowAndPathBarMenusOpenAndMatch() 'menuOpen(rowMenu)' returned FALSE. ()
16/44 Test #16: directory_model ..................***Failed    4.93 sec
FAIL!  : DirectoryModelTests::selectionSurvivesRelistSortAndDropsRemovedKeys() Compared lists have different sizes.
22/44 Test #22: ui_settings ......................   Passed    0.53 sec
42/44 Test #42: monitor_customization ............   Passed    6.96 sec
43/44 Test #43: monitor_customization_ui .........   Passed   15.21 sec
44/44 Test #44: layout_presets_ui ................   Passed    8.20 sec
93% tests passed, 3 tests failed out of 44
Total Test time (real) = 449.17 sec
```

The other 37 tests passed. The three failures are exactly the baseline functions that docs/handoff/PHASE0-CYCLE1-2026-10-22.md lists for the conda-forge Qt. `folder_grid` and `input_ui` are budget tests; the authoritative gate decides.

### Environment

Same as the original handoff: Ubuntu 24.04, 4 vCPU, offscreen platform.
- aqt failed with a proxy 403 on `download.qt.io`.
- `micro.mamba.pm` is also blocked here, so `qt6-main 6.8.3 hb82b983_5` came from conda-forge through py-rattler into `/opt/qtconda`.
- I added `libgl1-mesa-dev libegl-dev` with apt, because Qt6Gui needs WrapOpenGL.
- Docker and the Windows gate were not run. The hosted `Build and test` run for this push is the first MSVC check of round 1.

### Design-review note (round 1)

The ui-design-skills (application-interface-design, layout, spacing, typography, design-review) are **not available in this cloud session** either. I applied their principles from AGENTS.md and the reviewer's notes, using only existing theme tokens and components. Captures are in `docs/verification/STACK-F049-PRESETS/`:
- `settings-presets-{dark,light}-{560,1180}.png`, showing a frozen tree pane;
- `settings-presets-saving-dark-1180.png`, taken while a display save is pending.

- **Measure.** The scope sentence, the blocked reason, the Reset note and the preset controls are capped at `measure`, 70 average characters of `Theme.textSmall` measured with `FontMetrics`, so the cap follows text scaling. In the captures the scope sentence's first line breaks after 68 characters at both 560 and 1180 px (two lines in all), not about 20. The test asserts at least 40 characters per line and a width no wider than the measure.
- **Stacking and labels.** The combo, Apply and Delete sit on one row under the text, and the combo takes the spare width. The section heading "Pane layout presets" names the block, so the row label "Pane layout" was dropped.
- **Grouping.** "Workspace layout" holds the lock and the Reset that also clears it. "Pane layout presets" holds the presets. "Pane placement" now holds only per-pane controls.
- **Disabled states explain themselves.**
  - Apply: locked, frozen with the pane names, or saving.
  - Lock switch: saving.
  - Freeze switch: saving, or locked.
  - Reset: saving.
  - The reason replaces the description in place, so the layout does not jump, and it reaches assistive technology through each control's `Accessible.description`.
- **Scope words agree across both dialogs.** Settings says "Saved for every window"; Displays and workspaces says "Saved for this window only".
- **Open items for the reviewer:**
  - Preset save and apply refusals still show in the general `paneLayout.feedback` note at the end of "Pane placement", below the fold at 560 px. A note under the save field would be closer. I left it out to keep the round small.
  - "Save pane layout as" keeps the side-by-side `SettingRow`, because its label is short and has no description.
