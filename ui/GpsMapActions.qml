import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// F3 (F072): the metadata inspector's location actions. "Show on map" hands one URL to the default browser, only when
// the user asks; "Copy coordinates" is the no-network alternative. The viewer itself never contacts a map service.
// Owns the "show-on-map" and "copy-coordinates" commands (EN-07), so they work while the inspector is hidden too.
ColumnLayout {
    id: actions
    objectName: "gpsMapActions"
    property var model: null // the inspector's MetadataModel
    readonly property alias handoff: handoff
    spacing: Theme.gapXxs
    // Shown for a usable position, and for a damaged one (to say why there is no map link). Missing GPS shows nothing.
    visible: handoff.available || (handoff.problem.length > 0 && handoff.problem !== "missing" && handoff.problem !== "pending")

    GpsMapHandoff {
        id: handoff
        objectName: "gpsMapHandoff"
        metadata: actions.model
        currentPath: viewer.filePath
        onOpened: url => actions.announce(qsTr("Opened %1 in your browser.").arg(url.host))
        onRefused: problem => actions.announce(actions.reasonFor(problem))
    }

    // Why there is no map link, in words a screen reader can read out. Every code GpsMapHandoff reports has one.
    function reasonFor(problem) {
        switch (problem) {
        case "pending": return qsTr("The photo's location is still being read. Try again in a moment.")
        case "missing": return qsTr("This photo has no location data.")
        case "partial": return qsTr("The location data is incomplete, so there is no map link.")
        case "reference": return qsTr("The location has no valid north/south or east/west reference, so there is no map link.")
        case "range": return qsTr("The recorded coordinates are out of range, so there is no map link.")
        case "null-island": return qsTr("The location is 0, 0, which cameras record when they had no position, so there is no map link.")
        case "template": return qsTr("The custom map link in Settings is not usable. Fix it or choose OpenStreetMap.")
        case "open-failed": return qsTr("The system could not open a browser for the map link.")
        }
        return ""
    }
    readonly property string mapReason: !handoff.available ? reasonFor(handoff.problem)
        : handoff.templateProblem.length ? reasonFor("template") : ""
    function showOnMap() { handoff.openMap() }
    function copyCoordinates() {
        if (handoff.copyCoordinates().length) announce(qsTr("Coordinates copied: %1").arg(handoff.coordinates))
    }
    function announce(text) {
        if (!text.length) return
        notice.text = text
        notice.open()
        noticeTimer.restart()
    }

    Connections {
        target: viewer
        function onViewCommand(id) {
            if (id === "show-on-map") actions.showOnMap()
            else if (id === "copy-coordinates") actions.copyCoordinates()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gapXs
        visible: handoff.available

        UiButton {
            objectName: "gpsShowOnMap"
            text: qsTr("Show on map")
            variant: "outline"
            enabled: handoff.templateProblem.length === 0
            tip: handoff.providerHost.length ? qsTr("Opens %1 in your web browser with this photo's coordinates").arg(handoff.providerHost) : actions.mapReason
            Accessible.description: tip
            onClicked: actions.showOnMap()
        }
        UiButton {
            objectName: "gpsCopyCoordinates"
            text: qsTr("Copy coordinates")
            tip: qsTr("Copy %1 as decimal degrees, without opening anything").arg(handoff.coordinates)
            Accessible.description: tip
            onClicked: actions.copyCoordinates()
        }
        Item { Layout.fillWidth: true }
    }

    // The reason, in visible text: a damaged position, or a custom map link that cannot be used.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gapXs
        visible: reasonText.text.length > 0
        UiIcon {
            name: "alert"
            size: 14
            color: Theme.muted
            Layout.alignment: Qt.AlignTop
        }
        Text {
            id: reasonText
            objectName: "gpsMapReason"
            text: actions.mapReason
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textCaption
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    // Feedback for the command and buttons that stays readable while the inspector is hidden or narrow.
    Popup {
        id: notice
        objectName: "gpsMapNotice"
        property alias text: noticeLabel.text
        parent: Overlay.overlay
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: Theme.controlHeight + Theme.gapLg * 2 + Theme.gapSm // under the top bar, clear of the canvas notices at the bottom
        width: parent ? Math.min(implicitWidth, parent.width - 2 * Theme.gapMd) : implicitWidth
        padding: Theme.gapSm
        leftPadding: Theme.gapMd
        rightPadding: Theme.gapMd
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        focus: false
        modal: false
        background: Rectangle { color: Theme.surface; radius: Theme.radiusCard; border.width: Theme.borderWidth; border.color: Theme.border }
        contentItem: Label {
            id: noticeLabel
            color: Theme.text
            font.pixelSize: Theme.textSmall
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
    }
    Timer { id: noticeTimer; interval: 5000; onTriggered: notice.close() }
}
