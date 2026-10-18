import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// The canonical button. Variants: "quiet" (toolbar and list actions, the default), "primary" (one per decision
// area), "outline" (secondary actions in content) and "danger" (the confirming action of a destructive decision
// only; its label names the outcome). The "on" state (checked toggles, or `selected` for an item that
// marks the current choice without toggling) shows a selection fill plus a primary marker bar, so it never relies on
// a faint fill alone: a leading bar on left-aligned items, an underline on centred ones.
// An icon-only button keeps `text` as its accessible name and tooltip.
Button {
    id: control
    property string variant: "quiet"
    property string iconName: ""
    property bool iconOnly: false
    property string tip: iconOnly ? text : ""
    property int alignment: Qt.AlignHCenter
    property bool selected: false
    readonly property bool isOn: checked || selected
    readonly property bool filled: variant === "primary" || variant === "danger"
    // Danger text uses the window background: every catalog family keeps it >= 7:1 against `danger`.
    property color foreground: variant === "primary" ? Theme.textOnPrimary : variant === "danger" ? Theme.background : Theme.text
    // Destructive confirmations set this so a held (auto-repeating) Enter can never activate them.
    property bool ignoreKeyRepeat: false

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
    font.weight: filled ? Font.Medium : Font.Normal // constant weight: toggling never shifts the layout
    Accessible.name: text
    Accessible.checked: checked
    // Return/Enter behave like Space: checkable buttons toggle (and emit toggled) before clicked.
    function activate() { if (checkable) { toggle(); toggled() } clicked() }
    Keys.onReturnPressed: event => { if (!(ignoreKeyRepeat && event.isAutoRepeat)) activate() }
    Keys.onEnterPressed: event => { if (!(ignoreKeyRepeat && event.isAutoRepeat)) activate() }
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
        color: control.variant === "primary" ? Theme.primary : control.variant === "danger" ? Theme.danger
               : (control.down ? Theme.pressed : (control.isOn ? Theme.selection : (control.hovered ? Theme.hover : "transparent")))
        border.width: control.visualFocus ? Theme.focusWidth : (control.variant === "outline" ? Theme.borderWidth : 0)
        border.color: control.visualFocus ? Theme.focusRing : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Rectangle { // filled hover/press feedback, drawn in the button's own foreground so it works in every family
            anchors.fill: parent
            radius: parent.radius
            visible: control.filled
            color: Qt.alpha(control.foreground, control.down ? 0.16 : (control.hovered ? 0.08 : 0))
        }
        Rectangle { // "on" marker; primary keeps >= 3:1 against selection, hover and surface in every catalog variant
            visible: control.isOn && !control.filled
            color: Theme.primary
            radius: 1.5
            readonly property bool leading: control.alignment === Qt.AlignLeft
            width: leading ? 3 : Math.min(parent.width - 2 * Theme.gapSm, 2 * Theme.iconSize)
            height: leading ? parent.height - 2 * Theme.gapSm : 2
            x: leading ? 0 : (parent.width - width) / 2
            y: leading ? Theme.gapSm : parent.height - height - Theme.gapXxs
        }
    }
}
