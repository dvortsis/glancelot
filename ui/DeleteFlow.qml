import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// DELETE-01: the confirmation, batch progress, success notice and failure results for moving items to the
// Trash/Recycle Bin. FileOperationService owns the work; this file presents one captured plan summary, so a selection
// or navigation change behind the dialog can never change what is confirmed. Cancel holds the initial focus and the
// destructive button ignores auto-repeated keys, so a held Enter or Delete cannot confirm.
Item {
    id: flow
    property var host
    readonly property string trashName: Qt.platform.os === "windows" ? "Recycle Bin" : "Trash"
    readonly property bool modal: confirmDialog.visible || resultsDialog.visible
    // "" | "planning" | "confirm" | "executing"
    property string phase: ""
    property real planId: 0
    property real executionId: 0
    property int requestedCount: 0
    property string requestFolder: ""
    property var summary: null
    property int done: 0
    property int total: 0
    property bool stopping: false
    property int movedCount: 0
    property var failures: []
    property string noticeText: ""
    // Folders where every requested item was on a read-only volume; the grid disables its action there.
    property var readOnlyFolders: ({})
    readonly property bool compactHeight: host ? host.height < 480 : false
    signal flowClosed()

    function folderKey(path) {
        let key = path.replace(/\\/g, "/").replace(/\/+$/, "")
        return Qt.platform.os === "windows" ? key.toLowerCase() : key
    }
    function isReadOnly(folder) { return folder.length > 0 && readOnlyFolders[folderKey(folder)] === true }
    function count(n, one, many) { return Number(n).toLocaleString(Qt.locale(), "f", 0) + " " + (n === 1 ? one : many) }
    function scopeText(s) {
        if (!s) return ""
        const parts = []
        if (s.fileCount > 0) parts.push(count(s.fileCount, "file", "files"))
        if (s.folderCount > 0) parts.push(count(s.folderCount, "folder", "folders"))
        let text = parts.join(" and ")
        if (s.folderCount > 0) {
            const inner = []
            if (s.contentFiles > 0) inner.push(count(s.contentFiles, "file", "files"))
            if (s.contentFolders > 0) inner.push(count(s.contentFolders, "folder", "folders"))
            if (inner.length) text += " (containing " + (s.contentCountComplete ? "" : "at least ") + inner.join(" and ") + ")"
            else if (s.contentCountComplete) text += s.folderCount === 1 ? " (empty)" : " (all empty)"
        }
        return text
    }
    function rejectionText(reason) {
        return ({ missing: "It no longer exists.",
                  root: "Drives and other top-level locations can't be moved to the " + trashName + ".",
                  duplicate: "It's inside another item that's already included.",
                  inaccessible: "It can't be read, or it's reached through a link.",
                  readOnlyVolume: "It's on a read-only drive.",
                  special: "It's a device or system file." })[reason] || "It can't be moved."
    }
    function outcomeText(outcome, message) {
        return ({ missing: "It was already moved or deleted.",
                  changed: "It changed after you confirmed, so it was left in place.",
                  denied: "You don't have permission to move it.",
                  inUse: "It's open in another app. Close it and try again.",
                  trashUnavailable: "This drive has no " + trashName + " that can take it, so it was left in place. Nothing was deleted.",
                  cancelled: "Not moved, because you stopped the operation." })[outcome]
               || "It couldn't be moved" + (message && message.length ? ": " + message + "." : ".")
    }
    readonly property var targets: summary ? summary.targets : []
    readonly property var rejected: summary ? summary.rejected : []
    readonly property int shownNames: compactHeight ? 3 : 5
    readonly property string title: {
        if (phase === "executing") return "Moving to the " + trashName + "…"
        if (phase === "planning") return requestedCount === 1 ? qsTr("Move this item to the %1?").arg(trashName) : qsTr("Move %1 items to the %2?").arg(requestedCount).arg(trashName)
        if (targets.length === 0) return "Nothing can be moved"
        if (targets.length === 1) return "Move this " + (targets[0].kind === "folder" ? "folder" : "file") + " to the " + trashName + "?"
        return "Move " + count(targets.length, "item", "items") + " to the " + trashName + "?"
    }

    // Entry point for every origin (grid header, tree row menu, command). Nothing is touched until confirm.
    function request(paths, origin, folder) {
        if (!paths || paths.length === 0) return
        if (fileOperations.busy) { showNotice("Wait for the current move to the " + trashName + " to finish."); return }
        summary = null; stopping = false; done = 0; total = 0
        requestedCount = paths.length
        requestFolder = folder || ""
        phase = "planning"
        planId = fileOperations.plan(paths, origin)
        confirmDialog.open()
    }
    function confirmPlan() {
        if (phase !== "confirm" || targets.length === 0) return
        const id = planId
        if (!fileOperations.confirm(id)) { closeConfirm(); showNotice("That list is out of date. Select the items again."); return }
        executionId = id; planId = 0
        total = targets.length; done = 0
        if (total > 1) { phase = "executing"; secondary.forceActiveFocus(Qt.TabFocusReason) } // Stop takes focus
        else { phase = ""; confirmDialog.close() }
    }
    function cancelOrStop() {
        if (phase === "executing") { stopping = true; fileOperations.cancel(executionId); return }
        closeConfirm()
    }
    function closeConfirm() { if (confirmDialog.visible) confirmDialog.close(); else discardPlan() }
    function discardPlan() {
        if (planId !== 0) fileOperations.cancel(planId)
        planId = 0
        if (phase !== "executing") phase = ""
    }
    function showNotice(text) { noticeText = text; noticeTimer.restart() }

    Connections {
        target: fileOperations
        function onPlanReady(id, plan) {
            if (id !== flow.planId) return
            flow.summary = plan
            flow.phase = "confirm"
            const rejected = plan.rejected
            if (plan.targets.length === 0 && rejected.length > 0 && rejected.every(r => r.reason === "readOnlyVolume") && flow.requestFolder.length) {
                const next = Object.assign({}, flow.readOnlyFolders); next[flow.folderKey(flow.requestFolder)] = true; flow.readOnlyFolders = next
            }
        }
        function onPlanFailed(id, message) {
            if (id !== flow.planId) return
            flow.planId = 0; flow.phase = ""
            confirmDialog.close()
            flow.showNotice(message)
        }
        function onProgress(id, done, total) { if (id === flow.executionId) { flow.done = done; flow.total = total } }
        function onFinished(id, results) {
            if (id !== flow.executionId) return
            flow.executionId = 0
            let moved = 0
            const failed = []
            for (const r of results) { if (r.outcome === "trashed") ++moved; else failed.push(r) }
            flow.movedCount = moved
            flow.failures = failed
            flow.phase = ""
            if (confirmDialog.visible) confirmDialog.close()
            if (failed.length === 0)
                flow.showNotice(moved === 1 ? qsTr("Moved 1 item to the %1. You can restore it from there.").arg(flow.trashName) : qsTr("Moved %1 items to the %2. You can restore them from there.").arg(moved).arg(flow.trashName))
            else resultsDialog.open()
        }
    }

    component Caption: Label {
        color: Theme.muted
        font.pixelSize: Theme.textSmall
        wrapMode: Text.Wrap
        lineHeight: 1.4
        Layout.fillWidth: true
    }

    UiDialog {
        id: confirmDialog
        objectName: "deleteConfirmation"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, (parent ? parent.width : 480) - 2 * Theme.gapLg)
        // Whole-dialog height stays inside the window; the details scroll when space is short.
        readonly property real maximumBody: (parent ? parent.height : 600) - 2 * Theme.gapLg - topPadding - bottomPadding - implicitHeaderHeight - Theme.controlHeight - Theme.gapLg - 8
        title: flow.title
        closePolicy: flow.phase === "executing" ? Popup.NoAutoClose : Popup.CloseOnEscape
        closable: flow.phase !== "executing" // a running batch is stopped with Stop, never dismissed
        onOpened: secondary.forceActiveFocus(Qt.TabFocusReason)
        onClosed: { if (flow.phase !== "executing") flow.discardPlan(); if (!resultsDialog.visible) flow.flowClosed() }
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gapLg
            ScrollView {
                id: details
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(body.implicitHeight, Math.max(Theme.rowHeight * 2, confirmDialog.maximumBody))
                contentWidth: availableWidth
                clip: true
                ScrollBar.vertical: UiScrollBar {}
                ColumnLayout {
                    id: body
                    width: details.availableWidth
                    spacing: Theme.gapMd
                    RowLayout { // planning: immediate feedback while the worker reads the items
                        objectName: "deletePlanning"
                        visible: flow.phase === "planning"
                        spacing: Theme.gapSm
                        UiIcon { name: "spinner"; color: Theme.muted }
                        Label { text: qsTr("Checking the selected items…"); color: Theme.muted; font.pixelSize: Theme.textBody; Layout.fillWidth: true }
                    }
                    Rectangle { // the named items: up to five (three in short windows), then "and N more"
                        objectName: "deleteTargets"
                        visible: flow.phase === "confirm" && flow.targets.length > 0
                        Layout.fillWidth: true
                        implicitHeight: names.implicitHeight + 2 * Theme.gapXs
                        radius: Theme.radiusControl
                        color: Theme.background
                        border.width: Theme.borderWidth
                        border.color: Theme.border
                        Column {
                            id: names
                            x: Theme.gapMd; y: Theme.gapXs
                            width: parent.width - 2 * Theme.gapMd
                            Repeater {
                                model: flow.targets.slice(0, flow.shownNames)
                                delegate: RowLayout {
                                    required property var modelData
                                    width: names.width
                                    height: Theme.rowHeight
                                    spacing: Theme.gapSm
                                    UiIcon { name: modelData.link ? "link" : (modelData.kind === "folder" ? "folder" : "file"); color: Theme.muted }
                                    Label {
                                        text: modelData.name
                                        elide: Text.ElideMiddle
                                        color: Theme.text
                                        font.pixelSize: Theme.textBody
                                        Layout.fillWidth: true
                                        Accessible.name: modelData.name + (modelData.kind === "folder" ? ", folder" : ", file") + (modelData.link ? ", link" : "")
                                    }
                                    Label { visible: modelData.link; text: qsTr("link only"); color: Theme.muted; font.pixelSize: Theme.textCaption }
                                }
                            }
                            Label {
                                objectName: "deleteMore"
                                visible: flow.targets.length > flow.shownNames
                                height: Theme.rowHeight
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: Theme.iconSize + Theme.gapSm
                                text: "and " + (flow.targets.length - flow.shownNames).toLocaleString(Qt.locale(), "f", 0) + " more"
                                color: Theme.muted
                                font.pixelSize: Theme.textBody
                            }
                        }
                    }
                    Label {
                        objectName: "deleteScope"
                        visible: flow.phase === "confirm" && flow.targets.length > 0
                        text: flow.scopeText(flow.summary)
                        color: Theme.text
                        font.pixelSize: Theme.textBody
                        font.features: { "tnum": 1 }
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    ColumnLayout { // items that will not be moved, each with its reason
                        objectName: "deleteRejected"
                        visible: flow.phase === "confirm" && flow.rejected.length > 0
                        spacing: Theme.gapXs
                        Layout.fillWidth: true
                        Label {
                            text: flow.targets.length === 0 ? (flow.rejected.length === 1 ? "This item can't be moved:" : "These items can't be moved:")
                                                            : "Not included (" + flow.rejected.length + "):"
                            color: Theme.text
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.DemiBold
                        }
                        Repeater {
                            model: flow.rejected.slice(0, flow.shownNames)
                            delegate: ColumnLayout {
                                required property var modelData
                                spacing: 0
                                Layout.fillWidth: true
                                Label { text: modelData.name; elide: Text.ElideMiddle; color: Theme.text; font.pixelSize: Theme.textBody; Layout.fillWidth: true }
                                Caption { text: flow.rejectionText(modelData.reason) }
                            }
                        }
                        Caption { visible: flow.rejected.length > flow.shownNames; text: "and " + (flow.rejected.length - flow.shownNames) + " more" }
                    }
                    Caption {
                        objectName: "deleteNote"
                        visible: flow.phase === "confirm" && flow.targets.length > 0
                        // Short windows keep the essentials so the note never ends under the fold.
                        text: flow.compactHeight ? "Only these items are moved; sidecars and pairs stay. You can restore them from the " + flow.trashName + "."
                              : "Only these items are moved. Files that share their names, such as .xmp sidecars or RAW+JPEG pairs, stay where they are. You can restore moved items from the " + flow.trashName + "."
                    }
                    ColumnLayout { // batch progress
                        objectName: "deleteProgress"
                        visible: flow.phase === "executing"
                        spacing: Theme.gapSm
                        Layout.fillWidth: true
                        ProgressBar {
                            id: bar
                            Layout.fillWidth: true
                            from: 0; to: Math.max(1, flow.total); value: flow.done
                            Accessible.name: "Processed " + flow.done + " of " + flow.total
                            background: Rectangle { implicitHeight: 6; radius: 3; color: Theme.hover }
                            contentItem: Item {
                                implicitHeight: 6
                                Rectangle { width: bar.visualPosition * parent.width; height: parent.height; radius: 3; color: Theme.primary }
                            }
                        }
                        Label {
                            text: flow.stopping ? "Stopping after the current item…" : "Processed " + flow.done.toLocaleString(Qt.locale(), "f", 0) + " of " + flow.total.toLocaleString(Qt.locale(), "f", 0)
                            color: Theme.text
                            font.pixelSize: Theme.textBody
                            font.features: { "tnum": 1 }
                        }
                        Caption { text: "Items already moved stay in the " + flow.trashName + "." }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gapSm
                Item { Layout.fillWidth: true }
                UiButton {
                    id: secondary
                    objectName: "deleteCancel"
                    variant: "outline"
                    text: flow.phase === "executing" ? (flow.stopping ? "Stopping…" : "Stop") : (flow.phase === "confirm" && flow.targets.length === 0 ? "Close" : "Cancel")
                    enabled: !flow.stopping
                    onClicked: flow.cancelOrStop()
                }
                UiButton {
                    id: destructive
                    objectName: "deleteConfirm"
                    visible: flow.phase !== "executing" && !(flow.phase === "confirm" && flow.targets.length === 0)
                    variant: "danger"
                    ignoreKeyRepeat: true
                    iconName: "trash"
                    text: "Move to " + flow.trashName
                    enabled: flow.phase === "confirm" && flow.targets.length > 0
                    Layout.maximumWidth: implicitWidth
                    onClicked: flow.confirmPlan()
                }
            }
        }
    }

    UiDialog {
        id: resultsDialog
        objectName: "deleteResults"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, (parent ? parent.width : 520) - 2 * Theme.gapLg)
        readonly property real maximumList: (parent ? parent.height : 600) - 2 * Theme.gapLg - topPadding - bottomPadding - implicitHeaderHeight - Theme.controlHeight - 120
        title: flow.movedCount > 0 ? qsTr("Moved %1 of %2 items to the %3").arg(flow.movedCount).arg(flow.movedCount + flow.failures.length).arg(flow.trashName)
                                   : "Nothing was moved to the " + flow.trashName
        onOpened: closeResults.forceActiveFocus(Qt.TabFocusReason)
        onClosed: flow.flowClosed()
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gapMd
            Label {
                text: flow.failures.length === 1 ? qsTr("This item was left in place:") : qsTr("These %1 items were left in place:").arg(flow.failures.length)
                color: Theme.text
                font.pixelSize: Theme.textBody
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            ListView {
                id: failureList
                objectName: "deleteFailures"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, Math.max(96, resultsDialog.maximumList))
                clip: true
                spacing: Theme.gapSm
                boundsBehavior: Flickable.StopAtBounds
                model: flow.failures
                ScrollBar.vertical: UiScrollBar {}
                delegate: ColumnLayout {
                    required property var modelData
                    width: failureList.width - Theme.gapSm
                    spacing: Theme.gapXxs
                    RowLayout {
                        spacing: Theme.gapSm
                        Layout.fillWidth: true
                        UiIcon { name: "alert"; color: Theme.danger }
                        Label { text: modelData.name; elide: Text.ElideMiddle; color: Theme.text; font.pixelSize: Theme.textBody; font.weight: Font.Medium; Layout.fillWidth: true }
                    }
                    Caption {
                        objectName: "deleteFailureReason"
                        text: flow.outcomeText(modelData.outcome, modelData.message)
                        color: Theme.text
                        Layout.leftMargin: Theme.iconSize + Theme.gapSm
                    }
                    Label {
                        text: modelData.path
                        elide: Text.ElideMiddle
                        color: Theme.muted
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.textCaption
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.iconSize + Theme.gapSm
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gapMd
                Caption { // recovery is honest: there is no in-app undo, the system trash restores
                    text: flow.movedCount > 0 ? qsTr("Moved items can be restored from the %1.").arg(flow.trashName) : qsTr("Nothing was deleted.")
                    Layout.alignment: Qt.AlignVCenter
                }
                UiButton { id: closeResults; objectName: "deleteResultsClose"; variant: "primary"; text: qsTr("Close"); onClicked: resultsDialog.close() }
            }
        }
    }

    Rectangle { // short success notice on the unobstructed part of the window; it never takes focus
        objectName: "deleteNotice"
        readonly property string text: flow.noticeText
        readonly property var area: flow.host ? flow.host.workspace._clear : ({ x: 0, y: 0, width: flow.width, height: flow.height })
        visible: text.length > 0
        x: area.x + (area.width - width) / 2
        y: area.y + area.height - height - Theme.gapXxl
        width: Math.max(0, Math.min(area.width - 2 * Theme.gapMd, noticeRow.implicitWidth + 2 * Theme.gapLg))
        height: noticeRow.implicitHeight + 2 * Theme.gapSm + Theme.gapXxs
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.border
        RowLayout {
            id: noticeRow
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 2 * Theme.gapLg)
            spacing: Theme.gapSm
            UiIcon { name: "check"; color: Theme.primary }
            Label {
                text: flow.noticeText
                color: Theme.text
                font.pixelSize: Theme.textSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.maximumWidth: 520
                Accessible.role: Accessible.AlertMessage
                Accessible.name: text
            }
        }
    }
    Timer { id: noticeTimer; interval: 6000; onTriggered: flow.noticeText = "" }
}
