# MONITOR-OUTPUT-01 Fix Pass & Review Resolution (Gemini Lane)

- **Task**: MONITOR-OUTPUT-01 (M5 second-display presentation window lifecycle, routing, and clean GPU rendering)
- **Branch**: `agent/monitor-output`
- **Worktree**: `.worktrees/monitor-output`
- **Predecessor**: `ae1b7e4` (`claude/monitor-output-fix`)
- **Reviewer reference**: `docs/verification/MONITOR-OUTPUT-01-ae1b7e4-CODEX-REVIEW.md` (7af09d6)

---

## 1. Summary of Review Findings & Resolutions

### Finding 1 (P1): Hidden secondary pixels survive budget eviction and low-memory release
- **Root Cause**: `DisplayManager::Private::applyImagesToWindow()` never cleared the secondary canvas image in `Mirror` or `Pinned` modes (`presentationWindow->setSecondaryImage(QImage())` was omitted). Additionally, `PresentationWindow::setComparisonMode(false)` toggled visibility without zeroing `m_canvasB`'s `QImage`, leaving previous secondary pixels retained in memory outside the manager's accounting.
- **Resolution**:
  - In `DisplayManager::Private::applyImagesToWindow()`, explicitly call `presentationWindow->setSecondaryImage(QImage())` whenever mode is `Mirror` or `Pinned`.
  - In `PresentationWindow::setComparisonMode(bool comparison)`, when leaving comparison mode (`!comparison`), immediately set `m_canvasB->setImage(QImage())`.
  - In `DisplayManager::Private::enforceBudget()`, call `applyImagesToWindow()` when any non-essential image is shed to synchronize canvas ownership with memory accounting.
  - In `DisplayManager::releaseCachedImages()`, ensure `presentationWindow->setSecondaryImage(QImage())` is cleared when not in active comparison mode.
  - In `DisplayManager::closePresentation()`, explicitly zero primary and secondary window images before window destruction.

### Finding 2 (P1 / Scope): Output-specific viewport / color / scale
- **Disposition**: Per lead decision recorded in `docs/WORK-QUEUE.yaml`:
  > *"per-output zoom/pan/color/scale not done and split to follow-up MONITOR-CUSTOM-01 scope"*
  `MONITOR-OUTPUT-01` delivers stable per-display ID fit mode persistence (`shrink`, `fit`, `fill`, `width`, `height`), rebindable single-key/chord dismissal, multi-monitor enumeration, and honest `Unknown` color-space reporting until hardware ICC adapters arrive in `COLOR-01` / `F044`.

### Finding 3 (P2): Clearing comparison content erases stale-result history
- **Root Cause**: `compareWithPinnedOrPrevious()` and `releaseCachedImages()` reset `d->comparisonGeneration` to 0, allowing older in-flight asynchronous comparison images (e.g. gen 7 vs earlier gen 20) to overwrite the current comparison selection.
- **Resolution**:
  - Added `quint64 comparisonGenerationHighWater{0}` in `DisplayManager::Private`.
  - Updated `setComparisonImage()` to check `if (generation < d->comparisonGenerationHighWater) return;` and monotonically advance `comparisonGenerationHighWater`.
  - Updated `compareWithPinnedOrPrevious()` to maintain `comparisonGenerationHighWater = (std::max)(comparisonGenerationHighWater, d->currentGeneration)`.
  - Updated `updateImage()` so new selection generations immediately advance `comparisonGenerationHighWater`, invalidating obsolete comparison responses.

### Finding 4 (P2): Invalid fit input overwrites remembered valid output state
- **Root Cause**: `DisplayManager::setOutputFitMode()` persisted `mode` into `d->fitModes[d->activeId]` before validation, so invalid modes (e.g. `"bogus"`) corrupted the remembered state and caused reopenings to fall back to default `"shrink"`.
- **Resolution**:
  - Added `isValidFitMode(const QString& mode)` helper checking for `"shrink"`, `"fit"`, `"fill"`, `"width"`, `"height"`.
  - In `setOutputFitMode()`, return immediately without modifying `d->fitModes` if `!isValidFitMode(mode)`.

### Finding 5 (P2): Rebindable dismissal excludes accepted chord bindings
- **Root Cause**: `PresentationWindow::keyPressEvent()` only matched single-key bindings (`bound.count() == 1`), ignoring multi-step chord sequences accepted by `ViewerController::setBinding()` (such as `"Ctrl+K, P"`).
- **Resolution**:
  - Added chord tracking state `QList<QKeyCombination> m_chordPrefix` to `PresentationWindow`.
  - Ignored pure modifier key events (`Qt::Key_Shift`, `Qt::Key_Control`, etc.).
  - Handled multi-step chord progression against `m_toggleBinding()`: intermediate steps are accepted and advance `m_chordPrefix`; matching the final step emits `escapePressed()` and resets state; mismatched keys clear `m_chordPrefix` and pass through.
  - Handled `Escape`: if a chord is in-flight, `Escape` cancels the chord without dismissing the presentation window; if no chord is in-flight, `Escape` dismisses the presentation window.
  - Overrode `focusOutEvent()` and `hideEvent()` to reset `m_chordPrefix`.

---

## 2. Test Execution & Verification

### Targeted Test Suite: `monitor_output_tests` (28 / 28 Passed)
Four new regression test cases directly validating the findings:
- `testHiddenCanvasMustReleaseShedImage()`
- `testOldComparisonCannotOverrideNewCompareCommand()`
- `testInvalidFitMustNotDestroyRememberedFit()`
- `testChordToggleKeyDismissesAndEscapeCancelsChord()`

Full suite output:
```text
********* Start testing of MonitorOutputTests *********
Config: Using QtTest library 6.8.3, Qt 6.8.3 (x86_64-little_endian-llp64 shared (dynamic) release build; by MSVC 2022), windows 11
PASS   : MonitorOutputTests::initTestCase()
PASS   : MonitorOutputTests::testDisplayEnumeration()
PASS   : MonitorOutputTests::testStableIdentity()
PASS   : MonitorOutputTests::testPreferredDisplay()
PASS   : MonitorOutputTests::testOpenCloseLifecycle()
PASS   : MonitorOutputTests::testTogglePresentation()
PASS   : MonitorOutputTests::testRoutingModes()
PASS   : MonitorOutputTests::testMirrorRoutingAndStaleRejection()
PASS   : MonitorOutputTests::testPinnedImageIsolation()
PASS   : MonitorOutputTests::testComparisonMode()
PASS   : MonitorOutputTests::testEscapeDismissal()
PASS   : MonitorOutputTests::testControllerIntegration()
PASS   : MonitorOutputTests::flushDeletes()
PASS   : MonitorOutputTests::testIdentityIdenticalMonitorsWithoutSerialAreDistinct()
PASS   : MonitorOutputTests::testIdentitySerialSurvivesReorderAndReconnect()
PASS   : MonitorOutputTests::testColorSpaceIsNotHardcoded()
PASS   : MonitorOutputTests::testCompareCommandSuppliesSecondImage()
PASS   : MonitorOutputTests::testStalePinAndComparisonRejected()
PASS   : MonitorOutputTests::testOutputFitModeCommandAndPerOutputState()
PASS   : MonitorOutputTests::testAggregateImageBudgetAndCloseRelease()
PASS   : MonitorOutputTests::testScreenRemovedAndAddedPaths()
PASS   : MonitorOutputTests::testFailedDecodeAndRepeatedOpenCloseDoNotLeak()
PASS   : MonitorOutputTests::testEscapeClosesOnceAndToggleKeyIsRebindable()
PASS   : MonitorOutputTests::testHiddenCanvasMustReleaseShedImage()
PASS   : MonitorOutputTests::testOldComparisonCannotOverrideNewCompareCommand()
PASS   : MonitorOutputTests::testInvalidFitMustNotDestroyRememberedFit()
PASS   : MonitorOutputTests::testChordToggleKeyDismissesAndEscapeCancelsChord()
PASS   : MonitorOutputTests::cleanupTestCase()
Totals: 28 passed, 0 failed, 0 skipped, 0 blacklisted, 99ms
********* Finished testing of MonitorOutputTests *********
```

### Related CTest Suite
```text
Test project C:/src/Github/Glancelot/.worktrees/monitor-output/build-monitor-output
    Start  5: viewer_acceptance
1/4 Test  #5: viewer_acceptance ................   Passed    9.10 sec
    Start 18: ui_settings
2/4 Test #18: ui_settings ......................   Passed    0.58 sec
    Start 24: shortcuts_help
3/4 Test #24: shortcuts_help ...................   Passed    0.20 sec
    Start 33: monitor_output
4/4 Test #33: monitor_output ...................   Passed    0.18 sec

100% tests passed out of 4
Total Test time (real) =  10.28 sec
```



### Full Windows Test Gate (`scripts/build.ps1 -Test` under Gate Lock)
```text
Test project C:/src/Github/Glancelot/.worktrees/monitor-output/build-monitor-output
      Start  1: perf_trace
 1/33 Test  #1: perf_trace .......................   Passed    0.11 sec
      Start  2: navigation
 2/33 Test  #2: navigation .......................   Passed    3.27 sec
      Start  3: directory_listing
 3/33 Test  #3: directory_listing ................   Passed   21.23 sec
      Start  4: file_operations
 4/33 Test  #4: file_operations ..................   Passed    0.25 sec
      Start  5: viewer_acceptance
 5/33 Test  #5: viewer_acceptance ................   Passed    8.06 sec
      Start  6: thumbnails
 6/33 Test  #6: thumbnails .......................   Passed    6.97 sec
      Start  7: folder_tree
 7/33 Test  #7: folder_tree ......................   Passed    9.48 sec
      Start  8: pane_layout
 8/33 Test  #8: pane_layout ......................   Passed    1.11 sec
      Start  9: window_slots
 9/33 Test  #9: window_slots .....................   Passed   54.24 sec
      Start 10: folder_grid
10/33 Test #10: folder_grid ......................   Passed  243.78 sec
      Start 11: file_operation_service
11/33 Test #11: file_operation_service ...........   Passed    1.31 sec
      Start 12: directory_model
12/33 Test #12: directory_model ..................   Passed   39.86 sec
      Start 13: viewport
13/33 Test #13: viewport .........................   Passed    0.16 sec
      Start 14: themes
14/33 Test #14: themes ...........................   Passed    0.47 sec
      Start 15: platform_integration
15/33 Test #15: platform_integration .............   Passed    1.04 sec
      Start 16: grid_search
16/33 Test #16: grid_search ......................   Passed    0.36 sec
      Start 17: grid_density
17/33 Test #17: grid_density .....................   Passed    0.26 sec
      Start 18: ui_settings
18/33 Test #18: ui_settings ......................   Passed    0.33 sec
      Start 19: fs_watch
19/33 Test #19: fs_watch .........................   Passed   85.91 sec
      Start 20: fs_watch_budget
20/33 Test #20: fs_watch_budget ..................   Passed  118.41 sec
      Start 21: fs_watch_ui
21/33 Test #21: fs_watch_ui ......................   Passed    3.03 sec
      Start 22: favorites
22/33 Test #22: favorites ........................   Passed    0.62 sec
      Start 23: folder_history
23/33 Test #23: folder_history ...................   Passed    0.21 sec
      Start 24: shortcuts_help
24/33 Test #24: shortcuts_help ...................   Passed    0.20 sec
      Start 25: filter
25/33 Test #25: filter ...........................   Passed    2.75 sec
      Start 26: cache
26/33 Test #26: cache ............................   Passed    4.79 sec
      Start 27: io_scheduler
27/33 Test #27: io_scheduler .....................   Passed    0.32 sec
      Start 28: metadata
28/33 Test #28: metadata .........................   Passed    0.30 sec
      Start 29: animation
29/33 Test #29: animation ........................   Passed    0.49 sec
      Start 30: mutation
30/33 Test #30: mutation .........................   Passed    0.15 sec
      Start 31: copy_move
31/33 Test #31: copy_move ........................   Passed    0.24 sec
      Start 32: format
32/33 Test #32: format ...........................   Passed    0.19 sec
      Start 33: monitor_output
33/33 Test #33: monitor_output ...................   Passed    0.32 sec

100% tests passed out of 33
Total Test time (real) = 612.32 sec
```
