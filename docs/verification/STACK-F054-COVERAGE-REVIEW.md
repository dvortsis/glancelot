# STACK-F054-COVERAGE review (part 1, re-review after round 1)

**Verdict: APPROVED** (part 1 only: every file except `ui/Main.qml`). No blocking findings. The lead must still accept the C++ change for item 7 and handle the integration notes below.

| | |
| --- | --- |
| Packet | STACK-F054-COVERAGE part 1, translation coverage. Part 2 (`ui/Main.qml`) is out of scope. |
| Branch / tip reviewed | `origin/claude/stack-f054-coverage` at `7281aa8` (code tip `20d292f`) |
| Diffs read | `git diff afeac1a 7281aa8` (packet) and `git diff ed022ba 7281aa8` (round 1) |
| Contract | F054 bullet in `docs/handoff/LEAD-WRAPUP-2026-10-22.md`: user-facing strings in MetadataHud, AnimationBar, RenameFlow, TransferHud and the presentation and settings controls; keep interpolation and accessible names; supply pseudo-locale, clipping and RTL evidence |
| Reviewer | Fresh-context Claude reviewer on the lead's Windows PC. It did not implement this packet and did not do the earlier review. |
| Checkout | `.worktrees/review-f054` (detached at `7281aa8`), build `build-review-f054`, MSVC 2022 with the project-local Qt 6.8.3 |
| UI skills | Loaded from `~/.agents/skills/ui-design-skills`: application-interface-design, typography-ui, layout-spacing, responsive-layout and design-review |

## Stage-1 findings and their status

The first review raised seven items:

1. **The RTL evidence was vacuous.** The test harness had no `LayoutMirroring`, the LTR and RTL captures were byte-identical, and the order assertion passed even without AnimationBar's opt-out.
2. **Plurals (`%n`)** were missing. The lead waived this; it is a TODO for E6.
3. **Settings strings were left unwrapped** in ThemePicker (about lines 35, 36, 55 and 123), SettingRow (about line 39) and UiSwitch (about line 62). The coverage test only checked visible Text.
4. **The handoff misdescribed the removed `Status` message.** It belongs on the C++ follow-up list.
5. **Two translations were double-escaped** (Delete and Grid).
6. **MonitorSettings wording at about lines 191 and 206** belongs to F049 and should be reverted here.
7. **`Accessible.checkable` was missing** next to `Accessible.checked`, and MetadataHud compared its text against the C++ string "Unknown Camera".

| # | Finding | Status | How it was checked |
| --- | --- | --- | --- |
| 1 | Vacuous RTL evidence | **Resolved** | `standaloneSurfaces` now puts AnimationBar and TransferHud under an Item with `LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft; childrenInherit: true`, the same way Main.qml does. A control copy of AnimationBar, with its opt-out removed, must mirror in RTL; this proves the harness mirrors. TransferHud must mirror: the cancel button and the leading edge of the progress fill are asserted. **Negative check run here:** with `LayoutMirroring.enabled: false` deleted from `ui/AnimationBar.qml`, `standaloneRightToLeft` fails with `'previousX<nextX' returned FALSE. (rtl: previous frame at x=460, next frame at x=424)`. With the line restored, all 4 tests pass. The Windows captures differ (`animation-transfer-ltr` `770d3ff3…`, `-rtl` `403a2d28…`) and look right: in RTL, TransferHud's cancel button leads on the left and the fill grows from the right, while AnimationBar keeps play, previous, next in order. The committed captures differ too (`a9c382a0…` and `17391cd8…`). The other RTL pairs (rename, new-folder, settings-browsing, monitor-page2, metadata and detached-metadata) also mirror: dialog buttons, close controls, the settings navigation, the top bar and the status bar all swap sides. |
| 2 | Plurals | **Waived by the lead** | The TODO for E6 is recorded in the handoff for "%1 tags" and "%1 of %2 items…". |
| 3 | Unwrapped Settings strings | **Resolved** | ThemePicker (placeholder, search name, list name, "%1, selected", empty-state text), SettingRow and UiSwitch are wrapped. The walk now also checks accessible names and descriptions, ToolTip text, UiButton `tip` and `placeholderText`. **Negative check run here:** reverting ThemePicker's placeholder to concatenation fails `pseudoLocaleLeftToRightDark` with `"Search 40 palettes (placeholderText)" at settings-appearance/…/ThemePicker/UiTextField`. A scan of every in-scope file for plain English literals and for `"…" +` concatenation finds only identifiers, the two F049-owned names in `deferredToF049` (SettingsDialog about lines 395 and 422) and the `sectionMatches` search keywords (documented as not wrapped). |
| 4 | Removed `Status` message | **Resolved (documentation)** | `git show 90e63e2 -- src/app/command_registry.cpp` confirms that the text became the plain literal `result.error="Saved shortcuts conflict…"`. The handoff now says this and lists it as a C++ follow-up. |
| 5 | Double-escaped translations | **Resolved** | No `&amp;apos;`-style sequence remains in `translations/glancelot_en_XA.ts`. `catalogCoversEveryInterfaceMessage` now fails when a decoded translation holds an entity that its source lacks, and it spot-checks both messages. |
| 6 | F049 wording in MonitorSettings | **Resolved** | Both strings now match `origin/main` (and `afeac1a`) word for word inside `qsTr`: "Named presets for this window slot" and the three-sentence note. The added `monitorPresetHeading` objectName is gone. |
| 7 | `checkable`; "Unknown Camera" | **Resolved; lead decision needed** | `Accessible.checkable: true` is set on the metadata filter pills and the loop toggle. `MetadataModel::cameraSummary()` now returns an empty string when there is no camera. `git grep cameraSummary` finds only the HUD (which hides empty text), `copySummary`, `metadata_tests` and the coverage test. The presentation window and other consumers do not read it, and the header has no doc comment that promises the sentinel. **This changes the value of a property declared in the frozen `include/glancelot/metadata_model.hpp`, and the edit lies outside F054's QML and translations grant.** The handoff offers to drop it. I see no functional risk. |

## Other checks

- **Identifiers are not translated.** Across the whole `ui/` diff, the set of `objectName: "…"`, `invoke("…")`/`invokeTarget("…")` command ids is identical before and after. Option values, filter keys (`["All","Camera",…]`), pane ids (`tree/grid/preview/metadata`), preset kinds and `SettingsDialog` section ids stay English; only display maps (`paneTitles`, pill `label`, choice labels) go through `qsTr`.
- **Catalog.** The project-local Windows Qt (`.tools/Qt/6.8.3/msvc2022_64/bin`) has no `lupdate` or `lrelease`. Real regeneration on Windows is therefore **unverified**; I did not download the Linguist tools. In its place I ran three checks:
  - A scratch extractor compared every `qsTr("…")` in `ui/*.qml` (418 messages in 17 contexts) with the catalog: 0 messages were missing and none was unfinished or vanished. The only catalog entries in QML-named contexts with no QML source are three `Theme` messages from `src/app/theme_manager.cpp` (`QCoreApplication::translate("Theme", …)`).
  - `python translations/pseudolocalize.py translations/glancelot_en_XA.ts` reported `0 message(s) pseudo-localized` and left the tree clean, so it is idempotent.
  - `git ls-files --eol` shows `i/lf` for the `.ts` file and the script.
  - The round-1 `git diff ed022ba 7281aa8 -- translations/` contains only the two escaping fixes, the two restored MonitorSettings strings in place of the reverted ones, and the SettingRow, ThemePicker and UiSwitch messages.
- **Merge with main.** `git merge-tree --write-tree --name-only origin/main origin/claude/stack-f054-coverage` (main at `c903e6f`) is clean: tree `d29dc818…`, no conflicted paths.

## Test output (Windows, this PC)

Build: `build-slot.ps1 -Run { … scripts/build.ps1 -BuildDir build-review-f054 }` finished all 571 steps with exit 0. The targeted run used `gate-lock.ps1 -Branch review/f054 -Run { … }`, with `GLANCELOT_F054_UI_EVIDENCE` pointing at a scratch folder so the committed captures stayed untouched:

```
 1/10 Test #13: input_ui .........................   Passed   73.83 sec
 2/10 Test #14: pathbar_ui .......................   Passed    4.85 sec
 3/10 Test #22: ui_settings ......................   Passed    1.58 sec
 4/10 Test #25: fs_watch_ui ......................   Passed    3.43 sec
 5/10 Test #28: shortcuts_help ...................   Passed    0.64 sec
 6/10 Test #32: metadata .........................   Passed    0.14 sec
 7/10 Test #33: animation ........................   Passed    0.48 sec
 8/10 Test #42: monitor_customization ............   Passed    8.54 sec
 9/10 Test #43: monitor_customization_ui .........   Passed   32.70 sec
10/10 Test #44: translation_coverage_ui ..........   Passed   32.91 sec
100% tests passed out of 10
Total Test time (real) = 159.61 sec
```

`input_ui`, the conda-forge baseline failure in the cloud handoff, passes on Windows. The Windows `coverage-report.txt` reports 475 messages and no untranslated text. Its clipping candidates are "Folder grid: details or thumbnails" (elided) and the metadata title in the 40%-expanded pass. The HDD storage-profile label that the Linux run elided fits with Windows fonts.

The negative checks were run directly on the test binary with `QT_QPA_PLATFORM=offscreen` and `QT_QUICK_BACKEND=software`; the files were restored afterwards and `git status` was clean:

```
(AnimationBar opt-out removed)
FAIL!  : TranslationCoverageUiTests::standaloneRightToLeft() 'previousX<nextX' returned FALSE. (rtl: previous frame at x=460, next frame at x=424)
Totals: 3 passed, 1 failed, 0 skipped, 0 blacklisted, 1231ms
(restored)
Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 1513ms
(ThemePicker placeholder concatenated)
FAIL!  : TranslationCoverageUiTests::pseudoLocaleLeftToRightDark() 'plain.isEmpty()' returned FALSE. (Search 40 palettes (placeholderText) at settings-appearance/…/ThemePicker/UiTextField)
```

I did not run a full gate (`build.ps1 -Test` and Docker). The integration gate decides.

## Design review (non-blocking)

I reviewed the native QML against application-interface-design, layout-spacing, typography-ui, responsive-layout and design-review. No new chrome, spacing values or colours were introduced. The only layout change in the packet is the MetadataHud pills: a `Flow` replaces a `RowLayout` (reflow over clipping), which fits the responsive-layout skill, and it holds at 560 px.

- **UiSwitch does not mirror.** In `monitor-page2-rtl` and `settings-browsing-rtl` the toggle stays at the right edge, with its label on the left. The indicator uses an explicit `x`. The handoff already lists this as a follow-up in `ui/UiSwitch.qml`, and I agree.
- **Text alignment in mirrored rows follows the text's direction.** Labels with `Layout.fillWidth` keep implicit alignment. In the pseudo-locale (Latin script, RTL layout), the detached Metadata title therefore sits on the far side from its info icon, and Settings row labels align left while the section heading aligns right. Real RTL scripts align right on their own, so this is acceptable. Setting an explicit `horizontalAlignment: Text.AlignLeft` on titles would let `LayoutMirroring` flip them, giving consistent results with mixed-script data such as file names.
- **TransferHud's pause button is blank**, because UiIcon has no `pause` or `play` path. It shows in both the Windows and the Linux captures. This is already routed to COPY-MOVE-01's owner.
- **Out of scope, for the C++/QML extraction follow-up:** `ui/DeleteFlow.qml` still builds English by concatenation (about lines 217, 228, 250 and 266: ", folder"/", file"/", link", "and N more", "This item can't be moved:"). DeleteFlow is not one of F054's named surfaces.

## Integration notes for the lead

1. **EN-03 labels.** `translation_coverage_ui` has no `glancelot_test_labels` line, so EN-03 computes it as `fast`. It loads `ui/Main.qml` and opens an image through the decoder, so declare `glancelot_test_labels(translation_coverage_ui LABELS ui process)`, the same as `monitor_customization_ui`. The provisional registration in the `foreach` of `cmake/MonitorCustomization.cmake` merges cleanly with main; it can move to its own fragment.
2. **EN-06 / part 2.** Main on `origin/main` still produces the routing reasons "Not in grid view", "Needs the folder grid" and "Needs the folder tree".
   - `deferredToMainQml` lists "Needs the photo view" and "Needs a photo", which `git grep` no longer finds anywhere in `ui` or `src` on `origin/main`, and does not list "Not in grid view". That string most likely passes today because the walk runs in photo view.
   - Part 2 must wrap these strings in Main.qml and delete the list. The merge itself is conflict-free.
3. **F049.** Resolve the adjacent `foreach` edit by listing both suites. Delete `deferredToF049` once F049 integrates, then regenerate the catalog.
4. **Catalog regeneration** needs a Qt that ships the Linguist tools (cloud or Linux). The Windows `.tools/Qt` has no `lupdate` or `lrelease`.
5. **Item 7.** Accept or drop the `src/app/metadata_model.cpp` and `tests/metadata_tests.cpp` hunks. They change the value of a property declared in the frozen `metadata_model.hpp`, but not its declaration.
6. **Lead-owned file.** Commit `8c2c3ea` edits `cmake/MonitorCustomization.cmake`, which is a lead-owned `cmake/*.cmake` file. The handoff discloses this edit as provisional. Keep it or replace it at integration (see note 1).
