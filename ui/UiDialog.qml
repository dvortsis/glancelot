import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// The canonical modal surface: large-container radius, title row with a close button, Escape to close,
// a scrim behind it. Content goes in the default slot.
Dialog {
    id: dialog
    modal: true
    // Short windows get tighter margins so the content, not the frame, keeps the space.
    readonly property bool tight: parent && (parent.height < 480 || parent.width < 640)
    // False hides the title-row close button while the dialog must not be dismissed (e.g. a running operation).
    property bool closable: true
    padding: tight ? Theme.gapLg : Theme.gapXl
    topPadding: tight ? Theme.gapSm : Theme.gapLg
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.scrim }

    header: RowLayout {
        spacing: Theme.gapSm
        Label {
            text: dialog.title
            color: Theme.text
            font.pixelSize: Theme.textTitle
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.leftMargin: dialog.leftPadding
            Layout.topMargin: dialog.tight ? Theme.gapSm : Theme.gapLg
            Accessible.role: Accessible.Heading
        }
        UiButton {
            text: "Close"
            iconName: "close"
            iconOnly: true
            tip: "Close (Esc)"
            focusPolicy: Qt.TabFocus
            enabled: dialog.closable
            opacity: dialog.closable ? 1 : 0 // keeps the title row height stable
            onClicked: dialog.close()
            Layout.rightMargin: dialog.tight ? Theme.gapSm : Theme.gapMd
            Layout.topMargin: dialog.tight ? Theme.gapXs : Theme.gapMd
        }
    }
    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.border
    }
}
