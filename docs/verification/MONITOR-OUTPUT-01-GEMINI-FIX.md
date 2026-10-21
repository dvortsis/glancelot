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
