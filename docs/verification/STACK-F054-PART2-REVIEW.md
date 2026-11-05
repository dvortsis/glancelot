# STACK-F049 follow-ups R1-R4 and STACK-F054 part 2: review

**Verdict: APPROVED**

- Reviewer: fresh-context session, did not implement either packet. Read-only worktree `.worktrees/review-f054p2`, build `build-review-f054p2` (Release, MSVC, Qt 6.8.3), built through `scripts/build-slot.ps1`, targeted tests run through `scripts/gate-lock.ps1`.
- Candidate: `origin/claude/stack-f054-part2` at `98adda9`. F049 follow-ups: `747b926..04aef7c` on top of the F049 merge `a425ed1`. F054 part 2: `e1d584b..98adda9`. `git merge-base origin/main 98adda9` = `894346385fe646f2e8dbfbafeb0cf533e39e1110`.
- Scope: `git diff a425ed1 98adda9` (src/app, tests, ui, translations). The F049 body was reviewed earlier (`origin/claude/review-stack-f049`, APPROVED with R1-R4).
- ui-design-skills: not available in this session (no such skill in the skill list). R2 and R3 were judged against the AGENTS.md design principles (feedback near the action, shared edges, measure for prose only) from the captures `docs/verification/STACK-F049-PRESETS/settings-presets-refusal-dark-560.png` and `settings-presets-light-1180.png`, and from the layout tests. Not a skill-backed review.

No blocking findings. Four LOW, non-blocking items at the end.

## R1-R4 resolution

| # | Resolved | Evidence |
|---|---|---|
| R1 forced write order | Yes | `tests/monitor_customization_tests.cpp:91-135` runs four explicit cases (queue order x landing order) with `attemptPendingSettingsNow`; each asserts its own outcome, so the "transaction superseded the preset" path now runs on Windows (log below shows all four). `supersededFeedbackSurvivesNewPresentationSession` forces the transaction first and asserts the replaced message leads. Mutation run: see M-R1. |
| R2 refusal beside the control | Yes | `ui/SettingsDialog.qml:46-72` (`presetSaveRefusal`, `presetApplyRefusal`, `savePanePreset`, `applyPanePreset`, `revealItem`), notes at 485-535 directly under the preset row and the save row; field kept for correction, cleared on edit/activate/close, mirrored to `Accessible.description`. Test `presetRefusalsShowBesideTheirControls` checks the text sits under the control inside the 560 px viewport, appears once (general note suppressed via `presetRefusalShown`), and is the field's accessible description. |
| R3 shared edges | Yes | Control rows no longer capped at `measure` (only `Note`s), "Save pane layout as" stacked like the preset row. Test `presetControlRowsShareTheColumnEdges` at 560 and 1180: lock, Delete, Save and pane-choice right edges within 1 px, name field left equals preset-choice left. Captures confirm. |
| R4 own outcome only | Yes | `ui/Main.qml:154-173` (`presetRequest`, `presetPendingRequest`, `presetWriteSaving`, `presetWriteOutcome`, 6 s `presetOutcomeTimer`) and the command at `Main.qml` ~755-767. Armed only when the feedback is the saving text; only the two end-of-write messages are shown; any other message disarms silently. Test `presetCommandShowsOnlyItsOwnWriteOutcome` covers immediate and waiting cases, checking the unrelated message first. Mutation run: see M-R4. |

R5 (polish) was not in scope of the follow-up and is not claimed.

## Mutation spot-checks (all fail against the old behaviour)

Run in this worktree, restored afterwards with `git checkout --`; no mutation is committed.

- M-R2/R3/R4 (one build): `ui/SettingsDialog.qml` replaced by the `a425ed1` version, and in `ui/Main.qml` the outcome/arming conditions changed back to "any non-empty feedback". `layout_presets_ui` result:

```
FAIL!  : LayoutPresetsUiTests::presetRefusalsShowBesideTheirControls() 'besideControl(h,collision,field).isEmpty()' returned FALSE. ("A display workspace preset already uses that name. Choose another name." is not shown under the control (y=566) inside the viewport (y=144-608); shown at y=850-873)
FAIL!  : LayoutPresetsUiTests::presetControlRowsShareTheColumnEdges() 'qAbs(fieldLeft-choiceLeft)<=1' returned FALSE. (width 560: name field left 263, preset choice left 32)
FAIL!  : LayoutPresetsUiTests::presetCommandShowsOnlyItsOwnWriteOutcome() '!h.window->property("splitMessage").toString().contains("center is reserved")' returned FALSE. (The center is reserved for image and grid content. Choose an edge for this pane.)
Totals: 6 passed, 3 failed, 1 skipped, 0 blacklisted, 38777ms
```

- M-R1: `src/app/monitor_customization.cpp:210` changed so the session note overwrites the "replaced" lead (`feedback=feedback.isEmpty()?replaced:feedback;`). `monitor_customization` result (Windows):

```
QINFO  : ...layoutPresetDuringPendingTransactionStaysCoherent() transaction queued first and lands first: it supersedes the preset: feedback "Pane layout changes made while saving were replaced by the saved monitor layout." frozen 1 preset 0
QINFO  : ... transaction queued first, preset lands first: transaction refused: feedback "The pane layout changed while saving. Retry with its current state." frozen 0 preset 1
QINFO  : ... preset applied first, transaction lands first: both apply in order: feedback "" frozen 1 preset 1
QINFO  : ... preset applied and lands first: transaction refused: feedback "The pane layout changed while saving. Retry with its current state." frozen 0 preset 1
PASS   : MonitorCustomizationTests::layoutPresetDuringPendingTransactionStaysCoherent()
FAIL!  : MonitorCustomizationTests::supersededFeedbackSurvivesNewPresentationSession() 'feedback.startsWith("Pane layout changes made while saving were replaced")&&feedback.contains("newer presentation session")' returned FALSE. (Preferences saved; the newer presentation session was left unchanged.)
Totals: 35 passed, 1 failed, 0 skipped, 0 blacklisted, 10149ms
```

The earlier review's M6 passed on Windows; this mutation now fails there. The coherence test passes under M-R1 by design (it proves all four orders run), as the handoff says.

## R1 test seam: `settings_detail::attemptPendingSettingsNow`

`src/app/settings_retry.hpp:22-24`, `src/app/settings_file.cpp:300-301, 341-344`. `git grep` shows callers only in `tests/monitor_customization_tests.cpp` (lines 108, 109, 180). `SettingsRetries::runNow()` is `timer.stop(); run();`, the same body the timer slot runs, and `run()` restarts the timer if entries remain, so a mistaken production call would only be an early retry attempt. The function is thread-checked like its siblings, lives in the internal header (not `include/glancelot`), and no shared contract changes. Harmless in production. Always compiled rather than test-only; the handoff already offers to gate it behind a define at the next freeze. Accepted.

## F054 part 2 (ui/Main.qml, catalog, coverage test)

- Non-string edits in `ui/Main.qml`: (1) `withShortcut(text, id)` (line 676) replaces `shortcutHint`. Output is the same as before: "text (binding)" or text alone; sort tip is `"%1; reverse with %2"` over the same pieces, settings tip `"%1. Find a command: %2"`. `git grep shortcutHint` finds no remaining caller. (2) `readonly property string orderName` on the sort button (line 1766) with `|| ""`; for an unknown `sortKey` the accessible name previously read "File order: undefined" and now reads "File order: ", which is a benign difference for a value the app never produces. Accessible name text for the four keys is unchanged. No other behaviour change found.
- Concatenation: every `+ "..."` that built translated text in the old lines was replaced by a `qsTr(...).arg(...)` pattern. Remaining `+` string joins in Main.qml are `split.fallback + "/" + ...` (a key, `Main.qml:1558`) and an icon path (`1673`). The two fallback texts "Showing the grid/photo" are whole sentences per branch, not assembled words.
- Accessibility names preserved: Photo canvas, state panel (title and detail joined by a translatable `"%1. %2"`), View, Zoom navigator, Resize grid and photo (text identical per orientation), File order, Folder tabs, "%1, selected", slideshow and chord announcements, "Zoom %1 percent. Show actual size", reference photo, locked-layout button. Texts match the pre-change strings.
- Catalog: `lupdate 6.8.3` (WSL, as the handoff used) plus `translations/pseudolocalize.py` on a copy of `translations/glancelot_en_XA.ts` regenerates it with no difference: "Found 582 source text(s) (0 new and 582 already existing)", "0 message(s) pseudo-localized". A script over the file found 582 messages (132 in context `Main`), 0 obsolete or vanished, 0 unfinished, and for every message the set of `%N`/`%L1`/`%n` markers equals that of its source. README count (582) matches.
- Coverage test: the two deferred lists are gone; `walkMainWindow` asserts the top bar and the status bar fully translated in photo and grid views (LTR dark 1180), and adds Main.qml/F049 translate checks and an RTL compact pass. Remaining plain text outside Main.qml is listed in `docs/verification/STACK-F054-COVERAGE/part2/coverage-report.txt` ("none" for named surfaces) and in the handoff's open questions (PhotoGrid, DeleteFlow, C++ strings); outside this packet's grant.
- Captures in `docs/verification/STACK-F054-COVERAGE/part2/` exist for LTR/RTL, dark/light, 1180/560, plain and expanded text.

## Test output (this worktree, Windows, offscreen, gate lock held)

```
Test project C:/src/Github/Glancelot/.worktrees/review-f054p2/build-review-f054p2
    Start 22: ui_settings
1/5 Test #22: ui_settings ......................   Passed    1.88 sec
    Start 42: monitor_customization
2/5 Test #42: monitor_customization ............   Passed    9.29 sec
    Start 43: monitor_customization_ui
3/5 Test #43: monitor_customization_ui .........   Passed   27.83 sec
    Start 44: translation_coverage_ui
4/5 Test #44: translation_coverage_ui ..........   Passed   24.11 sec
    Start 45: layout_presets_ui
5/5 Test #45: layout_presets_ui ................   Passed   21.27 sec

100% tests passed out of 5
Total Test time (real) =  84.64 sec
```

Command: `ctest --test-dir build-review-f054p2 -R "translation_coverage_ui|layout_presets|monitor_customization|ui_settings|settings"` inside `gate-lock.ps1 -Branch review-f054p2`, on the unmodified `98adda9` build. The full offscreen suite and the Docker gate were not run by this review; the authoritative gate decides. The handoff's note of one 300 s `pseudoLocaleRightToLeftDark` timeout during an overlapping evidence run did not recur here (24 s).

## Non-blocking findings

1. LOW, `ui/Main.qml:1072, 1443, 1458`: user data goes into the first `.arg()` and a later `.arg()` follows (`qsTr("%1, %2 by %3 pixels").arg(viewer.fileName).arg(w).arg(h)`, `qsTr("Move “%1” to the %2").arg(menuName(...)).arg(trashName)`). QString-style `arg` rescans the whole string, so a file name containing `%2` (for example `sale%2.jpg`) has that text replaced by the next argument in the accessible description and the delete menu label. The old concatenation did not do this. Cosmetic and rare; fix by passing the user-supplied value last where the placeholder order allows, or accept.
2. LOW, `ui/SettingsDialog.qml:56, 61, 609`: `presetRefusalShown` is never cleared (not on close, not on a later success). If a later pane-placement action produces exactly the same layout message as an earlier preset refusal, the general note would stay hidden. Only identical texts are affected; clear it on close or on the next successful save/apply.
3. LOW, coverage depth: only the top bar and status bar of Main.qml are asserted visible-and-translated; roughly 100 other new `qsTr` strings (menus, notices, splitter, delete flow) are protected by the catalog regeneration and the `translate()` spot checks, not by a walk. Acceptable for this packet; worth a notice-state walk in the follow-up that wraps DeleteFlow and PhotoGrid.
4. LOW, seam: `attemptPendingSettingsNow` is compiled into production builds (accepted above); consider a test define at the next contract freeze.
