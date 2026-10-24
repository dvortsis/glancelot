# STACK-CONFIRM round 2: review of STACK-CONFIRM-FIX and the round as a whole

**Verdict: CHANGES REQUIRED**

STACK-CONFIRM-FIX fixes what round 1 asked for. The slideshow no longer stalls on an undecodable photo and stops, with a notice, after a bounded run of failures. Tab switches and manual folder opens end the slideshow. Switching to details mode and back keeps the visible anchor. The type chips work from the keyboard and have accessible names. Long tab names elide, and the active tab stays in view at 560 and 1180 px. The lead's `lead_decisions` record every other B-2 clause. The eight targeted suites pass on Windows, and five product-hunk reverts fail the tests that claim to cover them.

One new blocking defect stops approval (R2-B1). The new `slideshowStep` stops the slideshow without a message as soon as its own folder-boundary step lands in a folder with only one photo. "continue" is the default boundary mode, so this is the slideshow's own folder-boundary behaviour, which this packet was told to protect. The base `e967a0c` carried on into the next folder. The fix is small and stays inside this packet's files.

| | |
| --- | --- |
| Reviewer | Fresh-context Claude Code (Opus 5.5) review subagent on the lead's Windows PC. It implemented none of STACK-CONFIRM-FIX and did not write the round 1 review. |
| Target | `git diff e967a0c 509f6f9` (`origin/claude/stack-confirm-fix`; base `e967a0c` = `origin/main` at round 1). |
| Inputs | Round 1 `docs/verification/STACK-CONFIRM-REVIEW.md` (`origin/claude/review-stack-confirm` `f441edd`); queue row STACK-CONFIRM on `origin/main` `1296b48` (`review`, `fix_packet`, `lead_decisions`); `docs/handoff/STACK-CONFIRM-FIX-HANDOFF.md`; the lead's answer to open question 1, given in the review brief (see below). |
| Environment | Windows 11 Pro 10.0.26200, MSVC 2022 Release, Qt 6.8.3 (`.tools/Qt/6.8.3/msvc2022_64`), Ninja. Detached worktree `.worktrees/review-stack-confirm-r2` at `509f6f9`. Build dir `C:\src\Github\Glancelot\build-review-stack-confirm-r2` (main checkout root, not inside the worktree), 746/746 steps, exit 0, through `scripts/build-slot.ps1`. Targeted ctest through `scripts/gate-lock.ps1`. |
| ui-design-skills | Available at `~/.agents/skills/ui-design-skills/skills`. Read in full: `application-interface-design` and `responsive-layout`. Not read: `component-geometry`, `typography-ui`, `motion-ui`, `layout-spacing`, `design-review`. The packet adds no new colour, type, motion or radius. |
| Evidence | `docs/verification/STACK-CONFIRM-R2/`: targeted ctest log, mutation results (`mut1`..`mut4`), probe source (`review-probes.diff.txt`, applied to the test file only while running and then reverted) and results, and native D3D captures `tabs-1180.png` and `tabs-560.png`. |

## 1. Round 1 items: status with this packet merged

Meaning of each status: **fixed** = code and a test in STACK-CONFIRM-FIX, verified here. **recorded** = carved out by the lead in the queue row's `lead_decisions` (2026-10-24) to a named later packet. E5 "Workspace completion" and E6 "Accessibility and i18n completion" are rows in `docs/handoff/LEAD-PLAN-2026-10-22.md`. CAPABILITY is the `capability_model.hpp` contract row there. **open** = neither of these.

| Item | What it was | Status | Where |
| --- | --- | --- | --- |
| B-1 | Slideshow stalls at an undecodable photo | **fixed** (but see R2-B1, a new defect next to it) | `ui/Main.qml:664-705`; tests `slideshowSkipsAnUndecodablePhoto`, `slideshowStopsAfterAnUnplayableRun` |
| G-1 | F009 details mode moves the visible anchor | **fixed** | `ui/PhotoGrid.qml:62-81`, `:84`, `:550`; test `detailsToggleKeepsTheVisibleAnchor` (6, 10 and 4 columns, both directions, exact) |
| G-2 | F031 animation-aware clock, prefetch, unplugged presentation monitor; F053 200 % text on stack controls | **recorded** (F031 parts -> E5; F053 -> E6) | queue `lead_decisions` |
| G-3 | F042 chip counts | **recorded** -> E5 | queue `lead_decisions` |
| G-4 | F051 keyboard-layout/IME tests, OS-reserved shortcut explanation | **recorded** -> E5 | queue `lead_decisions` |
| G-5 | F040 platform capability check and unsupported state | **recorded** -> CAPABILITY (Phase 2) | queue `lead_decisions` |
| N-1 | Type chips mouse-only | **fixed** | `ui/PhotoGrid.qml:141-149`, `:244`, `:337-351`; test `typeChipsWorkFromTheKeyboard` |
| N-2 | Tab switch / folder open do not end the slideshow | **fixed** (one side effect, R2-N2) | `ui/Main.qml:678-692`; tests `slideshowEndsOnTabSwitchAndFolderOpen`, guard `slideshowKeepsGoingAcrossAFolderBoundary` |
| N-3 | Cheat sheet chords, cleared bindings, no `qsTr` | **recorded** -> E5 (strings with E6) | queue `lead_decisions` |
| N-4 | Preset list stale after Import / Restore all | **recorded** -> E5 | queue `lead_decisions` |
| N-5 | Long tab names hide the active tab | **fixed** | `ui/Main.qml:1711`, `:1873-1960`; test `longTabNamesKeepTheActiveTabInView` |
| N-6 | Completion UNC/Unicode/long-path tests | **recorded** -> E5 | queue `lead_decisions` |
| N-7 | C++ refusal strings untranslated | **recorded** -> E6 | queue `lead_decisions` |
| N-8 | Offscreen captures draw icons outside the clip (evidence method) | info, no action | I judged the tab strip from native D3D captures (`STACK-CONFIRM-R2/tabs-*.png`), which match the implementer's |

Every B-1/B-2 item is fixed or recorded. The open item is a new one, R2-B1.

Lead decision on handoff open question 1 (from the brief): Ctrl+Shift+F stays a shortcut outside the registry for now, and making it a registry command is an E5 follow-up. This decision is not yet in the queue row. The lead should add it to `lead_decisions` at integration so the E5 list carries it. Handoff open question 2 (an overflow menu listing every tab at narrow widths) is unanswered. It is non-blocking (R2-N4).

## 2. Findings

### Blocking

**R2-B1 (MEDIUM-HIGH, regression in the slideshow's own folder-boundary steps): with the default "continue" boundary, the slideshow stops without a message when its own step enters a folder that has a single photo.**

- Where: `ui/Main.qml:666`, `if (viewer.fileCount < 2) { stopSlideshow(); return }` inside `slideshowStep()`. `viewer.fileCount` counts the current folder only (`viewer_controller.cpp:1548`). When the slideshow's own step crosses into a folder with one photo, the next tick stops it, with no notice. The base tick had no such check. The failure bound `Math.min(slideshowFailureLimit, viewer.fileCount)` at `:668` has the same per-folder dependence: in a one-photo folder the bound is 1. The notice would then read "the last 1 photos".
- Evidence (review probe `probeSlideshowThroughOnePhotoFolder`, folders a: 2 photos, b: 1, c: 2, boundary "continue", 1 s pace, offscreen):

  ```
  fix 509f6f9 ui/:   t=2145ms b #1/1 ready=1 loading=0 on=1
                     t=3014ms b #1/1 ready=1 loading=0 on=0      <- stopped in b, notice empty
  base e967a0c ui/:  t=2147ms b #1/1 ready=1 loading=0 on=1
                     t=3163ms c #1/2 ready=1 loading=0 on=1      <- carried on into c
  ```

  (`STACK-CONFIRM-R2/probes-fix-results.txt`, `probes-base-results.txt`, `p1-base-results.txt`.) Variant with b's photo undecodable: the fix stops in b. The base stalls there (B-1).
- A second cause, already in the base: the "the last step went nowhere" check at `:673` compares indexes only (`slideshowLastIndex === viewer.currentIndex`). A step from b #1 to c #1 therefore looks like a dead end, and the slideshow stops at c #1. The base does this (`p1-base-results.txt`: `c #1/2 ... on=0` at t=4030ms). The fix with only `:666` removed does it too (`p1-nocheck-results.txt`). Round 1 did not see this.
- Required fix:
  1. Drop the `fileCount < 2` stop from `slideshowStep` (`startSlideshow` already requires two photos).
  2. Detect a step that went nowhere by `viewer.filePath` (for example, compare against the `slideshowStepFrom` path the step left), not by index.
  3. Make the failure bound independent of the current folder's size when the boundary is "continue" (for example, a flat 5, or `min(5, fileCount)` only when the boundary is "loop").
  4. Add a test: a(2), b(1), c(2) with "continue" reaches c #2 while the slideshow is still on, and fails on `509f6f9`.

  This is all in this packet's slideshow block.

### Non-blocking

- **R2-N1 (LOW, test gap): the 1.2 s skip delay is not pinned by any test.** Removing `slideshowSkipTimer.start()` (`ui/Main.qml:687`) leaves both B-1 tests passing (`mut4-results.txt`), because at the tests' 1 s pace the main timer steps at the same moment. A case at a slow pace (say 10 s) that expects the bad photo to be passed in about 1.2 s would pin it. The behaviour itself is correct. Probe `probeGoodPhotosKeepTheirPace` shows good photos keep their full 3 s (`hold times 3034, 3003, 2997 ms`) and the skip timer never runs for them.
- **R2-N2 (LOW, N-2 side effect): closing another, inactive tab ends the slideshow.** Closing tab 0 while the slideshow runs in tab 1 shifts `activeTab` from 1 to 0, and `slideshowObserve` (`:680`) reads that as a tab switch. Probe `probeSlideshowWhenAnotherTabCloses`: `activeTab 0 slideshowOn false`; the base kept it on. Closing an unrelated tab is not navigation. Track the active tab's identity (for example its folder plus index adjusted on close) rather than its index. This can ride along with the R2-B1 fix.
- **R2-N3 (LOW, cosmetic, recorded in the handoff's "Unverified"): a clipped sliver of the previous tab's close button shows at the strip's left edge** at 1180 px (`STACK-CONFIRM-R2/tabs-1180.png`, x about 578), which reads as a stray glyph. `reveal()` could snap `contentX` to a tab boundary, or the strip could fade its edges.
- **R2-N4 (LOW, responsive-layout "nothing essential unreachable", open question 2): at 560 px only the active tab shows and nothing says that other tabs exist.** They remain reachable through the wheel over the strip (probe `probeWheelOverTabStrip`: `contentX 202 -> 0`, photo and slideshow unaffected), Tab focus, `tab-*` commands and the palette, so nothing is lost. An overflow affordance is a product choice for the lead. At 560 px the Sort control now yields while tabs show (`titleMinimum`, `:1711`). The handoff records this as intended, and Sort is still reachable from its command.
- **R2-N5 (info): no arrow-key movement between chips.** Arrow keys on a focused chip do nothing: focus, the grid cursor and the viewed photo stay put (probe `probeChipKeyboardEdges`). Tab and Shift+Tab walk the chips, Tab past the last chip returns to the grid (`photoGridView`), and Shift+Tab before the first goes to the visible "Open selected" button. There is no keyboard trap and no stray action. Arrow movement inside the group would be a nicety.

### Checked without findings

- **Slideshow failure stepping:** a settled failure (`!loading && !imageReady`, step landed) starts the skip timer once; `Timer.start()` on a running timer does not restart it, so repeated `stateChanged` cannot postpone it. The main tick yields while the skip timer runs. `stopSlideshow` stops both timers. Over 8 files, 7 undecodable, it stops at index 6 after five failures with "Slideshow stopped: the last 5 photos could not be opened." in the shared alert notice. The indicator hides.
- **Navigation ends it, its own boundary step does not:** `slideshowStepFrom` is set before `viewer.next()` and cleared when `filePath` changes, so a directory change during the slideshow's own step is accepted and a manual one ends it. Guard test passes. The mutation in the handoff's `guard-mutation-results.txt` is credible.
- **Details anchor:** the first visible item is read from the old geometry before `detailsLayout` flips. `forceLayout()` and one `Qt.callLater` re-anchor it, clamped to the content. A cursor that was in view is kept in view with `GridView.Contain`. `columns` and `cellHeight` follow `detailsLayout`, so keyboard row movement matches the geometry. `details` is only set by the `view-details` command (`Main.qml:799`), never by a startup binding, so the `!visible` shortcut cannot leave the two properties out of step.
- **Chips:** `StrongFocus` with the button's own `visualFocus` ring. Accessible names "Show only Folders/Photos/Other files", role CheckBox, checked state, and a description naming Space and Escape. Ctrl+Shift+F is free in `src/` (no binding), yields through `yields`, and does not open the name filter (`Ctrl+F` now excludes Shift, `PhotoGrid.qml:590`). A pointer click returns the keyboard to the grid. Escape is claimed in `ShortcutOverride`, so it returns to the grid without leaving grid mode. The native run passes too.
- **Tab strip:** the cap is 96 to 192 px, with `ElideRight`. The full folder path is the tooltip and the full title is the accessible name. New tab sits outside the scroll area. The active tab is revealed on width, content and tab changes, and a focused tab or close button is revealed too. The strip's own content no longer sets the bar's width. Native D3D captures at 1180 and 560 px match the implementer's after-images, with the active tab, its close button and New tab in view. `command_routing` (`mainQmlLoadsWithoutQmlWarnings`) passes, so the custom `contentItem` (`foreground` exists in `UiButton.qml:26`) raises no QML warning.
- **Round 1 confirmed items touched by these files:** R-3d (slideshow stop on manual steps: `invoke` `:480`, wheel `:634`, modal and deactivation `:937-939`, grid entry `:76`) is unchanged. The F015 tab strip keeps its PageTabList/PageTab roles and names. `tabs_recovery` passes. PhotoGrid F014 badges and F042 filtering are unchanged (`input_ui` passes, including `kindChipsFilterTheGridByType` and `detailsModeListsRowsWithoutThumbnails`). The translation catalog matches `translation_coverage_ui`.
- **Grant check:** `git diff --name-only e967a0c 509f6f9` matches the handoff's list. `CMakeLists.txt` (one include line) and `cmake/StackConfirmTests.cmake` are lead-owned; the handoff says they were granted. The queue row does not show the grant, so the lead should confirm it at integration.

## 3. Fail-before spot checks (product hunks reverted locally, then restored)

The test binary loads `ui/*.qml` from the worktree, so each mutation ran without a rebuild. I ran `git checkout -- ui` after each one; `git status --short` was clean afterwards, apart from the evidence folder.

| Mutation | Test | Result |
| --- | --- | --- |
| B-1: `slideshowStep` returns again while `!viewer.imageReady` | `slideshowSkipsAnUndecodablePhoto` | FAIL, `Actual (h.viewer.currentIndex()): 2`, expected 3 |
| same | `slideshowStopsAfterAnUnplayableRun` | FAIL, `'!flag(h, "slideshowOn")' returned FALSE` |
| G-1: `keepAnchor` returns at once | `detailsToggleKeepsTheVisibleAnchor` | FAIL, `width 1180 (6 columns): first visible 18 before, 19 in details` |
| N-2: the `activeTab !== slideshowTab` line deleted | `slideshowEndsOnTabSwitchAndFolderOpen` | FAIL at `:119` (after `tab-new`) |
| N-1: chip `focusPolicy` back to `Qt.NoFocus` | `typeChipsWorkFromTheKeyboard` | FAIL at `:216`, `'chip("image")->hasActiveFocus()' returned FALSE` |
| Bound: `slideshowFailureLimit` 5 -> 50 | `slideshowStopsAfterAnUnplayableRun` | FAIL at `:83` (never stops) |
| Skip timer: `slideshowSkipTimer.start()` removed | both B-1 tests | PASS: not pinned (R2-N1) |

The handoff's fail-before claims hold for every hunk I reverted.

## 4. Test output

Targeted suites, Windows, offscreen (CMake test environment), first run, no reruns. The run waited for another session's gate lock and then ran under it:
`& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch review-stack-confirm-r2 -Run { ctest --test-dir C:\src\Github\Glancelot\build-review-stack-confirm-r2 --output-on-failure -R "^(stack_confirm_ui|input_ui|folder_grid|tabs_recovery|translation_coverage_ui|viewer_acceptance|animation|command_routing)$" }`

```
Waiting for gate lock held by dvort on review-phase1-followups since 10/24/2026 15:04:30
Test project C:/src/Github/Glancelot/build-review-stack-confirm-r2
1/8 Test  #7: viewer_acceptance ................   Passed   10.64 sec
2/8 Test #12: folder_grid ......................   Passed  214.96 sec
3/8 Test #13: input_ui .........................   Passed   74.28 sec
4/8 Test #34: animation ........................   Passed    0.53 sec
5/8 Test #44: tabs_recovery ....................   Passed   60.90 sec
6/8 Test #47: translation_coverage_ui ..........   Passed   24.23 sec
7/8 Test #49: command_routing ..................   Passed   10.22 sec
8/8 Test #52: stack_confirm_ui .................   Passed   41.66 sec

100% tests passed out of 8
Total Test time (real) = 437.43 sec
```

Native platform (QPA and Quick backend unset), `stack_confirm_ui_tests longTabNamesKeepTheActiveTabInView typeChipsWorkFromTheKeyboard`: `Totals: 4 passed, 0 failed` (captures in `STACK-CONFIRM-R2/`).

Review probes (temporary additions to `tests/stack_confirm_ui_tests.cpp`, built through `build-slot.ps1`, then reverted). On the fix `ui/`:

```
FAIL!  probeSlideshowThroughOnePhotoFolder(good single photo)        (R2-B1: stopped in b)
FAIL!  probeSlideshowThroughOnePhotoFolder(undecodable single photo) (R2-B1: stopped in b)
PASS   probeGoodPhotosKeepTheirPace     hold times (ms): QList(3034, 3003, 2997) skip timer ran: false
PASS   probeChipKeyboardEdges           (informational, R2-N5)
PASS   probeSlideshowWhenAnotherTabCloses  activeTab 0 slideshowOn false (R2-N2)
PASS   probeWheelOverTabStrip           contentX 202 -> 0 | viewer index 1 -> 1 | slideshowOn true
Totals: 6 passed, 2 failed
```

## 5. To approve

Fix R2-B1 (drop the one-photo stop, detect a step that went nowhere by path, make the failure bound independent of folder size under "continue") and add the a(2)/b(1)/c(2) test with its fail-before. R2-N2 should go in the same change. A fresh reviewer then re-reviews only the slideshow block and its tests. The rest of this packet needs no re-review.

## Unverified

- Docker/Linux run of the new geometry assertions (the lead's gate decides; Linux fonts have moved geometry before).
- Screen readers (names and roles checked through `QAccessible` only), physical displays, fractional DPI.
- The full suite was not rerun by this review; the handoff's full run on `e0e7d58` is 52/52.
