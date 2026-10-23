# STACK-F040-PINNED re-review (fresh-context Claude reviewer, round 2)

**Current verdict (round 2 follow-ups, `91fedf3`): CHANGES REQUIRED.** `pinCurrentImage()` during a held-back frame loses the output pin and exceeds the image budget when a reference shares the pin's pixels, and no test covers that path. See "Round 2 follow-ups" at the end.

**Round 1 verdict (`b610348`): APPROVED. No blocking items.** Five LOW findings below are for the lead to schedule or waive. None of them breaks the frozen F040 contract.

- **Reviewed:** `origin/claude/stack-f040-pinned` at `b610348` ("fix(f040): release hidden reference renderer; review round 1"). Full packet `git diff afeac1a b610348`. Emphasis on round 1, `git diff 974c42e b610348`.
- **Reviewer:** a fresh-context Claude Code (Opus) subagent on the lead's Windows PC. It did not implement this packet and did no earlier review of it. Round 1 was implemented by the lead's local Windows session, so this is not a self-review.
- **Contract:** `docs/handoff/LEAD-WRAPUP-2026-10-22.md` (F040 bullet). Reference windows and the MONITOR-OUTPUT pinned output may coexist as distinct commands. Both take part in the aggregate pixel and texture admission and release accounting. They cause no duplicate decoding, bypass no budget and never change the selected image. Parity tests cover close, pin replacement and recovery while both are active.
- **Lead decisions applied:** `src/render/image_canvas.*` is granted to this packet. Per-window texture admission is accepted for now (the cross-window ledger is E1). The `display_manager.hpp` additions are accepted.
- **Platform:** Windows 11 Pro 10.0.26200, MSVC Release, Qt 6.8.3 (`.tools/Qt/6.8.3/msvc2022_64`), native RHI (Qt's Windows default, D3D11; the graphics API is not printed in the logs) through the threaded render loop, as the `(RT)` lines show. No physical multi-monitor run.
- **Worktree deviation:** `.worktrees/review-f040` already exists. It is the Gemini lane's earlier review worktree (`agent/review-f040`, `16ff381`), and AGENTS.md forbids editing another agent's worktree. I built in `.worktrees/review-f040-r2` (detached at `b610348`) with build directory `build-review-f040` inside it. This document was written on `.worktrees/review-f040-doc`, branch `claude/review-stack-f040`.

## Stage-1 review (round 0, `974c42e`) summary

The stage-1 review was not committed as a document. Its findings are recorded here so this file carries both rounds.

| Severity | Finding |
| --- | --- |
| HIGH | Closing or shedding the hidden reference window did not free its renderer. Qt's persistent scene graph kept the renderer's `QImage` copy, its texture and its `CanvasTextureAdmission` reservation. |
| MEDIUM | The tests could not catch texture leaks, because they ran only on the software backend. |
| LOW | Reference admission ignored `pendingImage`, so a reference pin could shed the output's held frame. |
| LOW | Weak checks in `reference_pin_tests.cpp`. `:203` used the wrong comparison (the output canvas against `referenceImage()`, which was the same frame as the pin). `:197` used `<=` where the claim is equality. `:154` could not detect a leaked old reference, because the output pin held the same frame. |
| LOW | A budget refusal showed "Open a photo first". |

## Round-1 disposition

| Stage-1 item | Status at `b610348` | Evidence |
| --- | --- | --- |
| HIGH renderer not freed | **Fixed** | `releaseWhenHidden` (analysis below). Native D3D11 run: admitted bytes return to 0 on close, on the window's close button and on shed. Reverting the QML property makes both native cases fail (run). |
| MEDIUM no texture-leak test | **Fixed** | `nativeTextureReleasedOnClose` and `nativeTextureReleasedOnShed` run, and are not skipped, on native RHI. They fail without the fix (run). |
| LOW admission ignored pending | **Fixed** | `setReferenceImage` counts the held, comparison and previous frames as essential and refuses rather than sheds. It touches no output status. `referenceNeverShedsOutputHeldFrame` covers it (run). |
| LOW weak checks | **Fixed** | `pinReplacementParity` uses distinct frames and expects exactly `2 * one`, where a leak gives `3 * one`. `recoveryWhileBothActive` uses distinct pins, `== 3 * one`, and compares the reopened output canvas with the pin's own `cacheKey`. |
| LOW wrong notice | **Fixed** | Route on `viewer.imageReady`. `budgetRefusalHasItsOwnNotice` and the notice check in `commandsStayDistinct` cover it (run). |

## Analysis of `releaseWhenHidden` (`src/render/image_canvas.{hpp,cpp}`)

**Opt-in and scope (analysis).**
- The property defaults to `false`. Only `ui/Main.qml`'s `referenceCanvas` sets it.
- The main-window canvases and the C++ presentation canvases never set it, so `watchVisibility` returns before it touches their windows. Their scene graphs stay persistent.
- The native log confirms this: `viewerWindow` still logs "persistent SG, avoiding cleanup" when it hides at teardown (run, see below).
- Setting the property, before or after the item joins its window, calls `setPersistentSceneGraph(false)` on that window and connects `visibleChanged`. Clearing it, or moving to another window, restores `setPersistentSceneGraph(true)` on the old window and disconnects.

**Hide, close and shed (run).**
- When the reference is cleared or shed, `visible: viewer.referenceName.length > 0` goes false and the window hides.
- With a non-persistent scene graph, the threaded loop's `hide()` → `WM_TryRelease` → `invalidateGraphics()` wipes the scene graph. The log shows "invalidating scene graph" and then "persistent GL, avoiding cleanup".
- That deletes the `QQuickRhiItemNode`, which deletes the `ImageRenderer`. `~ImageRenderer` calls `releaseTexture()`. The renderer's `QImage` copy goes with the object.
- Graphics stay persistent, so a re-show only rebuilds the scene graph (run: "persistent GL, avoiding cleanup"). Analysis, from Qt's source rather than these runs: Qt clears `QQuickRhiItem`'s node pointer through `invalidateSceneGraph()`, so the next `updatePaintNode` creates a fresh node and renderer.
- The re-pin path in `nativeTextureReleasedOnClose` exercises this and holds exactly one texture.

**Admission released exactly once (analysis, consistent with the run).**
- `releaseTexture()` releases `reservedBytes` and zeroes it. The destructor, `reset()`, the not-drawable path and the admission-swap path in `synchronize` all go through it, so no path releases twice.
- `CanvasTextureAdmission::release` does not clamp at zero. A double release would therefore show as a negative count, and the tests' final `QTRY_COMPARE(admittedTextureBytes(), 0)` would fail. A leak would show as a count above zero.
- The admission is a `shared_ptr` held by both the canvas and the renderer. A renderer destroyed after its canvas still releases into a live object.

**No use-after-free (analysis).**
- The new code holds the window only through `QPointer<QQuickWindow>`: `m_visibilityWindow`, plus a `QPointer` captured in the lambda. The connection's context object is the canvas, so it drops when either the canvas or the window dies.
- `~ImageRenderer` touches only the texture and the admission `shared_ptr`.
- The full native run of all 12 cases tears down windows that still have a pinned, visible reference. It exits cleanly (run).

**Minimize (analysis, not run).**
- Minimizing does not change `QWindow::visible`, so neither the canvas lambda nor `hide()` runs. The threaded loop only obscures the window (`WM_Obscure`).
- The renderer and its texture therefore stay while the reference window is minimized.
- This looks intended: the reference is still pinned, and its pixels remain counted in the display manager budget. It is not exercised by a test.

**Window destruction (analysis, partly run).**
- `QQuickWindow` destruction releases the scene graph with `inDestructor = true`, whatever the persistence flags.
- If the window dies first, `m_visibilityWindow` becomes null and `watchVisibility(nullptr)` is a no-op.
- If the canvas dies first, the window keeps `persistentSceneGraph == false` (LOW-5). This does not happen in `Main.qml`, where the canvas lives as long as its window.

**Device loss (analysis, not run).**
- Unchanged by this round. The loop tears down the scene graph on device loss, and `initialize()` resets per-device resources when `rhi()` changes (`device != rhi()`).
- The opt-in adds no path that runs during device loss.

**Basic (GUI-thread) render loop (analysis, documented by the implementer).** Its `hide()` never wipes the scene graph, so a hidden reference keeps its texture until it is re-shown or destroyed. Windows D3D11 uses the threaded loop, as the `(RT)` lines in the run log show.

## Analysis of `DisplayManager::setReferenceImage` and `enforceBudget`

**Essential set (analysis).**
- The essential set holds: current, pinned, the new reference, `pendingImage` when `pendingValid`, `comparisonImage` while comparing, and `previousImage` only while it is the visible comparison side (`comparing && comparison null && pinned null`, matching `secondaryImage()`). Duplicates are removed by `cacheKey`. The old reference is excluded, because it is being replaced.
- If the essential set exceeds the budget, the call returns `false` before any member changes. Nothing changes and no signal is emitted.
- Otherwise it drops only frames outside the essential set: the hidden previous frame, comparison when not comparing, and stale pending. It never calls `status()`, `applyImagesToWindow()` or `enforceBudget()`.
- After those drops the retained set equals the essential set, so the `Q_ASSERT` holds by construction and the bound also holds in Release builds.

**Output pin admission (analysis).** `pinImage` sets `referenceProtected`, so an output pin that cannot fit sheds itself, as before, never the reference. `budgetShedsReferenceBeforeOutputPin` covers this (run).

**New current frame (analysis).** A new current frame still goes through `enforceBudget`. The order is previous, pending, comparison, reference, pinned. See LOW-2.

## Findings

### LOW-1. The budget-refusal notice is inaccurate in two cases and offers no next step
- **Where:** `ui/Main.qml:731-734`.
- **Text:** "This photo doesn't fit in image memory beside the photos already shown, so the reference window wasn't opened."
- **Cases where it is wrong:**
  - When a reference window is already open, a refused replacement leaves the old window open with the old photo. "Wasn't opened" is then wrong.
  - When the refusal comes from the output's held (pending) frame, that frame is not being shown. "Already shown" is then wrong.
- **Missing next step:** `application-interface-design` (feedback and states) asks a notice to say what happened and to explain the prerequisite when useful. The text gives no next step.
- **Suggested text:** "Not enough image memory to pin this photo as a reference. Unpin the output photo or close the reference window, then try again." Adjust it for the replace case.
- **Other UI checks:** the `referenceStatus` pill uses existing tokens (`Theme.surface`, `danger`, `radiusCard`, `gapXxs`, `textSmall`), states the error in text rather than color alone, and has an `AlertMessage` role. No other skill concern.

### LOW-2. A held reference can still cause the output to shed its held frame, through the new-current-frame path
- **Where:** `enforceBudget` (`src/app/display_manager.cpp:197-226`).
- **What happens:** when the output is held and a newer current frame arrives, `enforceBudget` sheds `pendingImage` before the reference. It sets "Output holds its last image: the latest frame exceeds the retained-image budget." The reference pin itself no longer does this (round-1 fix), but its retained bytes can still be why a later navigation sheds the held frame.
- **Lead decision:** keep the order (pending is a latest frame, the reference is an explicit pin), or shed the reference before pending while the output is held. It is outside the pin-admission path that stage 1 flagged.

### LOW-3. The `AdmissionFloor` probe is nearly vacuous
- **Where:** `tests/reference_pin_tests.cpp:54-65`, `:380`, `:402`.
- **Why:** `lowest` starts at 0 and is sampled only on the reference window's `frameSwapped`. No frame swaps after the renderer is destroyed, which is exactly when a double release would show.
- **Why it is LOW:** the following `QTRY_COMPARE(admittedTextureBytes(), 0)` already catches a negative count. The probe adds little, but it is not wrong.

### LOW-4. Minimize retention and device loss are not tested
Both are analysis-only above. Physical-display and GPU-loss qualification belongs to MONITOR-VERIFY-01.

### LOW-5. A destroyed canvas leaves its window non-persistent
- **Where:** `~ImageCanvas` does not undo `setPersistentSceneGraph(false)`.
- **Impact:** harmless for `Main.qml`, where the canvas and its window share a lifetime. A future reuse inside a `Loader` would leave the window non-persistent. A one-line restore in the destructor would close it.

## Checks run

### Build
```
> & C:\src\Github\Glancelot\scripts\build-slot.ps1 -Branch review/f040 -Run { & .\scripts\build.ps1 -BuildDir build-review-f040 }
[568/571] Linking CXX executable recovery_controller_tests.exe
[569/571] Linking CXX executable monitor_customization_tests.exe
[570/571] Linking CXX executable monitor_customization_ui_tests.exe
[571/571] Linking CXX executable reference_pin_tests.exe
exit 0
```

### Targeted CTest (inside `gate-lock.ps1 -Branch review/f040 -Run { ... }`)
```
> ctest --test-dir build-review-f040 -R '^(reference_pin|monitor_output|input_ui|monitor_customization_ui|recovery_controller)$' --output-on-failure
Test project C:/src/Github/Glancelot/.worktrees/review-f040-r2/build-review-f040
1/5 Test #13: input_ui .........................   Passed   77.30 sec
2/5 Test #37: monitor_output ...................   Passed    2.48 sec
3/5 Test #40: recovery_controller ..............   Passed   16.64 sec
4/5 Test #43: monitor_customization_ui .........   Passed   30.34 sec
5/5 Test #44: reference_pin ....................   Passed   17.21 sec
100% tests passed out of 5
Total Test time (real) = 144.28 sec
```
`reference_pin` under CTest uses `QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software`, so its two native cases QSKIP there by design.

### Direct native runs (same gate-lock `-Run` block, no platform or backend override)
Environment check written inside the block:
```
env QT_QPA_PLATFORM=[] QT_QUICK_BACKEND=[] QT_LOGGING_RULES=[qt.scenegraph.renderloop=true]
```
I ran the test executable as `build-review-f040\reference_pin_tests.exe -o <file>,txt`. QTest's file logger captures the PASS/FAIL lines and the `qDebug` render-loop lines. Plain stdout redirection captured nothing on the first attempt, although its exit codes (0 / 2 / 0) match the runs below.

**Full suite, native D3D11.** All 12 cases ran; the native cases did not skip. Teardown with a still-pinned, visible reference exited cleanly. No QML warnings.
```
PASS   : ReferencePinTests::initTestCase()
PASS   : ReferencePinTests::commandsStayDistinct()
PASS   : ReferencePinTests::bothPinsShareOneDecodedFrame()
PASS   : ReferencePinTests::closeParity()
PASS   : ReferencePinTests::pinReplacementParity()
PASS   : ReferencePinTests::recoveryWhileBothActive()
PASS   : ReferencePinTests::budgetShedsReferenceBeforeOutputPin()
PASS   : ReferencePinTests::referenceNeverShedsOutputHeldFrame()
PASS   : ReferencePinTests::budgetRefusalHasItsOwnNotice()
PASS   : ReferencePinTests::nativeTextureReleasedOnClose()
PASS   : ReferencePinTests::nativeTextureReleasedOnShed()
PASS   : ReferencePinTests::cleanupTestCase()
Totals: 12 passed, 0 failed, 0 skipped, 0 blacklisted, 15706ms
exit 0
```
With render-loop logging on, QTest's default `-maxwarnings` (2000) cut the debug output off during the first case of this run. The render-loop evidence below therefore comes from the two-case runs.

**Revert check (fail without the fix).**
- I removed `releaseWhenHidden: true` from `ui/Main.qml` in my own review worktree. The harness loads the QML from the source tree at run time, so no rebuild was needed.
- I ran the two native cases, then restored the file in `finally`.
```
releaseWhenHidden occurrences after revert: 0
PASS   : ReferencePinTests::initTestCase()
FAIL!  : ReferencePinTests::nativeTextureReleasedOnClose() Compared values are not the same
   Actual   (canvas->admittedTextureBytes()): 256000
   Expected (qint64(0))                     : 0
FAIL!  : ReferencePinTests::nativeTextureReleasedOnShed() Compared values are not the same
   Actual   (canvas->admittedTextureBytes()): 256000
   Expected (qint64(0))                     : 0
PASS   : ReferencePinTests::cleanupTestCase()
Totals: 2 passed, 2 failed, 0 skipped, 0 blacklisted, 40203ms
exit 2
porcelain after restore: []
```
Render loop on reference close with the property reverted:
```
hide() QQuickWindowQmlImpl_QML_199 (referenceWindow)
(RT) WM_Obscure
releaseResources() in api-call QQuickWindowQmlImpl_QML_199
(RT) WM_TryRelease
(RT) - setting exit flag and invalidating
(RT) invalidateGraphics()
(RT) - persistent SG, avoiding cleanup
```

**Restored (pass with the fix).**
```
PASS   : ReferencePinTests::initTestCase()
PASS   : ReferencePinTests::nativeTextureReleasedOnClose()
PASS   : ReferencePinTests::nativeTextureReleasedOnShed()
PASS   : ReferencePinTests::cleanupTestCase()
Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 3605ms
exit 0
```
Render loop with the fix. All three reference hides after the window was first exposed log the same sequence: `reference-close`, the window's close button, and the hide before re-pin.
```
hide() QQuickWindowQmlImpl_QML_199 (referenceWindow)
(RT) WM_Obscure
releaseResources() in api-call QQuickWindowQmlImpl_QML_199
(RT) WM_TryRelease
(RT) - setting exit flag and invalidating
(RT) invalidateGraphics()
(RT) - invalidating scene graph
(RT) - persistent GL, avoiding cleanup
```
The main `viewerWindow` still logs "persistent SG, avoiding cleanup" on its own hide at teardown (two occurrences), so the main canvas is unaffected. The `nativeTextureReleasedOnShed` render-loop lines fell past the `-maxwarnings` cutoff. Its pass, and its failure when reverted, are the evidence for the shed path.

## Not verified
- Minimize, device loss and the basic render loop. These are analysis only.
- Pane migration through the pre-existing `windowChanged` release. Unchanged and not re-run.
- The full `scripts/build.ps1 -Test` suite and `docker build`. They are the integrator's gate.
- Physical multi-monitor, mixed-DPI and hardware runs.


## Round 2 follow-ups (`91fedf3`)

**Verdict: CHANGES REQUIRED.** One blocking item (R2-1). LOW-1, LOW-2 and LOW-5 are otherwise addressed, and their new tests fail without the product changes.

- **Reviewed:** `git diff b610348 91fedf3` on `claude/stack-f040-pinned` ("fix(f040): refusal notices, shed order and canvas persistence; review round 2").
- **Reviewer and platform:** as above. I built and ran in `.worktrees/review-f040-r2`, checked out at `91fedf3`, with an incremental build through `build-slot.ps1`.
- **Lead decision applied:** a photo pinned on the output is never dropped for a newer main frame. The newer frame is held back with a status and fetched again when the pin is released.

### R2-1 (blocking): pinning while a frame is held back can lose the output pin and exceed the budget, and it is untested

**Where:** `DisplayManager::pinCurrentImage()` (`src/app/display_manager.cpp`, the `currentWithheld` branch).
- That branch sets `admittingPin` and `referenceProtected`, then emits `currentImageRequested` synchronously.
- The controller re-routes the frame through `updateImage()`, which runs `enforceBudget()`.
- Because `admittingPin` is set, `holdingPin` is false, so `enforceBudget()` drops the **old** pin and switches the output to mirror before `pinImage()` admits the new one.
- With the reference protected and sharing the old pin's pixels, the new pin then cannot fit either. It is refused too.

**Reproduced (run).** I added two scratch cases to my review worktree only. They are not committed, and I restored the file afterwards. Run offscreen/software against the `91fedf3` product:
```
QINFO  : ReferencePinTests::scratchPinCurrentWhileWithheld() pinnedPathChanged QList(QList(QVariant(QString, "")), QList(QVariant(QString, ".../p2.png"))) modeChanges 2
PASS   : ReferencePinTests::scratchPinCurrentWhileWithheld()
QINFO  : ReferencePinTests::scratchPinCurrentWhileWithheldWithReference() after pin: pinnedPath "" mode "mirror" reference "p1.png" retained/one 2 budget/one 1.00006 status "Following controller"
FAIL!  : ReferencePinTests::scratchPinCurrentWhileWithheldWithReference() 'dm->retainedImageBytes() <= glancelot::DisplayManager::imageBudgetBytes()' returned FALSE. (budget exceeded)
Totals: 3 passed, 1 failed, 0 skipped, 0 blacklisted, 10677ms
```

**Required tests (exact scenarios), in `tests/reference_pin_tests.cpp`:**

1. **Pin the held-back frame, no reference.**
   - Setup: 2 generated photos. Run `presentation-toggle` and `presentation-pin` (pins p1). Set `setImageBudgetBytes(one + 16)`. `step` to p2.
   - Precondition: `routingStatus()` reads "Output keeps its pinned photo: …".
   - Action: `trigger("presentation-pin")`.
   - Expect: `pinnedPath() == p2`, and `presentationMode() == "pinned"`. The output's primary canvas `cacheKey` equals `viewer.image()`. `retainedImageBytes() == one`. The status no longer reads "keeps its pinned photo". The selection is unchanged.
   - Also expect no transient unpin: `pinnedPathChanged` fires once (with p2) and `presentationModeChanged` does not pass through mirror. Today it emits `""` and then p2, with 2 mode changes.

2. **Pin the held-back frame while a reference shares the pin.**
   - Setup: as above, but use `pinBoth` first, so the reference and the output pin share p1. After `step` to p2, the reference stays (`referenceName() == "p1.png"`) and `retainedImageBytes() == one`.
   - Action: `trigger("presentation-pin")`.
   - Expect: `retainedImageBytes() <= imageBudgetBytes()`, and the output is still in `pinned` mode.
   - Pick one outcome and assert it. Either the new pin p2 takes hold and the reference is shed with its reopen notice (this follows the round-2 order: the reference goes before refusing), or the old pin p1 stays with a status that says the pin was refused. **Today:** `pinnedPath() == ""`, mode `mirror`, `retainedImageBytes() == 2 * one`.

3. **Failure case: pin while a decode is in flight.**
   - Action: trigger `presentation-pin` while the frame is held back and the controller is loading the next photo. The `currentImageRequested` handler ignores the request while `loading`, so `pinCurrentImage()` returns silently.
   - Expect: the old pin stays, with a status or notice saying the pin waits for the photo to finish loading. Today there is no feedback.

**Fix direction (implementer's choice):**
- Decide admission before touching the old pin. Fetch the withheld frame without the `admittingPin` drop, then swap the pin atomically: either shed the reference first, or refuse and keep the old pin.
- Do not set `referenceProtected` here, or the protection contradicts the documented order, which sheds the reference before refusing.

### LOW-1 notice copy (application-interface-design: feedback and states)

Read for this round. The copy now says what did and did not happen, names the actual cause, and gives one next step. "Follow controller selection" matches the label in `ui/MonitorSettings.qml`. Remaining nits, none blocking:

- **R2-2 (LOW).** "The current one stays." In this app "current" also means the main window's photo. Suggest "The open reference stays."
- **R2-3 (LOW).** The shed notice text exists twice: in `ui/Main.qml` (`onReferenceShed`, plain English, no `qsTr`) and in the controller (`QCoreApplication::translate`). The two can drift, and the QML copy escapes F054 localization. Suggest one source, for example a controller-built string passed with the signal, or `qsTr` with an `arg`.
- **R2-4 (LOW).** "Output keeps its pinned photo: the latest frame exceeds the retained-image budget." "Retained-image budget" is internal wording. It matches the existing "Output holds its last image: …" status, so it is consistent, but both could say "image memory" like the new notices.

### LOW-2 and LOW-5 (analysis)

- **LOW-2.** `enforceBudget()` now sheds in this order:
  1. The previous frame, and a comparison frame the output is not showing.
  2. The reference, skipped when it shares pixels with the current, pinned or waiting frame.
  3. The waiting frame, with its existing status.
  4. A comparison being admitted.
  5. A pin being admitted.
  6. Otherwise, hold back the new main frame while the output is pinned.

  This matches the lead decision. `restoreWithheldCurrent()` is queued, so it does not re-enter a budget pass. `closePresentation()` clears the withheld flag. `updateImage()` keeps the "keeps its pinned photo" status while the frame is withheld. The only defect found is R2-1.
- **LOW-5.** `~ImageCanvas` restores `setPersistentSceneGraph(true)` through the `QPointer`. When the window is mid-destruction, the setter only writes a flag, so this is safe.

### Fail before, pass after (run)

I reverted the product hunks in my review worktree, rebuilt incrementally, ran offscreen/software, then restored with `git checkout 91fedf3 -- <files>`. Afterwards `git status --porcelain` was empty and HEAD was `91fedf3`.

**LOW-1 reverted** (`src/app/viewer_controller.cpp` and `ui/Main.qml` back to `b610348`):
```
FAIL!  : ReferencePinTests::budgetRefusalHasItsOwnNotice() Compared values are not the same
   Actual   (notice(h))                      : "This photo doesn't fit in image memory beside the photos already shown, so the reference window wasn't opened."
   Expected (joined(kOpenRefused, kTooLarge)): "Not enough image memory to open the reference window. This photo is too large for the image memory budget."
FAIL!  : ReferencePinTests::refusalNoticeNamesThePinnedOutput() Compared values are not the same
FAIL!  : ReferencePinTests::refusalNoticeNamesTheComparison() Compared values are not the same
FAIL!  : ReferencePinTests::refusalNoticeForReplacementNamesTheHeldFrame() Compared values are not the same
FAIL!  : ReferencePinTests::heldOutputKeepsItsFrameReferenceIsShed() Compared values are not the same
   Actual   (notice(h)) : ""
   Expected (shedNotice): "Reference window closed to free image memory. To reopen it, open p2.png and pin it again."
Totals: 2 passed, 5 failed, 0 skipped, 0 blacklisted, 12457ms
```

**LOW-2 reverted** (`src/app/display_manager.cpp` back to `b610348`):
```
FAIL!  : ReferencePinTests::heldOutputKeepsItsFrameReferenceIsShed() Compared values are not the same
   Actual   (shed.count()): 0
   Expected (1)           : 1
FAIL!  : ReferencePinTests::pinnedOutputPhotoIsNeverDropped() Compared values are not the same
   Actual   (dm->pinnedPath()): ""
   Expected (media.path(0))   : ".../p1.png"
Totals: 2 passed, 2 failed, 0 skipped, 0 blacklisted, 5876ms
```

### Checks at `91fedf3` (inside `gate-lock.ps1 -Branch review/f040 -Run { ... }`)
```
head 91fedf3 porcelain []
> ctest --test-dir build-review-f040 -R '^(reference_pin|monitor_output|input_ui|monitor_customization_ui)$' --output-on-failure
1/4 Test #13: input_ui .........................   Passed   70.76 sec
2/4 Test #37: monitor_output ...................   Passed    2.52 sec
3/4 Test #43: monitor_customization_ui .........   Passed   27.57 sec
4/4 Test #44: reference_pin ....................   Passed   26.46 sec
100% tests passed out of 4
Total Test time (real) = 127.60 sec
```

Direct native run in the same block, with no `QT_QPA_PLATFORM` or `QT_QUICK_BACKEND`, and with `QT_LOGGING_RULES=qt.scenegraph.renderloop=true` and `-maxwarnings 0`:
```
PASS   : ReferencePinTests::initTestCase()
PASS   : ReferencePinTests::commandsStayDistinct()
PASS   : ReferencePinTests::bothPinsShareOneDecodedFrame()
PASS   : ReferencePinTests::closeParity()
PASS   : ReferencePinTests::pinReplacementParity()
PASS   : ReferencePinTests::recoveryWhileBothActive()
PASS   : ReferencePinTests::budgetShedsReferenceBeforeOutputPin()
PASS   : ReferencePinTests::referenceNeverShedsOutputHeldFrame()
PASS   : ReferencePinTests::budgetRefusalHasItsOwnNotice()
PASS   : ReferencePinTests::refusalNoticeNamesThePinnedOutput()
PASS   : ReferencePinTests::refusalNoticeNamesTheComparison()
PASS   : ReferencePinTests::refusalNoticeForReplacementNamesTheHeldFrame()
PASS   : ReferencePinTests::heldOutputKeepsItsFrameReferenceIsShed()
PASS   : ReferencePinTests::pinnedOutputPhotoIsNeverDropped()
PASS   : ReferencePinTests::nativeTextureReleasedOnClose()
PASS   : ReferencePinTests::nativeTextureReleasedOnShed()
PASS   : ReferencePinTests::cleanupTestCase()
Totals: 17 passed, 0 failed, 0 skipped, 0 blacklisted, 26423ms
exit 0
```

With `-maxwarnings 0`, the whole log was kept this time:
- 55 lines of "invalidating scene graph", 4 of them inside `nativeTextureReleasedOnShed`, so the shed path's scene graph release is now logged.
- 40 lines of "persistent SG, avoiding cleanup", from the main window, which keeps its persistent scene graph.
- No QML warnings.