import QtQuick
import QtQuick.Controls
import Glancelot

// Thin overlay scroll bar that widens under the pointer; shown only while scrollable.
ScrollBar {
    id: control
    policy: ScrollBar.AsNeeded
    padding: Theme.gapXxs
    implicitWidth: orientation === Qt.Vertical ? (hovered || pressed ? 10 : 8) : 0
    implicitHeight: orientation === Qt.Horizontal ? (hovered || pressed ? 10 : 8) : 0
    contentItem: Rectangle {
        implicitWidth: 4
        implicitHeight: 4
        radius: Math.min(width, height) / 2
        color: control.pressed ? Theme.text : Theme.muted
        opacity: control.policy === ScrollBar.AlwaysOn || (control.active && control.size < 1.0) ? (control.hovered || control.pressed ? 0.8 : 0.5) : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
    }
    background: null
}
