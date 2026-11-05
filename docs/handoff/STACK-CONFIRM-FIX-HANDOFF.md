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

## Round 2 (STACK-CONFIRM-REVIEW-R2, origin/claude/review-stack-confirm-r2 6d0bfc4)

Base for this round: `509f6f9` (round 1 tip). Code commit the full suite ran on: `607546c` (the tip commit of this round is that plus this section and evidence files).

| Item | Result | Where |
| --- | --- | --- |
| R2-B1 slideshow stops in a one-photo folder | Fixed. The `fileCount < 2` stop is gone; "went nowhere" is judged by file path (`slideshowStepFrom` still equal to `viewer.filePath` at the next step), not index; the failure bound is a flat 5 under "continue" and "stop", and `min(5, fileCount)` (at least 2) only under "loop". `slideshowLastIndex` is removed. | `ui/Main.qml` `slideshowStep` |
| R2-N2 closing an inactive tab | Fixed. The slideshow remembers the tab count; an active-tab index change with a lower tab count is a renumbering, not navigation. Closing the active tab still ends it (the folder changes, or the count rule stops it). | `ui/Main.qml` `slideshowObserve` |
| R2-N5 arrow keys between chips | Done. Left/Right move along the chips and stop at the ends (also claimed in ShortcutOverride so no window shortcut takes them); the accessible description names them. | `ui/PhotoGrid.qml` chip delegate |
| R2-N1 skip delay pinned | Done: `slideshowPassesABadPhotoPromptlyAtASlowPace` (6 s pace, bad photo must be passed in under 3.5 s). | test file |
| R2-N3 clipped tab sliver | Fixed with a simple change: when scrolling right to reveal a tab the strip continues to the next tab boundary (`rightMargin` leaves room for that last step), so no tab is left clipped at the left edge. A test asserts it at both widths for every tab and close button. Capture: `tabs-r2-1180.png`, `tabs-r2-560.png`. A sliver can still appear if the user scrolls by wheel or drag to an arbitrary offset; that is user-driven. | `ui/Main.qml` `tabStrip.reveal` |
| R2-N4 no cue for other tabs at 560 px | Not done, as instructed (E5). | |

New tests (all in `tests/stack_confirm_ui_tests.cpp`): `slideshowPassesThroughAOnePhotoFolder` (a:2, b:1, c:2, default "continue", must reach c-2.png with the slideshow on and no notice), `slideshowSurvivesClosingAnotherTab`, `typeChipsMoveWithTheArrowKeys`, `slideshowPassesABadPhotoPromptlyAtASlowPace`, plus the sliver assertion in `longTabNamesKeepTheActiveTabInView`. The slideshow tests now stop the slideshow and wait for any step in flight before teardown (`settleSlideshow`).

Fail-before (the same test binary with `ui/` from `509f6f9`; `fail-before-results-r2.txt`): 9 passed, 4 failed:
```
FAIL!  slideshowPassesThroughAOnePhotoFolder()   Actual (h.viewer.fileName()): "b-1.png"   (stopped in b)
FAIL!  slideshowSurvivesClosingAnotherTab()      closing an inactive tab ended the slideshow
FAIL!  typeChipsMoveWithTheArrowKeys()           'chip("image")->hasActiveFocus()' returned FALSE
FAIL!  longTabNamesKeepTheActiveTabInView()      width 1180, active tab 2: tabClose-0 is clipped at the left edge (x -18..14)
```
Pass-after (`pass-after-results-r2.txt`): 13 passed, 0 failed. `slideshowPassesABadPhotoPromptlyAtASlowPace` passes on `509f6f9` (it pins existing behaviour); its fail evidence is a mutation, `skip-timer-mutation-results.txt` (skip-timer start removed: `FAIL! ... the requested timeout (4000 ms) was too short, 5100 ms would have been sufficient`).

Tests: targeted ctest `100% tests passed, 0 tests failed out of 6` (folder_grid, input_ui, tabs_recovery, translation_coverage_ui, command_routing, stack_confirm_ui; `targeted-ctest-r2.txt`), then the full suite on `607546c` (`full-suite-build-log-r2.txt`): `100% tests passed out of 52`, `Total Test time (real) = 905.99 sec`.

Decisions taken this round: the failure bound stays a constant 5 outside "loop" (rather than "distinct paths tried") because it is the simplest bound that does not depend on the folder the step landed in; tab identity for N-2 is the tab count rule rather than a per-tab id (tabs carry no id in `tabs()`; adding one is a controller change).

Observed, not explained: one intermittent hang of `slideshowKeepsGoingAcrossAFolderBoundary` (the round 1 guard test) while the machine was busy: `QFATAL: Test function timed out` after 300 s in one ctest run, and 1 of 5 sequential direct runs hung at 60 s; it did not reproduce in 41 sequential, 48 parallel and 40 parallel runs afterwards (the last 40 after `settleSlideshow` was added), and the two later full/targeted ctest runs passed. The hang happens after the usual start-up output, so it is in the test body or teardown. I did not establish whether it predates this round (it was not seen at `509f6f9`, but that test ran only a handful of times). If it appears at the gate, rerun once and tell me; the likely suspect is teardown racing an in-flight cross-folder navigation, which `settleSlideshow` now avoids.

## Round 3 (STACK-CONFIRM-REVIEW-R3, origin/claude/review-stack-confirm-r3 4f43bab)

Base for this round: `b7ca003`. Grants used: `src/app/viewer_controller.cpp`, a new `src/app/shared_memory_reaper.hpp`, and ONE addition to `include/glancelot/viewer_controller.hpp`.

Exact header diff:
```
+    // True from the moment a navigation step (next, previous, a folder step, an open) is requested until it has settled: its job has run,
+    // the destination (if any) has been handed to open(), and no further step is queued. A step that ends with this false and the same
+    // filePath went nowhere (a boundary). stateChanged is emitted when it falls back to false.
+    Q_PROPERTY(bool navigating READ navigating NOTIFY stateChanged)
 ...
-    bool imageReady() const;
+    bool imageReady() const; bool navigating() const;
```

| Item | Result |
| --- | --- |
| R3-B2 shared-memory hang | Fixed. Cause: on Windows `QSharedMemory` create/attach/detach take a named system semaphore (count 1, no owner). A worker killed or crashed while inside its own attach()/detach() never returns the count, and the GUI thread then blocked forever in `~QSharedMemory` -> `detach` -> `lock` (right after `cancel()`/`cancelLookahead()` killed the worker, or in a `lock()` before reading pixels). On POSIX Qt uses SysV semaphores with SEM_UNDO, so this is Windows-specific. Fix (`shared_memory_reaper.hpp`, `viewer_controller.cpp`): the GUI thread never calls `lock()`/`unlock()` on a transfer segment (the worker finished writing before it sent the message that makes the viewer read), and an attached segment is destroyed by a short-lived thread (`SharedMemoryReaper`), so a dead worker can block only that thread. At most 16 such threads; beyond that the segment is leaked on purpose (counter, warning) so thread count and lifetime stay bounded. |
| R3-B1 in-flight step | Fixed with the new `navigating` property (set from the request of a step until its result is applied and its destination handed to `open()`, including queued intents). The slideshow does not step while `navigating`, treats a step as a dead end only when it has settled on the photo it left (stopping at once, not at the next tick), and clears `slideshowStepFrom` on settle (R3-N4). The controller emits `stateChanged` when `navigating` falls. |
| R3-N1 540 px | Tab cap now shrinks so a tab plus its close button fits the strip; `reveal()` never scrolls past the active tab. Test widths now 1180, 560, 540. |
| R3-N2 wheel clamp | Includes `rightMargin`. Test `tabStripWheelRespectsTheMargin`. |
| R3-N3 bound pinned | `slideshowPassesAnUndecodableLoneFolderPhoto` (a:2, b:1 undecodable, c:2, "continue"); mutation to `min(5, fileCount)` fails it (`bound-mutation-results-r3.txt`). |

Alternatives for B2 considered: a native file mapping without a cross-process lock (needs the worker changed and Qt's key mangling replicated; rejected for this round), not killing workers that may be in attach/detach (cannot know), and a native-key `QSharedMemory` mode (it still uses the system semaphore). Not verified: Linux (no hang expected: SysV semaphores undo on death); the dead-worker state was reproduced by a holder that locks the segment and never unlocks, not by killing a real decoder at the exact instant.

New tests (`tests/stack_confirm_ui_tests.cpp`): `destroyingASegmentWaitsForTheLockOfADeadWorker` (the old blocking call, reproduced deterministically: the destructor does not return while a "dead worker" holds the lock), `theReaperNeverBlocksTheCallerOnAHeldLock`, `blockedTeardownsAreBounded`, `navigatingSpansAStep`, `slideshowWaitsForASlowFolderStep` (1 s pace with 300 empty sibling folders, 5 s pace with 600), `slideshowPassesAnUndecodableLoneFolderPhoto`, `tabStripWheelRespectsTheMargin`, and 540 px in `longTabNamesKeepTheActiveTabInView`. Fixed sleeps in the test file are replaced by condition waits (`frames()`, `QTRY` with deadlines of at least 10000 ms); the few that must let time pass are marked `// sleep-ok`.

Fail-before (same binary, `ui/Main.qml` from `b7ca003`; `fail-before-results-r3.txt`): `slideshowWaitsForASlowFolderStep` fails at both paces (`the slideshow ended at a-2.png (navigating 1)`), `tabStripWheelRespectsTheMargin` fails (`contentX 226 -> 210`), the 540 px case fails (`active tab 2: the active tab is out of view`). The reaper tests have no pre-fix counterpart; `destroyingASegmentWaitsForTheLockOfADeadWorker` shows the replaced call hangs.

Stress: `slideshowKeepsGoingAcrossAFolderBoundary` 200 runs (4 parallel x 50): see `stress-r3.txt`.

Round 3 results: stress 200/200 runs clean (`stress-r3.txt`); full suite on the round 3 code commit: `100% tests passed out of 52`, Total Test time 1146.87 sec (`full-suite-build-log-r3.txt`), including stack_confirm_ui with the new cases.

## Round 5 (STACK-CONFIRM-REVIEW-R4, origin/claude/review-stack-confirm-r4 3ad19a0)

New implementer for this round. Exact base: `9c0c919` (not stacked; `origin/main` not merged, because `git merge-tree` shows no conflict). The tip commit is this section and evidence files on top of code commit `16a251e`. Grants: `src/app/viewer_controller.cpp`, `src/app/io_job_lease.hpp`, `src/worker/main.cpp`, the new transfer class, the `ui/Main.qml` slideshow region, the test files named by the lead, `cmake/StackConfirmTests.cmake`.

| Item | Result | Where |
| --- | --- | --- |
| R4-B2 | Fixed at the root. Windows transfers no longer use `QSharedMemory`: `TransferSegment` is a named page-file mapping (`CreateFileMappingW`, `OpenFileMappingW`, `MapViewOfFile`) with no cross-process lock and no lock API. A dead worker cannot block anyone. The reaper, its threads, its cap and its tests are deleted. Linux keeps `QSharedMemory` behind the same class, with a plain destroy on the GUI thread. | `src/app/transfer_segment.hpp`, `src/app/viewer_controller.cpp`, `src/worker/main.cpp`; fixtures moved to the class |
| R4-B1 | Fixed. `aDecoderDyingMidTransferNeitherBlocksNorKeepsMemory` drives the real viewer and window with a fixture decoder that answers the probe, attaches, takes the old transport's lock and dies, in 4 rows, 18 deaths each. A heartbeat plus a watchdog prove the event loop runs. Mapped segments must not pile up and must return to the baseline. | `tests/stack_confirm_ui_tests.cpp`, `tests/fs_watch_decoder_fixture.cpp`, `cmake/StackConfirmTests.cmake` |
| Rate | 3,792 steps with real decoders: no stuck teardown is possible any more, GUI never blocked, memory flat. Table below. | `slideshowPatternRateRun` |
| R4-N1 | Fixed. `slideshowWaitsForASlowFolderStep` asserts every photo, in order. | test file |
| R4-N2 | Fixed. The pace restarts when the landed photo has shown or failed, so a photo gets the full pace however long its step took. | `ui/Main.qml` `slideshowLanding` |
| R4-N3 | Fixed. A photo in another folder, or a folder, opened by hand while the slideshow's own step runs now ends the slideshow. | `viewer_controller.cpp` (one notice at 3 sites), `ui/Main.qml` |
| R4-N4 | Gone with the reaper. | |
| R4-N5 | Fixed. All 60 plain `QTRY_*` in the file became `QTRY_*_WITH_TIMEOUT` of 10000, not only the six listed, because the whole file is added by this PR. The wrong `sleep-ok` reason and the misplaced comment are fixed. | test file |
| R4-N6 | Nothing needed. | |
| Found this round | Linux leak, older than this PR: a transfer let go while a killed worker was still exiting stayed in System V for good. Fixed: see Decisions. | `src/app/io_job_lease.hpp`, `~Private` |

**The data flow needs no lock.** The worker writes the pixels, then sends `decoded`. The viewer reads only after that message and copies them before it sends `ok`. Neither side touches the memory at the same time, and R4 found the same. The worker's old lock-free comment is kept in shorter form.

**Why bytes stay bounded.** `retireIoProcess` keeps the admission lease until the killed worker has stopped, and now keeps the viewer's share of the segment for the same time. The kernel frees the mapping when the last handle goes, so each retired worker's memory is counted by admission until it is gone. The existing limits (256 MiB per image, the headroom check, the scheduler's admission) are unchanged. The key is `Glancelot-<uuid>`, and `create` refuses a key already in use.

### Fail-before

Every new or changed assertion, each run reverted afterwards. Files are in `docs/verification/STACK-CONFIRM-FIX/`.

| Change | Test | Result | File |
| --- | --- | --- | --- |
| MC1: `TransferSegment` on Windows back to `QSharedMemory` destroyed on the GUI thread | dying decoder | **Frozen in the first row**: `The GUI thread has been blocked for 30000 ms: the viewer is frozen.` | `r5-fail-before-mc1-qsharedmemory-on-gui-thread.txt` |
| `9c0c919`'s `viewer_controller.cpp` and worker with its reaper, new tests | dying decoder | All 4 rows fail: `19 segments mapped at once: deaths are piling up` (R4-B2). The GUI does not freeze, as R4 found. | `r5-fail-before-9c0c919-reaper.txt` |
| No `ERROR_ALREADY_EXISTS` check | `aTransferSegmentRefusesAKeyInUse` | `'!second.create(4096)' returned FALSE` | `r5-fail-before-segment-mutations.txt` |
| No `CloseHandle` | `aTransferSegmentLivesUntilItsLastHandleCloses` | `the memory outlived its last handle` | same |
| R4-N1: `viewer.navigating` removed from the step guard | `slideshowWaitsForASlowFolderStep`, both rows | `Compared lists have different sizes`, 3 photos instead of 4 | `r5-fail-before-n1-guard-removed.txt` |
| R4-N2: no timer restart | `slideshowShowsAPhotoForTheFullPaceAfterASlowStep` | `c-1.png was on screen for 378 ms at a 1000 ms pace` | `r5-fail-before-n2-no-timer-restart.txt` |
| R4-N3: no notice handler | `slideshowEndsOnAManualOpenDuringItsOwnStep`, all 3 rows | `the manual open was taken for the slideshow's own step` | `r5-fail-before-n3-no-notice.txt` |
| Linux, `fdcb2a9`, segment released at retire | dying decoder | 2 rows fail with 13 and 3 segments left in `ipcs` | `r5-linux-fail-before-hold-fdcb2a9.txt` |
| Linux, wait removed from `~Private` | `closingTheViewerMidTransferLeavesNoMemoryBehind` | `systemSegments(bytes) - systemBefore`: 1, expected 0 | `r5-linux-pin.txt` |

Not shown failing: `aTransferSegmentReportsFailures` (it asserts `attach` and `create(0)` fail with a message), and `closingTheViewerMidTransferLeavesNoMemoryBehind` on Windows, because the MC1 run stopped at its first fatal. The first version of the closing test passed with the wait removed (`r5-linux-final.txt`), because polling ran the event loop and released the segment. It now checks once, with no event loop, and fails as shown.

### Rate acceptance (real decoders, no test hook)

`slideshowPatternRateRun`, kept in the test file and skipped unless `GL_STACK_RATE_PASSES` is set. Same pattern as R4: 40 folders of 2 photos, "continue", 79 steps a pass, three processes side by side. Photos are 640 x 480 here, against R4's 64 x 48, so a leak is easy to see. One viewer per process for all 16 passes, which is stricter on memory than R4's new viewer per pass. Windows, MSVC Release, on a loaded machine (the targeted suites and other sessions ran beside it).

| Pace | Steps | Undecoded | Segments mapped after each pass | Private bytes, pass 0 to 15 | Handles, pass 0 to 15 | Max GUI gap | File |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 40 ms + up to 20 ms | 1,264 | 0 | 0 | 187 to 189 MiB | 384 to 357 | 864 ms | `rate-r5-pace-40ms.txt` |
| 80 ms + up to 20 ms | 1,264 | 0 | 0 | 188 to 189 MiB | 385 to 356 | 960 ms | `rate-r5-pace-80ms.txt` |
| 120 ms + up to 20 ms | 1,264 | 0 | 0 | 186 to 188 MiB | 361 to 354 | 1,128 ms | `rate-r5-pace-120ms.txt` |
| **Total** | **3,792** | **0** | **0** | flat | flat | | |

R4 on the same pattern: 5 stuck teardowns. There is now nothing that can get stuck: no thread, no semaphore, no wait. The rate run ended on code commit `2f328dd`; later commits change only the test file.

Tearing down a full 256 MiB transfer with every page touched took 58 to 72 ms on the GUI thread in those loaded runs, and it never waits for another process. Decoded photos are usually far smaller.

### Linux

WSL2 (`~/gl-cs`, Qt 6.8.3 `gcc_64`), tree `16a251e` fetched from this machine. No Docker run. Targeted suites, offscreen (`r5-linux-head.txt`):
```
aDecoderDying... 12 runs in a row: each Totals: 6 passed, 0 failed
stack_confirm_ui: Totals: 30 passed, 0 failed, 1 skipped (the rate run)
fs_watch:         Totals: 83 passed, 0 failed
fs_watch_budget:  Totals: 3 passed, 0 failed
segments after: 0
```
A rate run on `2f328dd`, 10 passes, 790 steps at the 80 ms pace: 0 undecoded, max GUI gap 423 ms, 0 segments left in `ipcs` (`r5-linux-final.txt`). The reviewer's probe in R4 already showed no hang on a dead lock holder here.

### Tests

Windows, targeted, offscreen, under a build slot as the lead decided on November 5, not under the gate lock. Full suite not run, also by that decision: the tester gates the exact head.

The batch beside the rate runs on `2f328dd` (`targeted-ctest-r5.txt`): 11 of 14 passed. Passed: viewer_acceptance, thumbnails, fs_watch, cache, animation, tabs_recovery, translation_coverage_ui, command_routing, stack_confirm_ui and the others in the file. **`folder_grid`, `input_ui` and `fs_watch_budget` hit their ctest `TIMEOUT`.** Rerun on `a1e9074` with no rate run of mine, but with other sessions' builds and gates in the other slots (`targeted-ctest-r5-rerun.txt`): the same three hit `TIMEOUT` again, and `stack_confirm_ui` passed in 384 s against about 130 s when idle.

I read these as load, not as failures of the code, and this is the evidence. Windows, `16a251e`, direct runs with a longer limit, side by side in one slot:

| Suite | Result | Time | ctest limit | File |
| --- | --- | --- | --- | --- |
| `folder_grid` (`grid_ui_tests`) | 74 passed, 0 failed, 4 skipped (review captures) | 377 s | 400 s | `r5-direct-grid_ui_tests.txt` |
| `fs_watch_budget` | 3 passed, 0 failed. GUI gaps 64 and 113 ms; generating its 100,000 files took 101 to 143 s | 147 s | 180 s | `r5-direct-fs_watch_budget_tests.txt` |
| `input_ui` | 32 passed, **1 failed**: `precedenceMatrixForTheSameKey`, `canvas->rotation()` 0, expected 90, at `input_ui_tests.cpp:143` | 177 s | 240 s | `r5-direct-input_ui_tests.txt` |
| `stack_confirm_ui` | 30 passed, 0 failed, 1 skipped (the rate run) | 323 s | 600 s | `r5-stack-confirm-ui-windows-head.txt` |

`input_ui` rerun alone, as the budget-test rule says: `precedenceMatrixForTheSameKey` passed 3 of 3 runs, then ctest `input_ui` **Passed in 229.43 s** (`targeted-ctest-r5-input-ui.txt`). That failed assertion reads the rotation after a step whose decode the test does not wait for, so a slow decode landing late resets it. I did not instrument this.

Timed-out `fs_watch_budget` run on its own: steady progress until the limit, with no stall (it reached `removed 25000` at 168 s). Nothing in that suite runs a decoder transfer in the slow part.

**Durations are well above the earlier rounds** (`input_ui` 229 s against 94 s in R4, `folder_grid` over 377 s against 291 s), and the three budget suites were close to or over their limits while up to three slots and queued gates ran. I did not run the base under the same load, so I cannot exclude a slowdown from this change, though nothing added runs per test except the same transfer calls. The tester's gate runs alone and decides.

### Grant check

`git diff --name-only 9c0c919..HEAD`, code files:
```
cmake/StackConfirmTests.cmake
src/app/io_job_lease.hpp
src/app/shared_memory_reaper.hpp   (deleted)
src/app/transfer_segment.hpp       (new)
src/app/viewer_controller.cpp
src/worker/main.cpp
tests/fs_watch_budget_tests.cpp
tests/fs_watch_decoder_fixture.cpp
tests/fs_watch_tests.cpp
tests/stack_confirm_ui_tests.cpp
ui/Main.qml
```
Plus this document and `docs/verification/STACK-CONFIRM-FIX/`. All inside the grants; no new file needs a build-list line, because headers here are not listed. Nothing under `include/glancelot/`. In `ui/Main.qml` only the slideshow block changed. No new user-visible string, so no catalog change. In `viewer_controller.cpp`, one change is outside the transfer code: the three `explicit-navigation` notices.

`pr_guard` from `origin/main` (the branch does not hold the script), run in this worktree: `pr_guard: code=True, 0 error(s)`.

### Decisions taken

- **A segment is held until its worker has stopped (Linux leak).** Qt's System V `detach` removes a segment only if it then finds nobody attached. The viewer let go right after `kill()`, usually before the worker was gone, and the killed worker never detaches, so the segment stayed until reboot. My new test caught it. The R4 Linux probe missed it because its child died first. `retireIoProcess` now keeps the segment with the lease until the process has stopped, then destroys it on the GUI thread without waiting. Same code on both platforms, because it is harmless on Windows: the worker's handle holds the same pages until it dies. Alternative: a Linux-only `IPC_RMID` by hand, which relies on Qt's private key scheme. This means "plain direct destroy" on Linux happens at the worker's exit rather than at the `kill()`.
- **A bounded wait at viewer destruction, Linux only.** With no event loop left, nothing would release that hold, and a close during a decode would leave up to 256 MiB until reboot. `~Private` waits at most 1 s for the current workers it just killed. A worker retired earlier and still exiting at that moment is not tracked. Windows has no wait. Alternative: leave it, which breaks "Bound allocation" on every close during a decode.
- **The manual-open notice uses the existing `viewCommand` signal**, id `explicit-navigation`, because `include/glancelot/` is closed this round. Emitted from `openFolder`, `navigateDirectory`, and `open` when the folder changes and the call is not the navigation's own. The slideshow acts on it only while its own step runs. `handleViewCommand` also sees the id, so it ends a loupe, which a folder change ends anyway, and otherwise ignores it. Internal `navigateDirectory` callers (a tab's folder route, a renamed folder with no photo, the restore path) also emit it. It only matters while a slideshow's own step runs, and those cases either end the slideshow anyway or have no photo to show; I reasoned this through and did not test each one. Alternatives: a new signal (a cleaner name, needs a header grant), comparing the destination with what a step could reach (cannot tell a manual open inside the walk), or matching the status text (fragile). The lead may want the dedicated signal when the header next opens.
- **The pace counts from when the photo has shown or failed**, not from when navigation settles, so slow decodes count too. Alternative: restart on settle, which still shortens a photo by its decode time.
- **The fixture takes the old transport's lock before it dies** (`QSharedMemory::attach` + `lock`), although `TransferSegment` does not use it. So the committed test, not a separate patch, freezes if anyone puts `QSharedMemory` back on Windows. That is what the MC1 run shows.
- **The memory check counts real mappings of exactly one transfer's size** (`VirtualQuery`, `/proc/self/maps`), then that the key no longer opens on Windows, or the `/proc/sysvipc/shm` count on Linux. No counter in product code. A key probe is not polled on Linux, because the last detach would remove the very orphan it should report.
- **The test target gains `GLANCELOT_FS_WATCH_TESTING`** as granted. Hooks stay unset unless a test installs them, so the other cases run the production paths. `TIMEOUT` of `stack_confirm_ui` went from 400 to 600 s for the added cases.
- **`Local\` namespace with default security**, as `QSharedMemory` used. Alternative: an unnamed handle passed by inheritance, which `QProcess` makes awkward, for no gain against a same-session process.
- **Skills:** the ui-design-skills are available. I read the opening of `application-interface-design` and searched `motion-ui`. This round changes slideshow timing and one stop condition, with no visual change. I did not read the others.

### Open questions for the lead

1. Do you want a dedicated signal instead of the `explicit-navigation` id on `viewCommand`? It needs one line in `include/glancelot/viewer_controller.hpp`.
2. The queue row's `acceptance` names `STACK-CONFIRM-REVIEW.md`, while reviews are numbered by round (R4's note, still open).

### Unverified

- **Full `scripts/build.ps1 -Test` and `docker build`: not run,** by the lead's decision. The tester gates the head. `reference_pin_native`, native D3D captures and every suite not listed above were not run on this code.
- **One 64 MiB System V segment with no attachments was left once** on Linux, after the first suite run on `67e6788` (a 4096 x 4096 photo, so most likely from `liveLargeDecodeCancellationKeepsExplicitOpen` in `fs_watch`). It did not come back in 6 runs of that function, 4 more runs of the whole `fs_watch` suite, or the Linux rate run. Cause not established. A worker retired just before a test executable exits is one candidate; see the wait above.
- The fail-before for the 9c0c919 reaper and the segment mutations ran in a build slot on a loaded machine, not under the gate lock.
- Linux: WSL2 only; Docker's own `/dev/shm` and System V limits were not looked at. Qt's segments there are System V, so `/dev/shm` should not matter.
- A killed worker that never dies (stuck in a kernel I/O call on a dead share) holds its segment and lease until it does. Admission counts it, so the total stays bounded, but this case was not run.
- Screen readers, physical displays, fractional DPI: not checked; this round has no visual change.
