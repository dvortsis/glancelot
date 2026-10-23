# STACK-F049-PRESETS re-review (fresh-context Claude reviewer, round 1 fixes)

**Verdict: APPROVED. No blocking items.** All five stage-1 findings and the stage-1 design items are fixed at `b7add92`. The targeted Windows tests pass, including 10 of 10 repeats of each timer-ordered suite. Each changed C++ test except one fails when its fix is reverted (mutation runs below); the UI-test changes rest on the implementer's Linux fail-before run. The exception is the finding-4 message-ordering test: on Windows it takes the other order every time, so only the Linux run (and the Docker gate) exercises that path. That item, two design findings and the integration notes are for the lead to schedule or waive. None of them breaks the frozen contract.

- **Reviewed:** `origin/claude/stack-f049-presets` at `b7add92` ("docs(handoff): STACK-F049-PRESETS review round 1 fixes, evidence and design note"). Full packet: `git diff afeac1a b7add92`. Round 1 only: `git diff 380cf7b b7add92` (code commit `0d5afb6`, after the `origin/main` merge `380cf7b`).
- **Reviewer:** a fresh-context Claude Code (Opus) subagent on the lead's Windows PC. It did not implement this packet and did no earlier review of it. The ui-design-skills (application-interface-design, layout-grids, layout-spacing, typography-ui, design-review) were loaded from `~/.agents/skills/ui-design-skills` before the design review.
- **Contract:** the first frozen resolution in `docs/handoff/LEAD-WRAPUP-2026-10-22.md`. Shared `PaneLayout` and `ViewerController` headers keep main's API. F049 presets obey the canonical lock and frozen-pane rules, keep per-slot placement isolation and preserve unknown or newer preference data. Named controller layouts and monitor workspace presets have distinct names and scopes in the UI.
- **Lead decisions applied:** keep the `ui/Main.qml` navigator commit `9c28226` (minimal Main.qml edits). The lead re-lands the provisional CMake commit `aa2a9a4`. The MonitorSettings heading and scope note (old lines ~185 and ~200) are owned by F049.
- **Platform:** Windows 11 Pro 10.0.26200, MSVC Release, Qt 6.8.3 (`.tools/Qt/6.8.3/msvc2022_64`), offscreen platform with the software Quick backend (as the CMake test environment sets). No physical multi-monitor run.
- **Worktree deviation:** `.worktrees/review-f049` already exists. It is the Gemini lane's earlier review worktree (`agent/review-f049`, `cdb214c`, with local changes), and AGENTS.md forbids editing another agent's worktree. I built in `.worktrees/claude-review-f049` (detached at `b7add92`) with build directory `build-review-f049` inside it. This document was written on `.worktrees/review-f049-doc`, branch `claude/review-stack-f049`, as the lead's brief names it. The handoff names the review branch `claude/review-stack-f049-presets`; the lead's name was used.

## Stage-1 review summary (round 0, `4770e99`)

The stage-1 review was not committed as a document. Its findings are recorded here so this file carries both rounds. The contract core held.

| # | Stage-1 finding |
| --- | --- |
| 1 | The refusal test captured `layoutBefore` but never compared it (`Q_UNUSED(layoutBefore)`), so a refused apply that wrote to disk would pass. |
| 2 | Name-collision checks fired even for unchanged names. "Update snapshot" broke on legacy display presets named Review, Default or Browse, and on legacy pane layout presets that shared a display preset's name. |
| 3 | The copy "shared by all windows" was misleading: presets are saved for every window but apply to this window's docked panes. |
| 4 | `layout-preset-next` did not surface a reverted apply in the main window, and `monitor_customization.cpp` ~197 overwrote the "superseded" message with the presentation-session note. |
| 5 | Cross-scope name checks missed display preset saves still queued behind a busy file. |
| Design | The description column wrapped at about 20 characters (stack the controls under the labels). The "Pane layout" row label was redundant. The freeze reason did not name the pane. The lock and freeze switches lacked their own disabled reason. "Reset workspace layout" sat in the wrong group. |

## Round-1 disposition

| Item | Status at `b7add92` | Evidence |
| --- | --- | --- |
| 1 refusal not compared | **Fixed** | `presetsObeyLockAndFreezeAuthority` reads `layouts` before each refused apply (locked, then frozen) and `QCOMPARE`s it with disk afterwards. A disk write injected before either refusal makes it fail (mutations M1, M2). |
| 2 unchanged names refused | **Fixed** | `PaneLayout::savePreset` checks display names only when `layoutPresets` lacks that exact key. `MonitorCustomization::savePreset`/`renamePreset` use `layoutPresetNameTaken(id,name)`, which skips a record keeping its stored name. The write-time check skips `name == priorName`. Each guard has a failing mutation (M3, M4, M5). |
| 3 scope copy | **Fixed** | Settings: "Saved for every window; applies to this window's docked panes. Never changes lock, freeze or displays." MonitorSettings: "Display workspace presets for this window" and "Saved for this window only. … pane layout presets in Settings are saved for every window." Asserted verbatim in `settingsPresetBlockReadsAtEveryWidth`. |
| 4a command surfaces outcome | **Fixed** | `layout-preset-next` refuses while `monitor.saving` with "Display preferences are saving. Apply the preset when they finish.", so a transaction can no longer be pending when the command applies. A deferred preset write reports its end through a one-shot `Connections` on `paneLayout`. `presetCommandWaitsForDisplaySaveAndReportsOutcome` passes on Windows. |
| 4b superseded message overwritten | **Fixed in code; Windows test does not reach it** | The "replaced" text is composed last and leads. On Windows `supersededFeedbackSurvivesNewPresentationSession` took "preset landed first" in 12 of 12 direct runs and in the ctest run, so the mutation that restores the overwrite still passes there (M6). The implementer's Linux run took the superseded order and failed before the fix. See R1. |
| 5 queued saves | **Fixed** | In-process: `PaneLayout::savePreset` overlays the coordinator's queued edits (`overlayPendingSettings`) through the `_glancelotDisplayPresetQueue` dynamic property. Cross-process: the queued coordinator write re-reads `layoutPresets` and refuses a new or changed name. `layoutPresetNameChecksIncludeQueuedSaves` fails without the overlay (M3). |
| Design items | **Fixed** | Captures: controls stacked under the text at a 70-character measure; "Pane layout" row label gone; "Frozen: Folder tree. Unfreeze to apply a pane layout preset."; lock and freeze switches and Reset show "Unavailable while display preferences are saving." (the freeze switch's reason is below the fold in the saving capture and is test-verified only); Reset sits after the lock with a note that it clears the lock and every freeze. |

## Code review notes (round 1)

- **Overlay side effects are safe.** `overlayPendingSettings` runs the coordinator's queued edit lambda on a copy, which writes the shared `applied`, `refused`, `written` and `writtenDock` pointers. The lambda now clears `*applied` and `*refused` at its start, and every real attempt recomputes all four before its completion runs, synchronously, on the same thread. An overlay cannot fall between a real write and its completion.
- **The dynamic property is cleared** on the coordinator's `destroyed`, with the layout as context. There is one `PaneLayout` per process (`app/main.cpp:214`), so "queued in this process" is accurate. Another process's queued save is caught by the write-time check, so no collision reaches disk in either order.
- **No stuck "Saving…" on the superseded path.** The coordinator cancels the layout's pending writes without calling their completion, but `restoreSnapshot` clears `feedback_` and the coordinator emits `changed` after unblocking. Settings' note clears, and the main window's one-shot `Connections` disarms silently.
- **A preset applied before a transaction starts is not reverted.** The coordinator captures the runtime dock (with the preset) as `currentDock` and `expectedLayout` from disk. If the pane write lands first, the transaction is refused; if the transaction lands first, it persists the preset through the dock and `superseded` is false.
- **No shared header changed** (`git diff afeac1a b7add92 -- include/` is empty).

## Findings

| # | Severity | Finding | Suggested fix |
| --- | --- | --- | --- |
| R1 | MEDIUM (non-blocking) | `supersededFeedbackSurvivesNewPresentationSession` and `layoutPresetDuringPendingTransactionStaysCoherent` accept whichever order the two 25 ms retry timers produce. On Windows the superseded order never ran: 0 of 26 runs where it can occur (13 of `layoutPresetDuringPendingTransactionStaysCoherent` order 0 and 13 of `supersededFeedbackSurvivesNewPresentationSession`; order 1 starts the transaction after the preset, so it cannot supersede by construction). So the authoritative Windows gate cannot catch a regression of finding 4b; only the Linux Docker gate can, if Linux keeps the order the implementer saw. | Force the superseded order, for example by holding the layout's queued `_preset` write (or asserting the coordinator's completion runs first) instead of relying on timer order, and fail when the intended order did not run. |
| R2 | MEDIUM, design (non-blocking) | Save and apply refusals, including this packet's new name-collision refusals, appear only in the general `paneLayout.feedback` note at the end of "Pane placement", far below the fold at 560 px. Clicking Save then shows nothing nearby (application-interface-design: feedback near the action). The implementer listed it as an open item. | Show the refusal in a `Note` directly under the "Save pane layout as" row (and the apply refusal in the existing `layoutPresetBlocked` slot). |
| R3 | LOW, design | Two right edges in one page. The preset row and the "Save pane layout as" row are capped at `measure`, ending near x=862 at 1180 px (457 at 560 px), while the lock switch and the "Pane placement" controls end at 954 (516). The save field's left edge (609) aligns with nothing above it. The measure rule is for prose, not control rows (layout-grids: no near-miss alignment). | Cap only the `Note`s at `measure`; let the control rows fill the column, or stack "Save pane layout as" like the preset row so both control rows share the 438 px left edge. |
| R4 | LOW | `presetSavingFeedback` is armed with any non-empty feedback after a successful apply, including an immediate "a newer saved layout was preserved". The next different message, possibly unrelated and much later, then shows as the split message. | Arm only when `feedback` is the saving text, or disarm on a timer. |
| R5 | LOW, polish | `paneNames` in `SettingsDialog.qml` repeats the pane names that `paneChoice.model` lists unwrapped. The gap under the Reset note (~15 px) differs from the gap under the preset row (~8 px). No capture shows Apply enabled, so its primary treatment is unverified. | Derive `paneChoice.model` from `paneNames`; use one gap token; add an enabled-Apply capture. |

Pre-existing, not this packet: in "Workspace layout", "Displays and workspaces…" is centred while "Arrange panes" is left-aligned.

## Design review (captures in `docs/verification/STACK-F049-PRESETS/`)

Reviewed `settings-presets-{dark,light}-{560,1180}.png` and `settings-presets-saving-dark-1180.png` against application-interface-design, layout-grids, layout-spacing, typography-ui and design-review.

- **Blocking:** none.
- **Important:** R2 (refusal feedback far from Save).
- **Polish:** R3 (two right edges), R5.
- **Strengths:**
  - The block reads as three clear groups: "Workspace layout" (lock and the Reset that also clears it), "Pane layout presets" and "Pane placement".
  - Every disabled control explains itself in visible muted text, in place of its description, so nothing jumps. The same text reaches assistive technology through `Accessible.description`.
  - The scope sentence breaks after 68 characters at both widths and the measure follows text scaling (`FontMetrics` on `Theme.textSmall`). No horizontal overflow at 560 px. Dark and light themes use existing tokens only.
- **Highest-leverage change:** R2, the refusal note beside Save.

## Windows test runs

Build: `& C:\src\Github\Glancelot\scripts\build-slot.ps1 -Branch review/f049 -Run { …; & .\scripts\build.ps1 -BuildDir build-review-f049 }`, 571/571, exit 0.

Targeted, under `gate-lock.ps1 -Branch review/f049`:

```
> ctest --test-dir build-review-f049 -R "pane_layout|monitor_customization|layout_presets_ui|ui_settings|input_ui" --output-on-failure
1/6 Test #10: pane_layout ......................   Passed    1.79 sec
2/6 Test #13: input_ui .........................   Passed   80.44 sec
3/6 Test #22: ui_settings ......................   Passed    1.25 sec
4/6 Test #42: monitor_customization ............   Passed    9.20 sec
5/6 Test #43: monitor_customization_ui .........   Passed   28.40 sec
6/6 Test #44: layout_presets_ui ................   Passed   16.31 sec
100% tests passed out of 6
Total Test time (real) = 137.69 sec

> ctest --test-dir build-review-f049 -R "^(monitor_customization|layout_presets_ui)$" --repeat until-fail:10 --output-on-failure
    Test #42: monitor_customization ............   Passed    9.64 sec   (runs 1-10: 8.55-9.64 sec, all Passed)
1/2 Test #42: monitor_customization ............   Passed    8.65 sec
    Test #44: layout_presets_ui ................   Passed   11.37 sec   (runs 1-10: 11.03-11.71 sec, all Passed)
2/2 Test #44: layout_presets_ui ................   Passed   11.67 sec
100% tests passed out of 2
Total Test time (real) = 201.92 sec
```

QTest results files from the runs (`build-review-f049/*-results.txt`):

```
pane_layout:            Totals: 18 passed, 0 failed  (presetsObeyLockAndFreezeAuthority, legacyCollidingPresetNameStaysUpdatable PASS)
monitor_customization:  Totals: 36 passed, 0 failed  (legacyCollidingDisplayPresetNameStaysUpdatable, layoutPresetNameChecksIncludeQueuedSaves,
                                                      supersededFeedbackSurvivesNewPresentationSession PASS)
layout_presets_ui:      Totals: 6 passed, 0 failed, 1 skipped  (settingsPresetEvidence skipped: no GLANCELOT_F049_UI_EVIDENCE)
input_ui:               Totals: 32 passed, 0 failed, 1 skipped  (layoutPresetsFromCommandAndSettings PASS)
```

`input_ui`'s Linux baseline failure (`treeRowAndPathBarMenusOpenAndMatch`) does not occur on Windows.

Order taken on Windows by the order-dependent cases (ctest run, then 12 direct runs of both functions):

```
QINFO : layoutPresetDuringPendingTransactionStaysCoherent() order 0: preset landed first; transaction refused   (13/13)
QINFO : layoutPresetDuringPendingTransactionStaysCoherent() order 1: both applied in order                     (13/13)
QINFO : supersededFeedbackSurvivesNewPresentationSession() preset landed first: The pane layout changed while saving. Retry with its current state.   (13/13)
```

The "transaction superseded the preset" order never ran on Windows (R1).

## Mutation runs (fail without the fix)

Each mutation was applied to `src/` in the detached review tree, the two affected test targets were rebuilt in `build-review-f049` (MSVC, under `build-slot.ps1`), and the named function was run directly (`QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`). `git checkout -- src` restored the tree after each round; afterwards `git status --short` was empty, the targets were rebuilt, and `pane_layout_tests` (18 passed) and `monitor_customization_tests` (36 passed) passed in full again.

| # | Fix reverted (mutation) | Test | Result |
| --- | --- | --- | --- |
| M1 | Finding 1: `persistWhole()` before the lock refusal in `applyPreset` | `presetsObeyLockAndFreezeAuthority` | **FAIL** `Compared values are not the same`, `pane_layout_tests.cpp(295)` |
| M2 | Finding 1: `persistWhole()` before the freeze refusal | `presetsObeyLockAndFreezeAuthority` | **FAIL** `Compared values are not the same`, `pane_layout_tests.cpp(299)` |
| M3 | Finding 2: `PaneLayout::savePreset` checks display names even for an existing key (`if(true)`) | `legacyCollidingPresetNameStaysUpdatable` | **FAIL** `'model.savePreset("Studio")' returned FALSE. (A display workspace preset already uses that name. Choose another name.)`, line 338 |
| M3 | Finding 5: no `overlayPendingSettings` before the display-name check (same build) | `layoutPresetNameChecksIncludeQueuedSaves` | **FAIL** `'!h.panes.savePreset("queued")' returned FALSE.`, `monitor_customization_tests.cpp(149)` |
| M4 | Finding 2: `layoutPresetNameTaken` returns `layoutPresetNamed(name)` | `legacyCollidingDisplayPresetNameStaysUpdatable` | **FAIL** `'h.customization.savePreset(builtInId,"Review","presentation")' returned FALSE. (A pane layout preset already uses that name. Choose another name.)`, line 131 |
| M5 | Finding 2: write-time check without `name!=priorName` | `legacyCollidingDisplayPresetNameStaysUpdatable` | **FAIL** at line 131 with the same message: the unchanged legacy record "studio" in the same list is refused when "Review" is updated |
| M6 | Finding 4b: the old `if(superseded)feedback=…` before the session branch, new composition disabled (same build as M2 and M4) | `supersededFeedbackSurvivesNewPresentationSession` | **PASS** (not caught): `preset landed first: The pane layout changed while saving.` The superseded branch never runs on Windows (R1). The implementer's Linux run took that branch and failed before the fix. |

```
> pane_layout_tests.exe presetsObeyLockAndFreezeAuthority        # M1
FAIL!  : PaneLayoutTests::presetsObeyLockAndFreezeAuthority() Compared values are not the same
C:\src\Github\Glancelot\.worktrees\claude-review-f049\tests\pane_layout_tests.cpp(295) : failure location
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 74ms
> pane_layout_tests.exe legacyCollidingPresetNameStaysUpdatable  # M3
FAIL!  : PaneLayoutTests::legacyCollidingPresetNameStaysUpdatable() 'model.savePreset("Studio")' returned FALSE. (A display workspace preset already uses that name. Choose another name.)
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 41ms
> monitor_customization_tests.exe layoutPresetNameChecksIncludeQueuedSaves  # M3
FAIL!  : MonitorCustomizationTests::layoutPresetNameChecksIncludeQueuedSaves() '!h.panes.savePreset("queued")' returned FALSE. ()
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 35ms
> pane_layout_tests.exe presetsObeyLockAndFreezeAuthority        # M2
FAIL!  : PaneLayoutTests::presetsObeyLockAndFreezeAuthority() Compared values are not the same
C:\src\Github\Glancelot\.worktrees\claude-review-f049\tests\pane_layout_tests.cpp(299) : failure location
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 82ms
> monitor_customization_tests.exe legacyCollidingDisplayPresetNameStaysUpdatable  # M4
FAIL!  : MonitorCustomizationTests::legacyCollidingDisplayPresetNameStaysUpdatable() 'h.customization.savePreset(builtInId,"Review","presentation")' returned FALSE. (A pane layout preset already uses that name. Choose another name.)
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 76ms
> monitor_customization_tests.exe supersededFeedbackSurvivesNewPresentationSession  # M6
QINFO  : MonitorCustomizationTests::supersededFeedbackSurvivesNewPresentationSession() preset landed first: The pane layout changed while saving. Retry with its current state.
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 101ms
> monitor_customization_tests.exe legacyCollidingDisplayPresetNameStaysUpdatable  # M5
FAIL!  : MonitorCustomizationTests::legacyCollidingDisplayPresetNameStaysUpdatable() 'h.customization.savePreset(builtInId,"Review","presentation")' returned FALSE. (A pane layout preset already uses that name. Choose another name.)
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 121ms
```

The UI-test changes (finding 3, finding 4a and the design items) were not mutated here; the implementer's fail-before run with `src/` and `ui/` at `380cf7b` shows each new UI assertion failing (handoff, "Fail before, pass after").

## Integration notes for the lead

- `git merge-tree --write-tree --name-only origin/main origin/claude/stack-f049-presets` is clean: tree `513e4590862684e06fbfa7adddb538f230e12c4a`, no conflicted files. The branch already merged `origin/main` at `f325ef6`; main (`c903e6f`) is ahead by EN-02, EN-03, EN-06, EN-08 and the lead tooling.
- **EN-03 labels.** Re-land `aa2a9a4` or move `layout_presets_ui` into its own lead-provided `cmake/<Feature>Tests.cmake`, then add `glancelot_test_labels(layout_presets_ui LABELS ui process)` (same as `monitor_customization_ui`: shipped `Main.qml`, decoder dependency). Unlabelled tests are auto-labelled `fast`, which this 11 s UI suite is not. `monitor_customization` and `pane_layout` are unlabelled on main and so `fast`; `monitor_customization` now holds the settings file busy for up to 4–5 s in several cases, so the lead should decide whether it stays `fast`.
- **EN-06 Main.qml routing.** The packet's Main.qml edits merge cleanly: the `presetSavingFeedback` property (merged line 25), the `Connections` beside `splitMessageTimer` (~152), the `layout-preset-next` block (~739) and the navigator `parent`/`visible` (~1303–1306). `command_routing_tests` does not trigger `layout-preset-next`. Run `command_routing`, `input_ui` (`layoutPresetsFromCommandAndSettings`) and `layout_presets_ui` on the merged tree, because the command now refuses while a display save is pending.
- **Main.qml grant.** Beyond the navigator commit `9c28226`, round 1 adds about 15 lines to `ui/Main.qml`: the `presetSavingFeedback` property, the one-shot `Connections` beside `splitMessageTimer`, and the wait in the `// F049` `layout-preset-next` block. Finding 4a needs them. Confirm they fall within this cycle's Main.qml grant.
- **F054.** Expect a textual conflict on the two MonitorSettings lines (`monitorPresetHeading`, `monitorPresetScope`) with `claude/stack-f054-coverage`; resolve to this packet's text. The `"Layout: "` split message stays unwrapped for F054.
- **Contract follow-up.** The `_glancelotDisplayPresetQueue` dynamic property couples `MonitorCustomization` and `PaneLayout` without a header change. Formalize it at the next contract freeze.
