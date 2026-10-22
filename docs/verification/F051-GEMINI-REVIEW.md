# F051 independent Gemini review: Shortcut recorder and conflict explanations

- **Packet:** F051 (Shortcut recorder and conflict explanations in Settings)
- **SHA pair reviewed:** `f7aa764` → `8c3189c` (`claude/f051`)
- **Reviewer:** Gemini lane (`agent/*`), isolated worktree `.worktrees/review-f051`, branch `agent/review-f051`
- **Disposition:** **APPROVED**

## Scope and Diff Summary

The review evaluated `git diff f7aa764 8c3189c`:
- `docs/F051-PLAN.md`: Contract specification and delivery notes.
- `ui/ShortcutRecorder.qml`: Interactive native QML component replacing free-text shortcut entry with direct key-event capture, multi-step chords (e.g. `Ctrl+K, Ctrl+C`), Escape/focus-loss cancellation, and Backspace/Delete clearing.
- `ui/SettingsDialog.qml`: Integrated `ShortcutRecorder` into the Shortcuts settings panel, wired conflict feedback messages, and suppressed window-level shortcuts while recording (`recordingShortcut`).
- `include/glancelot/viewer_controller.hpp` & `src/app/viewer_controller.cpp`: Added `keyStep()` portable key-name serializer and `bindingProblem()` conflict analyzer with descriptive English explanations.
- `tests/ui_settings_tests.cpp`: Unit test coverage for settings lifecycle, binding problem reporting, and shortcut validation.
- Note: The stack tip also encompasses F052 (accessibility audit), F053 (high contrast / 44px touch targets), F054 (i18n / pseudo-locale), and main merges. The review evaluated the complete diff with targeted focus on F051.

## UI Design & Craft Evaluation

Evaluated against `application-interface-design`, component ergonomics, typography, and Qt/QML design guidelines:
1. **Interaction Design & Direct Manipulation**:
   - Replaces fragile text-input boxes with an intuitive recorder button (`ShortcutRecorder.qml`). Users record shortcuts by clicking or focusing and pressing the exact physical keys.
   - Intelligent chord handling: A single key followed by a 1.5s pause commits a single-key shortcut; a second key press within 1.5s immediately binds a two-step chord (e.g. `Ctrl+K, Ctrl+S`).
   - Clean escape routes: Plain Escape cancels recording without changes; Backspace or Delete resets the command to `"Unassigned"`; clicking outside or shifting focus auto-cancels.
2. **Keyboard Conflict Isolation**:
   - Implements `Keys.onShortcutOverride: event => { if (recording) event.accepted = true }` and coordinates with `Main.qml`'s `recordingKey` state. This prevents in-flight recorded keys from prematurely executing viewer actions (such as quitting or opening dialogs).
3. **Typography & Styling**:
   - Uses `Theme.monoFamily` (`Geist Mono`) for recorded shortcut sequences, ensuring clear character spacing and immediate visual distinction from standard button labels.
   - Visual states: Active recording displays `"Press a shortcut…"` or partial chord sequences (`"Ctrl+K, …"`) with `selected: true` styling.
4. **Validation & Helpful Explanations**:
   - Rejections are never silent. `bindingProblem()` produces human-readable diagnostic messages (e.g., `"Escape is reserved"`, `"Already used by '<command>'"`), displayed directly beneath the recorder.
5. **Accessibility**:
   - Full accessible roles and live dynamic descriptions:
     - `Accessible.role: Accessible.Button`
     - `Accessible.name: "Shortcut for " + commandLabel + ": " + (binding.length ? binding : "unassigned")`
     - Dynamic description: `"Press the new shortcut. Escape cancels, Backspace clears, Enter accepts."`

## Findings

1. **Scope Note (Global-on-native-pane warning)**:
   - As documented in `docs/F051-PLAN.md` ("Not done"), if a user assigns a global command to an arrow key that a focused pane intercepts natively, the pane still claims it. The conflict engine does not yet warn on that specific edge case. This is documented and non-blocking.
2. **Scope Note (Import/Export of shortcut sets)**:
   - Dedicated shortcut-set presets and export belong to future customization work.

## Targeted Test Output

Executed in `.worktrees/review-f051` (`build-review-f051`):
```
Test project C:/src/Github/Glancelot/.worktrees/review-f051/build-review-f051
    Start 20: ui_settings
1/1 Test #20: ui_settings ......................   Passed    0.57 sec

100% tests passed out of 1
Total Test time (real) =   0.63 sec
```
Direct run:
- `ui_settings_tests.exe`: All test cases passed.
