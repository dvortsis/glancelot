# STACK-F040-PINNED review, round 3 (fresh-context Claude reviewer)

**Verdict: APPROVED**

Conditions for the gate, not for the implementer: `reference_pin_native` could not be run to completion in this session (see N-1), so the authoritative gate must show it passing. No blocking findings.

- **Reviewed:** `git diff b3e530e b60b65f` on `claude/stack-f040-pinned` ("fix(f040): transactional output pin, copy and native ctest entry; review round 3"). `b3e530e` is the `origin/main` (b9dc0ec) merge and is excluded. Prior review: round 2, CHANGES REQUIRED, finding R2-1.
- **Environment:** Windows 11, MSVC Release, Qt 6.8.3, detached worktree `.worktrees/review-f040-r3`, build dir `build-review-f040-r3`, built through `build-slot.ps1`, tests run through `gate-lock.ps1`. Whole tree built at `b60b65f` (659 steps, exit 0).

## R2-1: fixed

`pinCurrentImage()` (`src/app/display_manager.cpp:670`) no longer unpins or re-enters `enforceBudget()` with flags. For a withheld frame it sets `capturingWithheld`, emits `currentImageRequested()`, and `updateImage()` (`:598`) stores the controller's reply in `captured*` instead of routing it. The capture is then admitted by `admitPin()` (`:250`) and nothing has changed before that call.

`admitPin()` decides before it touches anything:
- It counts the frames that must stay after the swap: the current frame (the adopted frame when `adoptAsCurrent`), the new pin and a waiting output frame. Shared pixels are counted once through `cacheKey()`. The old pin is not counted because it is being replaced.
- If that exceeds the budget it returns false having changed nothing: old pin, mode, reference and the comparison frame all stay. The status is "Not enough image memory to pin this photo on the output. The output keeps its pinned photo." (without the second sentence when there was no pin).
- Otherwise it swaps the pin in one step (no unpin, no pass through mirror), then makes room in the documented order: previous frame, comparison, reference with `referenceShed`, then a stale non-waiting pending frame. Because the kept set fits, the loop always ends within budget (`Q_ASSERT(fits())` at `:279` is a debug check of that).
- `pinImage()` (`:691`) uses the same function, so the two entry points no longer differ. The old `admittingPin` and `referenceProtected` flags are gone, and `enforceBudget()` has one `holdingPin` rule (`:231`).
- If the controller has no frame yet (decode in flight), nothing changes and the status says to pin again when the photo shows. A capture older than the pin returns silently (see N-3).

Behaviour change, an output pin that does not fit is refused: sensible. The old behaviour dropped the pin silently and left the output in mirror mode, so the user lost both the pin and the explanation. Now the output keeps showing what the user pinned and the status says why. Refusal happens only when the new pin plus current frame plus waiting frame cannot fit, and speculative caches and the reference are shed first in every case that can fit. This matches the shed order in the `enforceBudget()` comment.

Shed notice, one qsTr string: `ui/Main.qml:2029` is the only place that builds it (`qsTr("Reference window closed to free image memory. To reopen it, open %1 and pin it again.").arg(...)`), and the controller's duplicate status connection was removed (`viewer_controller.cpp`, `referenceShed` connect). The test asserts the exact notice text (`QCOMPARE(notice(h), shedNotice("p1.png"))`). "Open a photo first to pin it as a reference." (`Main.qml:726`) is now qsTr as well.

CMake: `ReferencePinTests.cmake` now uses `${GLANCELOT_VIEWER_SOURCES}` and `${GLANCELOT_VIEWER_CONTEXT_SOURCES}`, labels `reference_pin` as `ui process`, and adds `reference_pin_native` only under `if(WIN32)` (`:30-33`) with `ENVIRONMENT_MODIFICATION "QT_QPA_PLATFORM=unset:;QT_QUICK_BACKEND=unset:"` and labels `ui native process`. `ctest -N` lists both; the native run reached the exposure check, so it ran on the native RHI and did not QSKIP.

## Tests: they fail without the fix

Spot check: reverted only `src/app/display_manager.cpp` to `b3e530e` (tests as in `b60b65f`), rebuilt `reference_pin_tests`, ran offscreen:

```
FAIL!  : budgetShedsReferenceBeforeOutputPin()      Actual (dm->pinnedPath()): ""   Expected media.path(1)
FAIL!  : pinnedOutputPhotoIsNeverDropped()          Actual "...the latest frame exceeds the retained-image budget." Expected kKeepsPinned
FAIL!  : pinHeldBackFrameReplacesPinAtomically()    Actual (pins.count()): 2   Expected 1
FAIL!  : pinHeldBackFrameWithSharedReference()      'retainedImageBytes() <= imageBudgetBytes()' returned FALSE
FAIL!  : pinWhileDecodeInFlightKeepsOldPin()        Actual "Output keeps its pinned photo: the latest frame exceeds ..." Expected kPinWaitsForLoad
Totals: 13 passed, 5 failed, 2 skipped
```

The `pins.count()==2` and `retainedImageBytes() > budget` failures are R2-1 itself (pin dropped and re-added; budget overrun with a shared reference). The product file was restored afterwards. The new tests are `reference_pin_tests.cpp:488`, `:521`, `:555` (failure case) and the changed `:264`.

## Actual ctest output at b60b65f (`ctest -R "reference_pin|monitor_output|viewer_acceptance|translation_coverage_ui"`, inside `gate-lock.ps1`)

Quiet machine, second run:
```
1/4 Test  #7: viewer_acceptance ................   Passed   11.49 sec
2/4 Test #37: monitor_output ...................   Passed    2.65 sec
3/4 Test #46: reference_pin ....................   Passed   34.88 sec    (18 passed, 0 failed, 2 skipped: the two native cases)
4/4 Test #47: reference_pin_native .............***Failed   48.01 sec    (18 passed, 2 failed)
```
First run (other builds active on the machine) also had `translation_coverage_ui` Passed (18.88 s) and `viewer_acceptance` Failed: `consecutiveNavigationPreservesStepCount` got `n3.png`, expected `n4.png`. It passed (21/21) on the quiet rerun. That is the known "next" stall on main b9dc0ec, not this packet.

## Findings (none blocking)

- **N-1 (gate to confirm): `reference_pin_native` fails here for an environmental reason.** `nativeTextureReleasedOnClose` (`tests/reference_pin_tests.cpp:602`) and `nativeTextureReleasedOnShed` (`:638`) fail at `QTest::qWaitForWindowExposed(window)`: the reference window is never exposed on the real platform plugin in this session. The round-2 binary (`91fedf3`, built 10/23, which the earlier review and the author ran with these cases passing) fails the same way when run now, so this is not caused by the round-3 delta. The other 18 native cases pass. The GPU texture release path is therefore unverified by me; the gate's `reference_pin_native` must pass. If it fails on the gate machine, treat it as a real finding.
- **N-2 (LOW): `translations/glancelot_en_XA.ts` does not contain the two new `Main.qml` qsTr strings.** The catalog is refreshed by `lupdate` (translations/README.md); `translation_coverage_ui` passes. The lead can regenerate it at integration; it is not a packet defect.
- **N-3 (LOW): a stale capture is dropped without a status.** `pinCurrentImage()` returns silently when `capturedGeneration < pinnedGeneration` (`display_manager.cpp`, after the `image.isNull()` check). The output state is correct (old pin stays); the user just gets no message on that rare path.
- **N-4 (info): the refusal text is a plain status string** in `display_manager.cpp`, like the other routing statuses there, so it is not translatable. Consistent with existing code.

## step() and `next()` experiment (not pushed)

Local, detached: merged `origin/main` (e98ca72, contains EN-11) into `b60b65f` (clean merge, no conflicts) and changed `step()` back to `h.viewer.trigger("next")` (one line). Rebuilt `reference_pin_tests` and ran it 5 times offscreen/software inside the gate lock:

```
run 1: exit 0 Totals: 18 passed, 0 failed, 2 skipped, 0 blacklisted, 37487ms
run 2: exit 0 Totals: 18 passed, 0 failed, 2 skipped, 0 blacklisted, 35353ms
run 3: exit 0 Totals: 18 passed, 0 failed, 2 skipped, 0 blacklisted, 33177ms
run 4: exit 0 Totals: 18 passed, 0 failed, 2 skipped, 0 blacklisted, 36338ms
run 5: exit 0 Totals: 18 passed, 0 failed, 2 skipped, 0 blacklisted, 40691ms
```
5 of 5 passed with `next()`. After merging `origin/main`, `step()` can use `next()` again (the direct `open()` helper is no longer needed for the stall); that is a test-only simplification the implementer can take at the merge or leave. `pinWhileDecodeInFlightKeepsOldPin` still opens the photo directly, which is fine for that case.
