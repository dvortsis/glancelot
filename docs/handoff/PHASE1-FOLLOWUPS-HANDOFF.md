# PHASE1-FOLLOWUPS handoff

Branch `claude/phase1-followups`, worktree `.worktrees/claude-phase1-followups`, build dir `build-claude-phase1-followups`.
Base: `origin/main` e967a0c (not stacked). Tip SHA: see the final line of the implementer's report (this file is part of the tip commit).
Small non-blocking fixes from the Phase 1 reviews (docs/handoff/LEAD-PAUSE-2026-10-23.md section 8). Evidence: docs/verification/PHASE1-FOLLOWUPS/.

## What each item did

| # | Item | Result |
| --- | --- | --- |
| 1 | `reference_pin` `step()` presses "next" | `tests/reference_pin_tests.cpp`: `step(n)` now does `trigger("next")` and fails fast with a clear warning if it is not called from photo n-1. `photo()` stays for `pinWhileDecodeInFlightKeepsOldPin`. `ctest -R "^reference_pin$" --repeat until-fail:5`: 5 of 5 passed; `reference_pin_native` passed once. No stall. |
| 2 | One remaining unavailable tab shows the placeholder | `ViewerController::tabs()` returns the one active row (`unavailable: true`) while a lone tab is unavailable, empty otherwise, so the tab strip (`tabs.length > 1`) and the top-bar title (`< 2`) are unchanged. `Main.qml` placeholder reads the row at `rows.length > 0`. New `clearUnavailable()` announces `tabsChanged` whenever the state ends (user navigation, settle, folder visit), so the row and placeholder cannot go stale. The placeholder hint no longer says "Your other tabs keep their places" when there is one tab (new qsTr string, en_XA regenerated). Rendered at 1180 and 560 widths. |
| 3 | `%2` in a file name rescanned by a later `.arg()` | `Main.qml` `fillText(pattern, values)`: one regex pass over `%1..%9`, values never searched. QML's `String.arg()` takes one value, so the brief's `.arg(a, b)` form is not available. Applied at the three sites: canvas `Accessible.description`, the canvas menu "Move “%1” to the %2" row, the grid menu row. Source strings unchanged, so the catalog is unchanged. |
| 4 | `presetRefusalShown` never cleared | `SettingsDialog.qml`: a `Connections` on `paneLayout.changed` clears it for any layout change that is not this dialog's own Save or Apply (`presetRequestRunning` guards those). It is not cleared when the refusal is dismissed, because the existing test (`presetRefusalsShowBesideTheirControls`) requires the message to disappear after editing the name, and clearing then would reveal the stale feedback in the general note. |
| 5 | Stale capture in `pinCurrentImage()` | `display_manager.cpp` now sets "The photo changed while it was being pinned. The output keeps its pinned photo. Pin again to use the current photo." Plain status string like its siblings (see Decisions). |
| 6 | Header comment | Already done on main: the F040 comment in `include/glancelot/viewer_controller.hpp` (lines 74-76) reads "held in the display manager's retained-image budget, which may refuse it or release it for a newer frame" (commit b610348). No edit made. `git diff e967a0c -- include` is empty. |
| 7 | Translations | `lupdate` 6.8.3 (WSL) plus `pseudolocalize.py`: 1 new message (the one-tab hint), 589 total. `translation_coverage_ui` passes. |

## Pre-handoff checklist

### 1. Fail-before evidence

Tests were written first and built against base e967a0c (output in docs/verification/PHASE1-FOLLOWUPS/fail-before/); the same tests then pass with the fixes (after-fix/).

| Test | Base result |
| --- | --- |
| `tabs_recovery`: `lastTabUnavailableAfterClosingTheActiveTabKeepsItsRow` | FAIL "expected the one unavailable tab as a row, got 0 rows" |
| `tabs_recovery`: `lastTabUnavailableAfterClosingTheOtherTabKeepsItsRow` | FAIL, same message |
| `tabs_recovery`: `retryFromTheLastUnavailableTabOpensItAndEmptiesTheList` | FAIL, same message (precondition) |
| `tabs_recovery`: `navigatingAwayFromTheLastUnavailableTabEmptiesTheList` | FAIL, same message (precondition) |
| `layout_presets_ui`: `lastUnavailableTabShowsThePlaceholder` (1180 and 560) | FAIL `h.viewer.tabs().size()` 0, expected 1 |
| `layout_presets_ui`: `fileNamesWithPlaceholderMarkersAreShownAsTyped` (`%2`, `%1`, `%3`) | FAIL, for example actual "sale32.png, 32 by 24 pixels", expected "sale%2.png, 32 by 24 pixels" |
| `layout_presets_ui`: `identicalLaterLayoutMessageIsNotHiddenByAnEarlierRefusal` | FAIL "the identical later message is still hidden by the earlier refusal" |
| `reference_pin`: `pinWithAStaleCaptureSaysSo` | FAIL, actual "Output keeps its pinned photo: the newer photo doesn't fit in image memory." |

Notes. (a) Item 1 has no fail-before: it removes a workaround and cannot fail on base; its check is 5 of 5 passes with `next()`. If the stall returns, `step()` times out and says so; do not revert to opening files. (b) For item 3 the base run fails on the first (description) assertion, so the two menu-row assertions in the same test were not reached on base; they pass with the fix and use the same `fillText` path. (c) The "tabsChanged must announce" assertion in `navigatingAwayFrom...` is reached on base only after its precondition, which fails there, so it was not independently shown to fail.

### 2. Grant check

`git diff --name-only e967a0c..<tip>`:

- `src/app/viewer_controller.cpp` (tab-state code, item 2), `src/app/display_manager.cpp` (item 5)
- `ui/Main.qml` (items 2, 3, hint), `ui/SettingsDialog.qml` (item 4)
- `tests/reference_pin_tests.cpp`, `tests/tabs_recovery_tests.cpp`, `tests/layout_presets_ui_tests.cpp`
- `translations/glancelot_en_XA.ts` (item 7)
- `docs/handoff/PHASE1-FOLLOWUPS-HANDOFF.md`, `docs/verification/PHASE1-FOLLOWUPS/*`

Nothing under `include/`, `cmake/`, root `CMakeLists.txt`, `docs/WORK-QUEUE.yaml`, `AGENTS.md` or `.github/`. Judgement calls: `Main.qml` and `viewer_controller.cpp` are hot files; I touched only the regions named in the brief plus the placeholder hint, and `display_manager.cpp` is outside the list but is the file item 5 names. The item 3 and item 2 UI tests live in `tests/layout_presets_ui_tests.cpp` (existing target with the shipped-QML harness) to avoid a new `cmake/*.cmake` target; a dedicated per-feature test file would be cleaner if the lead wants one.

### 3. Exact base

`origin/main` e967a0c (docs: qualify cycle1-f integration). Not stacked.

### 4. ctest output

Targeted (docs/verification/PHASE1-FOLLOWUPS/after-fix/targeted1.txt), gate lock held:

```
ctest -R "tabs_recovery|layout_presets_ui|translation_coverage_ui|window_slots|monitor_output|monitor_customization|recovery"
100% tests passed, 0 tests failed out of 11
  window_slots, monitor_output, recovery, recovery_startup, recovery_controller, recovery_process, tabs_recovery,
  monitor_customization, monitor_customization_ui, translation_coverage_ui, layout_presets_ui: Passed
ctest -R "^reference_pin$" --repeat until-fail:5   -> 5 of 5 Passed (38.8, 37.1, 35.7, 31.1, 36.1 s)
ctest -R "^reference_pin_native$"                  -> Passed (44 s)
```

Full offscreen suite once, `scripts/build.ps1 -BuildDir build-claude-phase1-followups -Test` (summary in after-fix/full-suite-ctest-summary.txt):

```
100% tests passed out of 51        Total Test time (real) = 935.39 sec
```

No baseline failures; `folder_grid` (231 s), `fs_watch` and `fs_watch_budget` passed first time. Not run: Docker/Linux gate (the lead's tester does that), a physical display.

### 5. Decisions taken and open questions

**Decisions taken**

- Item 2: `tabs()` returns the active row only while unavailable at one tab (the reviewer's suggestion). Alternatives: a new Q_PROPERTY (needs a header change); reading the status text (fragile). The row is built from `activeLocation()`, not `tabList` (which is cleared at one tab). Other `tabs()` consumers (tab strip, title, translation coverage, `window_slots`) only look at `length > 1` / `< 2` or iterate rows, so they are unaffected; `window_slots` passes.
- Item 2: added `clearUnavailable()` so `tabsChanged` fires when the state ends (before, it ended silently; with one tab the placeholder binding could have stayed stale).
- Item 2: one-tab hint drops "Your other tabs keep their places" (there are none); new string, catalog regenerated.
- Item 3: a single-pass helper instead of `.arg(a, b)` because QML's `String.prototype.arg` accepts exactly one argument.
- Item 4: suppression ends on any foreign layout change, not when the refusal is dismissed (see table). Trade-off: an unrelated `changed()` after a dismissed refusal lets the layout's still-stored text show in the general note, which is the same behaviour as any other layout feedback.
- Item 5: status stays a plain string. `routingStatus` is not referenced from `ui/` and its sibling statuses are plain, so translating only this one would be inconsistent; translating all of them is a follow-up.
- Item 6: nothing to do; already on main.

**Open questions for the lead**

1. `include/glancelot/viewer_controller.hpp` line 46 documents `tabs` as "Empty while there is one tab". It is now "empty while there is one tab, except one unavailable row". Comment-only header edit, outside this grant; please update it.
2. Same defect class, not fixed because outside the named sites: `Main.qml` `qsTr("Layout: %1. %2").arg(names[presetCursor]).arg(feedback)` (preset name is user text, around line 770) and `qsTr("%1. %2").arg(stateTitle.text).arg(stateDetail.text)` (preview state accessible name, around line 1148). `fillText` is available for both.
3. `DisplayManager::pinImage()` still returns silently for a stale generation (`generation < pinnedGeneration`). Only `pinCurrentImage()` was in scope.
4. In the 560 px capture the top bar shows a window-control glyph overlapping "Settings" and a truncated "No ph... open" title. The top bar is untouched by this packet, so I treat it as existing; worth a look.
5. `translations/README.md` still says the catalog has 582 messages (now 589).
6. Cycle documents or the queue may want the `PHASE1-FOLLOWUPS` row; I did not edit `docs/WORK-QUEUE.yaml`.

## UI skills

The ui-design-skills (application-interface-design) were not re-read in full in this session; the placeholder was judged from the rendered captures against the existing empty-state pattern (one message, the path, one primary action, one line of guidance; muted folder glyph; compact panes hide the glyph, path and hint). The change to its copy removes a statement that was false for one tab.
