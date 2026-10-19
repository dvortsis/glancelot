import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// A full-width settings toggle: label (and optional description) on the leading edge, switch on the trailing
// edge. The whole row is the hit target, and the state is shown by knob position as well as fill.
Switch {
    id: control
    property string description: ""
    property bool modified: false
    implicitHeight: Math.max(Theme.controlHeight + Theme.gapSm, contentItem.implicitHeight + topPadding + bottomPadding)
    leftPadding: 0
    rightPadding: indicator.width + Theme.gapLg
    topPadding: Theme.gapXs
    bottomPadding: Theme.gapXs
    spacing: 0
    font.pixelSize: Theme.textBody
    opacity: enabled ? 1 : Theme.disabledOpacity
    hoverEnabled: true
    Accessible.name: text
    Accessible.description: description

    indicator: Rectangle {
        implicitWidth: 36
        implicitHeight: 20
        x: control.width - width
        y: (control.height - height) / 2
        radius: height / 2 // a switch track is one of the few genuinely pill-shaped controls
        color: control.checked ? Theme.primary : Theme.background
        border.width: control.visualFocus ? Theme.focusWidth : Theme.borderWidth
        border.color: control.visualFocus ? Theme.focusRing : (control.checked ? Theme.primary : Theme.muted)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Rectangle {
            width: 14; height: 14; radius: 7
            y: 3
            x: control.checked ? parent.width - width - 3 : 3
            color: control.checked ? Theme.textOnPrimary : Theme.muted
            Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Column {
        spacing: Theme.gapXxs
        RowLayout {
            width: parent.width
            spacing: Theme.gapSm
            Label {
                text: control.text
                font: control.font
                color: Theme.text
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Rectangle {
                width: 6
                height: 6
                radius: 3
                color: Theme.accent
                visible: control.modified
                Layout.alignment: Qt.AlignVCenter
                Accessible.role: Accessible.Indicator
                Accessible.name: "Modified from default"
            }
        }
        Label {
            width: parent.width
            visible: control.description.length > 0
            text: control.description
            font.pixelSize: Theme.textSmall
            color: Theme.muted
            wrapMode: Text.Wrap
            lineHeight: 1.3
        }
    }
    background: null
}
