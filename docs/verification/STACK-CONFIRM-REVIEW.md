# STACK-CONFIRM: whole-stack confirmation on integrated main

**Verdict: CHANGES REQUIRED**

The five merge resolutions, the four frozen corrections and the `ShortcutCheatSheet` fix are all present and correct on `e967a0c`, and 26 of 26 targeted suites pass on Windows (including `reference_pin_native`). Two things stop approval: a slideshow defect (B-1: it stalls for good at an undecodable photo while still showing as running) and acceptance clauses that are neither met nor recorded (B-2, chiefly F009's visible-anchor clause, which a probe shows failing). B-1 needs a code fix and a test; B-2 needs a fix or a lead carve-out per clause. Nothing else needs re-review.

| | |
| --- | --- |
| Reviewer | Fresh-context Claude Code (Opus 5.5) review subagent on the lead's Windows PC. It implemented none of the stack and did none of the per-packet reviews. |
| Target | `origin/main` `e967a0c`. Its code is identical to the gated `71477bd`: `git diff --stat 71477bd e967a0c -- . ':!docs'` shows only `.gitattributes` (+2 lines marking the cycle1-f evidence folder `-text`). Gate evidence on `71477bd`: Windows 51/51, Docker 50/50 (`docs/verification/integrate-cycle1-f-2026-10-24/README.md`). |
| Scope | The five merge resolutions in `3b2b0db`, the four frozen corrections in `docs/handoff/LEAD-WRAPUP-2026-10-22.md` plus the `ShortcutCheatSheet` `ReferenceError` fix, the nine confirm-only features (LEAD-PLAN section 9), cross-feature regressions. The four correction deltas were not re-reviewed line by line; their reviews are on `claude/review-stack-f040-r3` (`STACK-F040-PINNED-REVIEW-R3.md`, APPROVED), `claude/review-stack-f015-r3` (`STACK-F015-RECOVERY-REVIEW-R3.md`, APPROVED), `claude/review-stack-f049` (`STACK-F049-PRESETS-REVIEW.md`, APPROVED) and `claude/review-stack-f054-part2` (`STACK-F054-PART2-REVIEW.md`, APPROVED). |
| Environment | Windows 11 Pro 10.0.26200, MSVC 2022 Release, Qt 6.8.3 (`.tools/Qt/6.8.3/msvc2022_64`), Ninja. Detached worktree `.worktrees/review-stack-confirm`, build `build-review-stack-confirm` (719/719 steps, exit 0) through `scripts/build-slot.ps1`; tests through `scripts/gate-lock.ps1`. Offscreen, software Quick backend (the CMake test environment), except `reference_pin_native`. |
| ui-design-skills | Available on disk at `~/.agents/skills/ui-design-skills`. I read `application-interface-design` and judged the stack's UI against its keyboard, feedback, state and accessibility rules. I did not read the other skills in full. |

## Counting the items

- **Five resolutions in `3b2b0db`**: the five bullets of the merge message (PaneLayout, ViewerController signals, Main.qml, SettingsDialog, test harness).
- **Four frozen corrections** in LEAD-WRAPUP (header rule with F049, F040, F015, F054), plus the takeover's `ShortcutCheatSheet` row.
- **Nine confirm-only items** (LEAD-PLAN section 9): F002, F009, F014, F031, F037, F042-chips, F051, F052, F053.

## 1. Merge resolutions in `3b2b0db`

| # | Resolution | Where it is on `e967a0c` | Status |
| --- | --- | --- | --- |
| R-1 | PaneLayout: `snapshot`/`restoreSnapshot` beside the preset API | `include/glancelot/pane_layout.hpp:35-43` (snapshot extension, then `presetNames`, `savePreset`, `applyPreset`, `deletePreset`); `src/app/pane_layout.cpp:42-52`, `:238-372`; `snapshotExtras_` `:49`, `:70`, carried into the preset write `:104` | Present |
| R-2 | ViewerController: `configurationReloaded` beside `referenceChanged` | `include/glancelot/viewer_controller.hpp:221-222`; emitted by import/reset (`viewer_controller.cpp:2024`, `:2038`, `:2052`); wired to `MonitorCustomization::reloadPreferences` in `app/main.cpp:219` | Present |
| R-3a | Main.qml: main's blocked-reason routing | `commandBlockedReason` `ui/Main.qml:503-513`, used by `invoke` `:479`, `scopeAllows` `:516`, `routeAllows` `:527`, `routeBlockedReason` `:535` | Present |
| R-3b | Main.qml: `controllerOwnsKeys` | `:569-572`, gating the key tables `:912`, `:928`, `:935`, `:942` | Present |
| R-3c | Main.qml: pane focus restore | `pendingPaneFocus` `:329`, `focusDetachedPane` `:330`, restore on attach `:381` and after a modal closes `:897` | Present |
| R-3d | Main.qml: slideshow stop on manual steps | `invoke` `:480`, wheel detent `:634`, modal and deactivation `:894-896`, grid entry `:76` | Present (see F-2 for routes it misses) |
| R-3e | Main.qml: recorder key ownership | `recordingKey`/`keysLive` `:574-575` | Present |
| R-3f | Main.qml: slideshow and focus-mode Escape steps in `escapeContent()` | `:404-415` (chord, slideshow, focus mode, then main's loupe/layout/metadata/grid/fullscreen order) | Present |
| R-3g | Main.qml: relocated preview and empty-state blocks with `qsTr` | `previewState` `:1135-1220`, `emptyState` `:1259-1335` (now also F015's unavailable variant) | Present |
| R-4 | SettingsDialog: monitor settings entry kept, both labels translated | `ui/SettingsDialog.qml:413-414` `qsTr("Displays and workspaces…")`, `:419` `qsTr("Arrange panes")` | Present |
| R-5 | Test harness gains MonitorCustomization context | Superseded by EN-04's single wiring site: `tests/ui_test_support.hpp:69`, `:81-84` (`installViewerContext` with `.monitorCustomization`), same helper as `app/main.cpp:269` | Present |

## 2. Frozen corrections (LEAD-WRAPUP) and the cheat-sheet fix

| # | Correction | Where | Status |
| --- | --- | --- | --- |
| C-1 | Shared `PaneLayout`/`ViewerController` headers keep main's API; F049 presets obey the canonical lock and frozen panes; per-slot placement; unknown/newer data preserved; distinct names and scopes from monitor workspace presets | Lock/freeze refusal before any change `pane_layout.cpp:352-353`; presets never carry lock or freeze `:298`, `:304-305`; one retried merge into `layouts[slot]` keeping unknown root, pane and center fields and `snapshotExtras_` `:96-115`, `:345-348`; newer layout never rewritten `:99`; cross-scope name check including queued display saves `:253-263`, `:313-316`. UI: "Pane layout presets" / "Saved for every window; applies to this window's docked panes…" `SettingsDialog.qml:448-452` versus "Display workspace presets for this window" / "Saved for this window only…" `MonitorSettings.qml:191`, `:206`. Navigator follows `paneAvailable("preview")` `Main.qml:1378`. Distinct registry ids (`layout-preset-next` versus `workspace-*`). | Present. Reviewed APPROVED (F049 round 1, follow-ups R1-R4 in the F054 part-2 review). |
| C-2 | F040 reference window and MONITOR-OUTPUT pinned output: distinct commands, shared admission and release, no duplicate decode, selected image unchanged, parity tests | Commands `reference-pin`/`reference-close` (`command_registry.cpp:125`) versus `presentation-pin` (`:95`). One admission path: `DisplayManager::setReferenceImage` `display_manager.cpp:724`, `admitPin` (R3 review: `:250`), shed order with `referenceShed` `:223`, `:275`; `ViewerController::pinReference` `viewer_controller.cpp:1363` hands over the decoded frame (no second decode). One shed notice `Main.qml:2120`. Tests `tests/reference_pin_tests.cpp` (close, pin replacement, recovery while both active, shared-reference budget) and the Windows-only native target `reference_pin_native`. | Present. Reviewed APPROVED (R3). |
| C-3 | F015 tabs through the production recovery session | Appended defaulted `tabs`/`activeTab` in `ResumeState` and `CheckpointInput` (`include/glancelot/recovery_checkpoint.hpp:6-30`, `MaxTabs = 8`); validation `src/app/recovery_checkpoint.cpp:89-108`; checkpoint input built from the live tab list `viewer_controller.cpp:247`; the no-claim writer saves folder/image only and drops stale tabs `:523-532`; restore skipped under SAFE and explicit intent `:1309`, `:1334`; legacy read with the same validation `:1316-1321`. | Present. Reviewed APPROVED (R3). Known gap recorded in `LEAD-PAUSE-2026-10-23.md:106` (one remaining unavailable tab shows the welcome screen). |
| C-4 | F054 covers main's newer strings (MetadataHud, AnimationBar, RenameFlow, TransferHud, presentation/settings controls, DetachedPaneHost), interpolation and accessibility names preserved, pseudo-locale, clipping and RTL evidence | A scan for literal `text`/`title`/`tip`/`placeholderText`/`Accessible.*` strings finds none in `MetadataHud`, `AnimationBar`, `RenameFlow`, `TransferHud`, `DetachedPaneHost`, `MonitorSettings`, `SettingsDialog`, `ShortcutRecorder`. Catalog `translations/glancelot_en_XA.ts` regenerated at integration (588 messages). Evidence `docs/verification/STACK-F054-COVERAGE/part2/`. RTL: `Main.qml:20-21`, test `rightToLeftMirrorsTheWorkspace`, `translation_coverage_ui` `standaloneRightToLeft`. | Present. Reviewed APPROVED (part 2). Recorded follow-ups: PhotoGrid, DeleteFlow, C++ status strings (`STACK-F054-PART2-HANDOFF.md:297-320`). |
| C-5 | `ShortcutCheatSheet.qml` `ReferenceError: keys is not defined` | `ui/ShortcutCheatSheet.qml:324-325` defines `keys` on the `keyBadges` row and the repeater reads `keyBadges.keys` (`:328`, `:355`). Test `command_routing` `mainQmlLoadsWithoutQmlWarnings` (`tests/command_routing_tests.cpp:208-219`) opens the sheet and fails on any QML warning. The gate's QTest results files (`docs/verification/integrate-cycle1-f-2026-10-24/results/`, all 51 suites) contain no `ReferenceError`, `TypeError`, `is not defined` or binding-loop warning. | Fixed |

## 3. Per-feature acceptance

Each queue `acceptance` clause is in one of three classes: **met** (a test or code named), **carved out** (the row's `outcome`, the feature plan's "Not done", a RUNBOOK or a named later packet already records it), or **gap** (not met and not recorded anywhere I could find). Only gaps can block.

| Feature (queue row) | Met | Carved out | Gap (not recorded) | Status |
| --- | --- | --- | --- | --- |
| F002 PATH-COMPLETE-01 | Jump to ancestors (`pathbar_ui` `breadcrumbsJumpToAncestors`); completion for the typed scope only, off the GUI thread, cancelled by a newer request, generation-fenced, bounded to 20 names from a 5000-entry listing (`viewer_controller.cpp:1839-1862`; `pathbar_ui` `completionListsTypedScopeAndTabAccepts`). Address-field exact text semantics are PATH-BAR-01's (done). | Sibling jump menus, arrow-key selection in the list (`docs/F002-PLAN.md` "Not done") | Completion is not tested with UNC, Unicode or long-path scopes (N-6) | Met with recorded gaps |
| F037 VIEW-POLISH-01 | Predictable anchor (presets about the view centre), zoom lock, navigator (`canvas` `presetsZoomLockAndNavigator`, `loupeRestoresExactly`; `input_ui` `navigatorPresetsAndZoomLock`); hold cancelled by release, focus loss, a modal and an image change (`input_ui` `holdActualRestoresOnEveryEnd`, `cancelledPointerGrabRestoresAKeyHold`, `rightButtonClickIsTheMenuAndHoldIsTheLoupe`) | Physical 1:1 under fractional DPI (row text, plan "Not done") | none | Met |
| F031 SLIDESHOW-01 | User input cancels: keys, toolbar, wheel detent, Escape, modal, deactivation, grid entry (`input_ui` `slideshowAdvancesAndEndsCleanly`); never queues steps (`Main.qml:658`) | none in the plan's "Not done" for these clauses | **Missing or undecodable files are not skipped: the slideshow stalls (B-1).** Animation-aware clock (G-2). Tab switches and folder opens do not end it (N-2). Unplugged presentation monitor: nothing slideshow-specific; it relies on MONITOR-OUTPUT, physical unverified (G-2). Bounded prefetch: relies on the viewer's existing lookahead, no slideshow test (G-2). | **Not met** |
| F040 REFERENCE-WINDOW-01 | See C-2; recovery route: `reference-close` is a global command reachable from the palette, plus the window's own close (`input_ui` `referenceWindowKeepsItsPhoto`) | Platform capability checks beyond Qt's are not separately evidenced | none | Met |
| F049 LAYOUT-PRESETS-01 | See C-1; focus mode resets with Escape and leaves the saved layout untouched (`input_ui` `focusModeHidesChromeAndEscapeRestoresIt`); selection kept (presets change panes only) | none | Settings preset list is stale after Import or Restore all (N-4) | Met |
| F009 DETAILS-VIEW-01 | Shared selection, cursor keys and sort (same `GridView` and model; `input_ui` `detailsModeListsRowsWithoutThumbnails`); virtualized rows, no thumbnails requested (`PhotoGrid.qml:727`, `:858-875`) | Sortable persisted columns (outcome: packet A6) | **"Changing mode preserves the visible anchor" is not implemented** (no re-anchoring on `details` change; measured, G-1) | **Not met** |
| F014 GRID-BADGES-01 | Type badge and failed-preview badge in words plus icon, never colour alone, hidden below 96 px, left edge clear of the link badge, the "Viewing" tag and the file name (`PhotoGrid.qml:784-816`; `input_ui` `gridBadgesAreLabelledAndDoNotOverlap`); the cell's accessible description says "Preview unavailable" | Rating, dimension, quality badges (outcome: packet A4) | none | Met |
| F042 TYPE-CHIPS-01 | No metadata read per delegate (the `kind` role from the listing, `DirectoryEntryModel::setKindFilter` `directory_entry_model.cpp:673`); filtering consistent with the model and combined with the name filter, Esc clears both (`input_ui` `kindChipsFilterTheGridByType`); missing data: kind is always known | Capture-date/dimension sorts, rating/orientation/date chips (outcome: A2, A4) | Chips show no counts (G-3); chips are mouse-only (N-1) | Met except G-3 |
| F051 SHORTCUT-RECORDER-01 | Scope tests (`window_slots` `scopeAwareConflictsAndChordPrefixes`); no command fires while recording, including the taken key's own command (`Main.qml:574-575`; `input_ui` `shortcutRecorderCapturesChordsAndExplainsRefusals`) | AT trials, conflict view, import/export, global-on-native-key warning (`docs/F051-PLAN.md` "Not done") | Keyboard-layout and IME tests; explanation of OS-reserved shortcuts (G-4) | Not met (G-4) |
| F052 A11Y-AUDIT-01 | Accessible names and roles for every visible enabled interactive control on the main window and all Settings pages, at least 40 inspected (`input_ui` `everyInteractiveControlIsNamedForAssistiveTechnology`); semantic selection on grid cells (`PhotoGrid.qml:706-708`); pane focus restore (R-3c) | NVDA/Orca, live announcements, reading order (outcome: E6 RUNBOOK; plan "Not done") | none | Met with recorded gaps |
| F053 CONTRAST-TARGETS-01 | AAA pairs in both modes, 2 px borders, 3 px focus ring, 44 px targets on real controls and menu rows (`input_ui` `highContrastIsAAAAndLargeTargetsReachFortyFourPixels`); persistence and reset (`window_slots` `accessibilityOptionsPersistAndReset`) | Native high-contrast detection, 540x360 with large targets (outcome: E6; plan "Not done") | 200 % text with long translated labels is not evidenced for the stack's new controls (folded into G-2's list for E6) | Met with recorded gaps |
| F054 I18N-01 | See C-4 | Plural forms and remaining strings (E6); PhotoGrid, DeleteFlow and C++ strings (F054 part-2 handoff) | `ShortcutCheatSheet.qml` has no `qsTr` at all and is not in the follow-up list (N-3) | Met with recorded gaps |
| F015 TABS-01 | See C-3; closing a tab releases work and restores lazily (F015 R3); tab state in the recovery checkpoint (`tabs_recovery`, 68 cases) | One remaining unavailable tab (LEAD-PAUSE `:106`) | Tab strip overflow hides the active tab and the tab buttons (N-5) | Met |

## 4. Findings

### Blocking

**B-1 (HIGH, user-visible defect and acceptance failure, F031): the slideshow stops advancing for good at a photo that cannot be decoded, while it still shows as running.**

- Where: `ui/Main.qml:657-661`. The tick returns early while `!viewer.imageReady || viewer.loading`. A failed decode ends in `fail()` (`src/app/viewer_controller.cpp:850`), which leaves `imageReady=false` and `loading=false`, so every later tick returns at the first line and `viewer.next()` is never called again. The same path is taken by a file that vanishes between listing and decode.
- Evidence (probe `probeSlideshowOverUndecodablePhoto`, `docs/verification/STACK-CONFIRM/review-probes.cpp.txt`): three photos, the second overwritten with text bytes, 1 s per photo, loop boundary:

  ```
  QINFO  : probeSlideshowOverUndecodablePhoto() t=1s index=1 ready=1 loading=0 on=1 status="Untagged source assumed sRGB ? display profile not yet managed"
  QINFO  : probeSlideshowOverUndecodablePhoto() t=2s index=2 ready=0 loading=0 on=1 status="This alpha supports JPEG, PNG, GIF, WebP, TIFF, BMP, and TGA images."
  ... (identical lines for t=3s to t=7s)
  QINFO  : probeSlideshowOverUndecodablePhoto() t=8s index=2 ready=0 loading=0 on=1 status="This alpha supports JPEG, PNG, GIF, WebP, TIFF, BMP, and TGA images."
  FAIL!  : InputUiTests::probeSlideshowOverUndecodablePhoto() 'h.viewer.currentIndex() != 2 || !flag(h, "slideshowOn")' returned FALSE. (slideshow stays on, stuck at the undecodable photo)
  ```

  The status bar keeps "Slideshow · 1 s" and the accessible name "Slideshow running…" while nothing moves. The acceptance clause "missing files skipped with limits" is not met, and the plan does not carve it out.
- Fix: treat a settled failure (`!loading && !imageReady` after the step landed) as shown and step on; stop with a status after a bounded number of consecutive failures (or after a full pass with no decodable photo). Add the probe as a test, with a vanished file as a second case.

**B-2 (bookkeeping, acceptance): acceptance clauses that are neither met nor recorded.** Each needs a fix or a lead decision that moves it to a named packet (queue `outcome` or the plan's "Not done"), as was done for the other features' partial scope. No code is required if the lead carves them out, except G-1, where a fix is small.

- **G-1 (MEDIUM, F009): changing mode does not preserve the visible anchor.** Nothing re-anchors on `details` change (`ui/PhotoGrid.qml:61`, `:64`, `:507`, `:727`; no handler). The view keeps its `contentY`, so the anchor survives only when the row/tile height ratio happens to match the column count. Probe `probeDetailsToggleKeepsVisibleAnchor`, 120 files, scrolled to one fifth:

  ```
  QINFO  : width 1180 (6 columns): first visible before 18, after details 19
  QINFO  : width 1800 (10 columns): first visible before 10, after details 18
  QINFO  : width 760 (4 columns): first visible before 20, after details 21
  ```

  At 10 columns the first visible item moves from 10 to 18, so the item the user was looking at leaves the viewport. Suggested fix: on `detailsChanged`, record the first visible path (as `rememberViewport()` does, `PhotoGrid.qml:148-160`) and restore it after the relayout (`restoreViewport()`, `:161-172`), keeping a visible cursor in view.
- **G-2 (F031, F053):** the animation-aware clock, slideshow prefetch and unplugged-presentation-monitor clauses (F031), and 200 % text with long labels on the stack's new controls (F053), have no implementation or evidence and are not listed as "Not done".
- **G-3 (LOW, F042):** the clause says "chip counts … stay consistent with the grid model"; the chips show no counts (`PhotoGrid.qml:295-310`). The grid's "%1 matches" label is consistent with the model. Either add counts or reword the clause.
- **G-4 (F051):** no keyboard-layout or IME test exists, and `bindingProblem()` (`viewer_controller.cpp:1648-1661`) explains Escape, length, hold and conflicts but not OS-reserved shortcuts (for example Alt+F4, Alt+Tab, Win+L), which never reach or immediately leave the recorder without explanation.

### Non-blocking

- **N-1 (MEDIUM, F042/F052, keyboard access):** the type chips have `focusPolicy: Qt.NoFocus` (`PhotoGrid.qml:303`) and no registry command, so they cannot be reached or toggled from the keyboard. application-interface-design asks for complete keyboard operation; the name filter (Ctrl+F) remains keyboard-reachable, so nothing essential is lost. Suggested: Tab-focusable chips or a `filter-kind-*` command.
- **N-2 (LOW, F031 x F015/F002):** switching tabs and opening another folder (tree, breadcrumbs, path field) do not end the slideshow; it carries on in the new folder. The plan says it ends on "any manual navigation". Probe `probeSlideshowAcrossTabAndFolderNavigation`: `after tab-new: slideshowOn=1`, `after opening another folder in tab 2: slideshowOn=1`, `after tab-previous: slideshowOn=1`. The stop list in `invoke` (`Main.qml:480`) has only next/previous/next-folder/previous-folder.
- **N-3 (LOW, F054 scope):** `ui/ShortcutCheatSheet.qml` (SHORTCUTS-HELP-01, on main before `3b2b0db`) has no `qsTr`: title, category names, every label, the placeholder, "No shortcuts match …", and a concatenated English plural (`matches + " match" + …`, `:283`). It is not in F054's follow-up list. Its labels also duplicate the registry's command labels, and an unbound command falls back to its hard-coded default key (`activeBinding`, `:85-91`), so a cleared shortcut still shows as bound. Not a stack regression; worth adding to the E6 list.
- **N-4 (LOW, F049 x settings import/reset):** after Import settings or Restore all, `MonitorCustomization::reloadPreferences` reloads the pane layout (`monitor_customization.cpp:242-250`) but nothing emits `PaneLayout::presetsChanged`, so the Settings preset choice (`SettingsDialog.qml:463`, bound to `paneLayout.presetNames`) keeps the old list until a save or delete. Applying a name the import removed returns false with no message (`pane_layout.cpp:350-351`). The `layout-preset-next` command reads the list fresh (`Main.qml:759`) and is not affected. Restore all keeps user presets (`settings_file.cpp:486-503` does not remove `layoutPresets`), which is data-preserving and fine.
- **N-5 (MEDIUM, F015 UI):** tabs have no maximum width or elide and the strip does not keep the active tab in view (`Main.qml:1823-1866`). On the native platform at 1180 px, a long folder name pushes the active tab's close button and the New tab button out of the visible strip; at 560 px only a clipped first tab ("input_ui_t") shows and the active tab is not visible (`docs/verification/STACK-CONFIRM/f015-unavailable-dark-1180.png`, `f015-unavailable-light-560.png`). Commands still work (`tab-*`, palette). Suggested: cap tab width with middle elide and `ensureVisible` the active tab.
- **N-6 (LOW, F002):** completion has no UNC, Unicode or long-path test; the code passes names through `displayPath` and a case-insensitive prefix match, which reads correctly.
- **N-7 (LOW, F054 C++ strings):** `bindingProblem()` returns four of its five reasons as `QStringLiteral` (untranslated), `PaneLayout::edit/move` refusals `pane_layout.cpp:165`, `:179` are untranslated while the same sentence is `qsTr` in `Main.qml:506`, and the restore messages `viewer_controller.cpp:1347-1348` are English. Already covered by the recorded "C++ strings" follow-up; listed so the follow-up has the locations.
- **N-8 (info, evidence method):** on the offscreen platform with the software Quick backend, icons inside the clipped tab strip are drawn outside the clip (the close and New tab icons appear over "Settings"; `docs/verification/STACK-CONFIRM/offscreen-software-f015-unavailable-dark-1180.png`). The native D3D capture of the same state is clipped correctly. Offscreen captures used as UI evidence can therefore show artefacts that the product does not have; judge clipping from native captures.

### Confirmed without findings

- **F015 placeholder rendered** (the R3 review could not render it): native captures `docs/verification/STACK-CONFIRM/f015-unavailable-{dark,light}-1180.png` and `-light-560.png` show the folder glyph, the wrapped "<folder> is unavailable" heading, the middle-elided native path, one primary Retry button and the hint, with the status line naming the folder. It follows application-interface-design's empty-state rule (explain, one next step) and reads at 560 px.
- **Cross-feature QML health:** across the gate's 51 QTest results files there is no `ReferenceError`, `TypeError`, `is not defined` or binding-loop warning; the only QML warnings are thumbnail-provider failures on purpose-made broken files, `No QRhi` on the software backend, a missing Qt font directory, and three `raise()` notes from the offscreen plugin.
- **Commands:** every stack command id (`reference-*`, `focus-mode`, `layout-preset-next`, `tab-*`, `view-details`, `slideshow*`, `zoom-lock`, `navigator`, `zoom-50/200/400`, `shortcuts-help`) has exactly one handler, in `Main.qml` or in `ViewerController::trigger`; `command_routing` passes.
- **Persistence and write ordering:** presets write `layouts[slot]` through the retried layout channel and `layoutPresets` through one locked update; tabs go only through the recovery checkpoint (the no-claim writer drops them); the preset/transaction orderings are forced in `monitor_customization` (R1). `recovery*`, `tabs_recovery`, `window_slots`, `pane_layout`, `monitor_customization` pass.

## 5. Test output

Targeted suite, Windows, offscreen (CMake test environment), inside `& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch review-stack-confirm -Run { ctest --test-dir build-review-stack-confirm --output-on-failure -R "^(folder_grid|input_ui|pathbar_ui|fs_watch_ui|monitor_customization_ui|translation_coverage_ui|layout_presets_ui|command_routing|reference_pin|reference_pin_native|tabs_recovery|recovery|recovery_startup|recovery_controller|recovery_process|monitor_customization|monitor_output|window_slots|pane_layout|viewer_acceptance|shortcuts_help|themes|ui_settings|viewport|directory_model|filter)$" }`. This covers every `-L ui` test plus the persistence, recovery, monitor and settings suites. First run; no reruns.

```
Test project C:/src/Github/Glancelot/.worktrees/review-stack-confirm/build-review-stack-confirm
 1/26 Test  #7: viewer_acceptance ................   Passed   15.77 sec
 2/26 Test #10: pane_layout ......................   Passed    2.77 sec
 3/26 Test #11: window_slots .....................   Passed   41.21 sec
 4/26 Test #12: folder_grid ......................   Passed  366.13 sec
 5/26 Test #13: input_ui .........................   Passed  117.70 sec
 6/26 Test #14: pathbar_ui .......................   Passed    7.76 sec
 7/26 Test #16: directory_model ..................   Passed   28.39 sec
 8/26 Test #17: viewport .........................   Passed    0.15 sec
 9/26 Test #18: themes ...........................   Passed    0.79 sec
10/26 Test #22: ui_settings ......................   Passed    1.67 sec
11/26 Test #26: fs_watch_ui ......................   Passed    5.37 sec
12/26 Test #29: shortcuts_help ...................   Passed    0.80 sec
13/26 Test #30: filter ...........................   Passed    3.75 sec
14/26 Test #39: monitor_output ...................   Passed    2.72 sec
15/26 Test #40: recovery .........................   Passed   14.92 sec
16/26 Test #41: recovery_startup .................   Passed    0.74 sec
17/26 Test #42: recovery_controller ..............   Passed   15.80 sec
18/26 Test #43: recovery_process .................   Passed   35.52 sec
19/26 Test #44: tabs_recovery ....................   Passed   63.85 sec
20/26 Test #45: monitor_customization ............   Passed    9.78 sec
21/26 Test #46: monitor_customization_ui .........   Passed   38.16 sec
22/26 Test #47: translation_coverage_ui ..........   Passed   29.08 sec
23/26 Test #48: layout_presets_ui ................   Passed   28.74 sec
24/26 Test #49: command_routing ..................   Passed   13.16 sec
25/26 Test #50: reference_pin ....................   Passed   43.02 sec
26/26 Test #51: reference_pin_native .............   Passed   35.76 sec

100% tests passed, 0 tests failed out of 26
Total Test time (real) = 923.57 sec
```

`reference_pin_native` ran on the native platform and passed here (the F040 R3 reviewer could not run it to completion). The full suite was not rerun; the authoritative gate on the identical code (`71477bd`) passed Windows 51/51 and Docker 50/50.

Review probes (local edit of `tests/input_ui_tests.cpp`, rebuilt `input_ui_tests` only through `build-slot.ps1`, run by function name under `gate-lock.ps1`, then `git checkout -- tests/input_ui_tests.cpp`; `git status` clean afterwards). Source: `docs/verification/STACK-CONFIRM/review-probes.cpp.txt`. Offscreen/software run:

```
FAIL!  : InputUiTests::probeSlideshowOverUndecodablePhoto() ... (slideshow stays on, stuck at the undecodable photo)
PASS   : InputUiTests::probeSlideshowAcrossTabAndFolderNavigation()      (informational; see N-2)
PASS   : InputUiTests::probeGrabUnavailableTabPlaceholder()              (captures; see N-5, N-8)
FAIL!  : InputUiTests::probeDetailsToggleKeepsVisibleAnchor() 'qAbs(afterA - beforeA) <= 1' returned FALSE. (details toggle moved the visible anchor)
Totals: 4 passed, 2 failed, 0 skipped, 0 blacklisted, 25792ms
```

The anchor probe's first assertion uses the middle of the list, where the drift is 2 items; the width loop at one fifth (G-1) shows the 8-item drift. The native capture run (`QT_QPA_PLATFORM` and `QT_QUICK_BACKEND` unset): `Totals: 3 passed, 0 failed` for `probeGrabUnavailableTabPlaceholder`.

## Unverified

- Physical displays, fractional DPI, NVDA/Orca/Narrator, IME and keyboard layouts (as recorded per feature).
- The Docker/Linux build was not run by this review; the gate on `71477bd` covers it.
- G-2's prefetch and presentation-monitor clauses were judged from code, not exercised.
