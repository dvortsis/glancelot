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

    function wakeHud() { hudVisible = true; hideHud.restart() }
    function invoke(id) { if (gridMode && !gridAllowsCommand(id)) return; viewer.trigger(id); wakeHud() }
    function focusContent() { if (gridMode) photoGrid.focusGrid(); else canvas.forceActiveFocus() }
    function showCanvas() { gridMode = false; canvas.forceActiveFocus(); wakeHud() }
    function gridAllowsCommand(id) {
        return ["open", "settings", "palette", "quit", "grid", "tree", "fullscreen", "frameless", "sort", "sort-reverse"].indexOf(id) >= 0
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
            enabled: !app.modalOpen && modelData.binding.length > 0 && (!app.gridMode || app.gridAllowsCommand(modelData.id))
            onActivated: app.invoke(modelData.id)
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: !app.modalOpen
        onActivated: {
            canvas.endLoupe()
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
            if (!app.gridMode && viewer.fileCount > 0 && !app.modalOpen && !topBar.hovered && !bottomHover.hovered && !filmstripPanel.keepOpen && !treePanel.keepOpen)
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
    ImageCanvas {
        id: canvas
        objectName: "canvas"
        anchors { fill: parent; leftMargin: app.treeInset }
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
            if ((wheel.modifiers & Qt.ControlModifier) || viewer.wheelMode === "zoom") {
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
        anchors { left: parent.left; right: parent.right; top: topPanel.bottom; bottom: bottomPanel.top; leftMargin: app.treeInset }
        visible: app.gridMode
        inputEnabled: !app.modalOpen
        onItemActivated: app.showCanvas()
        onDismissed: app.showCanvas()
        onFoldersRequested: { if (!viewer.treeVisible) viewer.treeVisible = true; else treePanel.focusList() }
    }
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
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: app.treeInset / 2
        spacing: Theme.gapSm
        visible: !app.gridMode && viewer.fileCount === 0 && !viewer.loading
        width: Math.min(440, app.width - app.treeInset - 2 * Theme.gapXxl)
        Image {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: app.height < 480 ? 96 : 144
            Layout.preferredHeight: Layout.preferredWidth
            source: "qrc:/brand/mascots/transparent/01_mascot_standing.png"
            fillMode: Image.PreserveAspectFit
            sourceSize.width: 288; sourceSize.height: 288
            Accessible.ignored: true
        }
        Label {
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
            text: "Your photographs, front and center."
            color: Theme.muted
            font.pixelSize: Theme.textTitle
            wrapMode: Text.Wrap
        }
        UiButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.gapLg
            variant: "primary"
            text: "Open a photo"
            tip: "Open a photo" + app.shortcutHint("open")
            onClicked: app.invoke("open")
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: Theme.gapSm
            horizontalAlignment: Text.AlignHCenter
            text: "or drop a JPEG or PNG here. Arrow keys browse the folder."
            color: Theme.muted
            font.pixelSize: Theme.textSmall
            wrapMode: Text.Wrap
            lineHeight: 1.4
        }
    }
    Rectangle {
        id: topPanel
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Theme.barHeight
        color: Theme.surface
        visible: app.gridMode || app.hudVisible || app.frameless
        HoverHandler { id: topBar }
        MouseArea { anchors.fill: parent; enabled: app.frameless; onPressed: app.startSystemMove(); onDoubleClicked: app.visibility === Window.Maximized ? app.showNormal() : app.showMaximized() }
        Rectangle { // edge against the canvas when no filmstrip sits below
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: Theme.borderWidth
            color: Theme.border
            visible: !filmstripPanel.visible
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.gapMd; anchors.rightMargin: Theme.gapMd
            spacing: Theme.gapXs
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
                enabled: viewer.fileCount > 0
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
    Filmstrip {
        id: filmstripPanel
        objectName: "filmstrip"
        compact: app.height < 480
        anchors { left: parent.left; right: parent.right; top: topPanel.bottom }
        visible: !app.gridMode && (app.hudVisible || app.frameless) && viewer.filmstripVisible && filmstrip.count > 0
        onItemActivated: canvas.forceActiveFocus()
    }
    FolderTree {
        id: treePanel
        objectName: "folderTree"
        compact: app.width < 720 || app.height < 480
        anchors {
            left: parent.left
            top: filmstripPanel.visible ? filmstripPanel.bottom : topPanel.bottom
            bottom: bottomPanel.visible ? bottomPanel.top : parent.bottom
        }
        width: implicitWidth
        maximumWidth: Math.max(200, Math.round(app.width * 0.45))
        visible: viewer.treeVisible && (viewer.treePinned || app.hudVisible || app.frameless)
        onDismissed: app.focusContent()
        // Hiding a focused panel must not strand keyboard input on an invisible item.
        onVisibleChanged: if (!visible && !app.modalOpen) app.focusContent()
    }
    Rectangle {
        id: bottomPanel
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Theme.barHeight
        visible: app.gridMode || app.hudVisible
        color: Theme.surface
        HoverHandler { id: bottomHover }
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: Theme.borderWidth
            color: Theme.border
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.gapMd; anchors.rightMargin: Theme.gapMd
            spacing: Theme.gapXs
            UiButton { text: "Previous"; visible: !app.gridMode; enabled: viewer.fileCount > 0; tip: "Previous photo" + app.shortcutHint("previous"); onClicked: app.invoke("previous") }
            BarLabel {
                visible: !app.gridMode
                text: viewer.fileCount > 0 ? viewer.currentIndex + " of " + viewer.fileCount : "No photo"
                Layout.leftMargin: Theme.gapXs; Layout.rightMargin: Theme.gapXs
            }
            UiButton { text: "Next"; visible: !app.gridMode; enabled: viewer.fileCount > 0; tip: "Next photo" + app.shortcutHint("next"); onClicked: app.invoke("next") }
            BarLabel {
                visible: app.gridMode
                text: photoGrid.selectedIndex >= 0 ? (photoGrid.selectedIndex + 1) + " of " + filmstrip.count + " selected" : "No selection"
                Layout.leftMargin: Theme.gapXs
            }
            BarLabel {
                visible: app.gridMode && app.width > 680
                Layout.maximumWidth: Math.max(120, app.width - 520)
                Layout.leftMargin: Theme.gapMd
                elide: Text.ElideRight
                text: viewer.status.length > 0 ? viewer.status : "Arrow keys select · Enter opens"
                color: viewer.status.length > 0 ? Theme.text : Theme.muted
            }
            Item { Layout.fillWidth: true }
            UiButton { visible: app.gridMode; text: "Back to photo"; tip: "Back to photo (Esc)"; onClicked: app.showCanvas() }
            UiButton { visible: app.gridMode; variant: "primary"; text: "Open selected"; enabled: photoGrid.selectedIndex >= 0; tip: "Open selected (Enter)"; onClicked: photoGrid.activate() }
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
    Rectangle { // status toast, centred on the part of the canvas no panel covers
        readonly property int covered: treePanel.visible ? treePanel.width : 0
        anchors { horizontalCenter: parent.horizontalCenter; horizontalCenterOffset: covered / 2; bottom: parent.bottom; bottomMargin: Theme.barHeight + Theme.gapLg }
        width: Math.min(app.width - covered - 2 * Theme.gapXl, statusText.implicitWidth + 2 * Theme.gapLg)
        height: statusText.implicitHeight + 2 * Theme.gapSm + Theme.gapXxs
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: canvas.rendererError ? Theme.danger : Theme.border
        // The empty state already explains how to begin, so the unchanged start-up status is not repeated here;
        // any later status (an open error, a settings problem) still shows.
        property string startupStatus
        Component.onCompleted: startupStatus = viewer.status
        visible: !app.gridMode && (viewer.loading || canvas.rendererError.length > 0
                 || (viewer.status.length > 0 && app.hudVisible && !(emptyState.visible && viewer.status === startupStatus)))
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
