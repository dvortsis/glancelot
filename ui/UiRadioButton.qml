import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// The canonical choice between two or more explicit options in a dialog. The whole row is the hit target, the state is a shape
// as well as a colour, and a long label wraps rather than clips. A group is one Tab stop, on its checked option, and the arrow
// keys move and select within it (a held arrow moves once, so it can't churn through plans). Assign ButtonGroup.group.
RadioButton {
    id: control
    Layout.fillWidth: true
    implicitHeight: Math.max(Theme.controlHeight, labelText.implicitHeight + Theme.gapXs)
    padding: 0
    spacing: Theme.gapSm
    hoverEnabled: true
    font.pixelSize: Theme.textBody
    opacity: enabled ? 1 : Theme.disabledOpacity
    focusPolicy: checked ? Qt.StrongFocus : Qt.ClickFocus
    Accessible.name: text
    function step(delta) {
        const options = ButtonGroup.group ? ButtonGroup.group.buttons.filter(b => b.visible && b.enabled) : []
        if (options.length < 2) return
        const next = options[(options.indexOf(control) + delta + options.length) % options.length]
        next.forceActiveFocus(Qt.TabFocusReason)
        if (!next.checked) { next.toggle(); next.clicked() }
    }
    Keys.onPressed: event => {
        const forward = event.key === Qt.Key_Down || event.key === (mirrored ? Qt.Key_Left : Qt.Key_Right)
        const back = event.key === Qt.Key_Up || event.key === (mirrored ? Qt.Key_Right : Qt.Key_Left)
        if (!forward && !back) return
        event.accepted = true
        if (!event.isAutoRepeat) step(forward ? 1 : -1)
    }
    onActiveFocusChanged: { // in a short window the dialog body scrolls: keep the focused choice in view
        if (!activeFocus) return
        for (let view = parent; view; view = view.parent) {
            if (!(view instanceof Flickable)) continue
            const top = mapToItem(view.contentItem, 0, 0).y
            if (top < view.contentY) view.contentY = top
            else if (top + height > view.contentY + view.height) view.contentY = top + height - view.height
            break
        }
    }
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
