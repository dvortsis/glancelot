import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// File-manager grid of the current folder: folders first, then every regular file. The cursor and the
// multi-selection live in `fileGrid` as stable path keys; the opened photo ("Viewing") is separate.
// Only activation acts: folders navigate through the controller, photos open, other files only explain.
Rectangle {
    id: panel
    color: Theme.background
    readonly property int selectedIndex: fileGrid.cursorRow
    readonly property string selectedPath: fileGrid.cursorRow >= 0 ? fileGrid.pathAt(fileGrid.cursorRow) : ""
    readonly property int selectedCount: fileGrid.selectedCount
    property string entryCurrentPath: ""
    property string revealPath: ""
    property bool placeCursor: false
    property bool inputEnabled: true
    // INPUT-02: while the host has a chord pending, Escape is its to cancel, not the grid's.
    property bool chordPending: false
    // INPUT-02: a function(event) -> bool; true when a registered remap owns this key, so the native handling steps aside.
    property var yields: null
    property int liveDelegateCount: 0
    property string noticeText: ""
    property var resetViewport: null
    property string searchQuery: ""
    property int preSearchRow: -1
    // A host that pairs the grid with another pane takes Tab (GRID-PREVIEW-01); null keeps the focus chain.
    property var tabHandler: null
    // Thumbnail size buckets (F010): 96, 128, 160 (default), 220, 320
    readonly property var sizeBuckets: [96, 128, 160, 220, 320]
    property int bucketIndex: 2
    readonly property int currentBucketSize: sizeBuckets[bucketIndex]
    function zoomIn() {
        if (bucketIndex < sizeBuckets.length - 1) {
            bucketIndex++
            Qt.callLater(panel.reveal)
        }
    }
    function zoomOut() {
        if (bucketIndex > 0) {
            bucketIndex--
            Qt.callLater(panel.reveal)
        }
    }
    function resetZoom() {
        if (bucketIndex !== 2) {
            bucketIndex = 2
            Qt.callLater(panel.reveal)
        }
    }
    // Short panes (minimum window, docked bars) use a smaller header and tiles so more than one row shows.
    readonly property bool compact: height < 420
    // Capped so delegates (and thumbnail requests) stay under the service queue on very large displays.
    // Measured on the full view width so a half-width combined pane (about 360 px) still gets two columns.
    // F009: one row per item with name, size and modified time instead of thumbnail tiles.
    property bool details: false
    function sizeText(bytes) { return bytes < 1024 ? bytes + " B" : bytes < 1048576 ? (bytes / 1024).toFixed(0) + " KB" : bytes < 1073741824 ? (bytes / 1048576).toFixed(1) + " MB" : (bytes / 1073741824).toFixed(2) + " GB" }
    readonly property int targetWidth: compact ? Math.max(96, Math.floor(currentBucketSize * 0.85)) : (currentBucketSize + 16)
    readonly property int columns: details ? 1 : Math.max(1, Math.min(24, Math.floor(grid.width / targetWidth)))
    readonly property int count: grid.count
    readonly property int thumbEdge: currentBucketSize >= 220 ? 512 : 256
    // DirectoryEntryModel::SelectMode (the model is a context property, so its enum type is not registered)
    readonly property int modeReplace: 0
    readonly property int modeToggle: 1
    readonly property int modeRange: 2
    readonly property int modeExtendRange: 3
    readonly property string folderPath: fileGrid.directory
    readonly property string parentPath: parentOf(folderPath)
    readonly property string directoryError: viewer.directoryError
    // "loading" | "inaccessible" | "empty" | "none" | "" (entries showing)
    readonly property string stateName: !visible ? "" : folderPath.length === 0 ? "none"
        : (fileGrid.loading && fileGrid.count === 0) ? "loading" : !fileGrid.accessible ? "inaccessible"
        : (fileGrid.count === 0 && !fileGrid.loading) ? "empty" : ""
    signal itemActivated(string path)
    signal dismissed()
    signal foldersRequested()
    // INPUT-02: a context menu was asked for at (x, y) in this pane over `row` (-1: empty area). The host owns the menu.
    signal contextRequested(real x, real y, int row)
    // DELETE-01: the header action asks the host to confirm moving these paths to the trash; nothing happens here.
    signal deleteRequested(var paths)
    property bool deleteBlocked: false // every item here is on a read-only drive
    readonly property string trashName: Qt.platform.os === "windows" ? "Recycle Bin" : "Trash"
    // The selected paths, or the cursor item when nothing is selected.
    function deletionTargets() {
        const selected = fileGrid.selectedPaths()
        return selected.length ? selected : (selectedPath.length ? [selectedPath] : [])
    }
    Accessible.role: Accessible.Pane
    Accessible.name: "Folder grid"

    // Pure text: the parent of a cleaned path, or "" at a drive, share or filesystem root. Never touches the disk.
    function parentOf(path) {
        let p = path.replace(/\\/g, "/")
        while (p.length > 1 && p.endsWith("/") && !/^[A-Za-z]:\/$/.test(p)) p = p.slice(0, -1)
        if (/^[A-Za-z]:\/?$/.test(p) || p === "/" || p.length === 0) return ""
        if (p.startsWith("//") && p.split("/").filter(s => s.length).length <= 2) return "" // \\server\share
        const cut = p.lastIndexOf("/")
        if (cut < 0) return ""
        let parent = p.slice(0, cut)
        if (/^[A-Za-z]:$/.test(parent)) parent += "/"
        if (parent.length === 0) parent = "/"
        return parent
    }
    function fileExtension(name) {
        const dot = name.lastIndexOf(".")
        return dot > 0 && dot < name.length - 1 ? name.slice(dot + 1).toUpperCase().slice(0, 5) : ""
    }
    function select(index, mode) {
        if (fileGrid.count <= 0) return
        const row = Math.max(0, Math.min(fileGrid.count - 1, index))
        fileGrid.select(row, mode === undefined ? panel.modeReplace : mode)
        grid.positionViewAtIndex(row, GridView.Contain)
    }
    function focusGrid() { if (visible && inputEnabled) grid.forceActiveFocus(Qt.TabFocusReason) }
    function enter() {
        const current = fileGrid.currentImage
        // Re-entering keeps the browsing cursor unless the viewed photo changed meanwhile.
        if (current !== entryCurrentPath) revealPath = current
        entryCurrentPath = current
        placeCursor = true
        entering = true
        reveal()
        focusGrid()
    }
    // True from entering the grid until its first listing places the cursor: a photo opened meanwhile (the grid
    // restored visible at start-up, before the start-up photo opens) becomes the cursor's target.
    property bool entering: false
    // Places the cursor once a listing has landed: on the revealed photo, else keeps it, else the first entry.
    // Runs once per grid entry or folder change; an ordinary refresh (external change) never moves the cursor,
    // replaces the selection or scrolls the view.
    function reveal() {
        if (!visible || fileGrid.loading || !placeCursor || folderPath.length === 0) return
        placeCursor = false; entering = false
        if (revealPath.length) {
            const row = fileGrid.rowOf(revealPath)
            revealPath = ""
            if (row >= 0) { select(row); return }
        }
        if (fileGrid.cursorRow < 0) { if (fileGrid.count > 0) select(0) }
        else grid.positionViewAtIndex(fileGrid.cursorRow, GridView.Contain)
    }
    function showNotice(text) { noticeText = text; noticeTimer.restart() }
    function rememberViewport() {
        // A failed or empty relisting of the same folder keeps the viewport saved before it, so a transient error
        // followed by recovery returns to the browsed position.
        if (fileGrid.count === 0 && resetViewport && resetViewport.directory === folderPath) return
        resetViewport = null
        if (!visible || placeCursor || fileGrid.count === 0) return
        const offset = Math.max(0, grid.contentY - grid.originY)
        const first = Math.min(fileGrid.count - 1, Math.floor(offset / grid.cellHeight) * columns)
        const cursorTop = Math.floor(fileGrid.cursorRow / columns) * grid.cellHeight
        resetViewport = { directory: folderPath, path: fileGrid.pathAt(first), offset: offset % grid.cellHeight,
                          scroll: offset, cursorVisible: fileGrid.cursorRow >= 0 && cursorTop < offset + grid.height
                          && cursorTop + grid.cellHeight > offset }
    }
    function restoreViewport(saved) {
        if (!saved || !visible || placeCursor || saved.directory !== folderPath) return
        if (saved.cursorVisible && fileGrid.cursorRow >= 0) {
            grid.positionViewAtIndex(fileGrid.cursorRow, GridView.Contain)
            return
        }
        // Mouse-wheel browsing need not move the keyboard cursor. Preserve the visible item when a
        // scattered refresh resets the model, rather than pulling the viewport back to that cursor.
        const row = fileGrid.rowOf(saved.path)
        const offset = row >= 0 ? Math.floor(row / columns) * grid.cellHeight + saved.offset : saved.scroll
        grid.contentY = grid.originY + Math.max(0, Math.min(offset, grid.contentHeight - grid.height))
    }
    function activate() {
        const row = fileGrid.cursorRow
        if (!inputEnabled || row < 0) return
        const path = fileGrid.pathAt(row), kind = fileGrid.kindAt(row)
        if (kind === "folder") { noticeText = ""; viewer.navigateDirectory(path); return }
        if (kind === "image") { if (!viewer.isCurrentImage(path)) viewer.open(path); itemActivated(path); return }
        // Unsupported files are never decoded, previewed or launched.
        const name = path.slice(path.lastIndexOf("/") + 1)
        showNotice("“" + name + "” isn't a photo Glancelot can open. Supported formats are JPEG and PNG.")
    }
    // The menu key opens the menu on the cursor item, or the pane's corner when there is none.
    function contextAtCursor() {
        const row = fileGrid.cursorRow
        if (row < 0) { contextRequested(grid.x + Theme.gapMd, grid.y + Theme.gapMd, -1); return }
        const column = row % columns, line = Math.floor(row / columns)
        const x = grid.x + (column + 0.5) * grid.cellWidth, y = grid.y + (line + 1) * grid.cellHeight - grid.contentY + grid.originY
        contextRequested(Math.max(grid.x, Math.min(x, grid.x + grid.width - 1)), Math.max(grid.y, Math.min(y, grid.y + grid.height - 1)), row)
    }
    function goUp() { if (inputEnabled && parentPath.length) { noticeText = ""; viewer.navigateDirectory(parentPath) } }
    function clickRow(row, modifiers) {
        const ctrl = (modifiers & Qt.ControlModifier) !== 0, shift = (modifiers & Qt.ShiftModifier) !== 0
        fileGrid.select(row, ctrl && shift ? panel.modeExtendRange : shift ? panel.modeRange
                        : ctrl ? panel.modeToggle : panel.modeReplace)
        focusGrid()
    }
    // One table for both shortcut override and handling, so the grid claims exactly the keys it uses.
    function keyAction(event) {
        const mods = event.modifiers & ~Qt.KeypadModifier
        if (yields && yields(event)) return ""
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && mods === Qt.ShiftModifier)) return "menu"
        const navigation = [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End, Qt.Key_PageUp, Qt.Key_PageDown]
        if (navigation.indexOf(event.key) >= 0) {
            if (mods === Qt.NoModifier) return "move"
            if (mods === Qt.ShiftModifier) return "extend"
            if (mods === Qt.AltModifier && event.key === Qt.Key_Up) return "up"
            return ""
        }
        if (mods === Qt.NoModifier) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) return "activate"
            if (event.key === Qt.Key_Escape) return panel.chordPending ? "" : "escape"
            if (event.key === Qt.Key_Backspace) return "up"
        }
        if (mods === Qt.ControlModifier) {
            if (event.key === Qt.Key_A) return "all"
            if (event.key === Qt.Key_I) return "invert"
            if (event.key === Qt.Key_D) return "clear"
            if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) return "zoomIn"
            if (event.key === Qt.Key_Minus) return "zoomOut"
            if (event.key === Qt.Key_0) return "zoomReset"
            if (event.key === Qt.Key_Space) return "toggle"
        }
        return ""
    }
    onVisibleChanged: {
        searchQuery = ""
        preSearchRow = -1
        searchTimer.stop()
        fileGrid.setActive(visible) // hidden: no listing, no rows, no thumbnail requests
        if (visible) Qt.callLater(enter)
        else noticeText = ""
    }
    // A grid restored as visible at start-up (a saved grid or combined mode) never sees a visibility change.
    Component.onCompleted: if (visible) { fileGrid.setActive(true); Qt.callLater(enter) }
    onDirectoryErrorChanged: if (visible && directoryError.length) showNotice(directoryError)
    onFolderPathChanged: {
        searchQuery = ""
        preSearchRow = -1
        searchTimer.stop()
        placeCursor = true
    }
    Connections {
        target: fileGrid
        function onLoadingChanged() { if (panel.visible && !fileGrid.loading) Qt.callLater(panel.reveal) }
        function onModelAboutToBeReset() { panel.rememberViewport() }
        function onModelReset() { Qt.callLater(panel.restoreViewport, panel.resetViewport) }
        function onCurrentImageChanged() { if (panel.entering && panel.placeCursor) { panel.revealPath = fileGrid.currentImage; panel.entryCurrentPath = fileGrid.currentImage } }
    }
    Timer { id: noticeTimer; interval: 4000; onTriggered: panel.noticeText = "" }
    Timer { id: searchTimer; interval: 3000; onTriggered: { searchQuery = ""; preSearchRow = -1 } }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }

    // Location header: parent route, folder name, item count and multi-selection count.
    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.leftMargin: Theme.gapMd; anchors.rightMargin: Theme.gapMd
        height: panel.compact ? Theme.controlHeight + 2 * Theme.gapXs : Theme.barHeight
        RowLayout {
            anchors.fill: parent
            spacing: Theme.gapSm
            UiButton {
                objectName: "gridParent"
                iconName: "corner-up"
                iconOnly: true
                text: "Up one folder"
                tip: "Up one folder (Backspace or Alt+Up)"
                enabled: panel.parentPath.length > 0 && panel.inputEnabled
                focusPolicy: Qt.NoFocus
                onClicked: panel.goUp()
            }
            UiIcon { name: "folder-open"; color: Theme.muted; visible: panel.folderPath.length > 0 }
            Label {
                objectName: "gridFolderName"
                Layout.fillWidth: true
                Layout.minimumWidth: 40
                text: {
                    const p = panel.folderPath.replace(/\\/g, "/").replace(/\/+$/, "")
                    const leaf = p.slice(p.lastIndexOf("/") + 1)
                    return leaf.length ? leaf : panel.folderPath
                }
                elide: Text.ElideMiddle
                color: Theme.text
                font.pixelSize: Theme.textTitle
                font.weight: Font.Medium
                Accessible.name: "Folder " + panel.folderPath
            }
            Label {
                objectName: "gridItemCount"
                visible: fileGrid.count > 0 && panel.width >= 480
                text: fileGrid.count === 1 ? "1 item" : fileGrid.count.toLocaleString(Qt.locale(), "f", 0) + " items"
                color: Theme.muted
                font.pixelSize: Theme.textSmall
                font.features: { "tnum": 1 }
            }
            Rectangle { // selection count: visible only for a multi-selection, with its clear action beside it
                objectName: "gridSelectionCount"
                visible: fileGrid.selectedCount > 1
                property string text: fileGrid.selectedCount.toLocaleString(Qt.locale(), "f", 0) + " selected"
                Layout.preferredHeight: Theme.controlHeight - Theme.gapXs
                Layout.preferredWidth: countRow.implicitWidth + Theme.gapSm + Theme.gapXxs
                radius: Theme.radiusControl
                color: Theme.selection
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                RowLayout {
                    id: countRow
                    anchors.verticalCenter: parent.verticalCenter
                    x: Theme.gapSm
                    spacing: Theme.gapXxs
                    Label {
                        text: parent.parent.text
                        color: Theme.text
                        font.pixelSize: Theme.textSmall
                        font.weight: Font.Medium
                        font.features: { "tnum": 1 }
                    }
                    UiButton {
                        objectName: "gridInvertSelection"
                        iconName: "refresh"
                        iconOnly: true
                        text: "Invert selection"
                        tip: "Invert selection (Ctrl+I)"
                        focusPolicy: Qt.NoFocus
                        implicitHeight: Theme.controlHeight - Theme.gapSm
                        implicitWidth: implicitHeight
                        onClicked: { fileGrid.invertSelection(); panel.focusGrid() }
                    }
                    UiButton {
                        objectName: "gridClearSelection"
                        iconName: "close"
                        iconOnly: true
                        text: "Clear selection"
                        tip: "Clear selection (Esc)"
                        focusPolicy: Qt.NoFocus
                        implicitHeight: Theme.controlHeight - Theme.gapSm
                        implicitWidth: implicitHeight
                        onClicked: { fileGrid.clearSelection(); panel.focusGrid() }
                    }
                }
            }
            Label { // a known read-only drive: the action stays visible, disabled, with the reason beside it
                objectName: "gridTrashBlocked"
                visible: panel.deleteBlocked && fileGrid.selectedCount > 0
                text: "Read-only drive"
                color: Theme.muted
                font.pixelSize: Theme.textSmall
            }
            UiButton {
                objectName: "gridMoveToTrash"
                visible: fileGrid.selectedCount > 0
                iconName: "trash"
                iconOnly: panel.width < 640
                text: "Move to " + panel.trashName
                tip: "Move the selected items to the " + panel.trashName + ". You can restore them from there."
                Accessible.description: panel.deleteBlocked ? "Unavailable: this drive is read-only" : tip
                enabled: panel.inputEnabled && !panel.deleteBlocked && !fileOperations.busy
                focusPolicy: Qt.NoFocus
                onClicked: panel.deleteRequested(panel.deletionTargets())
            }
        }
    }
    Rectangle {
        id: truncatedBanner
        objectName: "gridTruncated"
        visible: fileGrid.truncated && fileGrid.count > 0
        anchors { left: parent.left; right: parent.right; top: header.bottom; leftMargin: Theme.gapMd; rightMargin: Theme.gapMd }
        height: visible ? truncatedText.implicitHeight + 2 * Theme.gapSm : 0
        radius: Theme.radiusControl
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.border
        Label {
            id: truncatedText
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: Theme.gapMd }
            // The limit keeps enumeration order, not display order, so this must not claim "the first" items.
            text: "Showing 100,000 items. This folder has more that are not listed here."
            wrapMode: Text.Wrap
            color: Theme.text
            font.pixelSize: Theme.textSmall
            Accessible.role: Accessible.AlertMessage
        }
    }
    GridView {
        id: grid
        objectName: "photoGridView"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; top: truncatedBanner.visible ? truncatedBanner.bottom : header.bottom }
        anchors.leftMargin: Theme.gapMd; anchors.rightMargin: Theme.gapMd; anchors.bottomMargin: Theme.gapMd
        anchors.topMargin: truncatedBanner.visible ? Theme.gapSm : 0
        clip: true
        model: panel.visible ? fileGrid : null
        cellWidth: width / panel.columns
        // Bound rows as well as columns, including cache/partially visible rows. A very tall
        // viewport grows the cells instead of submitting hundreds of simultaneous previews.
        cellHeight: panel.details ? Theme.rowHeight + Theme.gapSm : panel.compact
                    ? (height >= 200 ? Math.floor(height / Math.max(2, Math.floor(height / Math.min(panel.currentBucketSize, cellWidth * 0.6 + 40))))

                                     : Math.max(100, height))
                    : Math.max(Math.floor(panel.currentBucketSize * 0.9), Math.min(Math.floor(panel.currentBucketSize * 1.4), cellWidth * 0.75 + 46),
                               Math.ceil(height / Math.max(1, Math.floor(panel.currentBucketSize / panel.columns) - 4)))
        cacheBuffer: cellHeight // one extra row; requests disappear with delegates and the hidden model
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        activeFocusOnTab: true
        enabled: panel.inputEnabled
        Accessible.role: Accessible.List
        Accessible.name: "Items in " + panel.folderPath
        Accessible.description: "Arrow keys move. Shift extends, Ctrl+Space toggles, Ctrl+A selects all. Enter opens a folder or photo. Backspace goes up. Escape clears the selection, then returns to the photo."
        ScrollBar.vertical: UiScrollBar { }
        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: event => {
                if (event.angleDelta.y > 0) panel.zoomIn()
                else if (event.angleDelta.y < 0) panel.zoomOut()
            }
        }
        // Grid keys beat remapped window shortcuts while the grid has focus (same key, correct scope).
        Keys.onShortcutOverride: event => {
            if (panel.yields && panel.yields(event)) return // INPUT-02: a registered grid-command key beats type-to-select and native keys
            if (panel.searchQuery.length > 0) {
                if (event.key === Qt.Key_Escape || event.key === Qt.Key_Return || event.key === Qt.Key_Enter ||
                    event.key === Qt.Key_Backspace || event.key === Qt.Key_F3 || event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                    event.accepted = true
                    return
                }
            }
            if (event.key === Qt.Key_F3) { event.accepted = true; return }
            const mods = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
            if (event.key >= 0x20 && event.key <= 0x7e && !mods) {
                event.accepted = true
                return
            }
            if (panel.keyAction(event).length) event.accepted = true
        }
        Keys.onPressed: event => {
            if (panel.yields && panel.yields(event)) return
            const mods = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)

            // Printable character (key >= 0x20 and key <= 0x7e, without Ctrl/Alt/Meta modifiers)
            if (event.key >= 0x20 && event.key <= 0x7e && !mods && event.text.length > 0) {
                if (panel.searchQuery.length === 0) {
                    panel.preSearchRow = fileGrid.cursorRow
                }
                panel.searchQuery += event.text
                const match = fileGrid.findMatch(panel.searchQuery, fileGrid.cursorRow >= 0 ? fileGrid.cursorRow : 0, true)
                if (match >= 0) {
                    panel.select(match, panel.modeReplace)
                    searchTimer.restart()
                } else {
                    searchTimer.restart()
                }
                event.accepted = true
                return
            }

            // F3 or Down/Up while searching
            if (event.key === Qt.Key_F3 || ((event.key === Qt.Key_Down || event.key === Qt.Key_Up) && panel.searchQuery.length > 0)) {
                if (panel.searchQuery.length > 0 && fileGrid.count > 0) {
                    const forward = !(event.modifiers & Qt.ShiftModifier) && event.key !== Qt.Key_Up
                    const start = forward ? (fileGrid.cursorRow + 1) % fileGrid.count : (fileGrid.cursorRow - 1 + fileGrid.count) % fileGrid.count
                    const next = fileGrid.findMatch(panel.searchQuery, start, forward)
                    if (next >= 0) {
                        panel.select(next, panel.modeReplace)
                        searchTimer.restart()
                    }
                }
                event.accepted = true
                return
            }

            // Backspace while searching
            if (event.key === Qt.Key_Backspace && panel.searchQuery.length > 0) {
                panel.searchQuery = panel.searchQuery.slice(0, -1)
                if (panel.searchQuery.length === 0) {
                    panel.preSearchRow = -1
                    searchTimer.stop()
                } else {
                    const match = fileGrid.findMatch(panel.searchQuery, fileGrid.cursorRow >= 0 ? fileGrid.cursorRow : 0, true)
                    if (match >= 0) {
                        panel.select(match, panel.modeReplace)
                    }
                    searchTimer.restart()
                }
                event.accepted = true
                return
            }

            // Escape while searching
            if (event.key === Qt.Key_Escape && panel.searchQuery.length > 0) {
                if (panel.preSearchRow >= 0 && panel.preSearchRow < fileGrid.count) {
                    panel.select(panel.preSearchRow, panel.modeReplace)
                }
                panel.searchQuery = ""
                panel.preSearchRow = -1
                searchTimer.stop()
                event.accepted = true
                return
            }

            // Return / Enter while searching
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && panel.searchQuery.length > 0) {
                panel.searchQuery = ""
                panel.preSearchRow = -1
                searchTimer.stop()
                panel.activate()
                event.accepted = true
                return
            }

            const action = panel.keyAction(event)
            if (!action.length) return
            event.accepted = true
            if (action === "activate") { panel.activate(); return }
            if (action === "up") { panel.goUp(); return }
            if (action === "menu") { panel.contextAtCursor(); return }
            if (action === "all") { fileGrid.selectAll(); return }
            if (action === "invert") { fileGrid.invertSelection(); return }
            if (action === "clear") { fileGrid.clearSelection(); return }
            if (action === "zoomIn") { panel.zoomIn(); return }
            if (action === "zoomOut") { panel.zoomOut(); return }
            if (action === "zoomReset") { panel.resetZoom(); return }
            if (action === "escape") { if (fileGrid.selectedCount > 1) fileGrid.clearSelection(); else panel.dismissed(); return }
            if (action === "toggle") {
                if (fileGrid.cursorRow >= 0) fileGrid.select(fileGrid.cursorRow, panel.modeToggle)
                else panel.select(0)
                return
            }
            const page = Math.max(1, Math.floor(height / cellHeight)) * panel.columns
            const last = fileGrid.count - 1
            let next = fileGrid.cursorRow
            if (next < 0) next = 0
            else switch (event.key) {
            case Qt.Key_Left: --next; break
            case Qt.Key_Right: ++next; break
            case Qt.Key_Up: next -= panel.columns; break
            case Qt.Key_Down: next += panel.columns; break
            case Qt.Key_Home: next = 0; break
            case Qt.Key_End: next = last; break
            case Qt.Key_PageUp: next -= page; break
            case Qt.Key_PageDown: next += page; break
            }
            panel.select(next, action === "extend" ? panel.modeRange : panel.modeReplace)
        }
        // Right click: the item under the pointer, or the empty area. Left clicks stay with the delegates.
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: point => {
                const row = grid.indexAt(point.position.x + grid.contentX, point.position.y + grid.contentY)
                const at = grid.mapToItem(panel, point.position.x, point.position.y)
                panel.contextRequested(at.x, at.y, row)
            }
        }
        Keys.onBacktabPressed: event => { panel.foldersRequested(); event.accepted = true }
        Keys.onTabPressed: event => { if (panel.tabHandler) panel.tabHandler(); event.accepted = panel.tabHandler !== null }
        delegate: ItemDelegate {
            id: cell
            required property int index
            required property string name
            required property string path
            required property string kind
            required property bool isLink
            required property bool isSelected
            required property bool isCursor
            required property var size
            required property var modified
            required property bool isCurrent
            readonly property bool selected: isSelected
            readonly property bool image: kind === "image"
            property bool previewFailed: false // set by the tile's image (it lives in the tile component, absent in details mode)
            readonly property string extension: panel.fileExtension(name)
            objectName: "photoGridCell"
            Component.onCompleted: ++panel.liveDelegateCount
            Component.onDestruction: --panel.liveDelegateCount
            width: grid.cellWidth - Theme.gapSm
            height: grid.cellHeight - Theme.gapSm
            padding: Theme.gapSm
            focusPolicy: Qt.NoFocus
            Accessible.role: Accessible.ListItem
            Accessible.name: kind === "folder" ? name + ", folder" + (isLink ? ", link" : "")
                           : kind === "file" ? name + ", " + (extension.length ? extension + " file" : "file") + ", not a supported photo" + (isLink ? ", link" : "")
                           : name + (isCurrent ? ", current photo" : "") + (isLink ? ", link" : "")
            Accessible.description: kind === "folder" ? "Press Enter or double-click to open the folder."
                                  : kind === "file" ? "Glancelot opens JPEG and PNG photos only."
                                  : (previewFailed ? "Preview unavailable. " : "") + "Press Enter or double-click to open."
            Accessible.selected: isSelected
            Accessible.focusable: true
            Accessible.focused: isCursor && grid.activeFocus
            Accessible.onPressAction: { panel.select(index); panel.activate() }
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: name + (kind === "folder" && isLink ? " (link)" : "") + (kind === "file" ? " (not a supported photo)" : "") + (previewFailed ? " (preview unavailable)" : "")
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onClicked: mouse => panel.clickRow(cell.index, mouse.modifiers)
                onDoubleClicked: mouse => { if (!(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) { panel.select(cell.index); panel.activate() } }
            }
            // Selected = selection fill plus a primary ring; the keyboard cursor adds the focus ring while the grid has focus.
            background: Rectangle {
                radius: Theme.radiusCard
                color: cell.isSelected ? Theme.selection : (cell.hovered ? Theme.hover : Theme.surface)
                readonly property bool cursorRing: cell.isCursor && grid.activeFocus
                border.width: cursorRing || cell.isSelected ? Theme.focusWidth : 0
                border.color: cursorRing ? Theme.focusRing : Theme.primary
            }
            contentItem: Loader { sourceComponent: panel.details ? detailsComp : tileComp }
            Component {
                id: tileComp
            Column {
                spacing: Theme.gapXs + Theme.gapXxs
                Item {
                    id: preview
                    width: parent.width
                    height: cell.availableHeight - filename.height - parent.spacing
                    Rectangle { anchors.fill: parent; color: Theme.hover; visible: !cell.image || thumb.status !== Image.Ready; radius: Theme.radiusSmall }
                    Image {
                        id: thumb
                        objectName: "gridThumb" + cell.index
                        anchors.fill: parent
                        asynchronous: true
                        // Only image rows have a source; the epoch term re-requests after an on-disk change.
                        source: cell.image ? (fileGrid.epoch, fileGrid.thumbnailSource(cell.index, panel.thumbEdge)) : ""
                        sourceSize.width: panel.thumbEdge
                        sourceSize.height: panel.thumbEdge
                        fillMode: Image.PreserveAspectFit
                        visible: status === Image.Ready
                        onStatusChanged: cell.previewFailed = cell.image && status === Image.Error
                        Component.onDestruction: cell.previewFailed = false
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 12
                        visible: cell.image && thumb.status !== Image.Ready
                        text: cell.previewFailed ? "Preview unavailable" : "Loading preview…"
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: Theme.muted
                        font.pixelSize: Theme.textSmall
                    }
                    Column { // folder and generic-file glyphs: one outline family, a lighter stroke at tile size
                        visible: !cell.image
                        anchors.centerIn: parent
                        spacing: Theme.gapXs
                        readonly property int glyph: Math.max(24, Math.min(48, Math.round(preview.height * 0.42)))
                        UiIcon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            name: cell.kind === "folder" ? "folder" : "file"
                            size: parent.glyph
                            lineWidth: 1.5 * 22 / parent.glyph
                            color: cell.kind === "folder" ? Theme.text : Theme.muted
                        }
                        Label {
                            objectName: "gridExtension"
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: cell.kind === "file" && cell.extension.length > 0
                            text: cell.extension
                            color: Theme.muted
                            font.pixelSize: Theme.textCaption
                            font.weight: Font.Medium
                            font.letterSpacing: 0.4
                        }
                    }
                    // F014 badges from data the grid already has: the photo type, and a failed preview. Words, not colour
                    // alone; they sit on the left edge so the link badge (right) and the "Viewing" tag (bottom) stay clear.
                    Rectangle {
                        objectName: "gridTypeBadge"
                        visible: cell.image && cell.extension.length > 0 && preview.width >= 96
                        anchors { left: parent.left; top: parent.top; margins: Theme.gapXs }
                        width: typeLabel.implicitWidth + 2 * Theme.gapXs
                        height: Theme.iconSize + Theme.gapXs
                        radius: Theme.radiusSmall
                        color: Theme.surface
                        border.width: Theme.borderWidth
                        border.color: Theme.border
                        Accessible.ignored: true // the extension is already in the cell's description
                        Label { id: typeLabel; anchors.centerIn: parent; text: cell.extension; color: Theme.text; font.pixelSize: Theme.textCaption; font.weight: Font.Medium; font.letterSpacing: 0.4 }
                    }
                    Rectangle {
                        objectName: "gridErrorBadge"
                        visible: cell.previewFailed
                        anchors { left: parent.left; bottom: parent.bottom; margins: Theme.gapXs }
                        width: errorRow.implicitWidth + 2 * Theme.gapXs
                        height: Theme.iconSize + Theme.gapXs
                        radius: Theme.radiusSmall
                        color: Theme.surface
                        border.width: Theme.borderWidth
                        border.color: Theme.danger
                        RowLayout {
                            id: errorRow
                            anchors.centerIn: parent
                            spacing: Theme.gapXxs
                            UiIcon { name: "alert"; size: 12; lineWidth: 2; color: Theme.danger }
                            Label { text: "No preview"; visible: preview.width >= 96; color: Theme.text; font.pixelSize: Theme.textCaption }
                        }
                    }
                    Rectangle { // link badge
                        objectName: "gridLinkBadge"
                        visible: cell.isLink
                        anchors { right: parent.right; top: parent.top; margins: Theme.gapXs }
                        width: linkRow.implicitWidth + 2 * Theme.gapXs
                        height: Theme.iconSize + Theme.gapXs
                        radius: Theme.radiusSmall
                        color: Theme.surface
                        border.width: Theme.borderWidth
                        border.color: Theme.border
                        RowLayout {
                            id: linkRow
                            anchors.centerIn: parent
                            spacing: Theme.gapXxs
                            UiIcon { name: "link"; size: 12; lineWidth: 2; color: Theme.text }
                            Label { text: "Link"; visible: preview.width >= 96; color: Theme.text; font.pixelSize: Theme.textCaption }
                        }
                    }
                    Label { // tag
                        anchors { left: parent.left; bottom: parent.bottom; margins: Theme.gapXs }
                        visible: cell.isCurrent
                        text: "Viewing"
                        leftPadding: Theme.gapXs + Theme.gapXxs; rightPadding: leftPadding
                        topPadding: Theme.gapXxs; bottomPadding: Theme.gapXxs
                        font.pixelSize: Theme.textCaption
                        font.weight: Font.Medium
                        color: Theme.textOnPrimary
                        background: Rectangle { color: Theme.primary; radius: Theme.radiusSmall }
                    }
                }
                Label {
                    id: filename
                    width: parent.width
                    text: cell.name
                    elide: Text.ElideMiddle
                    color: Theme.text
                    font.pixelSize: Theme.textSmall
                }
            }
            }
            Component {
                id: detailsComp
                // F009 details row: name, size and modified time on one line; no thumbnail is requested in this mode.
                RowLayout {
                    spacing: Theme.gapMd
                    UiIcon { name: cell.kind === "folder" ? "folder" : cell.image ? "image" : "file"; color: cell.kind === "folder" ? Theme.text : Theme.muted }
                    Label { Layout.fillWidth: true; text: cell.name; elide: Text.ElideMiddle; color: Theme.text; font.pixelSize: Theme.textBody }
                    Label {
                        Layout.preferredWidth: 84; horizontalAlignment: Text.AlignRight
                        text: cell.kind === "folder" ? "" : panel.sizeText(cell.size)
                        color: Theme.muted; font.pixelSize: Theme.textSmall; font.features: { "tnum": 1 }
                    }
                    Label {
                        Layout.preferredWidth: panel.width >= 520 ? 150 : 0; visible: panel.width >= 520
                        text: cell.modified ? Qt.formatDateTime(cell.modified, "yyyy-MM-dd HH:mm") : ""
                        color: Theme.muted; font.pixelSize: Theme.textSmall; font.features: { "tnum": 1 }
                    }
                }
            }
        }
    }
    // Folder states: one message and, where it helps, one action.
    ColumnLayout {
        objectName: "gridState"
        readonly property string stateName: panel.stateName
        visible: stateName.length > 0
        anchors.centerIn: grid
        width: Math.max(0, Math.min(420, grid.width - 2 * Theme.gapXl))
        spacing: Theme.gapSm
        UiIcon {
            Layout.alignment: Qt.AlignHCenter
            name: panel.stateName === "loading" ? "spinner" : panel.stateName === "inaccessible" ? "lock" : "folder"
            size: 32
            lineWidth: 1.5 * 22 / 32
            color: Theme.muted
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: ({ loading: "Loading folder…", inaccessible: "Can't open this folder", empty: "This folder is empty",
                     none: "No folder open" })[panel.stateName] || ""
            color: Theme.text
            font.pixelSize: Theme.textTitle
            font.weight: Font.Medium
            wrapMode: Text.Wrap
            Accessible.role: Accessible.Heading
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            horizontalAlignment: Text.AlignHCenter
            text: ({ inaccessible: "It may have moved, or you may not have permission to read it.",
                     empty: "It has no files or folders. Go up a level or choose another folder.",
                     none: "Open a photo or choose a folder to browse." })[panel.stateName] || ""
            color: Theme.muted
            font.pixelSize: Theme.textSmall
            wrapMode: Text.Wrap
            lineHeight: 1.4
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.gapXs
            spacing: Theme.gapSm
            UiButton { // a share or permission error may be transient
                objectName: "gridRetry"
                visible: panel.stateName === "inaccessible"
                variant: "outline"
                text: "Try again"
                onClicked: fileGrid.refresh()
            }
            UiButton {
                visible: (panel.stateName === "empty" || panel.stateName === "inaccessible") && panel.parentPath.length > 0
                variant: "outline"
                iconName: "corner-up"
                text: "Up one folder"
                onClicked: panel.goUp()
            }
        }
    }
    Rectangle { // inline explanation (unsupported file, folder that could not be entered)
        objectName: "gridNotice"
        readonly property string text: panel.noticeText
        visible: text.length > 0
        anchors { horizontalCenter: grid.horizontalCenter; bottom: grid.bottom; bottomMargin: Theme.gapMd }
        width: Math.min(grid.width - 2 * Theme.gapMd, noticeLabel.implicitWidth + 2 * Theme.gapLg)
        height: noticeLabel.implicitHeight + 2 * Theme.gapSm + Theme.gapXxs
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.border
        Label {
            id: noticeLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 2 * Theme.gapLg)
            text: panel.noticeText
            color: Theme.text
            wrapMode: Text.Wrap
            font.pixelSize: Theme.textSmall
            Accessible.role: Accessible.AlertMessage
        }
    }

    // Instant search (F004): floating chip overlay showing current query and match position
    Rectangle {
        id: searchChip
        objectName: "gridSearchChip"
        visible: panel.searchQuery.length > 0
        anchors {
            top: grid.top
            right: grid.right
            topMargin: Theme.gapMd
            rightMargin: Theme.gapMd
        }
        z: 10
        height: Theme.controlHeight
        implicitWidth: searchChipRow.implicitWidth + 2 * Theme.gapSm
        radius: Theme.radiusControl
        color: Qt.alpha(Theme.surface, 0.94)
        border.width: Theme.borderWidth
        border.color: Theme.border
        Accessible.role: Accessible.StaticText
        Accessible.name: panel.searchQuery.length > 0 ? ("Search: " + panel.searchQuery + ", " + searchCountText.text) : ""

        RowLayout {
            id: searchChipRow
            anchors.fill: parent
            anchors.leftMargin: Theme.gapSm
            anchors.rightMargin: Theme.gapSm
            spacing: Theme.gapXs

            UiIcon {
                name: "search"
                size: Theme.iconSize
                color: Theme.muted
            }

            Label {
                id: searchQueryText
                objectName: "gridSearchQuery"
                text: panel.searchQuery
                color: Theme.text
                font.pixelSize: Theme.textSmall
                font.weight: Font.Medium
            }

            Rectangle {
                width: 1
                height: 12
                color: Theme.border
            }

            Label {
                id: searchCountText
                objectName: "gridSearchCount"
                text: fileGrid.matchCount(panel.searchQuery) > 0
                      ? (fileGrid.matchIndex(panel.searchQuery, fileGrid.cursorRow) + " of " + fileGrid.matchCount(panel.searchQuery))
                      : "No matches"
                color: fileGrid.matchCount(panel.searchQuery) > 0 ? Theme.muted : Theme.danger
                font.pixelSize: Theme.textSmall
                font.features: { "tnum": 1 }
            }
        }
    }
}
