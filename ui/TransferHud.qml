import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// COPY-MOVE-01 (F020): Native floating HUD for asynchronous background file transfers.
// Displays live byte/item progress, active filename, pause/resume, and cancel controls.
Rectangle {
    id: root
    property var service
    // The window and panes the cut, copy and paste commands read their targets from.
    property var host
    property var gridPane
    property var treePane

    Connections {
        target: viewer
        function onViewCommand(id) {
            if (id === "cut-selection") {
                const targets = root.gridPane.deletionTargets()
                if (targets.length > 0) {
                    viewer.copyMoveService.markCut(targets)
                } else if (viewer.filePath.length > 0) {
                    viewer.copyMoveService.markCut([viewer.filePath])
                }
            }
            else if (id === "copy-selection") {
                const targets = root.gridPane.deletionTargets()
                if (targets.length > 0) {
                    viewer.copyMoveService.markCopy(targets)
                } else if (viewer.filePath.length > 0) {
                    viewer.copyMoveService.markCopy([viewer.filePath])
                }
            }
            else if (id === "paste") {
                let dest = viewer.directory.length > 0 ? viewer.directory : fileGrid.directory
                const treeFolder = root.treePane.currentSelectedFolder()
                if (root.host.focusWithin(root.treePane) && treeFolder.length > 0) dest = treeFolder
                if (dest.length > 0) {
                    viewer.copyMoveService.pasteTo(dest)
                }
            }
        }
    }

    visible: service ? service.active : false
    opacity: visible ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

    implicitWidth: Math.min(layout.implicitWidth + Theme.gapLg * 2, parent ? parent.width - Theme.gapLg * 2 : 440)
    implicitHeight: layout.implicitHeight + Theme.gapMd * 2
    radius: Theme.radiusCard
    color: Theme.surface
    border.color: Theme.border
    border.width: Theme.borderWidth

    function formatBytes(bytes) {
        if (bytes < 1024) return qsTr("%1 B").arg(bytes)
        if (bytes < 1024 * 1024) return qsTr("%1 KB").arg((bytes / 1024).toFixed(1))
        if (bytes < 1024 * 1024 * 1024) return qsTr("%1 MB").arg((bytes / (1024 * 1024)).toFixed(1))
        return qsTr("%1 GB").arg((bytes / (1024 * 1024 * 1024)).toFixed(2))
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.gapMd
        spacing: Theme.gapSm

        // Title and controls row
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapSm

            Label {
                text: root.service ? root.service.statusMessage : ""
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            // Pause / Resume Button
            UiButton {
                text: root.service && root.service.paused ? qsTr("Resume") : qsTr("Pause")
                iconName: root.service && root.service.paused ? "play" : "pause"
                iconOnly: true
                tip: root.service && root.service.paused ? qsTr("Resume transfer") : qsTr("Pause transfer")
                variant: "quiet"
                onClicked: {
                    if (!root.service) return
                    if (root.service.paused) root.service.resume()
                    else root.service.pause()
                }
            }

            // Cancel Button
            UiButton {
                text: qsTr("Cancel")
                iconName: "close"
                iconOnly: true
                tip: qsTr("Cancel transfer")
                variant: "quiet"
                onClicked: {
                    if (root.service) root.service.cancel()
                }
            }
        }

        // Progress Bar Track
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 6
            radius: 3
            color: Theme.background
            clip: true

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: parent.width * (root.service ? root.service.progress : 0)
                radius: 3
                color: root.service && root.service.paused ? Theme.muted : Theme.primary
                Behavior on width { NumberAnimation { duration: 100 } }
            }
        }

        // Details label: X of Y items (A MB / B MB)
        Label {
            text: {
                if (!root.service) return ""
                const s = root.service
                return qsTr("%1 of %2 items (%3 / %4) — %5%").arg(s.completedItems).arg(s.totalItems)
                       .arg(root.formatBytes(s.transferredBytes)).arg(root.formatBytes(s.totalBytes)).arg(Math.round(s.progress * 100))
            }
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textCaption
            Layout.fillWidth: true
        }
    }
}

