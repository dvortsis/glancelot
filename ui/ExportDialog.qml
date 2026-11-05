import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Glancelot

// B3 (F070): export a selection as a CSV file or a plain path list. SelectionExportService does all the file work on
// a worker, so this file only presents state. The items are captured when the dialog opens: a selection or navigation
// change behind the dialog can never change what is exported, it only offers to switch. The export writes one new
// file at the destination and never touches the listed media.
Item {
    id: flow
    property var host
    property var targetsProvider: null // () => the paths the command would export right now
    readonly property bool modal: dialog.visible || saveDialog.visible
    signal notice(string text)
    signal flowClosed()

    // "" | "checking" | "ready" | "running" | "replace" | "done"
    property string phase: ""
    property real jobId: 0
    property var capturedPaths: []
    property string rootFolder: ""
    property var summary: null
    property int done: 0
    property int total: 0
    property bool stopping: false
    property var result: null
    property string errorText: ""
    property string errorDetail: ""
    readonly property bool nothingExported: result !== null && result.status === "nothingToExport"
    property string statusText: ""
    property var currentPaths: []
    property bool selectionMoved: false
    readonly property bool usable: phase === "ready" && summary !== null && summary.status === "ready" && (summary.files + summary.folders) > 0
    readonly property bool csv: formatBox.currentIndex === 0
    readonly property bool relative: pathsBox.currentIndex === 1
    readonly property int shownNames: host && host.height < 480 ? 3 : 5

    SelectionExportService {
        id: service
        objectName: "exportService"
        onPrepared: (id, prepared) => {
            if (id !== flow.jobId) return
            flow.jobId = 0
            if (prepared.status === "cancelled") return
            flow.summary = prepared
            flow.phase = "ready"
            if (prepared.status === "error") flow.showError(qsTr("These items couldn't be read, so nothing can be exported."), prepared.detail)
        }
        onProgress: (id, done, total) => { if (id === flow.jobId) { flow.done = done; flow.total = total } }
        onFinished: (id, finished) => { if (id === flow.jobId) { flow.jobId = 0; flow.present(finished) } }
    }

    function fill(pattern, values) { return host ? host.fillText(pattern, values) : pattern }
    function number(n) { return Number(n).toLocaleString(Qt.locale(), "f", 0) }
    function itemsText(n) { return n === 1 ? qsTr("1 item") : qsTr("%1 items").arg(number(n)) }
    function leaf(path) { return String(path).replace(/[\\/]+$/, "").split(/[\\/]/).pop() }
    function parentFolder(path) {
        const text = String(path)
        const cut = Math.max(text.lastIndexOf("/"), text.lastIndexOf("\\"))
        return cut > 0 ? text.slice(0, cut) : ""
    }
    function separator(path) { return String(path).indexOf("\\") >= 0 ? "\\" : "/" }
    function joined(folder, name) {
        const sep = separator(folder)
        return folder.endsWith(sep) ? folder + name : folder + sep + name
    }
    function samePaths(a, b) {
        if (a.length !== b.length) return false
        for (let i = 0; i < a.length; ++i) if (a[i] !== b[i]) return false
        return true
    }

    // The command's entry point. Nothing is written until Export is pressed.
    function request(paths, folder) {
        if (!paths || paths.length === 0) { notice(qsTr("Select items in the grid to export them.")); return }
        if (destinationField.text.length === 0) destinationField.text = joined(service.defaultFolder(), csv ? "selection.csv" : "selection.txt")
        rootFolder = folder || parentFolder(paths[0])
        useTargets(paths)
        dialog.open()
    }
    function useTargets(paths) {
        capturedPaths = paths
        currentPaths = paths
        selectionMoved = false
        summary = null
        result = null
        clearMessages()
        phase = "checking"
        jobId = service.prepare(paths)
    }
    function checkSelection() {
        if (!dialog.visible || !targetsProvider) return
        currentPaths = targetsProvider()
        selectionMoved = !samePaths(currentPaths, capturedPaths)
    }
    function clearMessages() { errorText = ""; errorDetail = ""; statusText = "" }
    function showError(text, detail) { errorText = text; errorDetail = detail || ""; statusText = "" }

    function startExport(overwrite) {
        if (!(usable || (phase === "replace" && overwrite)) || service.busy) return
        clearMessages()
        stopping = false
        done = 0
        total = capturedPaths.length
        const id = service.start({ destination: destinationField.text, format: csv ? "csv" : "paths", relative: relative,
                                   root: rootFolder, neutralizeFormulas: formulaSwitch.checked, overwrite: overwrite === true })
        if (id === 0) return
        jobId = id
        phase = "running"
        stopButton.forceActiveFocus(Qt.TabFocusReason)
    }
    function focusDestination() {
        destinationField.forceActiveFocus(Qt.TabFocusReason)
        const name = leaf(destinationField.text)
        const dot = name.lastIndexOf(".")
        const end = destinationField.text.length - (dot > 0 ? name.length - dot : 0)
        destinationField.select(destinationField.text.length - name.length, end)
    }
    function present(outcome) {
        const name = leaf(outcome.destination)
        phase = "ready"
        switch (outcome.status) {
        case "written":
        case "nothingToExport":
            result = outcome
            phase = "done"
            doneButton.forceActiveFocus(Qt.TabFocusReason)
            return
        case "cancelled":
            statusText = qsTr("Export cancelled. Nothing was written.")
            cancelButton.forceActiveFocus(Qt.TabFocusReason)
            return
        case "destinationExists":
            result = outcome
            phase = "replace"
            keepButton.forceActiveFocus(Qt.TabFocusReason)
            return
        case "destinationEmpty": showError(qsTr("Enter where to save the file.")); break
        case "destinationRelative": showError(qsTr("Enter a full path, including the folder.")); break
        case "destinationIsFolder": showError(qsTr("That's a folder. Add a file name to the end of the path.")); break
        case "folderMissing": showError(qsTr("That folder doesn't exist. Choose another location.")); break
        case "destinationProtected": showError(qsTr("“%1” already exists and isn't a .csv or .txt file, so it can't be replaced. Choose another name.").arg(name)); break
        case "destinationIsItem": showError(qsTr("“%1” is one of the items being exported, so it can't be replaced. Choose another name.").arg(name)); break
        case "destinationLocked": showError(qsTr("“%1” couldn't be replaced. It may be open in another app, so close it and try again. The existing file hasn't changed.").arg(name)); break
        case "tooMany": showError(qsTr("That's too many items to export at once.")); break
        case "failed": showError(qsTr("That location can't be written to, so nothing was saved. Check that you have permission and enough space, or choose another location."), outcome.detail); break
        default: showError(qsTr("The export stopped because of an unexpected problem. Nothing was written."), outcome.detail); break
        }
        focusDestination()
    }
    function reasonText(reason) {
        return ({ missing: qsTr("It's missing or can't be read."),
                  changed: qsTr("It changed after the dialog opened."),
                  duplicate: qsTr("It's already in the list."),
                  outsideRoot: qsTr("It's outside the chosen folder."),
                  unrepresentable: qsTr("Its name can't be written as text.") })[reason] || qsTr("It couldn't be included.")
    }
    function cancelOrBack() {
        if (phase === "running") { stopping = true; service.cancel() }
        else if (phase === "replace") { phase = "ready"; focusDestination() }
        else dialog.close()
    }
    function applyChosenFile(url) { destinationField.text = service.localPath(url); clearMessages() }
    function setFormat(toCsv) {
        const text = destinationField.text
        destinationField.text = toCsv ? text.replace(/\.txt$/i, ".csv") : text.replace(/\.csv$/i, ".txt")
    }

    Connections {
        target: fileGrid
        function onSelectionChanged() { Qt.callLater(flow.checkSelection) }
    }
    Connections {
        target: viewer
        function onStateChanged() { Qt.callLater(flow.checkSelection) }
    }

    component Caption: Label {
        color: Theme.muted
        font.pixelSize: Theme.textSmall
        wrapMode: Text.Wrap
        lineHeight: 1.4
        Layout.fillWidth: true
    }
    component FieldLabel: Label {
        color: Theme.text
        font.pixelSize: Theme.textSmall
        font.weight: Font.Medium
        Layout.fillWidth: true
    }

    UiDialog {
        id: dialog
        objectName: "exportDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(560, (parent ? parent.width : 560) - 2 * Theme.gapLg)
        // The whole dialog stays inside the window; the form scrolls when space is short.
        readonly property real maximumBody: (parent ? parent.height : 600) - 2 * Theme.gapLg - topPadding - bottomPadding - implicitHeaderHeight - Theme.controlHeight - Theme.gapLg - 8 - (footer.visible ? footer.implicitHeight + Theme.gapLg : 0)
        title: flow.phase === "done" ? qsTr("Export finished") : qsTr("Export selection")
        closePolicy: flow.phase === "running" || flow.phase === "replace" ? Popup.NoAutoClose : Popup.CloseOnEscape
        closable: flow.phase !== "running" // a running export is cancelled, never dismissed
        onOpened: flow.focusDestination()
        onClosed: {
            if (flow.phase === "checking") service.cancel()
            flow.jobId = 0
            flow.phase = ""
            flow.flowClosed()
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gapLg
            // Escape cancels a running export and backs out of the replace question. Elsewhere the dialog handles it.
            Keys.onEscapePressed: event => {
                if (flow.phase === "running" || flow.phase === "replace") { flow.cancelOrBack(); event.accepted = true }
                else event.accepted = false
            }
            ScrollView {
                id: details
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(body.implicitHeight, Math.max(Theme.rowHeight * 2, dialog.maximumBody))
                contentWidth: availableWidth
                clip: true
                ScrollBar.vertical: UiScrollBar {}
                ColumnLayout {
                    id: body
                    width: details.availableWidth
                    spacing: Theme.gapLg

                    // What will be exported, or the result once it has been.
                    ColumnLayout {
                        visible: flow.phase !== "done"
                        spacing: Theme.gapXs
                        Layout.fillWidth: true
                        RowLayout {
                            objectName: "exportChecking"
                            visible: flow.phase === "checking"
                            spacing: Theme.gapSm
                            UiIcon { name: "spinner"; color: Theme.muted }
                            Label { text: qsTr("Checking the selected items…"); color: Theme.muted; font.pixelSize: Theme.textBody; Layout.fillWidth: true }
                        }
                        Label {
                            objectName: "exportSummary"
                            visible: flow.summary !== null && flow.phase !== "checking"
                            text: {
                                const s = flow.summary
                                if (!s) return ""
                                if (s.status === "tooMany") return qsTr("%1 items is more than can be exported at once. The limit is %2.").arg(flow.number(s.total)).arg(flow.number(s.maxItems))
                                const count = s.files + s.folders
                                if (count === 0) return qsTr("None of the selected items can be exported.")
                                if (s.folders === 0) return count === 1 ? qsTr("1 file to export") : qsTr("%1 files to export").arg(flow.number(count))
                                if (s.files === 0) return count === 1 ? qsTr("1 folder to export") : qsTr("%1 folders to export").arg(flow.number(count))
                                return qsTr("%1 to export").arg(flow.itemsText(count))
                            }
                            color: Theme.text
                            font.pixelSize: Theme.textBody
                            font.features: { "tnum": 1 }
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Caption {
                            objectName: "exportUnavailable"
                            visible: flow.summary !== null && flow.phase !== "checking" && flow.summary.unavailable > 0
                            text: flow.summary ? (flow.summary.unavailable === 1 ? qsTr("1 selected item can't be included, because it's missing or its name can't be written as text.")
                                                                                 : qsTr("%1 selected items can't be included, because they're missing or their names can't be written as text.").arg(flow.number(flow.summary.unavailable))) : ""
                        }
                    }

                    // The selection moved behind the dialog: what was captured stays, and switching is explicit.
                    ColumnLayout {
                        objectName: "exportSelectionChanged"
                        visible: flow.selectionMoved && flow.phase !== "done"
                        spacing: Theme.gapSm
                        Layout.fillWidth: true
                        Caption {
                            objectName: "exportSelectionNote"
                            text: flow.currentPaths.length === 0
                                  ? qsTr("The selection is now empty. This export still uses the %1 you started with.").arg(flow.itemsText(flow.capturedPaths.length))
                                  : flow.fill(qsTr("The selection is now %1. This export still uses the %2 you started with."), [flow.itemsText(flow.currentPaths.length), flow.itemsText(flow.capturedPaths.length)])
                            Accessible.role: Accessible.AlertMessage
                            Accessible.name: text
                        }
                        UiButton {
                            objectName: "exportUseCurrent"
                            visible: flow.currentPaths.length > 0
                            enabled: flow.phase === "ready"
                            variant: "outline"
                            text: qsTr("Use the current selection")
                            Layout.maximumWidth: implicitWidth
                            onClicked: flow.useTargets(flow.currentPaths)
                        }
                    }

                    // The form stays visible, and disabled, while the export runs, so the dialog doesn't jump.
                    GridLayout {
                        visible: flow.phase !== "done"
                        enabled: flow.phase === "ready" || flow.phase === "checking"
                        columns: body.width >= 440 ? 2 : 1
                        columnSpacing: Theme.gapMd
                        rowSpacing: Theme.gapMd
                        Layout.fillWidth: true
                        ColumnLayout {
                            spacing: Theme.gapXs
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            FieldLabel { text: qsTr("File type") }
                            UiComboBox {
                                id: formatBox
                                objectName: "exportFormat"
                                implicitWidth: 120
                                Layout.fillWidth: true
                                model: [qsTr("CSV spreadsheet"), qsTr("Plain text path list")]
                                Accessible.name: qsTr("File type")
                                onActivated: index => flow.setFormat(index === 0)
                            }
                        }
                        ColumnLayout {
                            spacing: Theme.gapXs
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            FieldLabel { text: qsTr("Paths") }
                            UiComboBox {
                                id: pathsBox
                                objectName: "exportPaths"
                                implicitWidth: 120
                                Layout.fillWidth: true
                                model: [qsTr("Full paths"), qsTr("Relative to “%1”").arg(flow.leaf(flow.rootFolder))]
                                Accessible.name: qsTr("Paths")
                            }
                        }
                        ColumnLayout {
                            spacing: Theme.gapXs
                            Layout.columnSpan: parent.columns
                            Layout.fillWidth: true
                            FieldLabel { text: qsTr("Save to") }
                            RowLayout {
                                spacing: Theme.gapSm
                                Layout.fillWidth: true
                                UiTextField {
                                    id: destinationField
                                    objectName: "exportDestination"
                                    Layout.fillWidth: true
                                    Accessible.name: qsTr("Save to")
                                    Accessible.description: flow.errorText
                                    onTextEdited: flow.clearMessages()
                                    Keys.onReturnPressed: flow.startExport(false)
                                    Keys.onEnterPressed: flow.startExport(false)
                                }
                                UiButton {
                                    objectName: "exportBrowse"
                                    variant: "outline"
                                    text: qsTr("Browse…")
                                    onClicked: {
                                        const folder = flow.parentFolder(destinationField.text)
                                        if (folder.length) saveDialog.currentFolder = service.folderUrl(folder)
                                        saveDialog.open()
                                    }
                                }
                            }
                            RowLayout {
                                objectName: "exportError"
                                visible: flow.errorText.length > 0
                                spacing: Theme.gapSm
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.gapXxs
                                UiIcon { name: "alert"; color: Theme.danger; Layout.alignment: Qt.AlignTop; Layout.topMargin: Theme.gapXxs }
                                ColumnLayout {
                                    spacing: Theme.gapXxs
                                    Layout.fillWidth: true
                                    Label {
                                        objectName: "exportErrorText"
                                        text: flow.errorText
                                        color: Theme.danger
                                        font.pixelSize: Theme.textSmall
                                        wrapMode: Text.Wrap
                                        lineHeight: 1.4
                                        Layout.fillWidth: true
                                        Accessible.role: Accessible.AlertMessage
                                        Accessible.name: text
                                    }
                                    Caption {
                                        objectName: "exportErrorDetail"
                                        visible: flow.errorDetail.length > 0
                                        text: flow.errorDetail
                                    }
                                }
                            }
                        }
                        UiSwitch {
                            id: formulaSwitch
                            objectName: "exportFormulas"
                            visible: flow.csv
                            checked: true
                            text: qsTr("Protect against spreadsheet formulas")
                            description: qsTr("Adds an apostrophe in front of values that start with =, +, - or @, so a spreadsheet shows them as text and doesn't run them.")
                            Layout.columnSpan: parent.columns
                            Layout.fillWidth: true
                        }
                    }

                    // The result.
                    ColumnLayout {
                        objectName: "exportResult"
                        visible: flow.phase === "done"
                        spacing: Theme.gapSm
                        Layout.fillWidth: true
                        Label {
                            objectName: "exportResultText"
                            text: {
                                if (!flow.result) return ""
                                const name = flow.leaf(flow.result.destination)
                                if (flow.result.status === "nothingToExport")
                                    return flow.result.replacing ? qsTr("Nothing could be exported, so “%1” was not changed.").arg(name)
                                                                 : qsTr("Nothing could be exported, so “%1” was not created.").arg(name)
                                return flow.fill(qsTr("Exported %1 to “%2”."), [flow.itemsText(flow.result.written), name])
                            }
                            color: Theme.text
                            font.pixelSize: Theme.textBody
                            font.features: { "tnum": 1 }
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Accessible.role: Accessible.AlertMessage
                            Accessible.name: text
                        }
                        Caption {
                            objectName: "exportResultPath"
                            visible: !flow.nothingExported
                            text: flow.result ? flow.result.destination : ""
                            wrapMode: Text.WrapAnywhere
                        }
                        ColumnLayout { // items left out, each with its reason
                            objectName: "exportSkipped"
                            visible: flow.result !== null && flow.result.skippedCount > 0
                            spacing: Theme.gapXs
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.gapSm
                            Label {
                                objectName: "exportSkippedTitle"
                                text: flow.result ? qsTr("Left out (%1):").arg(flow.number(flow.result.skippedCount)) : ""
                                color: Theme.text
                                font.pixelSize: Theme.textSmall
                                font.weight: Font.DemiBold
                            }
                            Repeater {
                                model: flow.result ? flow.result.skipped.slice(0, flow.shownNames) : []
                                delegate: ColumnLayout {
                                    required property var modelData
                                    spacing: 0
                                    Layout.fillWidth: true
                                    Label { text: modelData.name; elide: Text.ElideMiddle; color: Theme.text; font.pixelSize: Theme.textBody; Layout.fillWidth: true }
                                    Caption { objectName: "exportSkipReason"; text: flow.reasonText(modelData.reason) }
                                }
                            }
                            Caption {
                                visible: flow.result !== null && flow.result.skippedCount > flow.shownNames
                                text: flow.result ? qsTr("and %1 more").arg(flow.number(flow.result.skippedCount - flow.shownNames)) : ""
                            }
                        }
                    }
                }
            }
            // Outside the scrolled form, so progress and the replace question are never below the fold in a short window.
            ColumnLayout {
                id: footer
                visible: flow.statusText.length > 0 || flow.phase === "running" || flow.phase === "replace"
                spacing: Theme.gapSm
                Layout.fillWidth: true
                Label {
                    objectName: "exportStatus"
                    visible: flow.statusText.length > 0
                    text: flow.statusText
                    color: Theme.text
                    font.pixelSize: Theme.textBody
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: text
                }
                ColumnLayout {
                    objectName: "exportProgress"
                    visible: flow.phase === "running"
                    spacing: Theme.gapSm
                    Layout.fillWidth: true
                    ProgressBar {
                        id: bar
                        Layout.fillWidth: true
                        from: 0; to: Math.max(1, flow.total); value: flow.done
                        Accessible.name: qsTr("Exported %1 of %2").arg(flow.number(flow.done)).arg(flow.number(flow.total))
                        background: Rectangle { implicitHeight: 6; radius: 3; color: Theme.hover }
                        contentItem: Item {
                            implicitHeight: 6
                            Rectangle { width: bar.visualPosition * parent.width; height: parent.height; radius: 3; color: Theme.primary }
                        }
                    }
                    Label {
                        objectName: "exportProgressText"
                        text: flow.stopping ? qsTr("Stopping after the current item…") : qsTr("Exported %1 of %2").arg(flow.number(flow.done)).arg(flow.number(flow.total))
                        color: Theme.text
                        font.pixelSize: Theme.textBody
                        font.features: { "tnum": 1 }
                    }
                }
                Label {
                    objectName: "exportReplaceQuestion"
                    visible: flow.phase === "replace"
                    text: flow.result ? qsTr("“%1” already exists. Replace it with this export?").arg(flow.leaf(flow.result.destination)) : ""
                    color: Theme.text
                    font.pixelSize: Theme.textBody
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: text
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gapSm
                Item { Layout.fillWidth: true }
                UiButton {
                    objectName: "exportReveal"
                    visible: flow.phase === "done" && !flow.nothingExported
                    variant: "outline"
                    text: qsTr("Show in folder")
                    onClicked: viewer.revealInFileManager(flow.result.destination)
                }
                UiButton {
                    id: doneButton
                    objectName: "exportDone"
                    visible: flow.phase === "done"
                    variant: "primary"
                    text: qsTr("Done")
                    onClicked: dialog.close()
                }
                // Replace is not in the trailing slot that Export just occupied, so a double-click on Export can't confirm it.
                UiButton {
                    objectName: "exportReplace"
                    visible: flow.phase === "replace"
                    variant: "danger"
                    ignoreKeyRepeat: true
                    text: qsTr("Replace")
                    onClicked: flow.startExport(true)
                }
                UiButton {
                    id: keepButton
                    objectName: "exportKeep"
                    visible: flow.phase === "replace"
                    variant: "outline"
                    text: qsTr("Keep the file")
                    onClicked: flow.cancelOrBack()
                }
                UiButton {
                    id: stopButton
                    objectName: "exportStop"
                    visible: flow.phase === "running"
                    variant: "outline"
                    text: flow.stopping ? qsTr("Stopping…") : qsTr("Cancel export")
                    enabled: !flow.stopping
                    onClicked: flow.cancelOrBack()
                }
                UiButton {
                    id: cancelButton
                    objectName: "exportCancel"
                    visible: flow.phase === "checking" || flow.phase === "ready"
                    variant: "outline"
                    text: qsTr("Cancel")
                    onClicked: dialog.close()
                }
                UiButton {
                    objectName: "exportStart"
                    visible: flow.phase === "checking" || flow.phase === "ready"
                    variant: "primary"
                    text: qsTr("Export")
                    enabled: flow.usable
                    onClicked: flow.startExport(false)
                }
            }
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Export selection")
        fileMode: FileDialog.SaveFile
        // This dialog asks before replacing, after checking that the file can be replaced at all.
        options: FileDialog.DontConfirmOverwrite
        nameFilters: flow.csv ? [qsTr("CSV files (%1)").arg("*.csv"), qsTr("All files (%1)").arg("*")] : [qsTr("Text files (%1)").arg("*.txt"), qsTr("All files (%1)").arg("*")]
        defaultSuffix: flow.csv ? "csv" : "txt"
        onAccepted: flow.applyChosenFile(selectedFile)
    }
}
