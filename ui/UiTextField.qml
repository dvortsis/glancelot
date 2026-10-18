import QtQuick
import QtQuick.Controls
import Glancelot

// The canonical single-line field. `searchIcon` adds a leading search glyph; `mono` is for shortcut text.
TextField {
    id: control
    property bool searchIcon: false
    property bool mono: false
    implicitHeight: Theme.controlHeight
    leftPadding: searchIcon ? Theme.gapSm + Theme.iconSize + Theme.gapSm : Theme.gapMd
    rightPadding: Theme.gapMd
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    font.pixelSize: Theme.textBody
    font.family: mono ? Theme.monoFamily : Theme.fontFamily
    color: Theme.text
    placeholderTextColor: Theme.muted
    selectionColor: Theme.primary
    selectedTextColor: Theme.textOnPrimary
    opacity: enabled ? 1 : Theme.disabledOpacity
    background: Rectangle {
        radius: Theme.radiusControl
        color: Theme.background
        border.width: control.activeFocus ? Theme.focusWidth : Theme.borderWidth
        border.color: control.activeFocus ? Theme.focusRing : (control.hovered ? Theme.muted : Theme.border)
        UiIcon {
            visible: control.searchIcon
            name: "search"
            color: Theme.muted
            anchors { left: parent.left; leftMargin: Theme.gapSm + Theme.gapXxs; verticalCenter: parent.verticalCenter }
        }
    }
}
