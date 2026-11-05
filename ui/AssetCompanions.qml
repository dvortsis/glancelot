import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// ASSET-GROUP (F022): the part every delete, copy, move and rename confirmation shares. Lists the files that belong with the
// named ones, each with what will happen to it, and offers the one explicit choice between the named files and the whole
// asset. File-only is the default every time and is never remembered, so the owning flow resets `scope` per operation.
// Nothing is decided here: a choice asks the flow for a new plan, and what the list shows is that plan's snapshot.
ColumnLayout {
    id: root
    property var summary: null
    property string scope: "file"
    property bool replanning: false
    property string fileLabel: qsTr("Only the selected files")
    property string assetLabel: qsTr("The selected files and the files that belong with them")
    property string excludedLabel: qsTr("Not included")
    // path -> new name, for a preview that shows where each member would end up (asset rename).
    property var previews: ({})
    property int visibleRows: 5
    signal scopeRequested(string scope)

    readonly property var companions: summary && summary.companions ? summary.companions : []
    readonly property int notListed: summary && summary.companionsNotListed ? summary.companionsNotListed : 0
    readonly property bool incomplete: !!(summary && summary.companionsIncomplete)
    readonly property bool hasCompanions: companions.length > 0 || notListed > 0

    function reasonText(row) {
        switch (row.exclusion) {
        case "collision": return qsTr("It shares its name with another photo, so it isn't included.")
        case "ambiguous": return qsTr("It could belong to more than one photo, so it isn't included.")
        case "missing": return qsTr("It no longer exists.")
        case "limitReached": return qsTr("It's past the limit for one operation.")
        case "notApplicable": return root.scope === "asset" ? qsTr("An edited copy or export stays out of the set.") : ""
        default: return ""
        }
    }
    function roleText(role) {
        switch (role) {
        case "raw": return qsTr("RAW original")
        case "rendered": return qsTr("Photo")
        case "derived": return qsTr("Edited copy or export")
        case "sidecar": return qsTr("Sidecar")
        case "motionVideo": return qsTr("Motion video")
        default: return qsTr("Related file")
        }
    }

    objectName: "assetCompanions"
    visible: hasCompanions || incomplete
    spacing: Theme.gapSm

    ColumnLayout {
        visible: root.hasCompanions
        spacing: 0
        Layout.fillWidth: true
        ButtonGroup { id: scopeGroup }
        UiRadioButton {
            objectName: "assetScopeFile"
            text: root.fileLabel
            ButtonGroup.group: scopeGroup
            checked: root.scope !== "asset"
            onClicked: root.scopeRequested("file")
        }
        UiRadioButton {
            objectName: "assetScopeAsset"
            text: root.assetLabel
            ButtonGroup.group: scopeGroup
            checked: root.scope === "asset"
            onClicked: root.scopeRequested("asset")
        }
    }

    Rectangle { // the files by name: scrolls inside the dialog, so a long set never pushes the actions away
        visible: root.hasCompanions
        opacity: root.replanning ? 0.5 : 1 // the list is about to be replaced; the actions wait for it
        Layout.fillWidth: true
        implicitHeight: rows.height + 2 * Theme.gapXs
        radius: Theme.radiusControl
        color: Theme.background
        border.width: rows.activeFocus ? Theme.focusWidth : Theme.borderWidth
        border.color: rows.activeFocus ? Theme.focusRing : Theme.border
        ListView {
            id: rows
            objectName: "assetCompanionList"
            x: Theme.gapMd
            y: Theme.gapXs
            width: parent.width - 2 * Theme.gapMd
            height: Math.min(contentHeight, root.visibleRows * Theme.rowHeight + Theme.rowHeight / 2)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: contentHeight > height // only when the arrows have something to reveal
            model: root.companions
            ScrollBar.vertical: UiScrollBar {}
            footer: Label {
                objectName: "assetCompanionsMore"
                visible: root.notListed > 0
                width: rows.width
                height: visible ? Theme.rowHeight : 0
                verticalAlignment: Text.AlignVCenter
                text: qsTr("and %L1 more").arg(root.notListed)
                color: Theme.muted
                font.pixelSize: Theme.textBody
            }
            delegate: ColumnLayout {
                required property var modelData
                readonly property string reason: root.reasonText(modelData)
                readonly property string preview: root.previews[modelData.path] && root.previews[modelData.path] !== modelData.name ? root.previews[modelData.path] : ""
                width: rows.width
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.rowHeight
                    spacing: Theme.gapSm
                    Label {
                        text: modelData.name
                        elide: Text.ElideMiddle
                        color: Theme.text
                        font.pixelSize: Theme.textBody
                        Layout.fillWidth: true
                        Accessible.name: modelData.name + ", " + root.roleText(modelData.role) + ", " + (modelData.included ? qsTr("Included") : root.excludedLabel)
                    }
                    Label { // a word, never colour alone, says what happens to it
                        objectName: "assetCompanionState"
                        text: modelData.included ? qsTr("Included") : root.excludedLabel
                        color: modelData.included ? Theme.text : Theme.muted
                        font.pixelSize: Theme.textCaption
                        font.weight: modelData.included ? Font.Medium : Font.Normal
                    }
                }
                Label {
                    visible: text.length > 0
                    text: preview.length ? qsTr("Would become %1").arg(preview) : reason
                    color: Theme.muted
                    font.pixelSize: Theme.textCaption
                    wrapMode: Text.Wrap
                    bottomPadding: Theme.gapXs
                    Layout.fillWidth: true
                }
            }
        }
    }

    RowLayout { // the expansion ran out of time or couldn't read a folder
        objectName: "assetCompanionsIncomplete"
        visible: root.incomplete
        spacing: Theme.gapSm
        Layout.fillWidth: true
        UiIcon { name: "alert"; color: Theme.muted; Layout.alignment: Qt.AlignTop }
        Label {
            text: qsTr("Companions couldn't all be checked.")
            color: Theme.text
            font.pixelSize: Theme.textSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
