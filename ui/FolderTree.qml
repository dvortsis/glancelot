import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// Collapsible, lazily loaded folder tree for the left HUD. The model lists one level per expansion on a
// background thread; this file only presents it and routes selection to viewer.openFolder().
// Standard tree conventions: chevron disclosure, per-level indent with guides, folder glyphs, full-row hover
// and selection, a keyboard cursor distinct from the current folder, double-click to expand.
Rectangle {
    id: panel
    property bool compact: false
    property alias hovered: hover.hovered
    // The HUD must stay open while the pointer is over the panel or the keyboard cursor is inside it.
    property int focusedControls: 0
    readonly property bool keepOpen: hover.hovered || list.activeFocus || focusedControls > 0 || address.activeFocus || resizer.pressed
    readonly property int indent: 12
    // Drag the trailing edge to resize (this session); the host bounds it so the photo keeps most of the window.
    property int userWidth: 0
    property int maximumWidth: 480
    signal dismissed()
    function focusList() {
        list.forceActiveFocus(Qt.TabFocusReason)
        if (list.currentIndex < 0) list.currentIndex = Math.max(0, folderTree.currentRow)
    }
    function revealCurrent() { if (folderTree.currentRow >= 0) list.positionViewAtIndex(folderTree.currentRow, ListView.Contain) }
    // A drive or "/" would become the traversal root and let navigation walk the whole volume, so roots only expand.
    function activate(row, depth, expandable, isCurrent, path) {
        if (depth === 0 && expandable) folderTree.toggle(row)
        else if (!isCurrent) viewer.openFolder(path)
    }
    implicitWidth: Math.min(maximumWidth, userWidth > 0 ? userWidth : (compact ? 232 : 280))
    color: Theme.surface
    Accessible.role: Accessible.Pane
    Accessible.name: "Folders"
    HoverHandler { id: hover }
    MouseArea { // swallow pointer input on blank panel space so it never reaches the canvas behind
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }
    onVisibleChanged: if (visible) Qt.callLater(revealCurrent)

    Rectangle { // edge against the canvas; doubles as the resize affordance under the pointer
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: resizer.containsMouse || resizer.pressed ? Theme.focusWidth : Theme.borderWidth
        color: resizer.containsMouse || resizer.pressed ? Theme.primary : Theme.border
    }
    MouseArea {
        id: resizer
        objectName: "treeResizer"
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right; rightMargin: -Theme.gapXs }
        width: Theme.gapSm
        z: 2
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        onPositionChanged: mouse => {
            if (pressed) panel.userWidth = Math.round(Math.max(200, Math.min(panel.maximumWidth, mapToItem(panel, mouse.x, 0).x)))
        }
        onDoubleClicked: panel.userWidth = 0 // back to the default width
    }

    component PanelButton: UiButton {
        onActiveFocusChanged: panel.focusedControls += activeFocus ? 1 : -1
        Component.onDestruction: if (activeFocus) panel.focusedControls -= 1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: Theme.borderWidth
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.controlHeight + Theme.gapSm
            Layout.topMargin: Theme.gapXs
            Layout.leftMargin: Theme.gapMd // shares the leading edge of the depth-0 chevrons
            Layout.rightMargin: Theme.gapSm
            spacing: Theme.gapXxs
            Label {
                text: "Folders"
                color: Theme.text
                font.pixelSize: Theme.textBody
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
            }
            PanelButton {
                objectName: "treeParent"
                text: "Show parent folder"
                iconName: "corner-up"
                iconOnly: true
                tip: "Widen the browsing root to the parent folder"
                enabled: viewer.canRaiseRoot
                onClicked: viewer.raiseRoot()
            }
            PanelButton {
                objectName: "treePin"
                text: viewer.treePinned ? "Unpin folder panel" : "Pin folder panel open"
                iconName: "pin"
                iconOnly: true
                checkable: true
                checked: viewer.treePinned
                tip: viewer.treePinned ? "Unpin: let the panel hide with the other controls" : "Pin: keep the panel open and make room for the photo"
                onToggled: { viewer.treePinned = checked; checked = Qt.binding(() => viewer.treePinned) } // stays in sync with Settings
            }
        }

        PanelButton {
            objectName: "allLocations"
            text: "All locations"
            iconName: "computer"
            alignment: Qt.AlignLeft
            leftPadding: Theme.gapXs + panel.indent + Theme.gapXxs // icon sits on the same column as the root glyphs
            Accessible.name: "Browse all filesystem locations"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapSm; Layout.rightMargin: Theme.gapSm
            Layout.bottomMargin: Theme.gapXs
            onClicked: { folderTree.browseComputer(); panel.focusList() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapSm; Layout.rightMargin: Theme.gapSm
            Layout.bottomMargin: Theme.gapXs
            spacing: Theme.gapXs
            UiTextField {
                id: address
                objectName: "folderAddress"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                mono: true
                placeholderText: "Folder path"
                Accessible.name: "Current folder path"
                Accessible.description: "Enter a path and press Enter to navigate. Escape restores the current folder."
                property bool modified: false
                text: viewer.directory
                selectByMouse: true
                onTextEdited: modified = true
                onAccepted: { modified = false; viewer.navigateDirectory(text) }
                Keys.onEscapePressed: event => {
                    viewer.cancelDirectoryNavigation(); text = viewer.directory; modified = false; panel.focusList(); event.accepted = true
                }
                ToolTip.visible: hovered && !activeFocus
                ToolTip.text: viewer.directory
                Connections {
                    target: viewer
                    function onStateChanged() {
                        if (!address.activeFocus || !address.modified) {
                            if (!viewer.directoryPending && viewer.directoryError.length === 0) address.text = viewer.directory
                        }
                    }
                }
            }
            PanelButton {
                objectName: "copyFolderAddress"
                text: "Copy"
                tip: "Copy the full current folder path"
                enabled: viewer.directory.length > 0
                onClicked: viewer.copyDirectory()
            }
        }
        Label {
            objectName: "folderAddressFeedback"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapMd; Layout.rightMargin: Theme.gapMd
            Layout.bottomMargin: visible ? Theme.gapSm : 0
            visible: viewer.directoryPending || viewer.directoryError.length > 0
            text: viewer.directoryPending ? "Opening folder…" : viewer.directoryError
            color: Theme.text
            font.pixelSize: Theme.textSmall
            wrapMode: Text.Wrap
            Accessible.name: text
        }

        ListView {
            id: list
            objectName: "folderList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            leftMargin: Theme.gapSm
            rightMargin: Theme.gapSm
            bottomMargin: Theme.gapSm
            clip: true
            // A hidden panel must not load folders: no model, no rows.
            model: panel.visible ? folderTree : null
            currentIndex: -1
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: false
            Accessible.role: Accessible.Tree
            Accessible.name: "Folder tree"
            ScrollBar.vertical: UiScrollBar {}
            Connections {
                target: folderTree
                enabled: panel.visible
                function onCurrentRowChanged() { panel.revealCurrent() }
            }
            // Keys the tree owns while it has focus; without this the window-level shortcuts would also fire.
            Keys.onShortcutOverride: event => {
                event.accepted = [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End,
                                  Qt.Key_PageUp, Qt.Key_PageDown, Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space, Qt.Key_Escape].indexOf(event.key) >= 0
            }
            Keys.onUpPressed: currentIndex = Math.max(0, currentIndex - 1)
            Keys.onDownPressed: currentIndex = Math.min(count - 1, currentIndex + 1)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Escape) { panel.dismissed(); event.accepted = true; return }
                if (currentIndex < 0 || currentIndex >= count) return
                const row = currentIndex
                const cell = folderTree.rowInfo(row)
                const page = Math.max(1, Math.floor(height / Theme.rowHeight) - 1)
                if (event.key === Qt.Key_Home) { currentIndex = 0; event.accepted = true }
                else if (event.key === Qt.Key_End) { currentIndex = count - 1; event.accepted = true }
                else if (event.key === Qt.Key_PageUp) { currentIndex = Math.max(0, row - page); event.accepted = true }
                else if (event.key === Qt.Key_PageDown) { currentIndex = Math.min(count - 1, row + page); event.accepted = true }
                else if (event.key === Qt.Key_Right) {
                    if (cell.expandable && !cell.expanded) folderTree.expand(row)
                    else if (cell.expanded && row + 1 < count) currentIndex = row + 1
                    event.accepted = true
                } else if (event.key === Qt.Key_Left) {
                    if (cell.expanded || cell.loading) folderTree.collapse(row)
                    else { const parent = folderTree.parentRow(row); if (parent >= 0) currentIndex = parent }
                    event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    panel.activate(row, cell.depth, cell.expandable, cell.isCurrent, cell.path)
                    event.accepted = true
                }
            }

            delegate: Item {
                id: row
                objectName: "folderRow"
                required property int index
                required property string name
                required property string path
                required property int depth
                required property bool expanded
                required property bool loading
                required property int nodeState
                required property bool isCurrent
                readonly property bool expandable: nodeState !== 2 && nodeState !== 3 // not Empty, not Inaccessible
                readonly property bool cursor: ListView.isCurrentItem && list.activeFocus
                readonly property int levelX: Theme.gapXs + depth * panel.indent
                width: ListView.view.width - list.leftMargin - list.rightMargin
                height: Theme.rowHeight
                Accessible.role: Accessible.TreeItem
                Accessible.name: name + (isCurrent ? ", current folder" : "") + (nodeState === 3 ? ", no access" : "")
                Accessible.description: expandable ? (expanded ? "Expanded" : "Collapsed") : "" // state in words: Qt 6.8 QML has no expandable/expanded accessibility properties
                Accessible.selected: isCurrent

                Repeater { // indent guides: one hairline per ancestor level, centred under that level's chevron
                    model: row.depth
                    delegate: Rectangle {
                        required property int index
                        x: Theme.gapXs + index * panel.indent + panel.indent / 2
                        width: Theme.borderWidth
                        height: row.height
                        color: Theme.border
                    }
                }
                Rectangle { // row surface: hover and current folder; the keyboard cursor is a separate ring
                    anchors.fill: parent
                    radius: Theme.radiusControl
                    color: row.isCurrent ? Theme.selection : (rowHover.hovered ? Theme.hover : "transparent")
                    border.width: row.cursor ? Theme.focusWidth : 0
                    border.color: Theme.focusRing
                    Rectangle { // current-folder marker, so the state does not rely on fill colour alone
                        visible: row.isCurrent
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom; topMargin: 6; bottomMargin: 6 }
                        width: 3; radius: 1.5
                        color: Theme.primary
                    }
                }
                HoverHandler { id: rowHover }
                TapHandler {
                    onTapped: point => {
                        if (tapCount > 1) return // the second click of a double-click belongs to onDoubleTapped
                        // The disclosure owns clicks inside its box; they must not also open the folder.
                        const local = row.mapToItem(disclosure, point.position.x, point.position.y)
                        if (row.expandable && disclosure.contains(local)) return
                        list.currentIndex = row.index
                        list.forceActiveFocus(Qt.MouseFocusReason)
                        panel.activate(row.index, row.depth, row.expandable, row.isCurrent, row.path)
                    }
                    // Double-click expands or collapses, as in standard trees; roots already toggle on a single click.
                    onDoubleTapped: point => {
                        const local = row.mapToItem(disclosure, point.position.x, point.position.y)
                        if (row.depth > 0 && row.expandable && !disclosure.contains(local)) folderTree.toggle(row.index)
                    }
                }
                Item { // disclosure: generous hit area, 16px glyph
                    id: disclosure
                    x: row.levelX - Theme.gapXs - Theme.gapXxs
                    width: 22 // >= 22px target even with the compact 12px indent
                    height: parent.height
                    UiIcon {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 1 // glyph centre stays on the indent guide column
                        visible: row.expandable && !row.loading
                        name: "chevron-right"
                        color: Theme.muted
                        rotation: row.expanded ? 90 : 0
                        Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
                    }
                    UiIcon {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 1 // glyph centre stays on the indent guide column
                        visible: row.loading
                        name: "spinner"
                        color: Theme.muted
                    }
                    TapHandler {
                        enabled: row.expandable
                        gesturePolicy: TapHandler.WithinBounds
                        onTapped: { if (tapCount > 1) return; list.currentIndex = row.index; folderTree.toggle(row.index) } // a double-click must not collapse and relist
                    }
                }
                UiIcon {
                    id: glyph
                    x: row.levelX + panel.indent + Theme.gapXxs
                    anchors.verticalCenter: parent.verticalCenter
                    name: row.depth === 0 ? "drive" : (row.expanded ? "folder-open" : "folder")
                    color: row.isCurrent ? Theme.primary : (row.nodeState === 3 ? Theme.border : Theme.muted)
                }
                Label { // state in words, so it never relies on colour or a missing chevron alone
                    id: stateNote
                    anchors { right: parent.right; rightMargin: Theme.gapSm; verticalCenter: parent.verticalCenter }
                    text: row.nodeState === 2 ? (row.depth === 0 ? "no subfolders" : "empty") : (row.nodeState === 3 ? "no access" : (row.nodeState === 4 ? "partial" : ""))
                    color: Theme.muted
                    font.pixelSize: Theme.textCaption
                }
                Label {
                    id: nameLabel
                    anchors { left: glyph.right; leftMargin: Theme.gapSm - Theme.gapXxs; right: stateNote.text.length > 0 ? stateNote.left : parent.right; rightMargin: Theme.gapSm; verticalCenter: parent.verticalCenter }
                    text: row.name
                    elide: Text.ElideRight
                    color: row.nodeState === 3 ? Theme.muted : Theme.text
                    font.pixelSize: Theme.textBody
                    font.weight: row.isCurrent ? Font.DemiBold : Font.Normal
                }
                // Full name when it is cut off, and the reason behind a state note.
                readonly property string tip: (nameLabel.truncated ? row.name : "")
                    + (row.nodeState === 3 ? (nameLabel.truncated ? " — " : "") + "This folder cannot be listed"
                       : row.nodeState === 4 ? (nameLabel.truncated ? " — " : "") + "Some folders are not shown because the list limit was reached" : "")
                ToolTip.visible: rowHover.hovered && tip.length > 0
                ToolTip.delay: 400
                ToolTip.text: tip
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.gapXl
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: folderTree.count === 0
                text: "No filesystem locations are available."
                color: Theme.muted
                font.pixelSize: Theme.textSmall
            }
        }
    }
}
