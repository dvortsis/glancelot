import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQml.Models
import Glancelot

ApplicationWindow {
    id: app
    objectName: "viewerWindow"
    width: 1180
    height: 780
    minimumWidth: 540
    minimumHeight: 360
    visible: true
    title: viewer.fileName ? viewer.fileName + " — Glancelot" : "Glancelot"
    color: Theme.background
    property bool frameless: false
    property bool hudVisible: true
    property bool gridMode: false
    property bool layoutEditing: false
    property var resizePreview: ({})
    readonly property var workspace: {
        const state=JSON.parse(JSON.stringify(paneLayout.panes))
        const active={tree:(viewer.treeVisible||state.tree.frozen)&&state.tree.visible&&(viewer.treePinned||state.tree.frozen||hudVisible||frameless),
            topbar:state.topbar.visible&&(gridMode||hudVisible||frameless||state.topbar.frozen),
            statusbar:state.statusbar.visible&&(gridMode||hudVisible||state.statusbar.frozen),
            filmstrip:state.filmstrip.visible&&!gridMode&&(viewer.filmstripVisible||state.filmstrip.frozen)&&filmstrip.count>0&&(hudVisible||frameless||state.filmstrip.frozen),
            preview:!gridMode,grid:gridMode}
        const ids=["topbar","statusbar","filmstrip","tree","preview","grid"].filter(id=>active[id])
        function horizontal(id) { return ["top","bottom"].indexOf(state[id].region)>=0 }
        function overlay(id) { return id==="tree"&&(width<720||!viewer.treePinned)&&["left","right"].indexOf(state[id].region)>=0 }
        function minimum(id) {
            if(id==="topbar"||id==="statusbar")return horizontal(id)?48:200
            if(id==="tree")return 200
            if(id==="filmstrip")return horizontal(id)?68:100
            return 120
        }
        function group(horizontalAxis) { return ids.filter(id=>state[id].region!=="center"&&!overlay(id)&&horizontal(id)===horizontalAxis) }
        const reservedCenter=ids.some(id=>overlay(id))?320:120
        let recovered=group(true).reduce((n,id)=>n+minimum(id),0)>height-120 || group(false).reduce((n,id)=>n+minimum(id),0)>width-reservedCenter
        if(recovered) {
            const defaults={topbar:["top",0,48],statusbar:["bottom",0,48],filmstrip:["top",1,92],tree:["left",0,280],preview:["center",0,640],grid:["center",1,640]}
            for(const id of Object.keys(defaults)) {
                state[id].region=defaults[id][0];state[id].order=defaults[id][1];state[id].extent=defaults[id][2]
            }
        }
        function preferred(id) { return Math.max(minimum(id),!recovered&&resizePreview[id]!==undefined?resizePreview[id]:state[id].extent) }
        const extents={}
        for(const axis of [true,false]) {
            const members=group(axis),budget=axis?height-120:width-(ids.some(id=>overlay(id))?320:120)
            const base=members.reduce((n,id)=>n+minimum(id),0)
            const extra=members.reduce((n,id)=>n+preferred(id)-minimum(id),0)
            const scale=Math.min(1,Math.max(0,budget-base)/Math.max(1,extra))
            for(const id of members)extents[id]=minimum(id)+(preferred(id)-minimum(id))*scale
        }
        let bounds={x:0,y:0,width:width,height:height},result={}
        ids.sort((a,b)=>state[a].order-state[b].order)
        for(const region of ["top","bottom","left","right"]) {
            for(const id of ids.filter(id=>state[id].region===region&&!overlay(id))) {
                const h=horizontal(id),extent=extents[id]
                const rect={x:bounds.x,y:bounds.y,width:h?bounds.width:extent,height:h?extent:bounds.height}
                if(region==="bottom")rect.y+=bounds.height-extent
                if(region==="right")rect.x+=bounds.width-extent
                result[id]=rect
                if(h){if(region==="top")bounds.y+=extent;bounds.height-=extent}
                else{if(region==="left")bounds.x+=extent;bounds.width-=extent}
            }
        }
        for(const id of ids.filter(id=>state[id].region==="center"))result[id]=Object.assign({},bounds)
        let content=Object.assign({},result[gridMode?"grid":"preview"]||bounds)
        for(const id of ids.filter(id=>overlay(id))) {
            const extent=Math.min(preferred(id),Math.max(0,bounds.width-120))
            result[id]={x:state[id].region==="right"?bounds.x+bounds.width-extent:bounds.x,y:bounds.y,width:extent,height:bounds.height}
            const cover=result[id]
            const intersection=Math.max(0,Math.min(content.x+content.width,cover.x+cover.width)-Math.max(content.x,cover.x))
            if(cover.y<content.y+content.height && cover.y+cover.height>content.y && intersection>0) {
                if(cover.x<=content.x)content.x+=intersection
                content.width-=intersection
            }
        }
        for(const id of Object.keys(active))if(!active[id])result[id]={x:0,y:0,width:0,height:0}
        result._state=state;result._recovered=recovered;result._content=content
        return result
    }
    function paneRect(id) { return workspace[id]||{x:0,y:0,width:0,height:0} }
    function paneState(id) { return workspace._state[id] }

    readonly property bool editingText: activeFocusItem !== null && typeof activeFocusItem.selectAll === "function" && typeof activeFocusItem.text === "string"
    property bool modalOpen: settingsDialog.opened || settingsDialog.visible || openDialog.visible
    property real pointerX: width / 2
    property real pointerY: height / 2
    property real wheelRemainder: 0
    flags: Qt.Window | (frameless ? Qt.FramelessWindowHint : 0)
    font.family: Theme.fontFamily
    font.pixelSize: Theme.textBody
    // Basic-style controls that are not restyled individually (tool tips, busy states) read these roles.
    palette.window: Theme.surface
    palette.windowText: Theme.text
    palette.base: Theme.background
    palette.alternateBase: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surface
    palette.buttonText: Theme.text
    palette.brightText: Theme.textOnPrimary
    palette.highlight: Theme.primary
    palette.highlightedText: Theme.textOnPrimary
    palette.light: Theme.hover
    palette.midlight: Theme.hover
    palette.mid: Theme.border
    palette.dark: Theme.muted
    palette.shadow: Theme.scrim
    palette.placeholderText: Theme.muted
    palette.toolTipBase: Theme.text
    palette.toolTipText: Theme.background
    palette.link: Theme.primary

    onLayoutEditingChanged: { if(layoutEditing)wakeHud();else resizePreview=({}) }
    Connections { target: paneLayout; function onChanged() { if(paneLayout.locked)app.resizePreview=({}) } }
    function wakeHud() { hudVisible = true; hideHud.restart() }
    function focusWithin(item) {
        for (let focused = activeFocusItem; focused; focused = focused.parent)
            if (focused === item) return true
        return false
    }
    function invoke(id) { if (gridMode && !gridAllowsCommand(id)) return; viewer.trigger(id); wakeHud() }
    function focusContent() { if (gridMode) photoGrid.focusGrid(); else canvas.forceActiveFocus() }
    function showCanvas() { gridMode = false; canvas.forceActiveFocus(); wakeHud() }
    function gridAllowsCommand(id) {
        return ["open", "settings", "palette", "quit", "grid", "tree", "fullscreen", "frameless", "sort", "sort-reverse", "arrange-panes", "layout-lock", "layout-reset"].indexOf(id) >= 0
    }
    function fuzzyMatch(text, query) {
        text = text.toLowerCase(); query = query.toLowerCase()
        let offset = 0
        for (let i = 0; i < query.length; ++i) {
            offset = text.indexOf(query[i], offset)
            if (offset < 0) return false
            ++offset
        }
        return true
    }
    function shortcutHint(id) { const binding = viewer.bindingFor(id); return binding.length ? " (" + binding + ")" : "" }
    // Kept for --show-themes: appearance now lives in Settings.
    function openThemes() { settingsDialog.openSection("appearance", false) }
    function handleViewCommand(id) {
        canvas.endLoupe()
        if (id === "grid") { gridMode = !gridMode; wakeHud(); if (!gridMode) canvas.forceActiveFocus() }
        else if (id === "open") openDialog.open()
        else if (id === "fullscreen") visibility = visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
        else if (id === "palette") settingsDialog.openSection("controls", true)
        else if (id === "settings") settingsDialog.openSection(settingsDialog.section, false)
        else if (id === "fit") canvas.fit("shrink")
        else if (id === "actual") canvas.actualSize()
        else if (id === "zoom-in") canvas.zoomAt(1.25, pointerX, pointerY)
        else if (id === "zoom-out") canvas.zoomAt(0.8, pointerX, pointerY)
        else if (id === "rotate") canvas.rotateClockwise()
        else if (id === "background") canvas.cycleBackground()
        else if (id === "frameless") { frameless = !frameless; wakeHud() }
        else if (id === "arrange-panes") { if (!paneLayout.locked) layoutEditing = true }
        else if (id === "layout-lock") { paneLayout.locked = !paneLayout.locked; wakeHud() }
        else if (id === "layout-reset") paneLayout.reset()
        else if (id === "quit") close()
    }
    onActiveChanged: if (!active) canvas.endLoupe()
    onModalOpenChanged: if (modalOpen) canvas.endLoupe()
    Connections {
        target: viewer
        function onViewCommand(id) { app.handleViewCommand(id) }
        function onTreeShown() { app.hudVisible = true; hideHud.restart(); Qt.callLater(treePanel.focusList) }
    }
    Instantiator {
        model: viewer.commands
        delegate: Shortcut {
            required property var modelData
            sequence: modelData.binding
            context: Qt.WindowShortcut
            enabled: !app.modalOpen && !app.editingText && modelData.binding.length > 0 && (!app.gridMode || app.gridAllowsCommand(modelData.id))
            onActivated: app.invoke(modelData.id)
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: !app.modalOpen && (app.layoutEditing || !app.editingText)
        onActivated: {
            canvas.endLoupe()
            if(app.layoutEditing) { app.layoutEditing=false; return }
            if (app.gridMode) app.showCanvas()
            else if (app.visibility === Window.FullScreen) app.showNormal()
            else app.wakeHud()
        }
    }
    Timer {
        id: hideHud
        interval: 2400
        running: true
        onTriggered: {
            if (!app.layoutEditing && !app.gridMode && viewer.fileCount > 0 && !app.modalOpen && !topBar.hovered && !bottomHover.hovered && !app.focusWithin(topPanel) && !app.focusWithin(bottomPanel) && !filmstripPanel.keepOpen && !treePanel.keepOpen)
                app.hudVisible = false
            else restart()
        }
    }
    component BarLabel: Label {
        color: Theme.muted
        font.pixelSize: Theme.textSmall
        font.features: { "tnum": 1 }
        verticalAlignment: Text.AlignVCenter
    }
    component BarDivider: Rectangle {
        implicitWidth: Theme.borderWidth
        implicitHeight: Theme.iconSize + Theme.gapXs
        color: Theme.border
        Layout.leftMargin: Theme.gapXs
        Layout.rightMargin: Theme.gapXs
    }

    // A pinned folder panel makes room instead of covering the photo, except in narrow windows.
    readonly property int treeInset: treePanel.visible && viewer.treePinned && app.width >= 720 ? treePanel.width : 0
    // Width of the canvas hidden by the folder panel, pinned or overlaid; centred overlays avoid it.
    readonly property int canvasCover: treePanel.visible ? treePanel.width : 0
    ImageCanvas {
        id: canvas
        objectName: "canvas"
        x: app.paneRect("preview").x; y: app.paneRect("preview").y
        width: app.paneRect("preview").width; height: app.paneRect("preview").height
        image: viewer.image
        background: "#191919" // photo backdrop, deliberately independent of the interface palette
        focus: !app.gridMode
        visible: !app.gridMode
        // Keyboard route into the auto-hiding filmstrip: Tab wakes the HUD and focuses the current photo's cell.
        // Shift+Tab: keyboard route into the folder tree (shows it when hidden).
        Keys.onBacktabPressed: event => {
            if (!viewer.treeVisible) viewer.treeVisible = true
            else { app.hudVisible = true; hideHud.restart(); Qt.callLater(treePanel.focusList) }
            event.accepted = true
        }
        Keys.onTabPressed: event => {
            if (viewer.filmstripVisible && filmstrip.count > 0) {
                app.hudVisible = true; hideHud.restart()
                Qt.callLater(filmstripPanel.focusCurrent)
                event.accepted = true
            }
        }
        Accessible.name: "Photo canvas"
        Accessible.description: viewer.fileName.length ? viewer.fileName + ", " + viewer.imageWidth + " by " + viewer.imageHeight + " pixels" : "Open or drop a photo to begin"
    }
    MouseArea {
        id: interaction
        visible: !app.gridMode
        anchors.fill: canvas
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        hoverEnabled: true
        property real lastX: 0
        property real lastY: 0
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
        onPositionChanged: mouse => {
            app.pointerX = mouse.x; app.pointerY = mouse.y
            if (pressed && !(pressedButtons & Qt.RightButton)) canvas.panBy(mouse.x-lastX, mouse.y-lastY)
            lastX=mouse.x; lastY=mouse.y
            app.wakeHud()
        }
        onPressed: mouse => {
            canvas.forceActiveFocus(); lastX=mouse.x; lastY=mouse.y
            if (mouse.button === Qt.RightButton) canvas.beginLoupe(mouse.x,mouse.y)
        }
        onReleased: canvas.endLoupe()
        onCanceled: canvas.endLoupe()
        onDoubleClicked: mouse => { if (mouse.button === Qt.LeftButton) app.invoke("fullscreen") }
        onWheel: wheel => {
            app.wakeHud()
            let delta=wheel.angleDelta.y || wheel.angleDelta.x
            let pixel=wheel.pixelDelta.y || wheel.pixelDelta.x
            if ((wheel.modifiers & Qt.ControlModifier) || (!(wheel.modifiers & Qt.ShiftModifier) && viewer.wheelMode === "zoom")) {
                canvas.zoomAt(Math.pow(1.0015, pixel !== 0 ? pixel*3 : delta), wheel.x, wheel.y)
            } else if ((wheel.modifiers & Qt.ShiftModifier) || viewer.wheelMode === "pan") {
                let amount=pixel !== 0 ? pixel : delta/3
                canvas.panBy((wheel.modifiers & Qt.ShiftModifier) ? amount : wheel.pixelDelta.x, (wheel.modifiers & Qt.ShiftModifier) ? 0 : amount)
            } else {
                app.wheelRemainder += pixel !== 0 ? pixel*2 : delta
                while (app.wheelRemainder >= 120) { viewer.previous(); app.wheelRemainder-=120 }
                while (app.wheelRemainder <= -120) { viewer.next(); app.wheelRemainder+=120 }
            }
            wheel.accepted=true
        }
    }
    PhotoGrid {
        id: photoGrid
        objectName: "photoGrid"
        x: app.paneRect("grid").x; y: app.paneRect("grid").y
        width: app.paneRect("grid").width; height: app.paneRect("grid").height
        visible: app.gridMode
        inputEnabled: !app.modalOpen
        onItemActivated: app.showCanvas()
        onDismissed: { if(app.layoutEditing)app.layoutEditing=false;else app.showCanvas() }
        onFoldersRequested: { if (!viewer.treeVisible) viewer.treeVisible = true; else treePanel.focusList() }
    }
    Label {
        objectName: "layoutRecoveryNotice"
        visible: app.workspace._recovered
        x: app.workspace._content.x+Theme.gapSm
        y: app.workspace._content.y+Theme.gapSm
        width: Math.max(0,app.workspace._content.width-2*Theme.gapSm)
        text: app.workspace._content.width < 280 ? "Layout adapted. Reset in Settings." : "Layout adapted to fit this window. Saved preferences are kept. Change or reset it in Settings → Browsing."
        wrapMode: Text.Wrap
        color: Theme.text
        padding: Theme.gapSm
        background: Rectangle { color: Theme.surface;radius: Theme.radiusControl }
        z: 15
        Accessible.name: text
    }
    PaneArrangement { anchors.fill: parent; host: app; visible: app.layoutEditing; z: 20 }
    DropArea {
        anchors.fill: parent
        onEntered: drag => { drag.accepted = drag.hasUrls }
        onDropped: drop => { if (drop.hasUrls && drop.urls.length) { viewer.open(drop.urls[0].toString()); app.showCanvas(); drop.acceptProposedAction() } }
        Rectangle { anchors.fill: parent; visible: parent.containsDrag; color: Qt.alpha(Theme.primary, 0.15); border.width: Theme.focusWidth; border.color: Theme.primary }
    }
    Rectangle { // with no photo there is nothing to protect, so the empty state sits on the interface background
        anchors.fill: canvas
        visible: emptyState.visible
        color: Theme.background
    }
    ColumnLayout { // empty state: one message, one action
        id: emptyState
        readonly property bool compactContent: app.workspace._content.width < 280 || app.workspace._content.height < 320
        x: app.workspace._content.x+(app.workspace._content.width-width)/2
        y: app.workspace._content.y+(app.workspace._content.height-height)/2
        spacing: Theme.gapSm
        visible: !app.gridMode && viewer.fileCount === 0 && !viewer.loading
        width: Math.max(0,Math.min(440,app.workspace._content.width-2*Theme.gapMd))
        Image {
            visible: !emptyState.compactContent
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: app.height < 480 ? 96 : 144
            Layout.preferredHeight: Layout.preferredWidth
            source: "qrc:/brand/mascots/transparent/01_mascot_standing.png"
            fillMode: Image.PreserveAspectFit
            sourceSize.width: 288; sourceSize.height: 288
            Accessible.ignored: true
        }
        Label {
            visible: !emptyState.compactContent
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.gapSm
            text: "Glancelot"
            color: Theme.text
            font.pixelSize: Theme.textDisplay
            font.weight: Font.Light
            font.letterSpacing: -0.02 * Theme.textDisplay
            lineHeight: 1.1
            Accessible.role: Accessible.Heading
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            visible: !emptyState.compactContent
            text: "Your photographs, front and center."
            color: Theme.muted
            font.pixelSize: Theme.textTitle
            wrapMode: Text.Wrap
        }
        UiButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: emptyState.compactContent ? 0 : Theme.gapLg
            Layout.maximumWidth: emptyState.width
            Layout.minimumWidth: 0
            variant: "primary"
            text: emptyState.compactContent ? "Open" : "Open a photo"
            Accessible.name: "Open a photo"
            tip: "Open a photo" + app.shortcutHint("open")
            onClicked: app.invoke("open")
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: Theme.gapSm
            horizontalAlignment: Text.AlignHCenter
            visible: !emptyState.compactContent
            text: "or drop a JPEG or PNG here. Arrow keys browse the folder."
            color: Theme.muted
            font.pixelSize: Theme.textSmall
            wrapMode: Text.Wrap
            lineHeight: 1.4
        }
    }
    Rectangle {
        id: topPanel
        x: app.paneRect("topbar").x; y: app.paneRect("topbar").y
        width: app.paneRect("topbar").width; height: app.paneRect("topbar").height
        color: Theme.surface
        visible: width > 0 && height > 0
        HoverHandler { id: topBar }
        MouseArea { anchors.fill: parent; enabled: app.frameless; onPressed: app.startSystemMove(); onDoubleClicked: app.visibility === Window.Maximized ? app.showNormal() : app.showMaximized() }
        Rectangle { // edge against the canvas when no filmstrip sits below
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: Theme.borderWidth
            color: Theme.border
            visible: !filmstripPanel.visible
        }
        ScrollView {
            id: topbarScroll
            anchors.fill: parent
            clip: true
            contentWidth: availableWidth
            contentHeight: barGridtopbar.implicitHeight
            ScrollBar.vertical: UiScrollBar {}
        GridLayout {
            id: barGridtopbar
            columns: app.paneState("topbar").region === "left" || app.paneState("topbar").region === "right" ? 1 : children.length
            rowSpacing: Theme.gapXs; columnSpacing: Theme.gapXs
            width: topbarScroll.availableWidth-2*Theme.gapMd
            x: Theme.gapMd
            y: Math.max(0,(topbarScroll.availableHeight-implicitHeight)/2)
            Image {
                Layout.preferredWidth: 24; Layout.preferredHeight: 24
                Layout.rightMargin: Theme.gapXs
                source: "qrc:/brand/icons/app/" + (Theme.dark ? "dark" : "light") + "/glancelot-64.png"
                sourceSize.width: 48; sourceSize.height: 48
                fillMode: Image.PreserveAspectFit
                Accessible.ignored: true
            }
            UiButton { text: "Open"; tip: "Open a photo" + app.shortcutHint("open"); onClicked: app.invoke("open") }
            UiButton {
                objectName: "foldersToggle"
                text: "Folders"
                checkable: true
                checked: viewer.treeVisible
                tip: (viewer.treeVisible ? "Hide the folder tree" : "Show the folder tree") + app.shortcutHint("tree")
                Accessible.description: viewer.treeVisible ? "Hide the folder tree" : "Show the folder tree"
                onClicked: { checked = Qt.binding(() => viewer.treeVisible); app.invoke("tree") }
            }
            UiButton {
                objectName: "gridToggle"
                text: "Grid"
                checkable: true
                checked: app.gridMode
                enabled: viewer.directory.length > 0
                tip: (app.gridMode ? "Return to the photo" : "Browse the folder as a grid") + app.shortcutHint("grid")
                Accessible.description: app.gridMode ? "Return to photo canvas" : "Browse folder as a grid"
                onClicked: { checked = Qt.binding(() => app.gridMode); app.invoke("grid") }
            }
            UiButton {
                objectName: "sortButton"
                visible: app.width >= 760
                iconName: viewer.sortDescending ? "arrow-down" : "arrow-up"
                text: ({ name: "Name", modified: "Date", size: "Size", shuffle: "Shuffle" }[viewer.sortKey])
                enabled: viewer.fileCount > 0
                onClicked: app.invoke("sort")
                tip: "Cycle file order" + app.shortcutHint("sort") + (viewer.bindingFor("sort-reverse").length ? "; reverse with " + viewer.bindingFor("sort-reverse") : "")
                Accessible.name: "File order: " + ({ name: "name", modified: "date modified", size: "size", shuffle: "shuffle" }[viewer.sortKey]) + (viewer.sortDescending ? ", descending" : ", ascending")
                Accessible.description: "Changes the order of photos inside folders, not the folder order"
            }
            BarDivider {}
            Label {
                text: viewer.fileName || "No photo open"
                elide: Text.ElideMiddle
                Layout.fillWidth: true
                color: viewer.fileName ? Theme.text : Theme.muted
                font.pixelSize: Theme.textBody
            }
            UiButton { // lock state stays visible outside Settings; activating it unlocks
                objectName: "layoutLockIndicator"
                visible: paneLayout.locked
                iconName: "lock"
                text: "Layout locked"
                tip: "Workspace layout is locked. Select to unlock" + app.shortcutHint("layout-lock")
                Accessible.name: "Workspace layout locked. Unlock layout"
                onClicked: app.invoke("layout-lock")
            }
            UiButton {
                objectName: "settingsButton"
                text: "Settings"
                tip: "Settings, palettes and shortcuts" + app.shortcutHint("settings") + (viewer.bindingFor("palette").length ? ". Find a command: " + viewer.bindingFor("palette") : "")
                onClicked: app.invoke("settings")
            }
            UiButton { text: "Exit full screen"; visible: app.visibility === Window.FullScreen; tip: "Exit full screen" + app.shortcutHint("fullscreen"); onClicked: app.invoke("fullscreen") }
            UiButton { text: "Show frame"; visible: app.frameless; tip: "Restore the system title bar" + app.shortcutHint("frameless"); onClicked: app.invoke("frameless") }
            UiButton { text: "Close window"; iconName: "close"; iconOnly: true; visible: app.frameless; onClicked: app.close() }
        }
        }
    }
    Filmstrip {
        id: filmstripPanel
        objectName: "filmstrip"
        compact: app.height < 480
        x: app.paneRect("filmstrip").x; y: app.paneRect("filmstrip").y
        width: app.paneRect("filmstrip").width; height: app.paneRect("filmstrip").height
        vertical: app.paneState("filmstrip").region === "left" || app.paneState("filmstrip").region === "right"
        visible: width > 0 && height > 0
        onItemActivated: canvas.forceActiveFocus()
    }
    FolderTree {
        id: treePanel
        objectName: "folderTree"
        compact: app.width < 720 || app.height < 480
        x: app.paneRect("tree").x; y: app.paneRect("tree").y
        width: app.paneRect("tree").width; height: app.paneRect("tree").height
        maximumWidth: Math.max(200, Math.round(app.width * 0.45))
        layoutFrozen: true
        arrangementMode: app.layoutEditing
        onResizeCommitted: extent => paneLayout.resize("tree", extent)
        visible: width > 0 && height > 0
        onDismissed: { if(app.layoutEditing)app.layoutEditing=false;app.focusContent() }
        // Hiding a focused panel must not strand keyboard input on an invisible item.
        onVisibleChanged: if (!visible && !app.modalOpen) app.focusContent()
    }
    Rectangle {
        id: bottomPanel
        x: app.paneRect("statusbar").x; y: app.paneRect("statusbar").y
        width: app.paneRect("statusbar").width; height: app.paneRect("statusbar").height
        visible: width > 0 && height > 0
        color: Theme.surface
        HoverHandler { id: bottomHover }
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: Theme.borderWidth
            color: Theme.border
        }
        ScrollView {
            id: statusbarScroll
            anchors.fill: parent
            clip: true
            contentWidth: availableWidth
            contentHeight: barGridstatusbar.implicitHeight
            ScrollBar.vertical: UiScrollBar {}
        GridLayout {
            id: barGridstatusbar
            columns: app.paneState("statusbar").region === "left" || app.paneState("statusbar").region === "right" ? 1 : children.length
            rowSpacing: Theme.gapXs; columnSpacing: Theme.gapXs
            width: statusbarScroll.availableWidth-2*Theme.gapMd
            x: Theme.gapMd
            y: Math.max(0,(statusbarScroll.availableHeight-implicitHeight)/2)
            UiButton { text: "Previous"; visible: !app.gridMode; enabled: viewer.fileCount > 0; tip: "Previous photo" + app.shortcutHint("previous"); onClicked: app.invoke("previous") }
            BarLabel {
                visible: !app.gridMode
                text: viewer.fileCount > 0 ? viewer.currentIndex + " of " + viewer.fileCount : "No photo"
                Layout.leftMargin: Theme.gapXs; Layout.rightMargin: Theme.gapXs
            }
            UiButton { text: "Next"; visible: !app.gridMode; enabled: viewer.fileCount > 0; tip: "Next photo" + app.shortcutHint("next"); onClicked: app.invoke("next") }
            BarLabel {
                visible: app.gridMode
                font.features: { "tnum": 1 }
                text: photoGrid.selectedCount > 1 ? photoGrid.selectedCount + " selected"
                    : photoGrid.selectedIndex >= 0 ? (photoGrid.selectedIndex + 1) + " of " + fileGrid.count : "No selection"
                Layout.leftMargin: Theme.gapXs
            }
            BarLabel {
                visible: app.gridMode && app.width > 680
                Layout.maximumWidth: Math.max(120, app.width - 520)
                Layout.leftMargin: Theme.gapMd
                elide: Text.ElideRight
                text: viewer.status.length > 0 ? viewer.status : "Arrow keys move · Shift or Ctrl selects · Enter opens · Backspace goes up"
                color: viewer.status.length > 0 ? Theme.text : Theme.muted
            }
            Item { Layout.fillWidth: true }
            UiButton { visible: app.gridMode; text: "Back to photo"; tip: "Back to photo (Esc)"; onClicked: app.showCanvas() }
            UiButton { objectName: "gridOpenSelected"; visible: app.gridMode; variant: "primary"; text: fileGrid.kindAt(photoGrid.selectedIndex) === "folder" ? "Open folder" : "Open"; enabled: photoGrid.selectedIndex >= 0 && fileGrid.kindAt(photoGrid.selectedIndex) !== "file"; tip: "Open the item under the cursor (Enter)"; onClicked: photoGrid.activate() }
            BarLabel { visible: !app.gridMode && app.width > 780 && viewer.imageWidth > 0; text: viewer.imageWidth + " × " + viewer.imageHeight; Layout.rightMargin: Theme.gapSm }
            UiButton {
                visible: !app.gridMode
                text: Math.round(canvas.zoom*100) + "%"
                font.features: { "tnum": 1 }
                enabled: viewer.imageWidth > 0
                onClicked: app.invoke("actual")
                tip: "Actual size: one image pixel per display pixel" + app.shortcutHint("actual")
                Accessible.name: "Zoom " + Math.round(canvas.zoom*100) + " percent. Show actual size"
            }
            UiButton { text: "Fit"; visible: !app.gridMode; enabled: viewer.imageWidth > 0; onClicked: app.invoke("fit"); tip: "Fit without enlarging" + app.shortcutHint("fit") }
            UiButton { text: "Rotate"; visible: !app.gridMode && app.width > 680; enabled: viewer.imageWidth > 0; onClicked: app.invoke("rotate"); tip: "Rotate the view clockwise" + app.shortcutHint("rotate") }
        }
        }
    }
    Rectangle { // status toast, centred on the part of the canvas no panel covers
        objectName: "statusToast"
        x: app.workspace._content.x+(app.workspace._content.width-width)/2
        y: app.workspace._content.y+app.workspace._content.height-height-Theme.gapLg
        width: Math.max(0,Math.min(app.workspace._content.width-2*Theme.gapMd,statusText.implicitWidth+2*Theme.gapLg))
        height: statusText.implicitHeight + 2 * Theme.gapSm + Theme.gapXxs
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: canvas.rendererError ? Theme.danger : Theme.border
        // Only the controller's default "how to begin" hint is suppressed while the empty state says the same thing.
        // Every other status, including settings-load errors present at start-up, still shows (tested).
        readonly property bool defaultHint: viewer.status === "Open a JPEG or PNG to begin."
        visible: !app.gridMode && (viewer.loading || canvas.rendererError.length > 0
                 || (viewer.status.length > 0 && app.hudVisible && !(emptyState.visible && defaultHint)))
        Label {
            id: statusText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 2 * Theme.gapLg)
            text: canvas.rendererError || viewer.status
            color: canvas.rendererError ? Theme.danger : Theme.text
            wrapMode: Text.Wrap
            font.pixelSize: Theme.textSmall
            Accessible.role: Accessible.AlertMessage
        }
    }
    FileDialog {
        id: openDialog
        title: "Open a photo"
        nameFilters: ["Photographs (*.jpg *.jpeg *.png *.JPG *.JPEG *.PNG)", "All files (*)"]
        onAccepted: { viewer.open(selectedFile.toString()); app.showCanvas() }
        onRejected: app.focusContent()
    }
    SettingsDialog {
        id: settingsDialog
        host: app
        photoCanvas: canvas
        anchors.centerIn: parent
        width: Math.min(800, app.width - 2 * Theme.gapLg)
        height: Math.min(640, app.height - 2 * Theme.gapLg)
        onClosed: app.focusContent()
    }
    // Native system resizing remains available even in the optional frameless mode.
    Repeater {
        model: app.frameless && app.visibility !== Window.FullScreen && app.visibility !== Window.Maximized ? 8 : 0
        delegate: MouseArea {
            required property int index
            readonly property bool edgeLeft: index === 0 || index === 4 || index === 6
            readonly property bool edgeRight: index === 1 || index === 5 || index === 7
            readonly property bool edgeTop: index === 2 || index === 4 || index === 5
            readonly property bool edgeBottom: index === 3 || index === 6 || index === 7
            x: edgeRight ? app.width-6 : (edgeLeft ? 0 : 6)
            y: edgeBottom ? app.height-6 : (edgeTop ? 0 : 6)
            width: edgeLeft || edgeRight ? 6 : app.width-12
            height: edgeTop || edgeBottom ? 6 : app.height-12
            z: 20
            cursorShape: (edgeLeft && edgeTop) || (edgeRight && edgeBottom) ? Qt.SizeFDiagCursor : (edgeRight && edgeTop) || (edgeLeft && edgeBottom) ? Qt.SizeBDiagCursor : edgeLeft || edgeRight ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: app.startSystemResize((edgeLeft ? Qt.LeftEdge : 0) | (edgeRight ? Qt.RightEdge : 0) | (edgeTop ? Qt.TopEdge : 0) | (edgeBottom ? Qt.BottomEdge : 0))
        }
    }
}
