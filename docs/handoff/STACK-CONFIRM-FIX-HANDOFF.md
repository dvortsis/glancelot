# STACK-CONFIRM-FIX handoff

Fixes for the findings in `docs/verification/STACK-CONFIRM-REVIEW.md` (origin/claude/review-stack-confirm, f441edd): B-1, N-2, G-1, N-1, N-5, and the translation catalog.

| | |
| --- | --- |
| Branch | `claude/stack-confirm-fix` (worktree `.worktrees/claude-stack-confirm-fix`, build `build-claude-stack-confirm-fix`) |
| Exact base | `origin/main` `e967a0c` (not stacked on another packet) |
| Tip | see `git log -1` on the branch; the tip commit is this document plus evidence files only |
| Code commit the full suite ran on | `e0e7d58`. Later changes: the test file (one case dropped), one sentence added to the grid's `Accessible.description` in `ui/PhotoGrid.qml` (with its one catalog entry), evidence files, this document. Suites re-run after those changes are listed under "Tests". |

## What changed

| Item | Result | Where |
| --- | --- | --- |
| B-1 slideshow stall on an undecodable photo | Fixed. A settled failure (`!loading && !imageReady` after the step landed) counts as shown: a 1.2 s skip timer (or the pace, if shorter) steps on instead of holding the error for a whole interval. Stops with a notice after `min(5, fileCount)` failed photos in a row. | `ui/Main.qml` slideshow block (`slideshowStep`, `slideshowObserve`, `slideshowSkipTimer`) |
| N-2 tab switch / folder open ends the slideshow | Fixed. Active-tab change ends it; a directory change ends it unless it is the slideshow's own step (`slideshowStepFrom`), so the default "continue" boundary still crosses folders. | `ui/Main.qml` same block |
| G-1 details mode moves the anchor | Fixed. `details` flips the geometry through `detailsLayout` after the first visible row is read from the old geometry; the row is put back after `forceLayout()` (and once more a frame later), and a cursor that was in view stays in view. Measured before: 10 columns 10 -> 18; the test checks 6, 10 and 4 columns, both directions, exactly. | `ui/PhotoGrid.qml` (`onDetailsChanged`, `keepAnchor`, `columns`, `cellHeight`) |
| N-1 type chips mouse-only | Fixed. Chips are `StrongFocus` with the button's own focus ring, names and CheckBox role already existed; Ctrl+Shift+F moves to them from the grid (the active chip, else the first), Tab/Shift+Tab walk them, Space/Enter switch, Escape returns to the grid without leaving it. A pointer click leaves the keyboard on the grid. | `ui/PhotoGrid.qml` (`kindChips`, `focusKindChips`, `keyAction` "kinds") |
| N-5 long tab names | Fixed. Names elide inside a capped tab width, the strip scrolls and reveals the active tab (and focused tab/close button), New tab sits outside the scrolling part, Sort yields sooner while tabs show. Wheel over the strip scrolls it sideways. Captures below. | `ui/Main.qml` (`tabStrip`, `tabButton`, `tabNew`, `titleMinimum`) |
| Translations | `translations/glancelot_en_XA.ts` regenerated with lupdate 6.8.3 in WSL and `pseudolocalize.py`: 3 new messages, then 1 changed (the grid description). `translation_coverage_ui` passes. | `translations/glancelot_en_XA.ts` |

Before and after for N-5 (native D3D capture, not offscreen; tab text from the test, three tabs with 70-90 character names, last one active):
`docs/verification/STACK-CONFIRM-FIX/tabs-before-1180.png`, `tabs-after-1180.png`, `tabs-before-560.png`, `tabs-after-560.png`. Before: at 1180 the first tab fills the strip and the active third tab and New tab are not visible; at 560 only "A fairly lor" shows. After: the active tab and its close button are inside the strip at both widths, names end in an ellipsis, New tab is always visible, the mode selector keeps its labels.

## Tests

New file `tests/stack_confirm_ui_tests.cpp`, new target `stack_confirm_ui_tests` in the new `cmake/StackConfirmTests.cmake` (included from `CMakeLists.txt` next to `ReferencePinTests.cmake`, `glancelot_test_labels(stack_confirm_ui LABELS ui process)`). The harness loads `ui/*.qml` from source, so the fail-before run needs no rebuild: the same test binary was run with `ui/` checked out from `e967a0c` (`git checkout e967a0c -- ui`, then `git checkout HEAD -- ui`).

Fail-before (base `ui/`, `docs/verification/STACK-CONFIRM-FIX/fail-before-results.txt`): 3 passed, 6 failed:
```
FAIL!  slideshowSkipsAnUndecodablePhoto()          Actual (h.viewer.currentIndex()): 2        (stuck at the bad photo)
FAIL!  slideshowStopsAfterAnUnplayableRun()        '!flag(h, "slideshowOn")' returned FALSE
FAIL!  slideshowEndsOnTabSwitchAndFolderOpen()     '!flag(h, "slideshowOn")' returned FALSE
FAIL!  detailsToggleKeepsTheVisibleAnchor()        width 1180 (6 columns): first visible 18 before, 19 in details
FAIL!  typeChipsWorkFromTheKeyboard()              'chip("folder")->hasActiveFocus()' returned FALSE
FAIL!  longTabNamesKeepTheActiveTabInView()        width 1180, active tab 2: the active tab is out of view
PASS   slideshowKeepsGoingAcrossAFolderBoundary()  (guard, passes on the base by design)
```
Pass-after (fixed `ui/`, `pass-after-results.txt`): 9 passed, 0 failed (initTestCase and cleanupTestCase count as two).

`slideshowKeepsGoingAcrossAFolderBoundary` guards my own N-2 change (the default "continue" boundary must keep the slideshow running into the next folder). Its fail evidence is a mutation (`if (!slideshowStepFrom.length)` replaced by `if (true)`): `guard-mutation-results.txt` shows `FAIL! ... the slideshow's own step into the next folder ended it`.

The review asked for a vanished-file case. It was tried and dropped: the viewer relists as soon as the file is removed (even with the step fired by hand in the same turn), so the name never reaches a decode and the case passed on the base too. The undecodable case runs through the same `fail()` path.

Targeted `ctest` on `e0e7d58` (`targeted-ctest.txt`, 13 suites, offscreen CMake environment, run inside `build-slot.ps1`): `100% tests passed, 0 tests failed out of 13` (folder_grid 275 s, input_ui 96 s, pathbar_ui, viewport, ui_settings, shortcuts_help, filter, tabs_recovery, monitor_customization_ui, translation_coverage_ui, layout_presets_ui, command_routing, stack_confirm_ui).

Full suite on `e0e7d58`, `scripts/build.ps1 -BuildDir build-claude-stack-confirm-fix -Test` (gate lock taken by the script; `full-suite-build-log.txt`):
```
100% tests passed out of 52
Total Test time (real) = 939.99 sec
```
including `reference_pin_native`. No baseline failures, no reruns needed.

After the last code change (grid description sentence + catalog), `targeted-ctest-final.txt`: `100% tests passed, 0 tests failed out of 4` (input_ui, translation_coverage_ui, command_routing, stack_confirm_ui).

## Grant check

`git diff --name-only e967a0c..HEAD`: `CMakeLists.txt` (one include line, granted), `cmake/StackConfirmTests.cmake` (new, granted), `tests/stack_confirm_ui_tests.cpp` (new), `ui/Main.qml`, `ui/PhotoGrid.qml`, `translations/glancelot_en_XA.ts`, and files under `docs/verification/STACK-CONFIRM-FIX/` plus this handoff. Not touched: `docs/WORK-QUEUE.yaml`, `include/glancelot/*`, `ui/UiButton.qml`, `src/*`. In `ui/Main.qml` the edits are the slideshow block, the tab strip, `titleMinimum` in the top bar and the New tab button; none of the `.arg()` sites, SettingsDialog, display_manager or reference_pin regions that `claude/phase1-followups` owns.

## Decisions taken

- **Failure bound `min(5, fileCount)`.** Five tolerates a run of damaged files; a folder shorter than five is one full pass with nothing decodable. Alternatives: a fixed 5 (spins a 3-file folder twice), a time bound (depends on the pace).
- **Skip delay `min(pace, 1.2 s)`.** The error stays readable for a moment but a bad photo does not hold the slideshow for a whole interval (60 s paces). Alternative: wait the full interval (what a good photo gets); rejected because a failed photo has nothing to look at.
- **Stop message through `deleteFlow.showNotice`** ("Slideshow stopped: the last N photos could not be opened."), the notice every other `Main.qml` flow uses (alert role, 6 s). Alternative: a status-bar string, which the next state change overwrites.
- **Manual folder navigation is told from the slideshow's own step by `slideshowStepFrom`** (set before `next()`, cleared when the file path changes). A manual open inside the few milliseconds of an in-flight step is not noticed; accepted. Alternative needing the controller (not granted): a signal for "navigation initiated by user".
- **Ctrl+Shift+F, not a `filter-kind-*` registry command.** A registry command needs a handler, scope rows, help and routing-test updates in hot files, for a pane-local action. The key is pane-local like Ctrl+F, yields to remaps through `yields`, and is in the grid's accessible description and the chips' tooltips. See Open questions.
- **`detailsLayout` indirection** instead of a Connections on `details`: binding evaluation order is not guaranteed, so the first visible row is read before the geometry changes.
- **New tab moved outside the scrolling strip** and the strip's `Layout.preferredWidth` set to 5 controls so its content width never sets the bar's width (before, a long tab list squeezed the mode selector).
- **`titleMinimum` grows to `5 * controlHeight + 2 gaps` while tabs show**, so Sort yields before the strip is squeezed to a sliver; unchanged (72) with one tab.
- **Tab width cap** `max(3 * controlHeight, min(6 * controlHeight, strip / 2 - controlHeight))`, i.e. 96 to 192 px with the current theme; names end in an ellipsis, the full folder path stays the tooltip and the full title the accessible name.
- **Local label `contentItem` on the tab button** instead of changing the shared `UiButton`: a `RowLayout` does not shrink a non-filling label, so the elision needs its own label; changing `UiButton` (tried, reverted) squeezed the mode selector labels elsewhere.
- **Deviation from the brief:** targeted suites ran inside `build-slot.ps1` and directly, not inside `gate-lock.ps1`, because another session held the gate lock for most of the session (targeted `ctest -R` is exempt in AGENTS.md); the full suite ran under the gate lock via `scripts/build.ps1 -Test`.
- **Skills:** the ui-design-skills are available on disk. I read the first ~90 lines of `application-interface-design`, `responsive-layout` and `layout-spacing` and applied keyboard completeness, show/hide by measured fit, and the spacing ramp; I did not read `component-geometry`, `typography-ui`, `motion-ui` or `design-review`. No new motion, colour or type was added.

## Open questions for the lead

1. Ctrl+Shift+F is not in the command registry, so the shortcut recorder and the cheat sheet do not list it. Register `grid-kind-chips` (scope grid) if you want it remappable and listed; that touches `command_registry.cpp`, `Main.qml` routing and `command_routing` expectations.
2. At 560 px only the active tab shows (the other tabs are one scroll or a `tab-*` command away). If the product wants an overflow menu listing every tab, that is a new control.

## Unverified

- Docker/Linux build and run: not run. `longTabNamesKeepTheActiveTabInView` and `detailsToggleKeepsTheVisibleAnchor` assert geometry, and Linux fonts have moved geometry assertions before (942ebfe); the lead's gate decides.
- Physical displays, fractional DPI, screen readers (names and roles were checked through `QAccessible`, not NVDA or Narrator).
- The tab strip's scroll sliver: when the active tab is scrolled into view the neighbouring tab's edge can show as a few clipped pixels at the strip's left edge (visible in `tabs-after-1180.png`). Cosmetic; not changed.
