import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// The canonical choice between two or more explicit options in a dialog. The whole row is the hit target, the state is a shape
// as well as a colour, and a long label wraps rather than clips.
RadioButton {
    id: control
    Layout.fillWidth: true
    implicitHeight: Math.max(Theme.controlHeight, labelText.implicitHeight + Theme.gapXs)
    padding: 0
    spacing: Theme.gapSm
    hoverEnabled: true
    font.pixelSize: Theme.textBody
    opacity: enabled ? 1 : Theme.disabledOpacity
    Accessible.name: text
    indicator: Rectangle {
        x: control.mirrored ? control.width - width : 0
        y: (control.height - height) / 2
        implicitWidth: Theme.iconSize
        implicitHeight: Theme.iconSize
        radius: width / 2 // a radio is one of the few genuinely round controls
        color: control.hovered ? Theme.hover : Theme.background
        border.width: control.visualFocus ? Theme.focusWidth : Theme.borderWidth
        border.color: control.visualFocus ? Theme.focusRing : (control.checked ? Theme.primary : Theme.muted)
        Rectangle {
            anchors.centerIn: parent
            width: parent.width / 2
            height: width
            radius: width / 2
            color: Theme.primary
            visible: control.checked
        }
    }
    contentItem: Label {
        id: labelText
        leftPadding: control.mirrored ? 0 : control.indicator.width + control.spacing
        rightPadding: control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        color: Theme.text
        font: control.font
        wrapMode: Text.Wrap
        verticalAlignment: Text.AlignVCenter
    }
}
