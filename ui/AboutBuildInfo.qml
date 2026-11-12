import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// REL-01: this build's provenance on the About page, under its "Glancelot <version>" heading: the BuildInfo singleton
// (src/app/build_info.*) holds the values `glancelot --version` prints. Values are selectable for bug reports. The short
// commit is shown; its tooltip and accessible description give the full SHA. An executable that does not link build_info
// has no BuildInfo, and the rows stay hidden.
GridLayout {
    id: about
    objectName: "aboutBuildInfo"
    readonly property var info: typeof BuildInfo !== "undefined" ? BuildInfo : null
    readonly property bool shaKnown: info !== null && info.sha !== "unknown"
    readonly property string kindLabel: {
        switch (info ? info.kind : "") {
        case "release": return qsTr("Release")
        case "rc": return qsTr("Release candidate")
        case "dev": return qsTr("Development build")
        case "local": return qsTr("Local build")
        default: return ""
        }
    }
    visible: info !== null
    columns: 2
    columnSpacing: Theme.gapMd
    rowSpacing: Theme.gapXs
    Layout.fillWidth: true

    component Key: Label {
        color: Theme.muted
        font.pixelSize: Theme.textSmall
        Layout.alignment: Qt.AlignLeft | Qt.AlignBaseline
    }
    // Read-only and selectable, so a user can copy a value into a bug report.
    component Value: TextEdit {
        readOnly: true
        selectByMouse: true
        color: Theme.text
        selectionColor: Theme.primary
        selectedTextColor: Theme.textOnPrimary // as UiTextField
        font.family: Theme.monoFamily
        font.pixelSize: Theme.textSmall
        font.features: { "tnum": 1 }
        Layout.alignment: Qt.AlignLeft | Qt.AlignBaseline
        Accessible.role: Accessible.StaticText
    }

    Key { text: qsTr("Commit") }
    Value {
        id: commitValue
        objectName: "aboutCommit"
        text: about.shaKnown ? about.info.shortSha : qsTr("Unknown")
        font.family: about.shaKnown ? Theme.monoFamily : Theme.fontFamily
        Accessible.name: qsTr("Commit %1").arg(text)
        Accessible.description: about.shaKnown ? about.info.sha : qsTr("This build does not record its commit.")
        HoverHandler { id: commitHover; enabled: about.shaKnown }
        ToolTip.visible: commitHover.hovered
        ToolTip.text: about.shaKnown ? about.info.sha : ""
        ToolTip.delay: 500
    }
    Key { text: qsTr("Built") }
    Value {
        objectName: "aboutBuilt"
        text: about.info ? about.info.builtAt : ""
        Accessible.name: qsTr("Built %1").arg(text)
    }
    Key { text: qsTr("Build") }
    Label {
        objectName: "aboutKind"
        text: about.kindLabel
        color: Theme.text
        font.pixelSize: Theme.textSmall
        Layout.alignment: Qt.AlignLeft | Qt.AlignBaseline
    }
}
