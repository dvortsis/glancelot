import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// RENAME-01 (F018): Native modal dialogs for in-browser file/folder renaming and new folder creation.
// Enforces source safety (SYS-06): explicit target snapshot, instant inline validation, case-only rename
// handling, collision prevention, and atomic reconciliation across viewer, tree, and models.
Item {
    id: root
    property var host
    property var mutationService

    readonly property bool modal: renameDialog.visible || newFolderDialog.visible

    // State for Rename Dialog
    property string renameTargetPath: ""
    property string renameParentDir: ""
    property string renameInitialName: ""
    property bool renameIsFolder: false
    property var renameValidation: ({ valid: false, error: "" })
    // ASSET-GROUP: what the rename leaves behind, from a worker, and the choice. File-only every time, never remembered.
    property string renameScope: "file"
    property var renamePlan: null
    property real renamePlanId: 0
    readonly property var renamePreviews: {
        const names = {}
        if (renameScope === "asset" && renamePlan) for (const row of renamePlan.renames) names[row.from] = row.to.split("/").pop()
        return names
    }
    readonly property bool renameRefused: renameScope === "asset" && !!renamePlan && renamePlan.refused
    readonly property bool renamePlanCurrent: !!renamePlan && renamePlan.id === renamePlanId
    readonly property bool renameAllowed: renameValidation.valid && renameField.text !== renameInitialName && !(renameScope === "asset")

    // State for New Folder Dialog
    property string newFolderParentDir: ""
    property var newFolderValidation: ({ valid: false, error: "" })

    signal flowClosed()

    function openRename(targetPath) {
        if (!targetPath || targetPath.length === 0) return
        if (!mutationService) return

        renameTargetPath = targetPath.replace(/\\/g, "/")
        // Extract parent directory and name
        const cleanPath = renameTargetPath.replace(/\/+$/, "")
        const lastSlash = cleanPath.lastIndexOf("/")
        if (lastSlash < 0) return

        renameParentDir = cleanPath.substring(0, lastSlash)
        if (renameParentDir.length === 2 && renameParentDir.charAt(1) === ':') {
            renameParentDir += "/"
        }
        const fileName = cleanPath.substring(lastSlash + 1)
        renameInitialName = fileName

        // Detect if folder by checking extension or target
        const dot = fileName.lastIndexOf(".")
        renameIsFolder = (dot <= 0)

        renameScope = "file"
        renamePlan = null
        renameField.text = fileName
        renameValidation = mutationService.validateTargetName(renameParentDir, fileName, renameTargetPath)
        planPreview()
        renameDialog.open()

        // Focus and select base name without extension for easy editing
        Qt.callLater(() => {
            renameField.forceActiveFocus()
            if (dot > 0) {
                renameField.select(0, dot)
            } else {
                renameField.selectAll()
            }
        })
    }

    function openNewFolder(parentDirectory) {
        if (!parentDirectory || parentDirectory.length === 0) return
        if (!mutationService) return

        newFolderParentDir = parentDirectory.replace(/\\/g, "/")
        const suggested = mutationService.suggestNewFolderName(newFolderParentDir)
        newFolderField.text = suggested
        newFolderValidation = mutationService.validateTargetName(newFolderParentDir, suggested)
        newFolderDialog.open()

        Qt.callLater(() => {
            newFolderField.forceActiveFocus()
            newFolderField.selectAll()
        })
    }

    function planPreview() { renamePlanId = mutationService.planRename(renameTargetPath, renameField.text, renameScope) }
    function setRenameScope(next) {
        if (next === renameScope) return
        renameScope = next
        planPreview()
    }

    function executeRename() {
        if (renameScope === "asset") return // refused, with the reason on screen, until the operation journal lands
        if (!renameValidation.valid || renameField.text === renameInitialName) {
            renameDialog.close()
            return
        }
        const res = mutationService.renamePath(renameTargetPath, renameField.text)
        if (res && res.success) {
            renameDialog.close()
        } else {
            renameValidation = { valid: false, error: res && res.error ? res.error : qsTr("Failed to rename item.") }
        }
    }

    function executeCreateFolder() {
        if (!newFolderValidation.valid) return
        const res = mutationService.createFolder(newFolderParentDir, newFolderField.text)
        if (res && res.success) {
            newFolderDialog.close()
        } else {
            newFolderValidation = { valid: false, error: res && res.error ? res.error : qsTr("Failed to create folder.") }
        }
    }

    Connections {
        target: root.mutationService
        function onRenamePlanReady(id, plan) { if (id === root.renamePlanId) root.renamePlan = plan }
    }
    Timer { // the worker's preview follows typing without a directory read per key press
        id: previewDelay
        interval: 150
        onTriggered: if (root.renameTargetPath.length > 0) root.planPreview()
    }

    // 1. Rename Dialog
    UiDialog {
        id: renameDialog
        title: root.renameIsFolder ? qsTr("Rename Folder") : qsTr("Rename File")
        width: Math.min(480, host ? host.width - Theme.gapXl * 2 : 480)
        anchors.centerIn: parent

        onClosed: {
            previewDelay.stop()
            root.mutationService.planRename("", "", "file") // withdraws the preview, and stops its lookups
            root.renamePlanId = 0
            root.renamePlan = null
            root.renameTargetPath = ""
            root.flowClosed()
        }

        ColumnLayout {
            spacing: Theme.gapMd
            width: parent.width

            Label {
                text: root.renameIsFolder ? qsTr("Enter a new name for this folder:") : qsTr("Enter a new name for this file:")
                color: Theme.muted
                font.pixelSize: Theme.textSmall
                font.family: Theme.fontFamily
                Layout.fillWidth: true
            }

            UiTextField {
                id: renameField
                Layout.fillWidth: true
                Accessible.name: root.renameIsFolder ? qsTr("New folder name") : qsTr("New file name")
                onTextChanged: {
                    if (root.mutationService && root.renameParentDir.length > 0) {
                        root.renameValidation = root.mutationService.validateTargetName(root.renameParentDir, text, root.renameTargetPath)
                        previewDelay.restart()
                    }
                }
                Keys.onReturnPressed: event => {
                    if (root.renameAllowed) {
                        root.executeRename()
                        event.accepted = true
                    }
                }
                Keys.onEnterPressed: event => {
                    if (root.renameAllowed) {
                        root.executeRename()
                        event.accepted = true
                    }
                }
            }

            Label {
                id: renameErrorLabel
                visible: !root.renameValidation.valid && root.renameValidation.error.length > 0
                text: root.renameValidation.error
                color: Theme.danger
                font.pixelSize: Theme.textCaption
                font.family: Theme.fontFamily
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            AssetCompanions {
                summary: root.renamePlan
                scope: root.renameScope
                replanning: !root.renamePlanCurrent
                fileLabel: qsTr("Rename only this file")
                assetLabel: qsTr("Rename every file in the set")
                excludedLabel: qsTr("Keeps its name")
                previews: root.renamePreviews
                visibleRows: root.host && root.host.height < 480 ? 2 : 4
                Layout.fillWidth: true
                onScopeRequested: next => root.setRenameScope(next)
            }

            RowLayout { // asset scope previews every member and is refused, with the reason, until the operation journal lands
                objectName: "renameRefusal"
                visible: root.renameRefused
                spacing: Theme.gapSm
                Layout.fillWidth: true
                UiIcon { name: "alert"; color: Theme.muted; Layout.alignment: Qt.AlignTop }
                Label {
                    text: root.renamePlan && root.renamePlan.refusal ? root.renamePlan.refusal : ""
                    color: Theme.text
                    font.pixelSize: Theme.textSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            RowLayout {
                spacing: Theme.gapSm
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: Theme.gapSm

                UiButton {
                    objectName: "renameCancel"
                    text: qsTr("Cancel")
                    variant: "quiet"
                    onClicked: renameDialog.close()
                }

                UiButton {
                    objectName: "renameConfirm"
                    text: qsTr("Rename")
                    variant: "primary"
                    enabled: root.renameAllowed
                    onClicked: root.executeRename()
                }
            }
        }
    }

    // 2. New Folder Dialog
    UiDialog {
        id: newFolderDialog
        title: qsTr("Create New Folder")
        width: Math.min(480, host ? host.width - Theme.gapXl * 2 : 480)
        anchors.centerIn: parent

        onClosed: {
            root.newFolderParentDir = ""
            root.flowClosed()
        }

        ColumnLayout {
            spacing: Theme.gapMd
            width: parent.width

            Label {
                text: qsTr("Enter a name for the new folder:")
                color: Theme.muted
                font.pixelSize: Theme.textSmall
                font.family: Theme.fontFamily
                Layout.fillWidth: true
            }

            UiTextField {
                id: newFolderField
                Layout.fillWidth: true
                Accessible.name: qsTr("Name for the new folder")
                onTextChanged: {
                    if (root.mutationService && root.newFolderParentDir.length > 0) {
                        root.newFolderValidation = root.mutationService.validateTargetName(root.newFolderParentDir, text)
                    }
                }
                Keys.onReturnPressed: event => {
                    if (root.newFolderValidation.valid) {
                        root.executeCreateFolder()
                        event.accepted = true
                    }
                }
                Keys.onEnterPressed: event => {
                    if (root.newFolderValidation.valid) {
                        root.executeCreateFolder()
                        event.accepted = true
                    }
                }
            }

            Label {
                id: newFolderErrorLabel
                visible: !root.newFolderValidation.valid && root.newFolderValidation.error.length > 0
                text: root.newFolderValidation.error
                color: Theme.danger
                font.pixelSize: Theme.textCaption
                font.family: Theme.fontFamily
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            RowLayout {
                spacing: Theme.gapSm
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: Theme.gapSm

                UiButton {
                    text: qsTr("Cancel")
                    variant: "quiet"
                    onClicked: newFolderDialog.close()
                }

                UiButton {
                    text: qsTr("Create")
                    variant: "primary"
                    enabled: root.newFolderValidation.valid
                    onClicked: root.executeCreateFolder()
                }
            }
        }
    }
}

