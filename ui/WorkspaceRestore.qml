import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// B9 (F057): the in-app restore of a workspace recovered after an unexpected exit, over the controller's RestoreOffer
// (src/app/restore_offer.*). A non-modal banner under the top bar: it never takes focus on its own, so the keys keep
// browsing the photo that was opened. Ctrl+Shift+R ("workspace-restore", also in the command palette) moves focus to its
// first action; Tab moves between its actions, Escape dismisses it (or cancels a restore that is loading) and returns
// focus to the content. States: offer, unavailable, restoring, safe and reset; SAFE and reset never restore anything.
FocusScope {
    id: banner
    objectName: "workspaceRestore"
    property var host // Main (a Window): focusContent, showSplitMessage, modalOpen
    readonly property var offer: viewer.workspaceRestore()
    readonly property string mode: offer ? offer.state : "hidden"
    readonly property bool shown: mode !== "hidden"
    readonly property int count: offer ? offer.locationCount : 0
    readonly property bool narrow: width < 420

    visible: shown || fade.running
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { id: fade; duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
    width: Math.min(560, (parent ? parent.width : 560) - 2 * Theme.gapLg)
    height: panel.implicitHeight
    activeFocusOnTab: false

    function title() {
        if (mode === "offer") return count > 1 ? qsTr("Restore your previous tabs?") : qsTr("Restore your previous folder?")
        if (mode === "unavailable") return qsTr("Your previous folder isn't available")
        if (mode === "restoring") return qsTr("Restoring your previous workspace…")
        if (mode === "safe") return qsTr("Safe mode: nothing was restored")
        if (mode === "reset") return qsTr("Settings were reset: nothing was restored")
        return ""
    }
    function body() {
        if (!offer) return ""
        if (mode === "offer") {
            let text = count > 1
                ? qsTr("Glancelot closed unexpectedly with %1 tabs open, last in %2. What you opened stays open; restoring adds those tabs beside it.").arg(count).arg(offer.folderName)
                : qsTr("Glancelot closed unexpectedly while %1 was open. What you opened stays open; restoring adds that folder as a tab.").arg(offer.folderName)
            if (offer.unavailableCount > 0)
                text += " " + (offer.unavailableCount > 1 ? qsTr("%1 of those folders can't be found right now.").arg(offer.unavailableCount)
                                                          : qsTr("One of those folders can't be found right now."))
            return text
        }
        if (mode === "unavailable")
            return count > 1 ? qsTr("Glancelot closed unexpectedly with %1 tabs open, but none of their folders can be found now. Reconnect the drive to open them again.").arg(count)
                             : qsTr("Glancelot closed unexpectedly while %1 was open, but that folder can't be found now: %2").arg(offer.folderName).arg(offer.folder)
        if (mode === "restoring") return qsTr("Opening %1. Other restored tabs load when you switch to them.").arg(offer.folderName)
        if (mode === "safe") return qsTr("Glancelot is using temporary settings. Your saved folders, tabs and preferences are kept unchanged. Restart Glancelot normally to return to them.")
        if (mode === "reset")
            return offer.resetFile.length ? qsTr("Defaults are active, so no folders or tabs were restored. Your previous settings are kept as %1.").arg(offer.resetFile)
                                          : qsTr("Defaults are active, so no folders or tabs were restored.")
        return ""
    }
    function primaryAction() { return mode === "offer" ? restoreButton : mode === "restoring" ? (cancelButton.visible ? cancelButton : null) : dismissButton }
    // Keyboard entry point (workspace-restore). Returns false when there is nothing to act on.
    function focusOffer() {
        const target = primaryAction()
        if (!shown || !target) return false
        target.forceActiveFocus(Qt.ShortcutFocusReason)
        return true
    }
    function restore() {
        const focused = banner.activeFocus
        if (!offer.accept() || !focused) return
        if (cancelButton.visible) cancelButton.forceActiveFocus(Qt.OtherFocusReason)
        else if (host && !host.modalOpen) Qt.callLater(host.focusContent)
    }
    function dismissOrCancel() {
        const focused = banner.activeFocus
        const done = mode === "restoring" ? offer.cancel() : offer.dismiss()
        if (done && focused) { if (shown && primaryAction()) primaryAction().forceActiveFocus(); else if (host && !host.modalOpen) Qt.callLater(host.focusContent) }
    }

    // The banner owns Escape while focus is inside it, before Main's window-wide Escape shortcut.
    Keys.onShortcutOverride: event => { if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) event.accepted = true }
    Keys.onEscapePressed: event => { dismissOrCancel(); event.accepted = true }

    Connections {
        target: viewer
        function onViewCommand(id) {
            if (id !== "workspace-restore") return
            if (!banner.focusOffer() && banner.host && banner.host.showSplitMessage)
                banner.host.showSplitMessage(qsTr("There is no previous workspace to restore."))
        }
    }
    Connections {
        target: banner.offer
        function onChanged() {
            if (banner.offer.feedback.length && banner.offer.feedback !== banner.lastFeedback) {
                banner.lastFeedback = banner.offer.feedback
                if (banner.host && banner.host.showSplitMessage) banner.host.showSplitMessage(banner.offer.feedback)
            }
            if (!banner.shown && banner.activeFocus && banner.host && !banner.host.modalOpen) Qt.callLater(banner.host.focusContent)
        }
    }
    property string lastFeedback: ""

    Rectangle {
        id: panel
        objectName: "workspaceRestorePanel"
        anchors.left: parent.left; anchors.right: parent.right
        implicitHeight: content.implicitHeight + 2 * Theme.gapMd
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: banner.activeFocus ? Theme.focusRing : Theme.border
        Accessible.role: Accessible.AlertMessage
        Accessible.name: banner.title()
        Accessible.description: banner.body()

        GridLayout {
            id: content
            anchors.fill: parent
            anchors.margins: Theme.gapMd
            columns: banner.narrow ? 1 : 2
            columnSpacing: Theme.gapMd
            rowSpacing: Theme.gapSm

            RowLayout {
                spacing: Theme.gapSm
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                UiIcon {
                    name: banner.mode === "restoring" ? "spinner" : banner.mode === "unavailable" ? "alert" : banner.mode === "offer" ? "folder-open" : "info"
                    color: banner.mode === "unavailable" ? Theme.danger : Theme.text
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Theme.gapXxs
                    Accessible.ignored: true
                }
                ColumnLayout {
                    spacing: Theme.gapXxs
                    Layout.fillWidth: true
                    Label {
                        objectName: "workspaceRestoreTitle"
                        text: banner.title()
                        color: Theme.text
                        font.pixelSize: Theme.textBody
                        font.weight: Font.Medium
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Label {
                        objectName: "workspaceRestoreBody"
                        text: banner.body()
                        color: Theme.muted
                        font.pixelSize: Theme.textSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
            RowLayout {
                spacing: Theme.gapSm
                Layout.alignment: banner.narrow ? Qt.AlignRight : (Qt.AlignRight | Qt.AlignVCenter)
                UiButton {
                    id: restoreButton
                    objectName: "workspaceRestoreAccept"
                    visible: banner.mode === "offer"
                    variant: "primary"
                    text: banner.count > 1 ? qsTr("Restore tabs") : qsTr("Restore folder")
                    Accessible.description: banner.body()
                    KeyNavigation.right: dismissButton; KeyNavigation.tab: dismissButton
                    onClicked: banner.restore()
                }
                UiButton {
                    id: cancelButton
                    objectName: "workspaceRestoreCancel"
                    visible: banner.mode === "restoring" && banner.offer && banner.offer.cancellable
                    variant: "outline"
                    text: qsTr("Cancel")
                    onClicked: banner.dismissOrCancel()
                }
                UiButton {
                    id: dismissButton
                    objectName: "workspaceRestoreDismiss"
                    visible: banner.mode === "offer" || banner.mode === "unavailable" || banner.mode === "safe" || banner.mode === "reset"
                    variant: banner.mode === "offer" ? "quiet" : "outline"
                    text: qsTr("Dismiss")
                    Accessible.description: banner.mode === "offer" || banner.mode === "unavailable" ? qsTr("Don't offer this workspace again") : ""
                    KeyNavigation.left: restoreButton; KeyNavigation.backtab: restoreButton
                    onClicked: banner.dismissOrCancel()
                }
            }
        }
    }
}
