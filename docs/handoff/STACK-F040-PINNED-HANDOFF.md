# STACK-F040-PINNED handoff

Track session S5 (stack viewer), Phase 0 cycle 1. Contract: LEAD-WRAPUP-2026-10-22.md (F040 bullet) and LEAD-PLAN-2026-10-22.md section 5. Not self-approved; a fresh reviewer decides.

| | |
| --- | --- |
| Branch | `claude/stack-f040-pinned` |
| Base SHA | `afeac1a` (`origin/main`, "chore(lead): freeze Phase 0 cycle 1 contracts") |
| Implementation tip | `b13f6c1` (review diff: `git diff afeac1a b13f6c1`; this handoff is the commit after it) |
| Commits | `27ef1ab` fix(f040), `b13f6c1` cmake: provisional target for STACK-F040-PINNED |

## What changed

The reference window (`reference-pin` / `reference-close`) and the MONITOR-OUTPUT pinned output (`presentation-pin` / `presentation-close`) stay separate commands with separate labels and state. The reference is now a second explicit pin held by `DisplayManager`, so both pins go through the same retained-pixel accounting.

- **One decoded frame.** `pinReference()` hands `DisplayManager::setReferenceImage()` the frame the canvas already shows, with the controller's selection generation. Nothing is decoded again. `retainedBytes()` counts the reference and deduplicates by `cacheKey()`, so the same photo pinned both ways costs one image.
- **Admission.** A reference that cannot fit beside the current and pinned-output frames (after optional history is shed) is refused: it returns false and nothing changes. An output pin never evicts the reference. `referenceProtected` is set while either pin is admitted, and the output pin is shed as before.
- **Shedding for a newer current frame.** The current frame is never dropped. Order: previous, pending, comparison, reference, pinned output. The reference goes before the output pin because losing it closes a local window with a notice. Losing the output pin would silently change what an audience display shows. `referenceShed(path)` makes the controller set the status "The reference window for <name> closed to keep image memory within its budget."
- **Stale requests.** A reference whose generation is below the high-water mark is refused, even after a clear.
- **Selection unchanged.** The reference APIs never touch the current frame, the output mode, the output pin, `currentImageRequested` or `imageRouted`.
- **Texture release (corrected in review round 1).** The original claim here was wrong. Calling `releaseResources()` on the hidden reference window freed nothing: Qt 6.8.3 keeps a hidden window's scene graph by default ("persistent SG, avoiding cleanup"), so the renderer kept its `QImage` copy, its texture and its `CanvasTextureAdmission` reservation. The reference canvas now sets `releaseWhenHidden: true`. That makes its window's scene graph non-persistent, so hiding the window wipes the scene graph and destroys the renderer, which frees all three. See "Review round 1 fixes".
- **Visible refusal.** When the reference canvas refuses a texture admission, the window shows `referenceCanvas.rendererError` in a status pill. It uses the main window's pill tokens (`Theme.surface`, `Theme.danger`, `radiusCard`, `gapLg`, `textSmall`, `Accessible.AlertMessage`), so the window is no longer blank.

### Files

| File | Change |
| --- | --- |
| `include/glancelot/display_manager.hpp` | Shared header. Adds the reference slot API and two signals. The exact diff is below. |
| `src/app/display_manager.cpp` | Reference storage, admission, shed order, `referenceProtected` during output-pin admission |
| `src/app/viewer_controller.cpp` | Reference section (`referenceImage`, `referenceName`, `pinReference`, `clearReference`) delegates to `DisplayManager`. The `QImage reference` and `referenceLabel` members are removed. Two `connect`s sit next to the existing display-manager connections in `Private()`, about lines 407-412. |
| `ui/Main.qml` | Only the reference-window block (about lines 2041-2069): status pill and `onVisibleChanged` release (round 1 replaced the release with `releaseWhenHidden` and also changed the `reference-pin` notice). The F054 `rtl` leftovers and the `}    FileDialog {` line are untouched, so EN-06 can repair them without conflicting with this change. |
| `tests/reference_pin_tests.cpp` | New |
| `cmake/ReferencePinTests.cmake`, `CMakeLists.txt` (one `include` line after `MonitorCustomization.cmake`, before `glancelot_attach_recovery_consumers()`) | Provisional target |

`include/glancelot/viewer_controller.hpp` is unchanged. Its F040 comment ("shares its pixels, so it costs no decode or memory beyond the one image") is now only true while the reference is the current photo. **Lead request:** reword it when the header is next open, for example: "held in the display manager's retained-image budget; it may be released for a newer frame."

### Exact shared-header diff

```diff
--- a/include/glancelot/display_manager.hpp
+++ b/include/glancelot/display_manager.hpp
@@ -139,6 +139,14 @@ public:
     Q_INVOKABLE void setComparisonImage(const QString& path, const QImage& image, quint64 generation);
     // Comparison against the pinned image, else the previously shown image.
     Q_INVOKABLE void compareWithPinnedOrPrevious();
+    // F040 reference window: a second explicit pin, distinct from the output's pinned mode, held in the same
+    // aggregate retained-image budget. Callers pass an already decoded frame (shared, never re-decoded). Refused
+    // (false, nothing changes) when stale or when it cannot fit beside the current and pinned-output frames.
+    // Never changes the current frame, the output mode or the pinned output.
+    bool setReferenceImage(const QString& path, const QImage& image, quint64 generation);
+    void clearReferenceImage();
+    QImage referenceImage() const;
+    QString referencePath() const;
 
     // Per-output fit mode, remembered by display id and restored when that output reopens.
     Q_INVOKABLE void setOutputFitMode(const QString& mode);
@@ -174,6 +182,9 @@ signals:
     void pinnedPathChanged(const QString& path);
     void presentationClosed();
     void imageRouted(const QString& path, quint64 generation);
+    void referenceChanged();
+    // The budget released the reference to admit a newer current frame; referenceChanged follows.
+    void referenceShed(const QString& path);
     void outputColorStatusChanged();
 
 private:
```

## Requested CMake target (S1 / lead)

`cmake/ReferencePinTests.cmake`, included from root `CMakeLists.txt` right after `include(cmake/MonitorCustomization.cmake)` and before `glancelot_attach_recovery_consumers()`:

- Target `reference_pin_tests`. Sources: `tests/reference_pin_tests.cpp`, `src/app/viewer_controller.cpp`, `settings_file.cpp`, `pane_layout.cpp`, `directory_entry_model.cpp`, `theme_manager.cpp`, `filmstrip_model.cpp`, `folder_tree_model.cpp`, `file_operation_service.cpp`, `src/render/image_canvas.cpp`, plus `glancelot_attach_monitor_customization()`. This is the same set as `input_ui_tests`, so EN-05's `GLANCELOT_VIEWER_SOURCES` can replace the viewer part.
- Includes `include src/render`. `GLANCELOT_TEST_SOURCE_DIR`, theme catalog, fonts, brand resources and canvas shaders, all as for `input_ui_tests`. Depends on `glancelot-decoder`.
- Test `reference_pin`. Environment `QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software`, `TIMEOUT 240` (it takes about 5 s). Suggested EN-03 labels: `ui`, `fast`.

## Tests

`tests/reference_pin_tests.cpp` loads the shipped `ui/Main.qml` through `tests/ui_test_support.hpp` with generated PNGs. Commands go through `viewer.trigger()`, so they use real QML routing. Every case checks that the selection (index, path, frame `cacheKey`) is unchanged.

| Case | Covers |
| --- | --- |
| `commandsStayDistinct` | Four registry ids with distinct labels. Failure case: pinning with nothing decoded returns false, emits nothing and opens no window. |
| `bothPinsShareOneDecodedFrame` | Reference, output pin, reference canvas and output canvas share one `cacheKey`. `retainedImageBytes() == one image`. |
| `closeParity` | `reference-close` leaves the output pin. `presentation-close` leaves the reference. Unpinning the output leaves the reference. The window's close button clears only the reference. Byte accounting is exact after each step. |
| `pinReplacementParity` | Replacing the reference does not emit `pinnedPathChanged` and frees the old reference frame. Replacing the output pin does not emit `referenceChanged`. Stale (generation 0) requests are refused by both pins. |
| `recoveryWhileBothActive` | Low-memory `releaseCachedImages()` keeps both pins. `monitor-recover` closes the output and keeps the reference window and the output pin. Reopening the output shows its pin. |
| `budgetShedsReferenceBeforeOutputPin` | Budget of 2 images + 16 bytes. A newer current frame sheds the reference (signal, notice, window hidden) and keeps the output pin. Re-pinning the current frame is admitted at no extra cost. Failure: an oversize reference is refused and the old one stays. An oversize output pin is shed without evicting the reference. |

### Environment

Ubuntu 24.04 cloud container, GCC 13.3, Ninja, Release. Qt 6.8.3 came from conda-forge (`qt6-main 6.8.3`) because `download.qt.io` and its mirrors are blocked by this session's network policy. Only aqt is in the cycle doc's recipe. `CMAKE_BUILD_RPATH=$HOME/qtenv/lib`.

### Targeted run (after the final commit)

```
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build-claude-stack-f040-pinned -R "^(reference_pin|monitor_output)$"
1/2 Test #37: monitor_output ...................   Passed    2.04 sec
2/2 Test #44: reference_pin ....................   Passed    4.61 sec
100% tests passed, 0 tests failed out of 2
Total Test time (real) =   6.66 sec
```

`reference_pin-results.txt`: initTestCase, commandsStayDistinct, bothPinsShareOneDecodedFrame, closeParity, pinReplacementParity, recoveryWhileBothActive, budgetShedsReferenceBeforeOutputPin and cleanupTestCase all PASS (8 passed, 0 failed). The run printed no QML warnings apart from the expected "No QRhi found" from the software backend.

`input_ui` `referenceWindowKeepsItsPhoto` (the original F040 test) passes on this branch.

### Full offscreen suite (once, at `b13f6c1`)

```
$ QT_QPA_PLATFORM=offscreen ctest --test-dir build-claude-stack-f040-pinned --output-on-failure
...
11/44 Test #11: window_slots .....................   Passed   15.39 sec
12/44 Test #12: folder_grid ......................***Failed  163.79 sec
13/44 Test #13: input_ui .........................***Failed   69.65 sec
14/44 Test #14: pathbar_ui .......................   Passed    2.37 sec
16/44 Test #16: directory_model ..................***Failed    6.34 sec
23/44 Test #23: fs_watch .........................   Passed   70.28 sec
24/44 Test #24: fs_watch_budget ..................   Passed   20.30 sec
30/44 Test #30: cache ............................   Passed    2.28 sec
37/44 Test #37: monitor_output ...................   Passed    2.09 sec
38/44 Test #38: recovery .........................   Passed   11.15 sec
40/44 Test #40: recovery_controller ..............   Passed   12.33 sec
41/44 Test #41: recovery_process .................   Passed   27.93 sec
42/44 Test #42: monitor_customization ............   Passed    6.52 sec
43/44 Test #43: monitor_customization_ui .........   Passed   16.45 sec
44/44 Test #44: reference_pin ....................   Passed    5.54 sec
93% tests passed, 3 tests failed out of 44
Total Test time (real) = 458.98 sec
```

The other 29 tests passed. All three failures are single cases. Each reproduces deterministically at the base SHA `afeac1a`, built separately in `.worktrees/base-afeac1a`:

| Test | Failing case | On this branch (run 1 / rerun) | At base `afeac1a` |
| --- | --- | --- | --- |
| `folder_grid` | `GridUiTests::externalRenameAndDeleteRefreshKeepSelection` "Compared lists have different sizes" | fail / fail | fail (ctest and direct run) |
| `directory_model` | `DirectoryModelTests::selectionSurvivesRelistSortAndDropsRemovedKeys`: selectedPaths has 2 entries, 1 expected | fail / fail | fail |
| `input_ui` | `InputUiTests::treeRowAndPathBarMenusOpenAndMatch` `menuOpen(rowMenu)` false | fail / fail (3 isolated runs) | fail (base sources at the same build) |

The first two depend on how this container's filesystem reports rename identity (`birthTime`). The third is a context-menu open on the offscreen/software platform with conda Qt. None of them touch display-manager or reference code. The authoritative Windows gate decides.

## Unverified

- **GPU texture release on close** was unverified in the original handoff, and it did not work. Review round 1 fixed it and verified it on Windows D3D11 with the threaded render loop (see below). The status pill for a refused admission is still only exercised structurally (no QML errors, hidden when `rendererError` is empty).
- **Not done:** cross-window texture admission. The reference canvas keeps its own per-canvas `CanvasTextureAdmission` (256 MiB, the same bound as one presentation window). It is now released on close. Sharing one admission between the reference canvas and the presentation canvases would need a QML-visible hook in `src/render/image_canvas.hpp`, which is not in this packet's grant. `canvas_texture_admission.hpp` therefore needed no change. **Lead decision:** accept per-window texture admission plus aggregate pixel accounting as meeting the contract, or grant `image_canvas.hpp` for a follow-up.
- **Notice routing:** fixed in review round 1. The original claim that a UI pin is never refused by the budget was wrong: a held output frame or a small budget can refuse it.
- **UI design skills:** the `ui-design-skills` (application-interface-design and the others) are **not available** in this cloud session. The small QML addition reuses the existing status-pill pattern and theme tokens; no new visual language.
- No physical multi-monitor or hardware run.

## Review round 1 fixes

Implementer for this round: the lead's local Windows session (the original cloud implementer could not be reached). This is not a self-review; a fresh reviewer re-reviews. Base for the round: `974c42e`.

### Causes

1. **Closing or shedding the reference freed no GPU memory (high).** Qt 6.8.3 keeps a hidden window's scene graph by default. The QML `releaseResources()` call on hide reached the render thread as "persistent SG, avoiding cleanup". The renderer kept its own `QImage` copy (refreshed only during sync), its texture and its `CanvasTextureAdmission` reservation, so `retainedImageBytes()` reported bytes as freed while the GPU side still held them.
2. **Reference admission ignored the output's held frame.** `setReferenceImage()` counted only the current and pinned frames, then ran `enforceBudget()`. That shed `pendingImage` (the frame a held output shows on resume) and changed the output status to "Output holds its last image...".
3. **A budget refusal showed "Open a photo first".**
4. **Weak test checks.** `:154` could not see a leaked old reference because the output pin held the same frame. `:197` used `<=` where the claim is equality. `:203` compared the output canvas with `referenceImage()`, which was the same frame as the pin.

### Changes

| File | Change |
| --- | --- |
| `src/render/image_canvas.hpp`, `.cpp` (lead grant) | New opt-in property `releaseWhenHidden` (default false). When true, the canvas calls `setPersistentSceneGraph(false)` on its window, both when the property is set and when the item changes windows, and calls `releaseResources()` when the window hides. A non-persistent scene graph is wiped on hide: the `QQuickRhiItem` node and the `ImageRenderer` are destroyed on the render thread, and `releaseTexture()` frees the texture, the renderer's `QImage` copy and the admission reservation together. Graphics stay persistent (`setPersistentGraphics` is not touched), so re-showing rebuilds only the scene graph. Main-window and presentation canvases are unchanged. |
| `ui/Main.qml` | `referenceCanvas` sets `releaseWhenHidden: true`. The window's `onVisibleChanged: releaseResources()` is removed. The `reference-pin` route shows its own notice when a decoded photo is refused: "This photo doesn't fit in image memory beside the photos already shown, so the reference window wasn't opened." |
| `src/app/display_manager.cpp` | `setReferenceImage()` no longer calls `enforceBudget()`. Admission counts these frames as essential: current, pinned, the held `pendingImage` (when `pendingValid`), the comparison frame while comparing, and the previous frame while it is the visible comparison side. If they do not fit, the reference is refused and nothing changes. Otherwise only history the output is not showing is dropped. Admission never changes the output's frames or its routing status. |
| `include/glancelot/viewer_controller.hpp` | Comments only: the F040 note and `pinReference()`'s return value (lead request). |
| `tests/reference_pin_tests.cpp` | New: `referenceNeverShedsOutputHeldFrame`, `budgetRefusalHasItsOwnNotice`, `nativeTextureReleasedOnClose`, `nativeTextureReleasedOnShed`. The native cases QSKIP when the window's renderer is not RHI-based (software backend). Strengthened: `pinReplacementParity` (reference p1, output pin p2, current p3; expects exactly `2 * one`, while a leak would give `3 * one`), `recoveryWhileBothActive` (distinct pins; `retainedImageBytes() == 3 * one`; the reopened output canvas is compared with the pin's own `cacheKey`), and `commandsStayDistinct` (checks the "Open a photo first" notice). |

The native cases check three things before any close or shed: the window is exposed, the frame is `Format_RGBA8888`, and `admittedTextureBytes() == one`. After that, close, the window's own close button and a shed must each bring admitted bytes to 0. Re-pinning and pin replacement must hold exactly one texture. A `frameSwapped` probe on the render thread records the lowest admitted value, which must never go below 0.

The lead accepts per-window texture admission for now; the cross-window ledger is E1. The shared-header additions in `display_manager.hpp` are unchanged from the quote above.

### Fail before, pass after

**Before**: corrected tests built against the round-0 sources and QML (`reference_pin_tests_before.exe`, the same build directory).

Offscreen/software (`QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`):

```
FAIL!  : ReferencePinTests::referenceNeverShedsOutputHeldFrame() '!dm->setReferenceImage(media.path(2), extra, quint64(1) << 40)' returned FALSE. ()
FAIL!  : ReferencePinTests::budgetRefusalHasItsOwnNotice() '!notice(h).contains("Open a photo first")' returned FALSE. (Open a photo first to pin it as a reference.)
SKIP   : ReferencePinTests::nativeTextureReleasedOnClose() GPU texture admission needs a native RHI backend; the software backend has none.
SKIP   : ReferencePinTests::nativeTextureReleasedOnShed() GPU texture admission needs a native RHI backend; the software backend has none.
Totals: 8 passed, 2 failed, 2 skipped, 0 blacklisted, 19596ms
```

Native D3D11, threaded render loop (no platform/backend override, `QT_LOGGING_RULES=qt.scenegraph.renderloop=true`):

```
FAIL!  : ReferencePinTests::nativeTextureReleasedOnClose() Compared values are not the same
   Actual   (canvas->admittedTextureBytes()): 256000
   Expected (qint64(0))                     : 0
FAIL!  : ReferencePinTests::nativeTextureReleasedOnShed() Compared values are not the same
   Actual   (canvas->admittedTextureBytes()): 256000
   Expected (qint64(0))                     : 0
Totals: 2 passed, 2 failed, 0 skipped, 0 blacklisted, 43110ms
```

Render loop on reference close, before the fix:

```
hide() QQuickWindowQmlImpl_QML_205(referenceWindow)
releaseResources() in api-call QQuickWindowQmlImpl_QML_205(referenceWindow)
                    (RT) WM_TryRelease
                    (RT) - setting exit flag and invalidating
                    (RT) invalidateGraphics()
                    (RT) - persistent SG, avoiding cleanup
```

**After** (this commit, MSVC Release, `build-claude-f040`), run inside the machine gate lock (`gate-lock.ps1 -Branch claude/f040-fix`):

```
> ctest --test-dir build-claude-f040 -R '^(reference_pin|monitor_output|input_ui|monitor_customization_ui|recovery_controller)$' --output-on-failure
1/5 Test #13: input_ui .........................   Passed   81.58 sec
2/5 Test #37: monitor_output ...................   Passed    2.72 sec
3/5 Test #40: recovery_controller ..............   Passed   14.85 sec
4/5 Test #43: monitor_customization_ui .........   Passed   22.83 sec
5/5 Test #44: reference_pin ....................   Passed   14.51 sec
100% tests passed out of 5
Total Test time (real) = 136.81 sec
```

Direct offscreen/software run: `Totals: 10 passed, 0 failed, 2 skipped` (the two native cases QSKIP).

Direct native run (`reference_pin_tests.exe nativeTextureReleasedOnClose nativeTextureReleasedOnShed`, no offscreen or software environment):

```
PASS   : ReferencePinTests::initTestCase()
PASS   : ReferencePinTests::nativeTextureReleasedOnClose()
PASS   : ReferencePinTests::nativeTextureReleasedOnShed()
PASS   : ReferencePinTests::cleanupTestCase()
Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 3587ms
```

Render loop on reference close, after the fix. The main `viewerWindow` still logs "persistent SG, avoiding cleanup" when it hides at teardown, so its scene graph stays persistent:

```
hide() QQuickWindowQmlImpl_QML_205(referenceWindow)
                    (RT) WM_TryRelease
                    (RT) - setting exit flag and invalidating
                    (RT) invalidateGraphics()
                    (RT) - invalidating scene graph
                    (RT) - persistent GL, avoiding cleanup
```

The `releaseResources()` call that the canvas makes on `visibleChanged` reaches the threaded loop before `hide()`, so it logs "not releasing because window is still active". The release comes from `hide()` itself on the now non-persistent scene graph. The extra call is harmless.

### Still unverified or not done

- **Basic (GUI-thread) render loop.** Its `hide()` never wipes the scene graph, whatever the persistence flags, and its `releaseResources()` only drops caches. On that loop (opt-in, for example OpenGL on Windows) the hidden reference renderer still holds its texture until the window is shown again or destroyed. Windows D3D11 uses the threaded loop.
- **Pane migration.** The existing `windowChanged` release in `ImageCanvas` (pane migration) depends on the same persistent-scene-graph behaviour. It is unchanged in this round and not re-verified here.
- **Full offscreen suite.** Not re-run in this round; only the targeted set above was run.
- **UI design skills.** Read for this round: `application-interface-design` (feedback and states: an error notice says what happened, in specific terms). The QML change is a notice string and one property, with no layout, spacing, geometry, type or motion change, so the other skills did not apply.

## Review round 2 follow-ups

The round 1 re-review was APPROVED (`docs/verification/STACK-F040-PINNED-REVIEW.md` on `claude/review-stack-f040`, `4a894be`). This round makes the lead's follow-ups for LOW-1, LOW-2 and LOW-5. Base: `b610348`.

### LOW-1. The refusal notice says what happened and what to do

`ViewerController::pinReference()` writes the refusal into `status`. `Main.qml` shows that status as the notice. The first sentence depends on whether a reference window is already open:

- "Not enough image memory to open the reference window."
- "Not enough image memory to replace the reference photo. The current one stays."

The second sentence names whatever is actually holding the memory. The checks run in the order listed.

| Condition | Next step |
| --- | --- |
| Output is comparing | "Close the comparison on the output to make room." |
| Output is active, not pinned, and holding an older frame than the main photo (the held frame is off screen) | "Turn on Follow controller selection to release the output's held photo." |
| Output has a pin | "Unpin the output photo to make room." |
| None of these | "This photo is too large for the image memory budget." |

"Open a photo first to pin it as a reference." is unchanged.

UI skills: I applied `application-interface-design` (feedback and states). The text is sentence case and says what did and did not happen, with one next step. The notice is the existing `DeleteFlow` toast, so the theme tokens and layout are unchanged.

### LOW-2. Shed order (lead decision)

`enforceBudget()` now sheds in this order:

1. **Speculative caches:** the previous frame, and a comparison frame the output is not showing.
2. **The reference photo.** It is skipped when it shares pixels with a frame that stays (current, pin or waiting frame), because dropping it would free nothing. When it is shed, the window closes. The main window then shows the notice "Reference window closed to free image memory. To reopen it, open <name> and pin it again." The controller status uses the same text.
3. **Refuse the new frame:**
   - A waiting (held or timed) output frame is dropped with the existing status "Output holds its last image: the latest frame exceeds the retained-image budget."
   - A comparison being admitted is dropped.
   - A pin being admitted (`admittingPin`) is refused.

A photo pinned on the output is never dropped for a newer main frame. Instead, the output does not retain the newer frame (`currentWithheld`) and sets the status "Output keeps its pinned photo: the latest frame exceeds the retained-image budget." When the pin is released, the output fetches that frame again with a queued `currentImageRequested`. `pinCurrentImage()` fetches the withheld frame first. The current frame is still never dropped while the output shows it.

### LOW-5

`~ImageCanvas` restores `setPersistentSceneGraph(true)` on a window it had made non-persistent.

### Tests (`tests/reference_pin_tests.cpp`)

**Notice wording paths:**
- `budgetRefusalHasItsOwnNotice`: open refused, photo too large.
- `refusalNoticeNamesThePinnedOutput`: open refused, unpin.
- `refusalNoticeNamesTheComparison`: open refused, close the comparison.
- `refusalNoticeForReplacementNamesTheHeldFrame`: replace refused, the old reference stays, release the held frame.
- `commandsStayDistinct`: open a photo first.

**Shed order:**
- `heldOutputKeepsItsFrameReferenceIsShed`: the output holds p1, the reference is p2, and a new frame p3 arrives. The reference is shed with its notice. The held frame and the waiting frame stay intact, the output status is unchanged, `retainedImageBytes() == 2 * one`, and resuming delivers p3.
- `pinnedOutputPhotoIsNeverDropped`: the output pin survives a newer main frame. It reports its status and keeps exactly one image retained. Unpinning brings the main photo back.

### Output (MSVC Release, `build-claude-f040`)

Run inside `gate-lock.ps1 -Branch claude/f040-fix`:

```
> ctest --test-dir build-claude-f040 -R '^(reference_pin|monitor_output|input_ui|monitor_customization_ui)$' --output-on-failure
1/4 Test #13: input_ui .........................   Passed   79.64 sec
2/4 Test #37: monitor_output ...................   Passed    2.61 sec
3/4 Test #43: monitor_customization_ui .........   Passed   26.81 sec
4/4 Test #44: reference_pin ....................   Passed   26.02 sec
100% tests passed out of 4
Total Test time (real) = 135.09 sec
```

Direct offscreen/software run:

```
Totals: 15 passed, 0 failed, 2 skipped, 0 blacklisted, 25679ms
```

The two skips are the native cases.

Direct native run (D3D11, threaded loop, `QT_LOGGING_RULES=qt.scenegraph.renderloop=true`, no offscreen or software environment):

```
PASS   : ReferencePinTests::nativeTextureReleasedOnClose()
PASS   : ReferencePinTests::nativeTextureReleasedOnShed()
Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 3704ms
```

The first run of this round failed `heldOutputKeepsItsFrameReferenceIsShed`. The toast read `viewer.status`, which the decode that caused the shed had already replaced ("Untagged source assumed sRGB · display profile not yet managed"). The QML handler now builds the text from the shed path. The run above is after that fix.

This round has no fail-before run (the lead did not ask for one). Two tests encode behaviour that the round 1 code does not have:
- `heldOutputKeepsItsFrameReferenceIsShed`: round 1 shed the waiting frame before the reference.
- `pinnedOutputPhotoIsNeverDropped`: round 1 dropped the pin and switched to mirror.

**Still not covered by tests:**
- `pinCurrentImage()` while a frame is withheld.
- A comparison being admitted while the reference is open.