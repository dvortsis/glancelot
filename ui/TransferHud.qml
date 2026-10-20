import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// COPY-MOVE-01 (F020): Native floating HUD for asynchronous background file transfers.
// Displays live byte/item progress, active filename, pause/resume, and cancel controls.
Rectangle {
    id: root
    property var service

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
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GB"
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
                text: root.service && root.service.paused ? "Resume" : "Pause"
                iconName: root.service && root.service.paused ? "play" : "pause"
                iconOnly: true
                tip: root.service && root.service.paused ? "Resume transfer" : "Pause transfer"
                variant: "quiet"
                onClicked: {
                    if (!root.service) return
                    if (root.service.paused) root.service.resume()
                    else root.service.pause()
                }
            }

            // Cancel Button
            UiButton {
                text: "Cancel"
                iconName: "close"
                iconOnly: true
                tip: "Cancel transfer"
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
                return s.completedItems + " of " + s.totalItems + " items (" +
                       root.formatBytes(s.transferredBytes) + " / " + root.formatBytes(s.totalBytes) + ") — " +
                       Math.round(s.progress * 100) + "%"
            }
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textCaption
            Layout.fillWidth: true
        }
    }
}

