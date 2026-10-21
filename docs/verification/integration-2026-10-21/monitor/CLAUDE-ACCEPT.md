**ACCEPT (source review only).** I found no required code or test defects in what I checked. My coverage has gaps, listed below. I ran nothing, and this is not a gate pass.

**Skills read:** all nine `SKILL.md` files in full. The output is a bare native canvas with no chrome, so only keyboard recovery, state fidelity, honest status text and Theme consistency apply. Keyboard and native UI are not waived. Neither patch touches visual QML styling, so geometry, typography, iconography and motion have nothing to review.

**Verified from source**
- **Merge conflict:** `src/render/image_canvas.hpp:5-8` keeps `QVariantMap`, `QPointer` and `QQuickWindow`, each once, and no conflict markers remain anywhere in `src/`, `include/`, `tests/`, `ui/` or `docs/`.
- **Monitor viewport and color features still present:**
  - `ImageCanvas` still has `viewState`/`setViewState` (`image_canvas.cpp:283-302`) and `setOutputColorLut` (`:303`), which accepts only a 1089×33 RGBA8888 atlas.
  - The `refreshFit` DPR pan rescale and `m_lastDpr` are intact, as is the per-canvas texture budget.
  - The `image.frag` LUT binding at slot 2 and the identity 1×1 fallback are in the main delta.
  - `ImageCanvas::beginHold` zooms to physical 1:1 (`zoomAt(1/m_zoom)`), so it matches the monitor's physical-zoom definition.
- **Hold/loupe lifecycle:**
  - `setImage`, `fit`, `actualSize`, `rotateClockwise` and `setViewState` all call `endLoupe()`, and `endLoupe` calls `unwatchHold`, so the event filter is removed.
  - `~ImageCanvas` calls `unwatchHold()`, and the window is held in a `QPointer`.
  - The release filter ignores auto-repeat, and `WindowDeactivate` restores the view.
  - `Main.qml:652`, `:653`, `:693` and `:882` also end the loupe on deactivation, modal open and a cancelled grab.
  - The presentation windows never use hold, so it cannot disturb per-output viewport state.
- **Registry and scopes:**
  - The monitor commands (`presentation-*`, `presentation-view-*`) carry no scope in `viewer_controller.cpp:146-161`, so they take the `Command` default of `"global"` (`contracts.hpp:26`).
  - Global overlaps every scope, so `bindingsCollide` treats them against every other binding. That is conservative and correct.
  - Only `presentation-toggle` (F8) and `presentation-fit` (Ctrl+Alt+F) have defaults. I found no overlap with the INPUT defaults in the registry, though I did not run the pairwise check.
  - The new INPUT commands are `hold-actual` (`Z`, canvas scope, hold flag) and the grid/tree route commands. They sit before `quit` and don't change the monitor block.
  - `scopeAllows` returns true for non-canvas, non-grid, non-tree scopes, so the global presentation shortcuts work from any pane.
- **Monitor accepted fixes:**
  - **First-visit display:** `display_manager.cpp:399-405` sets the fit mode first, then restores with a defaults map.
  - **Texture budget:** `setComparisonMode` is at `presentation_window.cpp:59`, and `display_manager.cpp:187` calls it.
  - **Closed output:** `:474` clears `previous*` while inactive, and `:429` and `:578` also clear it.
  - The `retainedImageBytes` accessor is present at `:575`.
- **CMake:** `input_ui_tests` and `monitor_output_tests` are separate targets with separate `add_test` entries, and both compile `image.frag`/`image.vert`.

**Non-blocking findings**
1. **Stale zoom after a resize or DPR change during a hold** (`image_canvas.cpp:318-323`, `:240-257`).
   - `endLoupe` restores `m_savedZoom` and `m_savedPan` without calling `refreshFit()`.
   - While a hold is active the fit mode is empty, so a resize or DPR change only rescales the held pan, not the saved one.
   - After release, a `shrink` or `fit` view keeps the old zoom until the next geometry change. A saved manual pan keeps the old DPR scale.
   - This predates the merge for the right-button loupe. The monitor's `m_lastDpr` makes it slightly more visible. The presentation canvases don't use hold.
   - Suggested fix: in `endLoupe`, call `refreshFit()` when the restored fit mode is non-empty, and otherwise scale `m_pan` by `m_lastDpr/dpr()`. Add a resize-during-hold test.
2. **Hold with no release watcher** (`beginHold`, `:326-337`). If `window()` is null or the sequence isn't exactly one key, `m_loupe` is set but no filter is installed. The loupe then stays until the next `fit`, `setImage` or zoom command. The controller rejects chord holds (`usableBindings`, `setBinding`), so this is only reachable through a direct call. Consider returning early before saving state.
3. **A destroyed hold window.** If the `QPointer` window is destroyed during a hold, `m_loupe` stays true with no watcher. This is an edge case at shutdown.
4. The advisories carried over from `CLAUDE-FOLLOWUP.md` still apply: the budget test name overstates what it checks, and the manager defaults map has no headless test.

**Coverage gaps in this review**
- I had no shell, so I could not diff against `9b03718`. "No source lost" rests on the delta patches plus checking that the key symbols exist in the merged files, not on a byte diff.
- I did not read `sync-delta.patch` past line 745. The remainder is `input_ui_tests.cpp` bodies, which I only inspected up to the chord test.
- I did not read `display_manager.cpp` or `presentation_window.cpp` end to end. I checked only the grep hits above.
- I did not open the watcher/cancellation/decoder code. Only the `refresh` registry entry and the absence of conflict markers support "intact". Because the merge touched only one conflict, I expect it is intact, but I did not verify it directly.

**Unverified and left to the gates**
- Full `scripts/build.ps1 -Test` and `docker build -t glancelot-browser:main .` under the gate lock.
- Native opt-in monitor tests (two were skipped as opt-in in the Sonnet run).
- Real GPU readback and the actual color shader output.
- ICC accuracy, HDR, and multi-monitor DPI or hotplug behavior.
- Real keyboard layouts: AltGr/`Ctrl+Alt+F`, and the focus loss and key-release behavior of `hold-actual` in a real window manager.
- Chord timing on a slow machine.
