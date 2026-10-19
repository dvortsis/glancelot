# F051 contract and delivery: shortcut recorder and conflict explanations

Lane Claude Code; branch `claude/f051` (stacked on `claude/f014` merged with main after UI-SETTINGS-01); reviewer from the Codex lane.

## Frozen interface (approved additions)
`ViewerController::keyStep(key, modifiers)` (portable text of one key press) and `bindingProblem(id, sequence)` (a sentence explaining why a shortcut cannot be saved, empty when it can). New component `ui/ShortcutRecorder.qml`.

## Behaviour
- In Settings, each command's shortcut is a recorder, not a free-text field: activate (click, Space or Enter), press the keys, and a pause or a second press finishes (a second press makes a chord). Escape cancels, Backspace or Delete clears. Focus loss cancels.
- While recording, no window shortcut fires (`Main.qml` `recordingKey`), so pressing a key already bound to something never runs that command.
- A refused shortcut is explained in the dialog and nothing is saved: Escape is reserved, at most two presses, a hold needs one key, or "Already used by “<command>”" including chord prefixes. Commands in scopes that never overlap may share a key, as before.
- The save is deferred past the recorder's own signal, because saving rebuilds the list and destroys the row.
- Together with the INPUT-02 review fix (pane commands remapped onto native pane keys now win), the earlier "accepted but dead binding" gap is closed for pane commands. A global command put on a native pane key still loses to the pane inside that pane; the recorder does not yet warn about that case.

## Not done
A warning for the global-on-native-key case, a conflict view listing all clashes, import and export of shortcut sets, and assistive-technology trials (unverified).
