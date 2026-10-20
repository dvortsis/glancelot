import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: root
    color: Theme.surface
    clip: true

    property var model: null
    signal closeRequested()

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Theme.borderWidth
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.gapMd
        spacing: Theme.gapSm

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "Metadata"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textH2
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }
            UiButton {
                text: "✕"
                tip: "Close metadata inspector"
                onClicked: root.closeRequested()
            }
        }

        UiTextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: "Search tags..."
            onTextChanged: if (root.model) root.model.searchFilter = text
        }

        ListView {
            id: tagList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.model
            delegate: Rectangle {
                width: tagList.width
                height: 48
                color: index % 2 === 0 ? Theme.surface : Theme.hover
                radius: Theme.radiusSm

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.gapXs
                    spacing: 2
                    Text {
                        text: model.label || model.key
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textXs
                        color: Theme.muted
                        elide: Text.ElideRight
                    }
                    Text {
                        text: model.value || ""
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}

