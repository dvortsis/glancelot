import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// The canonical button. Variants: "quiet" (toolbar and list actions, the default), "primary" (one per decision
// area) and "outline" (secondary actions in content). Checkable buttons show their state with the selection fill.
// An icon-only button keeps `text` as its accessible name and tooltip.
Button {
    id: control
    property string variant: "quiet"
    property string iconName: ""
    property bool iconOnly: false
    property string tip: iconOnly ? text : ""
    property int alignment: Qt.AlignHCenter
    readonly property color foreground: variant === "primary" ? Theme.textOnPrimary : Theme.text

    implicitHeight: Theme.controlHeight
    implicitWidth: iconOnly ? Theme.controlHeight : Math.max(Theme.controlHeight, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: iconOnly ? 0 : (iconName.length ? Theme.gapSm + Theme.gapXxs : Theme.gapMd)
    rightPadding: iconOnly ? 0 : Theme.gapMd
    topPadding: 0
    bottomPadding: 0
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity
    font.pixelSize: Theme.textBody
    font.weight: variant === "primary" ? Font.Medium : Font.Normal
    Accessible.name: text
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: tip

    contentItem: RowLayout {
        spacing: Theme.gapSm - Theme.gapXxs
        Item { Layout.fillWidth: control.alignment === Qt.AlignHCenter; visible: Layout.fillWidth }
        UiIcon {
            visible: control.iconName.length > 0
            name: control.iconName
            color: control.foreground
            Layout.alignment: Qt.AlignVCenter
        }
        Label {
            visible: !control.iconOnly && control.text.length > 0
            text: control.text
            font: control.font
            color: control.foreground
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            Layout.fillWidth: control.alignment !== Qt.AlignHCenter
            Layout.alignment: Qt.AlignVCenter
        }
        Item { Layout.fillWidth: true; visible: control.alignment === Qt.AlignHCenter }
    }
    background: Rectangle {
        radius: Theme.radiusControl
        color: control.variant === "primary" ? Theme.primary
               : (control.down ? Theme.pressed : (control.checked ? Theme.selection : (control.hovered ? Theme.hover : "transparent")))
        border.width: control.visualFocus ? Theme.focusWidth : (control.variant === "outline" ? Theme.borderWidth : 0)
        border.color: control.visualFocus ? Theme.focusRing : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Rectangle { // primary hover/press feedback, drawn in the button's own foreground so it works in every family
            anchors.fill: parent
            radius: parent.radius
            visible: control.variant === "primary"
            color: Qt.alpha(Theme.textOnPrimary, control.down ? 0.16 : (control.hovered ? 0.08 : 0))
        }
    }
}
