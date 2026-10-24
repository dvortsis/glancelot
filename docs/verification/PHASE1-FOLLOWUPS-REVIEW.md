# PHASE1-FOLLOWUPS review

**Verdict: APPROVED**

Reviewer: fresh-context local reviewer (implemented none of this). Pinned SHAs: base `e967a0c` (origin/main), tip `5864a6a` (origin/claude/phase1-followups), reviewed with `git diff e967a0c 5864a6a` in `.worktrees/review-phase1-followups`, build dir `build-review-phase1-followups` (Release, MSVC 2022, Qt 6.8.3, built through `build-slot.ps1`). No blocking findings. Three non-blocking notes below.

## Grant check

`git diff --name-only e967a0c 5864a6a`: `include/glancelot/viewer_controller.hpp` (one comment line, the lead's grant; diff is comment-only, and the F040 comment at lines 74-76 was already correct on main), `src/app/viewer_controller.cpp`, `src/app/display_manager.cpp`, `ui/Main.qml`, `ui/SettingsDialog.qml`, `tests/reference_pin_tests.cpp`, `tests/tabs_recovery_tests.cpp`, `tests/layout_presets_ui_tests.cpp`, `translations/glancelot_en_XA.ts`, `translations/README.md` (lead's round 2 instruction), `docs/handoff/PHASE1-FOLLOWUPS-HANDOFF.md`, `docs/verification/PHASE1-FOLLOWUPS/*`. Nothing under `cmake/`, root `CMakeLists.txt`, `docs/WORK-QUEUE.yaml`, `AGENTS.md`, `.github/`. `display_manager.cpp` is outside the brief's list but is the file item 5 names (justified in the handoff). Edits to `Main.qml` and `viewer_controller.cpp` are confined to the regions the brief names plus the placeholder hint.

## Item by item

1. **reference_pin `step()` presses "next"** (`tests/reference_pin_tests.cpp`): correct. It guards that the caller is at photo n-1 and fails with a warning instead of silently changing meaning; `photo()` stays for the one test that needs it. `reference_pin` and `reference_pin_native` both pass here (below). No stall seen in two runs on this PC (handoff reports 5 of 5 with `--repeat until-fail:5`; not re-run by me). No fail-before is possible for a workaround removal; the handoff says so honestly.

2. **Lone unavailable tab shows the placeholder.** Correct.
   - `tabs()` (`src/app/viewer_controller.cpp:1554-1566`) returns one row built from `activeLocation()` only while `tabUnavailable` and fewer than two tabs. `activeLocation()` is `pendingFolder/pendingImage` while `tabSettling`, and `tabUnavailable` is only ever true inside a settle (set in `showUnavailable()`, cleared by `settleOn`, `clearUnavailable`), so the row is the tab's stored location.
   - Consumers checked: tab strip `Main.qml:1836` (`tabs.length > 1`) and top-bar title `Main.qml:1879` (`< 2`) are unchanged by a one-row list; `activeTab()` already returns 0 below two tabs, so `rows[viewer.activeTab]` in the placeholder binding is valid; the recovery checkpoint (`viewer_controller.cpp:249`) and the commands (`tab-close`, `tab-next`, `tab-previous`, `closeTab`, `activateTab`) read `tabList`, not `tabs()`; `tests/window_slot_tests.cpp` lines 991-1009 (one tab: `tabs().isEmpty()`) and `layout_presets_ui` pass. `newTab()` from the lone unavailable tab produces two rows with the active one unavailable, same as the multi-tab case (unchanged behaviour).
   - `tabsChanged` on exit: `clearUnavailable()` (`:219`) emits when the flag drops. Every site that cleared it silently now goes through it (`noteUserMove` `:217`, decode settle `:501`, folder visit `:1101`); `settleOn` still resets the flag without emitting, but all its callers (`activateTab`, `closeTab`, `retryTarget`) emit `tabsChanged` immediately after, and `showUnavailable()` emits after setting it. No stale-row path found. The `stateChanged -> tabsChanged` bridge (`:489`) still only fires above one tab, which is fine because the single row only changes with the flag.
   - Hint text: one-tab variant drops "Your other tabs keep their places"; correct and the test asserts it.

3. **`fillText`** (`ui/Main.qml:680`). Correct for the actual patterns. The callback form of `String.replace` inserts the returned text literally, so `$`, `$&`, `$1` in a value are safe; values are never scanned, so `%2`, `%1`, `%3` in a name stay as typed (the three test rows). A marker with no value is left as written; a marker's value maps by number, so a translation that drops or reorders markers fills correctly (the old chained `.arg()` filled by order). Ordinary names give identical text (the "bad%2.png" and "sale%2.png" tests, plus the unchanged output for plain names). All five `%N` sites that put user text first are converted (`Main.qml:776, 1082, 1154, 1504, 1519`). I grepped `ui/*.qml` for any other `.arg(user-text).arg(...)` chain, single and multi-line, and found none (remaining chains put numbers, command labels or the trash name first). Limits, none reachable today: `%10` and above and `%L1` are not understood by the regex (Qt `arg()` reads two digits and `L`); no converted pattern uses them.

4. **`presetRefusalShown` cleared** (`ui/SettingsDialog.qml:57-80`). Correct. `PaneLayout` emits `changed()` synchronously inside `savePreset`/`applyPreset` for every refusal (`pane_layout.cpp:302-359`), so the `presetRequestRunning` guard covers the dialog's own Save and Apply, and the refusal is recorded after the guard drops. Any other layout change (including the same text arriving again) clears it. The existing `presetRefusalsShowBesideTheirControls` still passes. The handoff's trade-off (not cleared on dismiss) is sound.

5. **`pinCurrentImage` stale capture status** (`src/app/display_manager.cpp:683-686`). Correct: early return, no state change, plain status string like its siblings; `pinWithAStaleCaptureSaysSo` asserts the status, pin path, mode, no signals, retained bytes and selection. See note N1 on wording.

6. **Header comments.** `viewer_controller.hpp:46` now describes the one-row exception accurately; F040 comment already fine.

7. **Translations.** `translations/glancelot_en_XA.ts`: 589 messages (checked by parsing), 0 obsolete or vanished, 0 unfinished, every translation wrapped in the `[...]` pseudo markers; exactly one new message (the one-tab hint) in the same context as its two-tab sibling, which is still present; `translations/README.md` count updated. `translation_coverage_ui` passes.

## Fail-before spot checks (done by me)

Product hunks reverted locally in the review worktree (QML is loaded from source by the tests, same binary), then restored with `git checkout`; worktree clean afterwards.

- Reverted `Main.qml:1082` to the chained `.arg()` and the `Connections` clear in `SettingsDialog.qml` to a no-op, ran `layout_presets_ui`:
  ```
  FAIL!  : identicalLaterLayoutMessageIsNotHiddenByAnEarlierRefusal() 'note->isVisible()' returned FALSE. (the identical later message is still hidden by the earlier refusal)
  FAIL!  : fileNamesWithPlaceholderMarkersAreShownAsTyped(%2) Actual "sale32.png, 32 by 24 pixels"  Expected "sale%2.png, 32 by 24 pixels"
  FAIL!  : fileNamesWithPlaceholderMarkersAreShownAsTyped(%1) Actual "a32b.png, 24 by %3 pixels"   Expected "a%1b.png, 32 by 24 pixels"
  FAIL!  : fileNamesWithPlaceholderMarkersAreShownAsTyped(%3) Actual "10024.png, 32 by 24 pixels"  Expected "100%3.png, 32 by 24 pixels"
  Totals: 11 passed, 4 failed, 1 skipped
  ```
  Both claims reproduce, with the same failures the handoff records. Not reproduced by me: the C++ fail-befores (`tabs()` and `pinWithAStaleCaptureSaysSo`; they need a rebuild), which the handoff's `fail-before/tabs.txt` and `pin.txt` document, and the "tabsChanged must announce" assertion, which the handoff itself says was not independently shown to fail (note N2).

## Targeted ctest (this PC, gate lock held, tip 5864a6a)

```
ctest -R "reference_pin|layout_presets_ui|tabs_recovery|monitor_output|translation_coverage_ui|viewer_acceptance|input_ui"
1/8 Test  #7: viewer_acceptance ................   Passed   13.44 sec
2/8 Test #13: input_ui .........................   Passed   91.57 sec
3/8 Test #39: monitor_output ...................   Passed    2.55 sec
4/8 Test #44: tabs_recovery ....................   Passed   64.56 sec
5/8 Test #47: translation_coverage_ui ..........   Passed
6/8 Test #48: layout_presets_ui ................   Passed   31.38 sec
7/8 Test #50: reference_pin ....................   Passed   31.73 sec
8/8 Test #51: reference_pin_native .............   Passed   32.75 sec
100% tests passed, 0 tests failed out of 8       Total Test time (real) = 290.48 sec
```

Not run by me: the full 51-test suite (the handoff records 51 of 51 twice, `after-fix/full-suite*-ctest-summary.txt`; the authoritative gate decides) and Docker/Linux.

## Placeholder captures (docs/verification/PHASE1-FOLLOWUPS/last-tab-unavailable-1180.png and -560.png)

The ui-design-skills (application-interface-design and the others) are not installed in this session, so this is a plain visual judgement, not a skills-based review. The state follows the existing empty-state pattern: one muted folder glyph, a heading naming the folder ("Holiday trip is unavailable"), the elided path, one primary Retry action, one guidance line (now true for one tab). At 560 px the glyph, path and hint remain readable and nothing clips; the status bar repeats the path as before (existing behaviour). In the 560 capture the top bar shows a window-control glyph over "Settings" and a truncated "No ph... open" title; the top bar is untouched by this packet, so this is existing (N3), not a regression.

## Notes (non-blocking)

- **N1 (low), `src/app/display_manager.cpp:684`.** The new text "The photo changed while it was being pinned ... Pin again to use the current photo." fits the case the test builds (a pin made at a newer generation than the capture). In normal use a withheld current photo always has a generation at or above the pin, so the branch is a guard; if it were ever reached by a persistent generation gap, pressing Pin again would capture the same frame and show the same message, so the advice would not help. Also untranslated, like its sibling statuses (handoff Decisions). Worth rewording only if the branch proves reachable.
- **N2 (low), tests.** `navigatingAwayFromTheLastUnavailableTabEmptiesTheList` asserts `changed.count() > 0` after `open()`, but other emits may satisfy it; the exit-announcement is not independently pinned to `clearUnavailable()`. The code review above found every exit path routed through it, so this is a test-strength note, not a defect.
- **N3 (info).** 560 px top bar overlap (above) predates this packet; it belongs to a separate top-bar packet. `fillText` does not support `%10+` or `%L1`; document that if a future pattern needs them.
