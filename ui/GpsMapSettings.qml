import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// F3 (F072): where "Show on map" sends a photo's coordinates. Lives on the Viewing page of Settings; the choice is shared
// with the metadata inspector through GpsMapHandoff and saved in settings.json ("mapHandoff").
ColumnLayout {
    id: page
    objectName: "gpsMapSettings"
    required property var settings // the Settings dialog: rowMatches() for the search filter
    readonly property alias handoff: handoff
    readonly property string customExample: "https://www.example.org/map?lat={lat}&lon={lon}&z={zoom}"
    // The text being typed, checked live; saved when editing finishes.
    readonly property string draftProblem: handoff.checkTemplate(templateField.text)
    Layout.fillWidth: true
    spacing: Theme.gapSm
    visible: settings.rowMatches(qsTr("Map links"), qsTr("Show on map provider OpenStreetMap custom link location privacy"))

    GpsMapHandoff { id: handoff; objectName: "gpsMapSettingsHandoff" }
    onVisibleChanged: if (visible) handoff.reloadSettings() // another window may have changed the choice

    function problemText(code) {
        switch (code) {
        case "too-long": return qsTr("The link is too long.")
        case "placeholder": return qsTr("The link needs both %1 and %2.").arg("{lat}").arg("{lon}")
        case "unknown": return qsTr("Only %1, %2 and %3 can be filled in.").arg("{lat}").arg("{lon}").arg("{zoom}")
        case "scheme": return qsTr("The link must start with %1 or %2.").arg("https://").arg("http://")
        case "malformed": return qsTr("That is not a valid web address.")
        }
        return ""
    }

    Label {
        text: qsTr("Map links")
        color: Theme.muted
        font.pixelSize: Theme.textSmall
        font.weight: Font.DemiBold
        Layout.topMargin: Theme.gapSm
        Layout.fillWidth: true
        Accessible.role: Accessible.Heading
    }
    SettingRow {
        label: qsTr("Map provider")
        description: qsTr("Used by Show on map in the metadata inspector.")
        modified: handoff.provider !== "openstreetmap"
        UiComboBox {
            objectName: "gpsMapProvider"
            readonly property var providers: ["openstreetmap", "custom"]
            model: [qsTr("OpenStreetMap"), qsTr("Custom link")]
            currentIndex: Math.max(0, providers.indexOf(handoff.provider))
            onActivated: handoff.provider = providers[currentIndex]
            Accessible.name: qsTr("Map provider")
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.gapXxs
        visible: handoff.provider === "custom"
        Label {
            text: qsTr("Custom map link")
            color: Theme.text
            font.pixelSize: Theme.textBody
            Layout.fillWidth: true
        }
        UiTextField {
            id: templateField
            objectName: "gpsMapTemplate"
            Layout.fillWidth: true
            mono: true
            text: handoff.customTemplate
            placeholderText: page.customExample
            maximumLength: 2048
            Accessible.name: qsTr("Custom map link")
            Accessible.description: draftNote.text
            onEditingFinished: handoff.customTemplate = text
        }
        Label {
            id: draftNote
            objectName: "gpsMapTemplateNote"
            // An empty field is not an error yet: it shows the hint (Show on map stays off and says why).
            readonly property bool failing: page.draftProblem.length > 0 && page.draftProblem !== "empty"
            text: failing ? page.problemText(page.draftProblem)
                : qsTr("%1 and %2 become decimal degrees and %3 a street-level zoom.").arg("{lat}").arg("{lon}").arg("{zoom}")
            color: failing ? Theme.danger : Theme.muted
            font.pixelSize: Theme.textSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.maximumWidth: page.settings.measure || -1
        }
    }
    Label {
        objectName: "gpsMapPrivacyNote"
        text: handoff.providerHost.length
            ? qsTr("Show on map opens %1 in your web browser and sends it the photo's coordinates. Glancelot never contacts a map service itself, and Copy coordinates sends nothing.").arg(handoff.providerHost)
            : qsTr("Glancelot never contacts a map service itself. Show on map stays off until the custom link is valid; Copy coordinates sends nothing.")
        color: Theme.muted
        font.pixelSize: Theme.textSmall
        wrapMode: Text.Wrap
        lineHeight: 1.4
        Layout.fillWidth: true
        Layout.maximumWidth: page.settings.measure || -1
    }
    Label {
        visible: handoff.saveError.length > 0
        text: qsTr("The map provider could not be saved. It applies until Glancelot closes.")
        color: Theme.danger
        font.pixelSize: Theme.textSmall
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
}
