import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// ASSET-GROUP (F022): what stands behind Paste. CopyMoveService owns the work; this file presents one captured plan, so nothing
// that changes behind the dialog can change what is confirmed. Plain files with nothing to decide run straight through, with no
// dialog, as Paste always has. Anything else (a companion, a collision, an exclusion, a check that couldn't finish) waits for
// Copy or Move to be pressed, and a held key can't press it. This file also remembers what was cut or copied, because the
// service's clipboard list isn't readable from QML.
Item {
    id: flow
    property var host
    property var service
    readonly property bool modal: confirmDialog.visible || resultsDialog.visible
    // "" | "planning" | "confirm"
    property string phase: ""
    property real planId: 0
    property bool replanning: false
    property var summary: null
    property string scope: "file"
    property bool keepBoth: true
    property var clipboardPaths: []
    property var request: null // {paths, destination, move}
    property var failures: []
    property int transferredCount: 0
    property bool awaitingBatch: false
    // How long a plan may take before anything appears, so the usual paste shows nothing at all. Only a slow share gets this far.
    property int progressDelay: 400
    readonly property bool isMove: request ? request.move : false
    readonly property bool compactHeight: host ? host.height < 480 : false
    readonly property var targets: summary ? summary.targets : []
    readonly property var rejected: summary ? summary.rejected : []
    readonly property var collisions: summary ? summary.collisions : []
    readonly property bool destinationMissing: !!(summary && summary.error)
    readonly property bool canConfirm: phase === "confirm" && !replanning && targets.length > 0 && !destinationMissing
    // The set shown above the choice: companions listed below it are named there, each with its state.
    readonly property var namedTargets: {
        const listed = {}
        for (const row of (summary && summary.companions ? summary.companions : [])) if (row.included) listed[row.path] = true
        return targets.filter(t => !listed[t.path])
    }
    // path -> the name it would take under keep-both, shown beside it before anything runs.
    readonly property var renamed: {
        const names = {}
        if (keepBoth) for (const t of targets) if (t.renamedTo && t.renamedTo.length) names[t.path] = t.renamedTo
        return names
    }
    readonly property int shownNames: compactHeight ? 3 : 5
    signal flowClosed()
    signal noticeRequested(string text)

    function markCut(paths) { clipboardPaths = paths; service.markCut(paths) }
    function markCopy(paths) { clipboardPaths = paths; service.markCopy(paths) }
    function paste(destination) {
        if (!service.hasClipboardTargets || clipboardPaths.length === 0) return
        if (service.active) { noticeRequested(qsTr("Wait for the current copy or move to finish.")); return }
        summary = null
        scope = "file" // never remembered: every operation starts file-only
        keepBoth = true
        replanning = false
        request = { paths: clipboardPaths, destination: destination, move: service.clipboardMode === "cut" }
        phase = "planning"
        plan()
        progressDelayTimer.interval = progressDelay
        progressDelayTimer.restart()
    }
    // Nothing to show and nothing to choose. File-only is the default, so no companion can be carried along by this.
    function nothingToDecide(plan) {
        return plan.targets.length > 0 && plan.rejected.length === 0 && plan.collisions.length === 0
            && plan.companions.length === 0 && !plan.companionsNotListed && !plan.companionsIncomplete && !plan.error
    }
    // A cut pasted where it already is: nothing would change, so it isn't a failure and doesn't need a dialog.
    function alreadyHere(plan) {
        return plan.targets.length === 0 && plan.rejected.length > 0 && !plan.error && plan.rejected.every(r => r.reason === "sameFolder")
    }
    function plan() { planId = service.planTransfer(request.paths, request.destination, request.move ? 1 : 0, scope) }
    function setScope(next) {
        if (phase !== "confirm" || next === scope) return
        scope = next
        replanning = true
        plan()
    }
    function confirmPlan() {
        if (!canConfirm || summary.id !== planId) return
        const wasMove = isMove
        // Collision policies: 0 skip, 2 keep both. Overwrite arrives with the operation journal.
        if (!service.confirmTransfer(planId, keepBoth ? 2 : 0)) {
            noticeRequested(qsTr("That list is out of date. Paste again."))
            confirmDialog.close()
            return
        }
        planId = 0
        failures = []
        transferredCount = 0
        awaitingBatch = true // the next batch to finish is this one: confirmTransfer refuses while another runs
        if (wasMove) { service.clearClipboard(); clipboardPaths = [] }
        phase = ""
        confirmDialog.close()
    }
    function discardPlan() {
        if (planId !== 0) service.planTransfer([], "", 0, "file") // withdraws the plan, and stops its lookups
        planId = 0
        phase = ""
        replanning = false
    }
    function rejectionText(reason) {
        return ({ missing: qsTr("It no longer exists."),
                  inaccessible: qsTr("It can't be read."),
                  sameFolder: qsTr("It's already in this folder.") })[reason] || qsTr("It can't be moved.")
    }
    function failureText(error) {
        if (error.startsWith("Changed")) return qsTr("It changed after you confirmed, so it was left in place.")
        if (error.startsWith("Missing")) return qsTr("It no longer exists.")
        return error
    }
    function leaf(path) { return path.split(/[\\/]/).pop() }

    Connections {
        target: flow.service
        function onTransferPlanReady(id, plan) {
            if (id !== flow.planId) return
            progressDelayTimer.stop()
            const first = flow.phase === "planning"
            flow.summary = plan
            flow.phase = "confirm"
            flow.replanning = false
            if (first && flow.alreadyHere(plan)) {
                flow.noticeRequested(plan.rejected.length === 1 ? qsTr("It's already in this folder.") : qsTr("These items are already in this folder."))
                flow.discardPlan() // what was cut stays cut, for pasting somewhere else
                confirmDialog.close()
                return
            }
            if (first && flow.nothingToDecide(plan)) { flow.confirmPlan(); return }
            if (!confirmDialog.visible) { confirmDialog.open(); return }
            // Not destructive, so the action holds the focus once it can act; a scope change leaves focus on the choice.
            if (first && flow.canConfirm) confirmAction.forceActiveFocus(Qt.TabFocusReason)
        }
        function onItemFinished(source, destination, success, error) {
            if (!flow.awaitingBatch) return
            if (success) ++flow.transferredCount
            else flow.failures = flow.failures.concat([{ name: flow.leaf(source), path: source, error: error }])
        }
        function onBatchFinished(id, succeeded, failed) {
            if (!flow.awaitingBatch) return
            flow.awaitingBatch = false
            if (flow.failures.length > 0) resultsDialog.open()
        }
    }

    Timer { // a slow plan: quiet progress, with Cancel, in the dialog that may then have nothing to ask
        id: progressDelayTimer
        onTriggered: if (flow.phase === "planning") confirmDialog.open()
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
        objectName: "transferConfirmation"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, (parent ? parent.width : 480) - 2 * Theme.gapLg)
        readonly property real maximumBody: (parent ? parent.height : 600) - 2 * Theme.gapLg - topPadding - bottomPadding - implicitHeaderHeight - Theme.controlHeight - Theme.gapLg - 8
        title: {
            const n = flow.targets.length
            if (flow.phase === "confirm" && (n === 0 || flow.destinationMissing)) return flow.isMove ? qsTr("Nothing can be moved") : qsTr("Nothing can be copied")
            if (flow.isMove) return n === 1 ? qsTr("Move this item here?") : flow.phase === "confirm" ? qsTr("Move %L1 items here?").arg(n) : qsTr("Move these items here?")
            return n === 1 ? qsTr("Copy this item here?") : flow.phase === "confirm" ? qsTr("Copy %L1 items here?").arg(n) : qsTr("Copy these items here?")
        }
        closePolicy: Popup.CloseOnEscape
        onOpened: (flow.canConfirm ? confirmAction : cancelAction).forceActiveFocus(Qt.TabFocusReason)
        onClosed: { progressDelayTimer.stop(); flow.discardPlan(); if (!resultsDialog.visible) flow.flowClosed() }
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
                    RowLayout {
                        objectName: "transferPlanning"
                        visible: flow.phase === "planning"
                        spacing: Theme.gapSm
                        UiIcon { name: "spinner"; color: Theme.muted }
                        Label { text: qsTr("Checking the selected items…"); color: Theme.muted; font.pixelSize: Theme.textBody; Layout.fillWidth: true }
                    }
                    Rectangle { // what is named, and where a colliding item would land
                        objectName: "transferTargets"
                        visible: flow.phase === "confirm" && flow.namedTargets.length > 0
                        Layout.fillWidth: true
                        implicitHeight: names.implicitHeight + 2 * Theme.gapXs
                        radius: Theme.radiusControl
                        color: Theme.background
                        border.width: Theme.borderWidth
                        border.color: Theme.border
                        Column {
                            id: names
                            x: Theme.gapMd
                            y: Theme.gapXs
                            width: parent.width - 2 * Theme.gapMd
                            Repeater {
                                model: flow.namedTargets.slice(0, flow.shownNames)
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
                                    }
                                    Label {
                                        objectName: "transferRenamedTo"
                                        visible: text.length > 0
                                        text: flow.renamed[modelData.path] ? qsTr("as %1").arg(flow.renamed[modelData.path]) : ""
                                        elide: Text.ElideMiddle
                                        color: Theme.muted
                                        font.pixelSize: Theme.textCaption
                                        Layout.maximumWidth: names.width / 2
                                    }
                                }
                            }
                            Label {
                                visible: flow.namedTargets.length > flow.shownNames
                                height: Theme.rowHeight
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: Theme.iconSize + Theme.gapSm
                                text: qsTr("and %L1 more").arg(flow.namedTargets.length - flow.shownNames)
                                color: Theme.muted
                                font.pixelSize: Theme.textBody
                            }
                        }
                    }
                    Caption {
                        objectName: "transferDestination"
                        visible: flow.phase === "confirm"
                        text: flow.destinationMissing ? qsTr("This folder no longer exists: %1").arg(flow.summary.destination)
                                                      : qsTr("To %1").arg(flow.summary ? flow.summary.destination : "")
                        wrapMode: Text.WrapAnywhere
                    }
                    AssetCompanions {
                        id: companions
                        summary: flow.summary
                        scope: flow.scope
                        replanning: flow.replanning
                        previews: flow.renamed
                        visibleRows: flow.compactHeight ? 3 : 5
                        enabled: flow.phase === "confirm"
                        Layout.fillWidth: true
                        onScopeRequested: next => flow.setScope(next)
                    }
                    ColumnLayout { // one decision per plan, never per member
                        objectName: "transferCollisions"
                        visible: flow.phase === "confirm" && flow.collisions.length > 0
                        spacing: 0
                        Layout.fillWidth: true
                        Label {
                            text: flow.collisions.length === 1 ? qsTr("“%1” is already in this folder.").arg(flow.collisions[0].name)
                                                               : qsTr("%L1 of these names are already in this folder.").arg(flow.collisions.length)
                            color: Theme.text
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.gapXs
                        }
                        ButtonGroup { id: collisionGroup }
                        UiRadioButton {
                            objectName: "transferKeepBoth"
                            text: flow.scope === "asset" ? qsTr("Keep both, renaming what's copied so each set stays together") : qsTr("Keep both, renaming what's copied")
                            ButtonGroup.group: collisionGroup
                            checked: flow.keepBoth
                            onClicked: flow.keepBoth = true
                        }
                        UiRadioButton {
                            objectName: "transferSkip"
                            text: flow.scope === "asset" ? qsTr("Skip each set with a name already here") : qsTr("Skip these files")
                            ButtonGroup.group: collisionGroup
                            checked: !flow.keepBoth
                            onClicked: flow.keepBoth = false
                        }
                    }
                    ColumnLayout { // items that will not run, each with its reason
                        objectName: "transferRejected"
                        visible: flow.phase === "confirm" && flow.rejected.length > 0
                        spacing: Theme.gapXs
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Not included (%L1):").arg(flow.rejected.length)
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
                        Caption { visible: flow.rejected.length > flow.shownNames; text: qsTr("and %L1 more").arg(flow.rejected.length - flow.shownNames) }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gapSm
                Item { Layout.fillWidth: true }
                UiButton {
                    id: cancelAction
                    objectName: "transferCancel"
                    variant: "outline"
                    text: flow.phase === "confirm" && !flow.canConfirm && !flow.replanning ? qsTr("Close") : qsTr("Cancel")
                    onClicked: confirmDialog.close()
                }
                UiButton {
                    id: confirmAction
                    objectName: "transferConfirm"
                    visible: !(flow.phase === "confirm" && !flow.canConfirm && !flow.replanning)
                    variant: "primary"
                    ignoreKeyRepeat: true
                    text: flow.isMove ? qsTr("Move here") : qsTr("Copy here")
                    enabled: flow.canConfirm
                    Layout.maximumWidth: implicitWidth
                    onClicked: flow.confirmPlan()
                }
            }
        }
    }

    UiDialog {
        id: resultsDialog
        objectName: "transferResults"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, (parent ? parent.width : 520) - 2 * Theme.gapLg)
        readonly property real maximumList: (parent ? parent.height : 600) - 2 * Theme.gapLg - topPadding - bottomPadding - implicitHeaderHeight - Theme.controlHeight - 120
        title: flow.failures.length === 1 ? qsTr("1 item wasn't transferred") : qsTr("%L1 items weren't transferred").arg(flow.failures.length)
        onOpened: closeResults.forceActiveFocus(Qt.TabFocusReason)
        onClosed: flow.flowClosed()
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gapMd
            ListView {
                id: failureList
                objectName: "transferFailures"
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
                    Caption { text: flow.failureText(modelData.error); color: Theme.text; Layout.leftMargin: Theme.iconSize + Theme.gapSm }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Caption { text: qsTr("Files that weren't transferred are still where they were."); Layout.alignment: Qt.AlignVCenter }
                UiButton { id: closeResults; objectName: "transferResultsClose"; variant: "primary"; text: qsTr("Close"); onClicked: resultsDialog.close() }
            }
        }
    }
}
