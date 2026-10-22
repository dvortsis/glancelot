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
    property bool favoritesExpanded: true
    readonly property bool keepOpen: hover.hovered || list.activeFocus || (typeof favoritesList !== "undefined" && favoritesList.activeFocus) || focusedControls > 0 || address.activeFocus || resizer.pressed
    readonly property int indent: 12
    // Drag the trailing edge to resize (this session); the host bounds it so the photo keeps most of the window.
    property int userWidth: 0
    property bool layoutFrozen: false
    property bool arrangementMode: false
    signal resizeCommitted(real extent)
    property int maximumWidth: 480
    signal dismissed()
    // INPUT-02: the window, for the command-menu rows (scope predicate, live bindings, command routing).
    property var host: null
    // F002: ancestors of the current folder as [{label, path}], split from text only (drive, UNC share and "/" roots kept).
    readonly property var crumbList: {
        const full = viewer.directory.replace(/\\/g, "/").replace(/(.)\/+$/, "$1")
        if (full.length === 0) return []
        const unc = full.startsWith("//")
        const parts = full.split("/").filter(s => s.length > 0)
        const list = []
        let acc = unc ? "/" : ""
        for (let i = 0; i < parts.length; ++i) {
            const first = i === 0
            acc = unc ? (first ? "//" + parts[0] : acc + "/" + parts[i]) : (first ? (full.startsWith("/") ? "/" + parts[0] : parts[0]) : acc + "/" + parts[i])
            const isDrive = !unc && first && /^[A-Za-z]:$/.test(parts[0])
            list.push({ label: parts[i], path: isDrive ? parts[0] + "/" : acc })
        }
        if (full.startsWith("/") && !unc) list.unshift({ label: "/", path: "/" })
        return list
    }
    // F002 completion: child folders of the typed scope, newest request only (cancelled and dropped in the controller).
    property var suggestions: []
    property string suggestionScope: ""
    function acceptSuggestion(name) {
        const text = address.text.replace(/\\/g, "/")
        address.text = text.slice(0, text.lastIndexOf("/") + 1) + name + "/"
        address.cursorPosition = address.text.length
        address.modified = true
        suggestions = []
        viewer.completePath(address.text)
    }
    Timer { id: completeDelay; interval: 120; onTriggered: viewer.completePath(address.text) }
    Connections {
        target: viewer
        function onPathCompletions(text, names) {
            if (text === address.text && address.activeFocus) panel.suggestions = names
        }
    }
    readonly property string trashName: Qt.platform.os === "windows" ? "Recycle Bin" : "Trash"
    function openRowMenu(row, x, y) {
        const info = folderTree.rowInfo(row)
        if (!info.path) return
        list.currentIndex = row // commands from the menu act on the cursor row
        rowMenu.path = info.path; rowMenu.depth = info.depth
        rowMenu.popup(panel, x, y)
    }
    function openRowMenuAtCursor() {
        if (list.currentIndex < 0) return
        list.positionViewAtIndex(list.currentIndex, ListView.Contain) // the cursor row may be scrolled out of view
        const item = list.itemAtIndex(list.currentIndex)
        const at = item ? item.mapToItem(panel, Theme.gapXl, item.height) : list.mapToItem(panel, Theme.gapXl, Theme.gapSm)
        openRowMenu(list.currentIndex, at.x, at.y)
    }
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
    // The "Open the focused folder" command: the same action as Enter on the cursor row.
    function openCursor() {
        if (list.currentIndex < 0) return
        const info = folderTree.rowInfo(list.currentIndex)
        if (info && info.path) activate(list.currentIndex, info.depth, info.expandable, info.isCurrent, info.path)
    }
    function cursorIsRoot() {
        if (list.currentIndex < 0) return false
        const info = folderTree.rowInfo(list.currentIndex)
        return !!info && info.depth === 0
    }
    function focusAddress() { address.forceActiveFocus(Qt.ShortcutFocusReason); address.selectAll() }
    // "Go to the folder path on the clipboard": paste over the field and navigate, only if something was pasted.
    function pasteAddress() {
        address.forceActiveFocus(Qt.ShortcutFocusReason); address.selectAll()
        const before = address.text
        address.paste()
        if (address.text === before) return
        address.modified = false
        viewer.navigateDirectory(address.text)
    }
    function currentSelectedFolder() {
        // The row menu opens on the cursor row and holds focus while a command from it runs.
        if (!(list.activeFocus || rowMenu.visible) || list.currentIndex < 0 || !folderTree) return ""
        const info = folderTree.rowInfo(list.currentIndex)
        return (info && info.depth > 0 && info.path) ? info.path : ""
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
        enabled: !panel.layoutFrozen
        onReleased: panel.resizeCommitted(panel.userWidth)
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

        // FAVORITES-01: Collapsible pinned favorites section
        ColumnLayout {
            id: favoritesSection
            visible: typeof favoritesModel !== "undefined"
            Layout.fillWidth: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.controlHeight
                Layout.leftMargin: Theme.gapSm
                Layout.rightMargin: Theme.gapSm
                Layout.topMargin: Theme.gapXs
                spacing: Theme.gapXs

                Rectangle {
                    id: favHeaderArea
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusSmall
                    color: favHeaderHover.hovered ? Theme.hover : "transparent"
                    HoverHandler { id: favHeaderHover }
                    TapHandler {
                        onTapped: panel.favoritesExpanded = !panel.favoritesExpanded
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.gapXs
                        anchors.rightMargin: Theme.gapXs
                        spacing: Theme.gapXs
                        UiIcon {
                            name: "chevron-right"
                            size: 14
                            color: Theme.muted
                            rotation: panel.favoritesExpanded ? 90 : 0
                            Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
                        }
                        UiIcon {
                            name: "star"
                            size: 14
                            color: Theme.primary
                        }
                        Label {
                            text: "Favorites"
                            color: Theme.text
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.DemiBold
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Label {
                            text: (typeof favoritesModel !== "undefined" && favoritesModel.count > 0) ? ("(" + favoritesModel.count + ")") : ""
                            color: Theme.muted
                            font.pixelSize: Theme.textCaption
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Item { Layout.fillWidth: true }
                    }
                }

                PanelButton {
                    objectName: "addCurrentFavorite"
                    text: "Add current folder to favorites"
                    iconName: "plus"
                    iconOnly: true
                    tip: "Add current folder to favorites"
                    enabled: typeof favoritesModel !== "undefined" && viewer.directory.length > 0 && !favoritesModel.isFavorite(viewer.directory)
                    onClicked: {
                        if (typeof favoritesModel !== "undefined") {
                            favoritesModel.addFavorite(viewer.directory)
                        }
                    }
                }
            }

            ListView {
                id: favoritesList
                objectName: "favoritesList"
                Layout.fillWidth: true
                Layout.preferredHeight: panel.favoritesExpanded && (typeof favoritesModel !== "undefined" && favoritesModel.count > 0) ? Math.min(favoritesModel.count * Theme.rowHeight, 144) : 0
                visible: panel.favoritesExpanded && (typeof favoritesModel !== "undefined" && favoritesModel.count > 0)
                leftMargin: Theme.gapSm
                rightMargin: Theme.gapSm
                clip: true
                model: panel.visible && (typeof favoritesModel !== "undefined") ? favoritesModel : null
                currentIndex: -1
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: false
                Accessible.role: Accessible.List
                Accessible.name: "Favorite folders"
                ScrollBar.vertical: UiScrollBar {}

                Keys.onShortcutOverride: event => {
                    event.accepted = [Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End, Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space, Qt.Key_Delete, Qt.Key_Backspace, Qt.Key_Menu].indexOf(event.key) >= 0
                                     || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))
                }
                Keys.onUpPressed: event => {
                    if (event.modifiers & Qt.AltModifier) {
                        if (currentIndex > 0) {
                            favoritesModel.moveUp(currentIndex)
                            currentIndex = currentIndex - 1
                        }
                        event.accepted = true
                    } else {
                        currentIndex = Math.max(0, currentIndex - 1)
                    }
                }
                Keys.onDownPressed: event => {
                    if (event.modifiers & Qt.AltModifier) {
                        if (currentIndex >= 0 && currentIndex + 1 < count) {
                            favoritesModel.moveDown(currentIndex)
                            currentIndex = currentIndex + 1
                        }
                        event.accepted = true
                    } else {
                        currentIndex = Math.min(count - 1, currentIndex + 1)
                    }
                }
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Escape) { panel.dismissed(); event.accepted = true; return }
                    if (currentIndex < 0 || currentIndex >= count) return
                    const row = currentIndex
                    const item = favoritesModel.get(row)
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                        viewer.openFolder(item.path)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                        favoritesModel.removeAt(row)
                        if (currentIndex >= count) currentIndex = count - 1
                        event.accepted = true
                    } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                        favMenu.path = item.path
                        favMenu.row = row
                        favMenu.alias = item.alias
                        favMenu.popup(panel, Theme.gapXl, (row + 1) * Theme.rowHeight)
                        event.accepted = true
                    }
                }

                delegate: Item {
                    id: favRow
                    objectName: "favoriteRow"
                    required property int index
                    required property string name
                    required property string path
                    required property string alias
                    required property bool isCurrent
                    readonly property bool cursor: ListView.isCurrentItem && favoritesList.activeFocus
                    width: ListView.view.width - favoritesList.leftMargin - favoritesList.rightMargin
                    height: Theme.rowHeight
                    Accessible.role: Accessible.ListItem
                    Accessible.name: name + (isCurrent ? ", current folder" : "")
                    Accessible.selected: isCurrent

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusControl
                        color: favRow.isCurrent ? Theme.selection : (favHover.hovered ? Theme.hover : "transparent")
                        border.width: favRow.cursor ? Theme.focusWidth : 0
                        border.color: Theme.focusRing
                        Rectangle {
                            visible: favRow.isCurrent
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; topMargin: 6; bottomMargin: 6 }
                            width: 3; radius: 1.5
                            color: Theme.primary
                        }
                    }
                    HoverHandler { id: favHover }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: point => {
                            favoritesList.currentIndex = favRow.index
                            favoritesList.forceActiveFocus(Qt.MouseFocusReason)
                            const at = favRow.mapToItem(panel, point.position.x, point.position.y)
                            favMenu.path = favRow.path
                            favMenu.row = favRow.index
                            favMenu.alias = favRow.alias
                            favMenu.popup(panel, at.x, at.y)
                        }
                    }
                    TapHandler {
                        onTapped: {
                            favoritesList.currentIndex = favRow.index
                            favoritesList.forceActiveFocus(Qt.MouseFocusReason)
                            viewer.openFolder(favRow.path)
                        }
                    }
                    UiIcon {
                        id: favGlyph
                        x: Theme.gapXs
                        anchors.verticalCenter: parent.verticalCenter
                        name: "star"
                        color: favRow.isCurrent ? Theme.primary : Theme.muted
                        size: 14
                    }
                    Label {
                        id: favLabel
                        anchors { left: favGlyph.right; leftMargin: Theme.gapXs; right: parent.right; rightMargin: Theme.gapSm; verticalCenter: parent.verticalCenter }
                        text: favRow.name
                        elide: Text.ElideRight
                        color: Theme.text
                        font.pixelSize: Theme.textBody
                        font.weight: favRow.isCurrent ? Font.DemiBold : Font.Normal
                    }
                    ToolTip.visible: favHover.hovered && (favLabel.truncated || favRow.alias.length > 0)
                    ToolTip.delay: 400
                    ToolTip.text: favRow.alias.length > 0 ? (favRow.alias + " (" + favRow.path + ")") : favRow.path
                }
            }

            Label {
                visible: panel.favoritesExpanded && (typeof favoritesModel !== "undefined" && favoritesModel.count === 0)
                text: "No favorite folders yet. Click + or right-click any folder to add."
                color: Theme.muted
                font.pixelSize: Theme.textCaption
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gapMd
                Layout.rightMargin: Theme.gapMd
                Layout.topMargin: Theme.gapXs
                Layout.bottomMargin: Theme.gapXs
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gapSm
                Layout.rightMargin: Theme.gapSm
                Layout.topMargin: Theme.gapXs
                Layout.bottomMargin: Theme.gapXs
                height: Theme.borderWidth
                color: Theme.border
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

        // F002 breadcrumbs: each ancestor of the current folder jumps there. Pure text; the path is split, never probed.
        Flickable {
            id: crumbs
            objectName: "pathBreadcrumbs"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapSm; Layout.rightMargin: Theme.gapSm
            Layout.preferredHeight: visible ? Theme.controlHeight : 0
            visible: panel.crumbList.length > 0
            clip: true
            contentWidth: crumbRow.childrenRect.width
            contentHeight: height
            interactive: contentWidth > width
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            onContentWidthChanged: contentX = Math.max(0, contentWidth - width) // the current folder stays in view
            Accessible.role: Accessible.Grouping
            Accessible.name: "Folder breadcrumbs"
            Row {
                id: crumbRow
                height: parent.height
                spacing: 0
                Repeater {
                    model: panel.crumbList
                    delegate: Row {
                        required property var modelData
                        required property int index
                        height: crumbRow.height
                        UiIcon { visible: index > 0; anchors.verticalCenter: parent.verticalCenter; name: "chevron-right"; color: Theme.muted; size: 12 }
                        UiButton {
                            objectName: "crumb-" + index
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            variant: index === panel.crumbList.length - 1 ? "outline" : "quiet"
                            tip: modelData.path
                            Accessible.name: "Go to " + modelData.label + (index === panel.crumbList.length - 1 ? ", current folder" : "")
                            onClicked: viewer.navigateDirectory(modelData.path)
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapSm; Layout.rightMargin: Theme.gapSm
            Layout.bottomMargin: Theme.gapXs
            spacing: Theme.gapXs

            PanelButton {
                id: historyBackButton
                objectName: "historyBack"
                iconName: "chevron-left"
                iconOnly: true
                text: "Back"
                tip: typeof folderHistory !== "undefined" && folderHistory.backDirectory.length > 0
                     ? ("Back to " + folderHistory.backDirectory + " (Alt+Left)")
                     : "Back (Alt+Left)"
                enabled: typeof folderHistory !== "undefined" && folderHistory.canGoBack
                visible: typeof folderHistory !== "undefined"
                onClicked: if (typeof folderHistory !== "undefined") folderHistory.navigateBack()
            }

            PanelButton {
                id: historyForwardButton
                objectName: "historyForward"
                iconName: "chevron-right"
                iconOnly: true
                text: "Forward"
                tip: typeof folderHistory !== "undefined" && folderHistory.forwardDirectory.length > 0
                     ? ("Forward to " + folderHistory.forwardDirectory + " (Alt+Right)")
                     : "Forward (Alt+Right)"
                enabled: typeof folderHistory !== "undefined" && folderHistory.canGoForward
                visible: typeof folderHistory !== "undefined"
                onClicked: if (typeof folderHistory !== "undefined") folderHistory.navigateForward()
            }

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
                onTextEdited: { modified = true; completeDelay.restart() }
                onActiveFocusChanged: if (!activeFocus) { completeDelay.stop(); panel.suggestions = [] }
                Keys.onTabPressed: event => { if (panel.suggestions.length > 0) { panel.acceptSuggestion(panel.suggestions[0]); event.accepted = true } }
                onAccepted: { modified = false; viewer.navigateDirectory(text) }
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) { pathMenu.popup(address, Theme.gapMd, address.height); event.accepted = true }
                }
                // Right click: the field has no built-in menu, so the path commands are offered here.
                TapHandler { acceptedButtons: Qt.RightButton; onTapped: point => { address.forceActiveFocus(Qt.MouseFocusReason); pathMenu.popup(address, point.position.x, point.position.y) } }
                Menu {
                    id: pathMenu
                    objectName: "pathContextMenu"
                    margins: Theme.gapSm
                    padding: Theme.gapXs
                    onOpened: panel.focusedControls += 1
                    onClosed: { panel.focusedControls -= 1; if (panel.visible) address.forceActiveFocus(Qt.PopupFocusReason) }
                    background: Rectangle { implicitWidth: 304; radius: Theme.radiusControl; color: Theme.surface; border.width: Theme.borderWidth; border.color: Theme.border }
                    CommandMenuItem { host: panel.host; commandId: "focus-path" }
                    CommandMenuItem { host: panel.host; commandId: "copy-folder-path" }
                    CommandMenuItem { host: panel.host; commandId: "paste-path" }
                }
                Keys.onEscapePressed: event => {
                    viewer.cancelDirectoryNavigation(); text = viewer.directory; modified = false; if(panel.arrangementMode)panel.dismissed();else panel.focusList(); event.accepted = true
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
        Column { // completion suggestions: folders in the typed scope; click or Tab accepts
            objectName: "pathSuggestions"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapSm; Layout.rightMargin: Theme.gapSm
            Layout.bottomMargin: visible ? Theme.gapXs : 0
            visible: panel.suggestions.length > 0 && address.activeFocus
            Accessible.role: Accessible.List
            Accessible.name: "Folder suggestions"
            Repeater {
                model: Math.min(6, panel.suggestions.length)
                delegate: UiButton {
                    required property int index
                    objectName: "suggestion-" + index
                    width: parent.width
                    alignment: Qt.AlignLeft
                    iconName: "folder"
                    text: panel.suggestions[index]
                    focusPolicy: Qt.NoFocus
                    onClicked: { panel.acceptSuggestion(panel.suggestions[index]); address.forceActiveFocus() }
                }
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
                                  Qt.Key_PageUp, Qt.Key_PageDown, Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space, Qt.Key_Escape, Qt.Key_Menu].indexOf(event.key) >= 0 && !(event.key === Qt.Key_Escape && panel.host && panel.host.chordPending) && !(panel.host && panel.host.remapped(event))
                                 || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))
            }
            Keys.onUpPressed: currentIndex = Math.max(0, currentIndex - 1)
            Keys.onDownPressed: currentIndex = Math.min(count - 1, currentIndex + 1)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Escape) { panel.dismissed(); event.accepted = true; return }
                if (currentIndex < 0 || currentIndex >= count) return
                const row = currentIndex
                const cell = folderTree.rowInfo(row)
                const page = Math.max(1, Math.floor(height / Theme.rowHeight) - 1)
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) { panel.openRowMenuAtCursor(); event.accepted = true }
                else if (event.key === Qt.Key_Home) { currentIndex = 0; event.accepted = true }
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
                TapHandler { // context menu
                    acceptedButtons: Qt.RightButton
                    onTapped: point => {
                        list.currentIndex = row.index
                        list.forceActiveFocus(Qt.MouseFocusReason)
                        const at = row.mapToItem(panel, point.position.x, point.position.y)
                        panel.openRowMenu(row.index, at.x, at.y)
                    }
                }
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
            Menu {
                id: rowMenu
                objectName: "folderRowMenu"
                property string path: ""
                readonly property string name: { const p = path.replace(/\\/g, "/").replace(/\/+$/, ""); const leaf = p.slice(p.lastIndexOf("/") + 1); return leaf.length > 30 ? leaf.slice(0, 18) + "…" + leaf.slice(-10) : leaf }
                property int depth: 0
                property bool handedOff: false // the confirmation dialog owns focus after a choice
                padding: Theme.gapXs
                onOpened: { handedOff = false; panel.focusedControls += 1 }
                onClosed: { panel.focusedControls -= 1; if (!handedOff && panel.visible) list.forceActiveFocus(Qt.PopupFocusReason); handedOff = false }
                background: Rectangle {
                    implicitWidth: 232
                    radius: Theme.radiusControl
                    color: Theme.surface
                    border.width: Theme.borderWidth
                    border.color: Theme.border
                }
                MenuItem {
                    id: favoriteItem
                    objectName: "folderFavoriteToggle"
                    property bool isFav: typeof favoritesModel !== "undefined" && favoritesModel.isFavorite(rowMenu.path)
                    text: isFav ? "Remove from Favorites" : "Add to Favorites"
                    enabled: typeof favoritesModel !== "undefined" && rowMenu.path.length > 0
                    implicitHeight: Theme.controlHeight
                    leftPadding: Theme.gapSm; rightPadding: Theme.gapSm
                    onTriggered: {
                        if (typeof favoritesModel !== "undefined") {
                            if (favoriteItem.isFav) favoritesModel.removeFavorite(rowMenu.path)
                            else favoritesModel.addFavorite(rowMenu.path)
                        }
                    }
                    contentItem: RowLayout {
                        spacing: Theme.gapSm
                        UiIcon { name: "star"; color: favoriteItem.isFav ? Theme.primary : Theme.text }
                        Label {
                            text: favoriteItem.text
                            color: Theme.text
                            font.pixelSize: Theme.textBody
                            Layout.fillWidth: true
                        }
                    }
                    background: Rectangle { radius: Theme.radiusSmall; color: favoriteItem.highlighted ? Theme.hover : "transparent" }
                }
                CommandMenuItem { host: panel.host; commandId: "tree-open-folder"; available: rowMenu.path.length > 0 }
                // The registered "delete-selection" command, like the Delete key: the host picks the target from the cursor
                // row (this menu opens on it), so the menu and the key cannot disagree. Drives and other roots are never
                // moved and a running operation blocks a second one; the row stays visible and explains itself.
                CommandMenuItem {
                    id: trashItem
                    host: panel.host
                    commandId: "delete-selection"
                    iconName: "trash"; danger: true
                    label: panel.host && panel.host.commandBlockedReason(commandId).length
                           ? panel.host.commandBlockedReason(commandId)
                           : "Move “" + rowMenu.name + "” to the " + panel.trashName
                }
                Connections { // the confirmation dialog owns focus after a choice
                    target: trashItem
                    function onTriggered() { rowMenu.handedOff = true }
                }
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

    Menu {
        id: favMenu
        objectName: "favoriteItemMenu"
        property string path: ""
        property int row: -1
        property string alias: ""
        padding: Theme.gapXs
        onOpened: panel.focusedControls += 1
        onClosed: { panel.focusedControls -= 1; if (panel.visible && favoritesList.visible) favoritesList.forceActiveFocus(Qt.PopupFocusReason) }
        background: Rectangle {
            implicitWidth: 200
            radius: Theme.radiusControl
            color: Theme.surface
            border.width: Theme.borderWidth
            border.color: Theme.border
        }
        MenuItem {
            id: miOpen
            text: "Open folder"
            implicitHeight: Theme.controlHeight
            leftPadding: Theme.gapSm; rightPadding: Theme.gapSm
            onTriggered: viewer.openFolder(favMenu.path)
            background: Rectangle { radius: Theme.radiusSmall; color: miOpen.highlighted ? Theme.hover : "transparent" }
        }
        MenuItem {
            id: miUp
            text: "Move up (Alt+Up)"
            enabled: favMenu.row > 0
            implicitHeight: Theme.controlHeight
            leftPadding: Theme.gapSm; rightPadding: Theme.gapSm
            onTriggered: {
                if (typeof favoritesModel !== "undefined") {
                    favoritesModel.moveUp(favMenu.row)
                    favoritesList.currentIndex = favMenu.row - 1
                }
            }
            background: Rectangle { radius: Theme.radiusSmall; color: miUp.highlighted ? Theme.hover : "transparent" }
        }
        MenuItem {
            id: miDown
            text: "Move down (Alt+Down)"
            enabled: favMenu.row >= 0 && favMenu.row + 1 < (typeof favoritesModel !== "undefined" ? favoritesModel.count : 0)
            implicitHeight: Theme.controlHeight
            leftPadding: Theme.gapSm; rightPadding: Theme.gapSm
            onTriggered: {
                if (typeof favoritesModel !== "undefined") {
                    favoritesModel.moveDown(favMenu.row)
                    favoritesList.currentIndex = favMenu.row + 1
                }
            }
            background: Rectangle { radius: Theme.radiusSmall; color: miDown.highlighted ? Theme.hover : "transparent" }
        }
        MenuItem {
            id: miAlias
            text: favMenu.alias.length > 0 ? "Edit alias…" : "Set alias…"
            implicitHeight: Theme.controlHeight
            leftPadding: Theme.gapSm; rightPadding: Theme.gapSm
            onTriggered: {
                aliasDialog.targetPath = favMenu.path
                aliasField.text = favMenu.alias
                aliasDialog.open()
            }
            background: Rectangle { radius: Theme.radiusSmall; color: miAlias.highlighted ? Theme.hover : "transparent" }
        }
        MenuItem {
            id: miRemoveAlias
            visible: favMenu.alias.length > 0
            text: "Remove alias"
            implicitHeight: Theme.controlHeight
            leftPadding: Theme.gapSm; rightPadding: Theme.gapSm
            onTriggered: {
                if (typeof favoritesModel !== "undefined") {
                    favoritesModel.setAlias(favMenu.path, "")
                }
            }
            background: Rectangle { radius: Theme.radiusSmall; color: miRemoveAlias.highlighted ? Theme.hover : "transparent" }
        }
        MenuItem {
            id: miCopy
            text: "Copy path"
            implicitHeight: Theme.controlHeight
            leftPadding: Theme.gapSm; rightPadding: Theme.gapSm
            onTriggered: viewer.copyFilePath([favMenu.path])
            background: Rectangle { radius: Theme.radiusSmall; color: miCopy.highlighted ? Theme.hover : "transparent" }
        }
        MenuItem {
            id: miRemove
            text: "Remove from Favorites"
            implicitHeight: Theme.controlHeight
            leftPadding: Theme.gapSm; rightPadding: Theme.gapSm
            onTriggered: {
                if (typeof favoritesModel !== "undefined") {
                    favoritesModel.removeAt(favMenu.row)
                }
            }
            background: Rectangle { radius: Theme.radiusSmall; color: miRemove.highlighted ? Theme.hover : "transparent" }
        }
    }

    UiDialog {
        id: aliasDialog
        objectName: "favoriteAliasDialog"
        title: "Favorite Alias"
        property string targetPath: ""
        x: Math.round((panel.width - width) / 2)
        y: Math.round((panel.height - height) / 2)
        width: Math.min(360, panel.width - 2 * Theme.gapLg)
        onOpened: {
            panel.focusedControls += 1
            aliasField.forceActiveFocus(Qt.TabFocusReason)
            aliasField.selectAll()
        }
        onClosed: {
            panel.focusedControls -= 1
            if (panel.visible && favoritesList.visible) favoritesList.forceActiveFocus(Qt.PopupFocusReason)
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.gapMd
            Label {
                text: "Set a custom display name for this folder:"
                color: Theme.muted
                font.pixelSize: Theme.textSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            UiTextField {
                id: aliasField
                objectName: "favoriteAliasField"
                placeholderText: "e.g. Vacation 2026"
                Layout.fillWidth: true
                onAccepted: {
                    if (typeof favoritesModel !== "undefined") {
                        favoritesModel.setAlias(aliasDialog.targetPath, text.trim())
                    }
                    aliasDialog.close()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.gapSm
                spacing: Theme.gapSm
                Item { Layout.fillWidth: true }
                UiButton {
                    text: "Cancel"
                    onClicked: aliasDialog.close()
                }
                UiButton {
                    text: "Save"
                    variant: "primary"
                    onClicked: {
                        if (typeof favoritesModel !== "undefined") {
                            favoritesModel.setAlias(aliasDialog.targetPath, aliasField.text.trim())
                        }
                        aliasDialog.close()
                    }
                }
            }
        }
    }

    Connections {
        target: viewer
        function onViewCommand(command) {
            if (command === "add-to-favorites") {
                if (typeof favoritesModel !== "undefined" && viewer.directory.length > 0) {
                    favoritesModel.addFavorite(viewer.directory)
                }
            } else if (command === "navigate-back") {
                if (typeof folderHistory !== "undefined") folderHistory.navigateBack()
            } else if (command === "navigate-forward") {
                if (typeof folderHistory !== "undefined") folderHistory.navigateForward()
            }
        }
    }
}
