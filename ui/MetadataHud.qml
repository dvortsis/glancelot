import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: root
    color: Theme.surface
    clip: true

    property var model: null
    signal closeRequested()

    property string copiedMessage: ""
    Timer {
        id: copyTimer
        interval: 1800
        onTriggered: root.copiedMessage = ""
    }

    function showCopied(msg) {
        root.copiedMessage = msg
        copyTimer.restart()
    }

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

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapSm

            UiIcon {
                name: "info"
                size: Theme.iconSize
                color: Theme.primary
            }

            Text {
                text: qsTr("Metadata")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTitle
                font.bold: true
                color: Theme.text
                elide: Text.ElideRight // longer translations give way to the actions instead of overlapping them
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }

            UiButton {
                visible: root.model && root.model.hasMetadata
                text: root.copiedMessage.length > 0 ? root.copiedMessage : qsTr("Copy Summary")
                tip: qsTr("Copy summary of camera, exposure, and image details")
                onClicked: {
                    if (root.model) {
                        root.model.copySummary()
                        root.showCopied(qsTr("Copied!"))
                    }
                }
            }

            UiButton {
                // Icon-only like UiDialog's close button: a named, compact control that leaves room for the title.
                text: qsTr("Close metadata inspector")
                iconName: "close"
                iconOnly: true
                tip: qsTr("Close metadata inspector (I)")
                onClicked: root.closeRequested()
            }
        }

        // Fast Overview / Camera & Exposure Card
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: cardLayout.implicitHeight + Theme.gapMd * 2
            color: Theme.background
            radius: Theme.radiusControl
            border.color: Theme.border
            border.width: Theme.borderWidth
            visible: root.model && root.model.hasMetadata

            ColumnLayout {
                id: cardLayout
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.gapMd
                spacing: Theme.gapXs

                Text {
                    text: root.model ? root.model.cameraSummary : ""
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textBody
                    font.bold: true
                    color: Theme.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    visible: text.length > 0 && text !== "Unknown Camera"
                }

                Text {
                    text: root.model ? root.model.exposureSummary : ""
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.textSmall
                    color: Theme.primary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    visible: text.length > 0
                }

                Text {
                    text: root.model ? root.model.dimensionsSummary : ""
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                    color: Theme.muted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    visible: text.length > 0
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.model && root.model.hasGps
                    spacing: Theme.gapXs

                    UiIcon {
                        name: "map-pin"
                        size: 14
                        color: Theme.accent
                    }

                    Text {
                        text: root.model ? root.model.locationSummary : ""
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.textCaption
                        color: Theme.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    UiButton {
                        text: qsTr("Copy GPS")
                        tip: qsTr("Copy latitude and longitude coordinates")
                        onClicked: {
                            if (root.model) {
                                root.model.copyGps()
                                root.showCopied(qsTr("GPS Copied!"))
                            }
                        }
                    }
                }
            }
        }

        // Category Filter Pills: wrap onto a second line instead of running past a narrow panel or longer translations.
        Flow {
            Layout.fillWidth: true
            spacing: Theme.gapXs

            Repeater {
                // Filter keys stay English (the model matches them); only the pill text is translated.
                model: ["All", "Camera", "Image", "GPS", "IPTC", "Summary"]
                delegate: Rectangle {
                    id: pill
                    required property string modelData
                    readonly property string label: ({All: qsTr("All"), Camera: qsTr("Camera"), Image: qsTr("Image"), GPS: qsTr("GPS"), IPTC: qsTr("IPTC"), Summary: qsTr("Summary")})[modelData] || modelData
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Show %1 tags").arg(label)
                    Accessible.checked: active
                    readonly property bool active: root.model && root.model.categoryFilter === modelData
                    implicitWidth: pillText.implicitWidth + Theme.gapSm * 2
                    implicitHeight: 24
                    radius: Theme.radiusSmall
                    color: active ? Theme.primary : (pillHover.hovered ? Theme.hover : Theme.background)
                    border.color: active ? Theme.primary : Theme.border
                    border.width: Theme.borderWidth

                    HoverHandler { id: pillHover }
                    TapHandler {
                        onTapped: if (root.model) root.model.categoryFilter = pill.modelData
                    }

                    Text {
                        id: pillText
                        anchors.centerIn: parent
                        text: pill.label
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textCaption
                        font.bold: pill.active
                        color: pill.active ? Theme.textOnPrimary : Theme.text
                    }
                }
            }
        }

        // Search bar
        UiTextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: qsTr("Search tags (e.g. ISO, Lens, GPS)...")
            Accessible.name: qsTr("Search metadata tags")
            text: root.model ? root.model.searchFilter : ""
            onTextChanged: if (root.model) root.model.searchFilter = text
        }

        // Matching tag counter
        RowLayout {
            Layout.fillWidth: true
            visible: root.model && root.model.count > 0

            Text {
                text: qsTr("%1 tags").arg(root.model ? root.model.count : 0)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textCaption
                color: Theme.muted
                Layout.fillWidth: true
            }

            Text {
                visible: searchField.text.length > 0
                text: qsTr("filtered")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textCaption
                color: Theme.accent
            }
        }

        // Virtualized Tag List
        ListView {
            id: tagList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.model
            spacing: Theme.gapXs

            ScrollBar.vertical: UiScrollBar {}

            delegate: Rectangle {
                id: rowRect
                width: tagList.width
                implicitHeight: rowLayout.implicitHeight + Theme.gapSm * 2
                color: rowHover.hovered ? Theme.hover : (index % 2 === 0 ? Theme.surface : Theme.background)
                radius: Theme.radiusSmall
                border.color: rowHover.hovered ? Theme.border : "transparent"
                border.width: Theme.borderWidth

                HoverHandler { id: rowHover }

                RowLayout {
                    id: rowLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.gapSm
                    spacing: Theme.gapSm

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gapXs

                            Text {
                                text: model.label || model.key || ""
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textSmall
                                font.bold: true
                                color: Theme.text
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            // Category badge
                            Rectangle {
                                implicitWidth: catText.implicitWidth + 8
                                implicitHeight: 16
                                radius: 8
                                color: Qt.alpha(Theme.muted, 0.15)
                                visible: (model.category || "").length > 0

                                Text {
                                    id: catText
                                    anchors.centerIn: parent
                                    text: model.category || ""
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 10
                                    color: Theme.muted
                                }
                            }
                        }

                        Text {
                            text: model.value || model.rawValue || ""
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.textSmall
                            color: Theme.text
                            wrapMode: Text.WrapAnywhere
                            Layout.fillWidth: true
                        }
                    }

                    UiButton {
                        visible: rowHover.hovered
                        text: qsTr("Copy")
                        tip: qsTr("Copy tag value to clipboard")
                        onClicked: {
                            if (root.model) {
                                root.model.copyTag(model.key)
                                root.showCopied(qsTr("Copied %1").arg(model.label || model.key))
                            }
                        }
                    }
                }
            }
        }

        // Empty state
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"
            visible: (!root.model || root.model.count === 0) && (!root.model || !root.model.loading)

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.gapSm

                UiIcon {
                    name: "info"
                    size: 32
                    color: Theme.muted
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: searchField.text.length > 0 ? qsTr("No tags match filter") : qsTr("No metadata available")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textBody
                    color: Theme.muted
                    Layout.alignment: Qt.AlignHCenter
                }

                UiButton {
                    visible: searchField.text.length > 0
                    text: qsTr("Clear filter")
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: searchField.text = ""
                }
            }
        }

        // Loading state
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"
            visible: root.model && root.model.loading

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.gapSm

                ProgressBar {
                    indeterminate: true
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: qsTr("Reading metadata...")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                    color: Theme.muted
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }
    }
}
