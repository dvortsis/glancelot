import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// A labelled settings row: label and optional description lead, the control trails. Give the control the
// same Accessible.name as the visible label.
RowLayout {
    id: row
    property string label
    property string description: ""
    default property alias control: slot.data
    spacing: Theme.gapLg
    Layout.fillWidth: true
    Layout.minimumHeight: Theme.controlHeight + Theme.gapSm

    ColumnLayout {
        spacing: Theme.gapXxs
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        Label {
            text: row.label
            color: Theme.text
            font.pixelSize: Theme.textBody
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            visible: row.description.length > 0
            text: row.description
            color: Theme.muted
            font.pixelSize: Theme.textSmall
            wrapMode: Text.Wrap
            lineHeight: 1.3
            Layout.fillWidth: true
        }
    }
    Row {
        id: slot
        spacing: Theme.gapSm
        Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
    }
}
