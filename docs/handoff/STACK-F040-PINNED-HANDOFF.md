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
- **Texture release.** The reference `Window` calls `releaseResources()` when it hides. Qt keeps a hidden window's scene graph by default, which held the reference texture and its `CanvasTextureAdmission` reservation until the next pin. On the threaded render loop, releasing a hidden window invalidates its scene graph and destroys the canvas renderer, which releases the texture. `ImageCanvas` already relies on the same mechanism for pane migration.
- **Visible refusal.** When the reference canvas refuses a texture admission, the window shows `referenceCanvas.rendererError` in a status pill. It uses the main window's pill tokens (`Theme.surface`, `Theme.danger`, `radiusCard`, `gapLg`, `textSmall`, `Accessible.AlertMessage`), so the window is no longer blank.

### Files

| File | Change |
| --- | --- |
| `include/glancelot/display_manager.hpp` | Shared header. Adds the reference slot API and two signals. The exact diff is below. |
| `src/app/display_manager.cpp` | Reference storage, admission, shed order, `referenceProtected` during output-pin admission |
| `src/app/viewer_controller.cpp` | Reference section (`referenceImage`, `referenceName`, `pinReference`, `clearReference`) delegates to `DisplayManager`. The `QImage reference` and `referenceLabel` members are removed. Two `connect`s sit next to the existing display-manager connections in `Private()`, about lines 407-412. |
| `ui/Main.qml` | Only the reference-window block (about lines 2041-2069): status pill and `onVisibleChanged` release. The F054 `rtl` leftovers and the `}    FileDialog {` line are untouched, so EN-06 can repair them without conflicting with this change. |
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

- **GPU texture release on close** (`releaseResources()` on a hidden reference window) is not observable under the software backend these UI tests use. It needs a native RHI run (Windows gate or physical). The status pill for a refused admission is likewise only exercised structurally (no QML errors, hidden when `rendererError` is empty).
- **Not done:** cross-window texture admission. The reference canvas keeps its own per-canvas `CanvasTextureAdmission` (256 MiB, the same bound as one presentation window). It is now released on close. Sharing one admission between the reference canvas and the presentation canvases would need a QML-visible hook in `src/render/image_canvas.hpp`, which is not in this packet's grant. `canvas_texture_admission.hpp` therefore needed no change. **Lead decision:** accept per-window texture admission plus aggregate pixel accounting as meeting the contract, or grant `image_canvas.hpp` for a follow-up.
- **Notice routing:** `Main.qml` line ~729 (`reference-pin` routing, outside this block) still says "Open a photo first…" for any false return. Through the UI, false can only mean "nothing decoded": pinning the current frame never adds bytes, so it is never refused by the budget.
- **UI design skills:** the `ui-design-skills` (application-interface-design and the others) are **not available** in this cloud session. The small QML addition reuses the existing status-pill pattern and theme tokens; no new visual language.
- No physical multi-monitor or hardware run.
