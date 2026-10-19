import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// F051 shortcut recorder: press the keys instead of typing their names. Activate (click, Enter or Space) to record; the next
// key press, or a two-step chord such as "Ctrl+K, Ctrl+C", becomes the shortcut. Escape cancels, Backspace or Delete clears,
// and a shortcut that cannot work says why before anything is saved. While recording, no window shortcut can fire.
UiButton {
    id: recorder
    required property string commandId
    required property string binding
    property string commandLabel: commandId
    readonly property bool recordingShortcut: recording // Main.qml keeps window shortcuts quiet while this is true
    property bool recording: false
    property var steps: []
    property string explanation: ""        // why the last attempt was refused (also shown by the host)
    signal committed(string sequence)      // "" clears
    signal refused(string reason)

    font.family: Theme.monoFamily
    alignment: Qt.AlignLeft
    implicitWidth: 168
    selected: recording
    text: recording ? (steps.length ? steps.join(", ") + ", …" : "Press a shortcut…") : (binding.length ? binding : "Unassigned")
    foreground: recording || binding.length ? Theme.text : Theme.muted
    Accessible.role: Accessible.Button
    Accessible.name: "Shortcut for " + commandLabel + ": " + (binding.length ? binding : "unassigned")
    Accessible.description: recording ? "Press the new shortcut. Escape cancels, Backspace clears, Enter accepts."
                                      : "Activate to record a new shortcut."

    function begin() {  steps = []; explanation = ""; recording = true; chordTimer.stop() }
    function cancel() {  recording = false; steps = []; chordTimer.stop() }
    function finish() {
        chordTimer.stop()
        const sequence = steps.join(", ")
        recording = false; steps = []
        if (sequence.length === 0) return
        const problem = viewer.bindingProblem(commandId, sequence)
        if (problem.length > 0) { explanation = problem; refused(problem); return }
        committed(sequence)
    }
    onClicked: if (recording) cancel(); else begin()
    onActiveFocusChanged: { if (!activeFocus && recording) cancel() }
    // Recording owns every key, including ones a window shortcut would take.
    Keys.onShortcutOverride: event => { if (recording) event.accepted = true }
    Keys.onPressed: event => {
        if (!recording) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { begin(); event.accepted = true }
            return
        }
        event.accepted = true
        const modifierOnly = [Qt.Key_Control, Qt.Key_Shift, Qt.Key_Alt, Qt.Key_Meta, Qt.Key_AltGr, Qt.Key_unknown].indexOf(event.key) >= 0
        if (modifierOnly) return
        const plain = (event.modifiers & ~Qt.KeypadModifier) === Qt.NoModifier
        if (event.key === Qt.Key_Escape && plain) { cancel(); return }
        if (plain && steps.length === 0 && (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete)) {
            recording = false; committed(""); return
        }
        if (plain && steps.length > 0 && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) { finish(); return }
        const step = viewer.keyStep(event.key, event.modifiers)
        if (step.length === 0) return
        steps = steps.concat([step])
        if (steps.length >= 2) finish(); else chordTimer.restart() // a second key makes a chord; a pause accepts one key
    }
    Timer { id: chordTimer; interval: 1500; onTriggered: recorder.finish() }
}
