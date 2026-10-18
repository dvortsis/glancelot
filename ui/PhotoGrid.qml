import QtQuick
import QtQuick.Controls
import Glancelot

// Selection is a browsing cursor, independent of the decoded photo. Only activation opens media.
Rectangle {
    id: panel
    color: Theme.background
    property int selectedIndex: -1
    property string selectedPath: ""
    property string entryCurrentPath: ""
    property bool inputEnabled: true
    property int liveDelegateCount: 0
    // Capped so delegates (and thumbnail requests) stay under the service queue on very large displays.
    readonly property int columns: Math.max(1, Math.min(12, Math.floor((grid.width - 24) / 176)))
    readonly property int count: grid.count
    readonly property int thumbEdge: 256
    signal itemActivated(string path)
    signal dismissed()
    signal foldersRequested()
    Accessible.role: Accessible.Pane
    Accessible.name: "Photo grid"

    function select(index) {
        selectedIndex = filmstrip.count > 0 ? Math.max(0, Math.min(filmstrip.count - 1, index)) : -1
        selectedPath = filmstrip.pathAt(selectedIndex)
        grid.currentIndex = selectedIndex
        if (selectedIndex >= 0) grid.positionViewAtIndex(selectedIndex, GridView.Contain)
    }
    function focusGrid() { if (visible && inputEnabled) grid.forceActiveFocus(Qt.TabFocusReason) }
    function enter() {
        const current = filmstrip.pathAt(filmstrip.currentRow)
        // Re-entering preserves the browsing cursor unless the viewed photo or listing changed.
        if (current !== entryCurrentPath) select(Math.max(0, filmstrip.currentRow))
        else if (selectedPath.length && filmstrip.pathAt(selectedIndex) !== selectedPath) {
            let found = -1
            for (let i = 0; i < filmstrip.count; ++i) {
                if (filmstrip.pathAt(i) === selectedPath) { found = i; break }
            }
            select(found >= 0 ? found : selectedIndex)
        } else select(selectedIndex < 0 ? Math.max(0, filmstrip.currentRow) : selectedIndex)
        entryCurrentPath = current
        focusGrid()
    }
    function activate() {
        const path = filmstrip.pathAt(selectedIndex)
        if (inputEnabled && path.length) { if (!viewer.isCurrentImage(path)) viewer.open(path); itemActivated(path) }
    }
    onVisibleChanged: if (visible) Qt.callLater(enter)
    Connections {
        target: filmstrip
        function onModelReset() { if (panel.visible) Qt.callLater(panel.enter) }
        function onCurrentRowChanged() { if (panel.visible) Qt.callLater(panel.enter) }
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
    GridView {
        id: grid
        objectName: "photoGridView"
        anchors.fill: parent
        anchors.margins: Theme.gapMd
        clip: true
        model: panel.visible ? filmstrip : null
        cellWidth: width / panel.columns
        // Bound rows as well as columns, including cache/partially visible rows. A very tall
        // viewport grows the cells instead of submitting hundreds of simultaneous previews.
        cellHeight: Math.max(160, Math.min(220, cellWidth * 0.75 + 46),
                             Math.ceil(height / Math.max(1, Math.floor(160 / panel.columns) - 4)))
        cacheBuffer: cellHeight // one extra row; requests disappear with delegates and the hidden model
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        activeFocusOnTab: true
        enabled: panel.inputEnabled
        Accessible.role: Accessible.List
        Accessible.name: "Photos in this folder"
        Accessible.description: "Arrow keys select a photo. Enter opens it. Escape returns to the canvas."
        ScrollBar.vertical: UiScrollBar { }
        // Grid navigation keys beat remapped window shortcuts while the grid has focus (same key, correct scope).
        Keys.onShortcutOverride: event => {
            if (event.modifiers !== Qt.NoModifier && event.modifiers !== Qt.KeypadModifier) return
            if ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End, Qt.Key_PageUp, Qt.Key_PageDown,
                 Qt.Key_Return, Qt.Key_Enter, Qt.Key_Escape].indexOf(event.key) >= 0) event.accepted = true
        }
        Keys.onPressed: event => {
            if (event.modifiers !== Qt.NoModifier && event.modifiers !== Qt.KeypadModifier) return
            const page = Math.max(1, Math.floor(height / cellHeight)) * panel.columns
            let next = panel.selectedIndex
            switch (event.key) {
            case Qt.Key_Left: --next; break
            case Qt.Key_Right: ++next; break
            case Qt.Key_Up: next -= panel.columns; break
            case Qt.Key_Down: next += panel.columns; break
            case Qt.Key_Home: next = 0; break
            case Qt.Key_End: next = count - 1; break
            case Qt.Key_PageUp: next -= page; break
            case Qt.Key_PageDown: next += page; break
            case Qt.Key_Return: case Qt.Key_Enter: panel.activate(); event.accepted = true; return
            case Qt.Key_Escape: panel.dismissed(); event.accepted = true; return
            default: return
            }
            panel.select(next)
            event.accepted = true
        }
        Keys.onBacktabPressed: event => { panel.foldersRequested(); event.accepted = true }
        delegate: ItemDelegate {
            id: cell
            required property int index
            required property string name
            required property string path
            required property bool isCurrent
            readonly property bool selected: panel.selectedIndex === index
            readonly property bool previewFailed: thumb.status === Image.Error
            objectName: "photoGridCell"
            Component.onCompleted: ++panel.liveDelegateCount
            Component.onDestruction: --panel.liveDelegateCount
            width: grid.cellWidth - Theme.gapSm
            height: grid.cellHeight - Theme.gapSm
            padding: Theme.gapSm
            focusPolicy: Qt.NoFocus
            Accessible.role: Accessible.ListItem
            Accessible.name: name + (isCurrent ? ", current photo" : "")
            Accessible.description: (previewFailed ? "Preview unavailable. " : "") + "Press Enter or double-click to open."
            Accessible.selected: selected
            Accessible.focusable: true
            Accessible.focused: selected && grid.activeFocus
            Accessible.onPressAction: { panel.select(index); panel.activate() }
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: name + (previewFailed ? " (preview unavailable)" : "")
            onClicked: { panel.select(index); panel.focusGrid() }
            onDoubleClicked: { panel.select(index); panel.activate() }
            // Selected = selection fill plus a ring (focus colour while the grid has keyboard focus); hover = hover fill.
            background: Rectangle {
                radius: Theme.radiusCard
                color: cell.selected ? Theme.selection : (cell.hovered ? Theme.hover : Theme.surface)
                border.width: cell.selected ? Theme.focusWidth : 0
                border.color: grid.activeFocus ? Theme.focusRing : Theme.primary
            }
            contentItem: Column {
                spacing: Theme.gapXs + Theme.gapXxs
                Item {
                    width: parent.width
                    height: cell.availableHeight - filename.height - parent.spacing
                    Rectangle { anchors.fill: parent; color: Theme.hover; visible: thumb.status !== Image.Ready; radius: Theme.radiusSmall }
                    Image {
                        id: thumb
                        objectName: "gridThumb" + cell.index
                        anchors.fill: parent
                        asynchronous: true
                        source: filmstrip.thumbnailSource(cell.path, panel.thumbEdge, filmstrip.epoch)
                        sourceSize.width: panel.thumbEdge
                        sourceSize.height: panel.thumbEdge
                        fillMode: Image.PreserveAspectFit
                        visible: status === Image.Ready
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 12
                        text: cell.previewFailed ? "Preview unavailable" : "Loading preview…"
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: Theme.muted
                        font.pixelSize: Theme.textSmall
                        visible: thumb.status !== Image.Ready
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
    }
    Label {
        anchors.centerIn: parent
        width: Math.max(0, Math.min(420, parent.width - 2 * Theme.gapXl))
        visible: filmstrip.count === 0
        text: viewer.loading ? "Loading folder…" : "No photos in this folder. Open a photo or choose another folder."
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        lineHeight: 1.4
        color: Theme.muted
    }
}
