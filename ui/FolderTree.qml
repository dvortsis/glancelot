import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

// Collapsible, lazily loaded folder tree for the left HUD. The model lists one level per expansion on a
// background thread; this file only presents it and routes selection to viewer.openFolder().
Rectangle {
    id: panel
    property bool compact: false
    property alias hovered: hover.hovered
    // The HUD must stay open while the pointer is over the panel or the keyboard cursor is inside it.
    property int focusedControls: 0
    readonly property bool keepOpen: hover.hovered || list.activeFocus || focusedControls > 0
    signal dismissed()
    function focusList() {
        list.forceActiveFocus(Qt.TabFocusReason)
        if (list.currentIndex < 0) list.currentIndex = Math.max(0, folderTree.currentRow)
    }
    function revealCurrent() { if (folderTree.currentRow >= 0) list.positionViewAtIndex(folderTree.currentRow, ListView.Contain) }
    implicitWidth: compact ? 224 : 280
    color: theme.colors.surface
    Accessible.role: Accessible.Pane
    Accessible.name: "Folders"
    HoverHandler { id: hover }
    MouseArea { // swallow pointer input on blank panel space so it never reaches the canvas behind
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }
    onVisibleChanged: if (visible) Qt.callLater(revealCurrent)

    Rectangle { // edge against the canvas
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: 1
        color: theme.colors.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.leftMargin: 12
            Layout.rightMargin: 8
            spacing: 4
            Label {
                text: "Folders"
                color: theme.colors.text
                font.pixelSize: 13
                font.weight: Font.Medium
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
            }
            ToolButton {
                text: "Parent"
                enabled: folderTree.canRaiseRoot
                focusPolicy: Qt.StrongFocus
                onActiveFocusChanged: panel.focusedControls += activeFocus ? 1 : -1
                Component.onDestruction: if (activeFocus) panel.focusedControls -= 1
                implicitHeight: 32
                Accessible.name: "Show parent folder"
                Accessible.description: "Widen the browsing root to the parent folder"
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: "Widen the browsing root to the parent folder"
                onClicked: viewer.raiseRoot()
                background: Rectangle { radius: 4; color: parent.down || parent.hovered || parent.visualFocus ? theme.colors.hover : "transparent"; border.color: parent.visualFocus ? theme.colors.primary : "transparent" }
            }
            ToolButton {
                text: viewer.treePinned ? "Unpin" : "Pin"
                focusPolicy: Qt.StrongFocus
                onActiveFocusChanged: panel.focusedControls += activeFocus ? 1 : -1
                Component.onDestruction: if (activeFocus) panel.focusedControls -= 1
                implicitHeight: 32
                checkable: true
                checked: viewer.treePinned
                Accessible.name: viewer.treePinned ? "Unpin folder panel" : "Pin folder panel open"
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: viewer.treePinned ? "Let the panel hide with the other controls" : "Keep the panel open and make room for the photo"
                onToggled: viewer.treePinned = checked
                background: Rectangle { radius: 4; color: parent.down || parent.hovered || parent.visualFocus || parent.checked ? theme.colors.hover : "transparent"; border.color: parent.visualFocus ? theme.colors.primary : "transparent" }
            }
        }

        ListView {
            id: list
            objectName: "folderList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            // A hidden panel must not load folders: no model, no rows.
            model: panel.visible ? folderTree : null
            currentIndex: -1
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: false
            Accessible.role: Accessible.Tree
            Accessible.name: "Folder tree"
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
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
                if (event.key === Qt.Key_Home) { currentIndex = 0; event.accepted = true }
                else if (event.key === Qt.Key_End) { currentIndex = count - 1; event.accepted = true }
                else if (event.key === Qt.Key_Right) {
                    const cell = itemAtIndex(row)
                    if (cell && cell.expandable && !cell.expanded) folderTree.expand(row)
                    else if (cell && cell.expanded && row + 1 < count) currentIndex = row + 1
                    event.accepted = true
                } else if (event.key === Qt.Key_Left) {
                    const cell = itemAtIndex(row)
                    if (cell && (cell.expanded || cell.loading)) folderTree.collapse(row)
                    else { const parent = folderTree.parentRow(row); if (parent >= 0) currentIndex = parent }
                    event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    const cell = itemAtIndex(row)
                    if (cell && !cell.isCurrent) viewer.openFolder(folderTree.pathAt(row))
                    event.accepted = true
                }
            }

            delegate: Item {
                id: row
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
                width: ListView.view.width
                height: 34
                Accessible.role: Accessible.TreeItem
                Accessible.name: name + (isCurrent ? ", current folder" : "") + (nodeState === 3 ? ", no access" : "")
                Accessible.description: expandable ? (expanded ? "Expanded" : "Collapsed") : ""
                Rectangle { // row surface: hover, current folder, keyboard cursor
                    anchors { fill: parent; leftMargin: 4; rightMargin: 4 }
                    radius: 4
                    color: row.isCurrent ? Qt.alpha(theme.colors.primary, 0.16)
                                         : (rowHover.hovered ? theme.colors.hover : "transparent")
                    border.width: row.cursor ? 2 : 0
                    border.color: theme.colors.text
                    Rectangle { // current-folder marker that does not rely on color alone with the bold name
                        visible: row.isCurrent
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom; topMargin: 6; bottomMargin: 6 }
                        width: 3; radius: 1.5
                        color: theme.colors.primary
                    }
                }
                HoverHandler { id: rowHover }
                TapHandler {
                    onTapped: point => {
                        // The disclosure owns clicks inside its box; they must not also open the folder.
                        const local = row.mapToItem(disclosure, point.position.x, point.position.y)
                        if (row.expandable && disclosure.contains(local)) return
                        list.currentIndex = row.index
                        list.forceActiveFocus(Qt.MouseFocusReason)
                        if (!row.isCurrent) viewer.openFolder(row.path)
                    }
                }
                Item { // disclosure
                    id: disclosure
                    x: 6 + row.depth * 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28; height: parent.height // generous hit area; the glyph stays 8x12
                    Shape {
                        anchors.centerIn: parent
                        width: 8; height: 12
                        visible: row.expandable && !row.loading
                        rotation: row.expanded ? 90 : 0
                        Behavior on rotation { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                        ShapePath {
                            strokeWidth: 2
                            strokeColor: theme.colors.muted
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            joinStyle: ShapePath.RoundJoin
                            startX: 1.5; startY: 1.5
                            PathLine { x: 6.5; y: 6 }
                            PathLine { x: 1.5; y: 10.5 }
                        }
                    }
                    BusyIndicator {
                        anchors.centerIn: parent
                        width: 16; height: 16
                        running: row.loading
                        visible: row.loading
                        Accessible.ignored: true
                    }
                    TapHandler {
                        enabled: row.expandable
                        gesturePolicy: TapHandler.WithinBounds
                        onTapped: { list.currentIndex = row.index; folderTree.toggle(row.index) }
                    }
                }
                Label { // state in words, so it never relies on color or a missing chevron alone
                    id: stateNote
                    anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    text: row.nodeState === 2 ? (row.depth === 0 ? "no subfolders" : "empty") : (row.nodeState === 3 ? "no access" : (row.nodeState === 4 ? "partial" : ""))
                    color: theme.colors.muted
                    font.pixelSize: 11
                }
                Label {
                    anchors { left: disclosure.right; leftMargin: 4; right: stateNote.text.length > 0 ? stateNote.left : parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    text: row.name
                    elide: Text.ElideMiddle
                    color: row.nodeState === 3 ? theme.colors.muted : theme.colors.text
                    font.pixelSize: 13
                    font.weight: row.isCurrent ? Font.DemiBold : Font.Normal
                }
                ToolTip.visible: rowHover.hovered && (row.nodeState === 3 || row.nodeState === 4)
                ToolTip.delay: 400
                ToolTip.text: row.nodeState === 3 ? "This folder cannot be listed" : "Some folders are not shown because the list limit was reached"
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: folderTree.count === 0
                text: "Open a photo to browse its folders."
                color: theme.colors.muted
                font.pixelSize: 12
            }
        }
    }
}
