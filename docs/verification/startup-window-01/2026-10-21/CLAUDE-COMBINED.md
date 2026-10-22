**Verdict: ACCEPT** for the geometry helper, the `app/main.cpp` adapter and the pregrab diagnostics. The combined-tree compatibility delta is only partly confirmed (see "Not verified"). I found no defect I'd require a change for. I didn't build, run tests, use the network or write files. Plan mode is active, so I didn't write a plan file either.

## Geometry helper (`src/app/startup_window_geometry.cpp`)
- **Order of checks:** the screen count is checked first and returns `TooManyScreens` with the geometry unchanged, before any rectangle is read (line 30). The test "invalid current + 65 screens" covers it.
- **Overflow:** every rectangle is widened to `qint64` from `QRect` inclusive endpoints (lines 13–16). The clamp at lines 24–25 keeps `x+width-1` inside `int`. Size is capped at `INT_MAX` first. I traced the integer-upper-edge, lower-edge and minimum-larger-than-area cases by hand and they hold.
- **Allocation:** none. The function uses a bounded loop over a span and returns a value.
- **Visibility:** any positive-area intersection with a usable screen returns `Unchanged` (line 36), so partial visibility is preserved. Touching an edge doesn't count; the left, bottom and 1-pixel cases are tested.
- **Fallback:** the first usable screen is used unless the preferred index is in range and usable.
- **Minimums:** minimums are forced to at least 1 and win over a small work area. The anchor falls back to the screen start because `max(0, …)` stops a negative centre offset.

## Adapter (`app/main.cpp:100-141`, `:329-383`)
- **Frame conversion:** `window->create()` runs first, then `frameGeometry()` and `frameMargins()` are read. The minimum is client minimum plus actual margins. The result goes back to client coordinates through `setGeometry`. There is no DPR math, and the arithmetic is clamped. This follows the frozen outer-frame contract.
- **Fullscreen and maximized:** these return `PreservedWindowState` before any placement change.
- **Truncated screen lists:** `TooManyScreens` is handled without reading the truncated array.
- **Ordering:** `--window-size` (line 279) runs before the `singleShot(0)` recovery. `quitTimerStartedMs` is taken after recovery, and `installReplay` comes after that. No timeout was extended, and there is no expose synthesis or fabricated frame.
- **Persisted geometry:** `restoreWindow` (`viewer_controller.cpp:1196`) restores only the folder and image, so it can't move the window back off-screen. Safe mode returns early and writes nothing.
- **Pregrab evidence:** the `preGrab` object (window, RHI, frames) is built before `grabWindow()`, and the existing post-grab fields are kept. Output is bounded to 64 screens and carries a truncation flag. It contains no media or secrets.

## Shared registry and wheel modifiers
- **Binding validation:** `binding_validation.hpp` implements the stated rules. Sequences are limited to two steps and unmodified Escape is rejected. Holds must be single-key, and overlapping scopes conflict.
  - Workspace overlaps the canvas and grid scopes, so those two can reuse a sequence.
  - Unknown command IDs are treated as global.
- **One rule everywhere:** the same `bindingsCollide` is used in `resolveCommandBindings` (`command_registry.cpp:107-129`, where user choices beat newly added defaults), in `bindingConflict`, and at `viewer_controller.cpp:1550`.
- **Wheel modifiers:** `setWheelModifier` (`viewer_controller.cpp:1423-1445`) sends one per-modifier channel through `saveWithRetry`. It merges only that key, so foreign and unknown modifier entries survive.
  - Retries are owned by the controller as a child object, so they die with it.
  - They stop after 4000 ms, and the completion callback is guarded by a `QPointer`.
  - The load path (lines 478–483) falls back per modifier.

## Advisories (not blocking)
1. A window with a single visible pixel counts as visible, as the spec says. The title bar can still be unreachable in that case.
2. The adapter has no automated test; only the pure helper does. The native process proof covers the adapter, so please capture it on the final tree.
3. Toggling `frameless` through `titleBarRefresh` can recreate the native window after recovery and change its margins. The `after` snapshot would show this.
4. `viewer_controller.cpp:1442`: `if(!alive)return true;` can never trigger before `alive` is used elsewhere. It is harmless.

## Not verified
- **Command-table byte-equality:** I confirmed `.cache/startup-window/command-table-equivalence.json` exists but did not diff it against main's table. Its integrity rests on the lead's record.
- **Recovery tests:** I only counted occurrences of the scope, hold, modifier and checkpoint tests (69 in `tests/recovery_tests.cpp`) and did not read them. I did not read `claude-combined.json`.
- **Test status:** the combined targeted tests and both full gates have not run on the final tree. The earlier isolated results on `92df42e` don't count here.
- **`scripts/verify-recovery.py`:** I couldn't confirm it is unchanged without git.
- **Hardware:** mixed-DPI placement and real exposure on the two-monitor machine are unverified. The measured margins of 8/31/8/8 appear only as test fixtures.

Before integration, I'd want the geometry target in both full gates and the native safe and normal runs to show actual swaps and exposure.
