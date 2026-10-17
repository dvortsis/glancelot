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
    color: theme.colors.background
    property bool frameless: false
    property bool hudVisible: true
    property bool modalOpen: paletteDialog.opened || settingsDialog.opened || themePicker.opened || openDialog.visible
    property real pointerX: width / 2
    property real pointerY: height / 2
    property real wheelRemainder: 0
    property string bindingMessage: ""
    readonly property color surface: theme.colors.surface
    readonly property color muted: theme.colors.muted
    readonly property color primaryColor: theme.colors.primary
    flags: Qt.Window | (frameless ? Qt.FramelessWindowHint : 0)
    font.family: Qt.platform.os === "windows" ? "Segoe UI" : "Noto Sans"
    font.pixelSize: 13
    palette.window: theme.colors.surface
    palette.windowText: theme.colors.text
    palette.base: theme.colors.background
    palette.text: theme.colors.text
    palette.button: theme.colors.surface
    palette.buttonText: theme.colors.text
    palette.highlight: theme.colors.primary
    palette.highlightedText: theme.colors.onPrimary
    palette.mid: theme.colors.border
    palette.placeholderText: theme.colors.muted

    function wakeHud() { hudVisible = true; hideHud.restart() }
    function invoke(id) { viewer.trigger(id); wakeHud() }
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
    function handleViewCommand(id) {
        canvas.endLoupe()
        if (id === "open") openDialog.open()
        else if (id === "fullscreen") visibility = visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
        else if (id === "palette") { paletteDialog.open(); commandSearch.forceActiveFocus() }
        else if (id === "settings") settingsDialog.open()
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
            enabled: !app.modalOpen && modelData.binding.length > 0
            onActivated: app.invoke(modelData.id)
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: !app.modalOpen
        onActivated: {
            canvas.endLoupe()
            if (app.visibility === Window.FullScreen) app.showNormal()
            else app.wakeHud()
        }
    }
    Timer {
        id: hideHud
        interval: 2400
        running: true
        onTriggered: {
            if (viewer.fileCount > 0 && !app.modalOpen && !topBar.hovered && !bottomHover.hovered && !filmstripPanel.keepOpen && !treePanel.keepOpen)
                app.hudVisible = false
            else restart()
        }
    }
    component ActionButton: ToolButton {
        implicitHeight: 36
        horizontalPadding: 12
        focusPolicy: Qt.StrongFocus
        Accessible.name: text
        background: Rectangle { radius: 4; color: parent.down || parent.hovered || parent.visualFocus ? theme.colors.hover : "transparent"; border.color: parent.visualFocus ? theme.colors.primary : "transparent" }
        ToolTip.visible: hovered && ToolTip.text.length > 0
        ToolTip.delay: 600
    }
    ThemePicker { id: themePicker; anchors.centerIn: parent; onClosed: canvas.forceActiveFocus() }
    function openThemes() { themePicker.open() }
    // A pinned folder panel makes room instead of covering the photo, except in narrow windows.
    readonly property int treeInset: treePanel.visible && viewer.treePinned && app.width >= 720 ? treePanel.width : 0
    ImageCanvas {
        id: canvas
        objectName: "canvas"
        anchors { fill: parent; leftMargin: app.treeInset }
        image: viewer.image
        background: "#191919"
        focus: true
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
    DropArea {
        anchors.fill: parent
        onEntered: drag => { drag.accepted = drag.hasUrls }
        onDropped: drop => { if (drop.hasUrls && drop.urls.length) { viewer.open(drop.urls[0].toString()); drop.acceptProposedAction() } }
        Rectangle { anchors.fill: parent; visible: parent.containsDrag; color: Qt.rgba(app.primaryColor.r, app.primaryColor.g, app.primaryColor.b, 0.15); border.width: 2; border.color: theme.colors.primary }
    }
    Column {
        anchors.centerIn: parent
        spacing: 16
        visible: viewer.fileCount === 0 && !viewer.loading
        width: Math.min(460, app.width-64)
        Image { anchors.horizontalCenter: parent.horizontalCenter; width: 160; height: 160; source: "qrc:/brand/mascots/transparent/01_mascot_standing.png"; fillMode: Image.PreserveAspectFit; sourceSize.width: 320; sourceSize.height: 320 }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Glancelot"; color: theme.colors.text; font.pixelSize: 38; font.weight: Font.Light }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: "Your photographs, front and center."; color: app.muted; font.pixelSize: 14; wrapMode: Text.Wrap }
        ActionButton { anchors.horizontalCenter: parent.horizontalCenter; text: "Open a photo"; onClicked: app.invoke("open"); background: Rectangle { radius: 4; color: theme.colors.primary } palette.buttonText: theme.colors.onPrimary }
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: "Drop a JPEG or PNG here\nBrowse the folder with the arrow keys"; color: theme.colors.muted; font.pixelSize: 12; lineHeight: 1.6 }
    }
    Rectangle {
        id: topPanel
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 52
        color: theme.colors.surface
        visible: app.hudVisible || app.frameless
        HoverHandler { id: topBar }
        MouseArea { anchors.fill: parent; enabled: app.frameless; onPressed: app.startSystemMove(); onDoubleClicked: app.visibility === Window.Maximized ? app.showNormal() : app.showMaximized() }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12; anchors.rightMargin: 12
            spacing: 4
            Image { Layout.preferredWidth: 28; Layout.preferredHeight: 28; source: "qrc:/brand/icons/app/" + (theme.dark ? "dark" : "light") + "/glancelot-64.png"; fillMode: Image.PreserveAspectFit }
            ActionButton { text: "Open"; onClicked: app.invoke("open"); ToolTip.text: viewer.bindingFor("open") }
            ActionButton { text: "Folders"; enabled: viewer.fileCount > 0; onClicked: app.invoke("tree"); ToolTip.text: viewer.bindingFor("tree"); Accessible.description: viewer.treeVisible ? "Hide the folder tree" : "Show the folder tree" }
            Label { text: " / "; color: theme.colors.border }
            Label { text: viewer.fileName || "Glancelot"; elide: Text.ElideMiddle; Layout.fillWidth: true; color: theme.colors.text }
            ActionButton { text: app.width > 720 ? "Commands" : "Menu"; onClicked: app.invoke("palette"); ToolTip.text: viewer.bindingFor("palette") }
            ActionButton { text: "Themes"; onClicked: app.openThemes(); ToolTip.text: "Choose from 80 theme variants" }
            ActionButton { text: "Settings"; onClicked: app.invoke("settings"); ToolTip.text: viewer.bindingFor("settings") }
            ActionButton { text: "Exit fullscreen"; visible: app.visibility === Window.FullScreen; onClicked: app.invoke("fullscreen") }
            ActionButton { text: "Decorate"; visible: app.frameless; onClicked: app.invoke("frameless") }
            ActionButton { text: "Close"; visible: app.frameless; onClicked: app.close() }
        }
    }
    Filmstrip {
        id: filmstripPanel
        objectName: "filmstrip"
        compact: app.height < 480
        anchors { left: parent.left; right: parent.right; top: topPanel.bottom }
        visible: (app.hudVisible || app.frameless) && viewer.filmstripVisible && filmstrip.count > 0
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
        visible: viewer.treeVisible && (viewer.treePinned || app.hudVisible || app.frameless)
        onDismissed: canvas.forceActiveFocus()
    }
    Rectangle {
        id: bottomPanel
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 58
        visible: app.hudVisible
        color: theme.colors.surface
        HoverHandler { id: bottomHover }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14; anchors.rightMargin: 14
            spacing: 8
            ActionButton { text: "Previous"; enabled: viewer.fileCount > 0; onClicked: app.invoke("previous"); ToolTip.text: viewer.bindingFor("previous") }
            Label { text: viewer.fileCount > 0 ? viewer.currentIndex + " / " + viewer.fileCount : "No photo"; color: app.muted; font.pixelSize: 12 }
            ActionButton { text: "Next"; enabled: viewer.fileCount > 0; onClicked: app.invoke("next"); ToolTip.text: viewer.bindingFor("next") }
            Item { Layout.fillWidth: true }
            Label { visible: app.width > 780 && viewer.imageWidth > 0; text: viewer.imageWidth + " × " + viewer.imageHeight; color: app.muted; font.pixelSize: 12 }
            ActionButton { text: Math.round(canvas.zoom*100) + "%"; enabled: viewer.imageWidth > 0; onClicked: app.invoke("actual"); ToolTip.text: "Actual size · one image pixel per display pixel" }
            ActionButton { text: "Fit"; enabled: viewer.imageWidth > 0; onClicked: app.invoke("fit"); ToolTip.text: "Fit without enlarging" }
            ActionButton { text: "Rotate"; visible: app.width > 680; enabled: viewer.imageWidth > 0; onClicked: app.invoke("rotate") }
        }
    }
    Rectangle {
        anchors { horizontalCenter: parent.horizontalCenter; horizontalCenterOffset: app.treeInset / 2; bottom: parent.bottom; bottomMargin: 78 }
        width: Math.min(app.width-app.treeInset-48, statusText.implicitWidth+32)
        height: statusText.implicitHeight+20
        radius: 4
        color: theme.colors.surface
        visible: viewer.loading || canvas.rendererError.length > 0 || (viewer.status.length > 0 && app.hudVisible)
        Text { id: statusText; anchors.centerIn: parent; width: Math.min(implicitWidth,app.width-app.treeInset-80); text: canvas.rendererError || viewer.status; color: canvas.rendererError ? theme.colors.danger : theme.colors.muted; wrapMode: Text.Wrap; font.pixelSize: 12 }
    }
    FileDialog {
        id: openDialog
        title: "Open a photo"
        nameFilters: ["Photographs (*.jpg *.jpeg *.png *.JPG *.JPEG *.PNG)", "All files (*)"]
        onAccepted: { viewer.open(selectedFile.toString()); canvas.forceActiveFocus() }
        onRejected: canvas.forceActiveFocus()
    }
    Dialog {
        id: paletteDialog
        title: "Commands"
        anchors.centerIn: parent
        width: Math.min(600,app.width-40)
        height: Math.min(570,app.height-60)
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: { commandSearch.text=""; canvas.forceActiveFocus() }
        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            TextField { id: commandSearch; Layout.fillWidth: true; placeholderText: "Find a command…"; Accessible.name: "Find a command"; onAccepted: {
                for (let i=0; i<viewer.commands.length; ++i) {
                    let command=viewer.commands[i]
                    if (app.fuzzyMatch(command.label,text)) { paletteDialog.close(); app.invoke(command.id); break }
                }
            } }
            ListView {
                id: commandList
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true
                model: viewer.commands
                spacing: 2
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    height: visible ? 42 : 0
                    visible: app.fuzzyMatch(modelData.label,commandSearch.text)
                    text: modelData.label
                    Accessible.name: modelData.label + (modelData.binding ? ", " + modelData.binding : "")
                    rightPadding: 150
                    Label { anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: modelData.binding || "Unassigned"; color: app.muted; font.pixelSize: 12 }
                    onClicked: { paletteDialog.close(); app.invoke(modelData.id) }
                }
            }
            Label { text: "Remap any command in Settings → Controls"; color: app.muted; font.pixelSize: 12 }
        }
    }
    Dialog {
        id: settingsDialog
        title: "Settings"
        anchors.centerIn: parent
        width: Math.min(660,app.width-32)
        height: Math.min(680,app.height-48)
        modal: true
        standardButtons: Dialog.Close
        onClosed: canvas.forceActiveFocus()
        ScrollView {
            anchors.fill: parent
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 14
                ActionButton { text: "Appearance · 40 theme families"; onClicked: { settingsDialog.close(); app.openThemes() } }
                Label { text: "VIEWING"; color: app.muted; font.pixelSize: 11; font.letterSpacing: 1.2 }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Fit mode"; Layout.fillWidth: true }
                    ComboBox { model: ["Shrink only", "Fit", "Fill", "Fit width", "Fit height"]; onActivated: canvas.fit(["shrink","fit","fill","width","height"][currentIndex]); Accessible.name: "Fit mode" }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Mouse wheel"; Layout.fillWidth: true }
                    ComboBox { model: ["Navigate", "Zoom", "Pan"]; currentIndex: ["navigate","zoom","pan"].indexOf(viewer.wheelMode); onActivated: viewer.wheelMode=["navigate","zoom","pan"][currentIndex]; Accessible.name: "Mouse wheel mode" }
                }
                Label { Layout.fillWidth: true; text: "Ctrl + wheel zooms at the pointer. Shift + wheel pans horizontally. Drag to pan; hold the right mouse button for a 100% loupe."; color: app.muted; wrapMode: Text.Wrap; font.pixelSize: 12 }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Folder boundary"; Layout.fillWidth: true }
                    ComboBox { model: ["Stop", "Loop current folder", "Continue through tree"]; currentIndex: ["stop","loop","continue"].indexOf(viewer.boundaryMode); onActivated: viewer.boundaryMode=["stop","loop","continue"][currentIndex]; Accessible.name: "Folder boundary mode" }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Background"; Layout.fillWidth: true }
                    ActionButton { text: "Cycle background"; onClicked: app.invoke("background") }
                }
                CheckBox { text: "Nearest-neighbor sampling for pixel inspection"; checked: canvas.nearest; onToggled: canvas.nearest=checked }
                Label { visible: viewer.windowSlot > 0; Layout.fillWidth: true; text: "This is window " + viewer.windowSlot + ". It remembers its own last folder and photo for the next time you open a window."; color: app.muted; wrapMode: Text.Wrap; font.pixelSize: 12 }
                CheckBox { text: "Pin folder tree open"; checked: viewer.treePinned; onToggled: viewer.treePinned=checked }
                CheckBox { text: "Show filmstrip"; checked: viewer.filmstripVisible; onToggled: viewer.filmstripVisible=checked }
                CheckBox { text: "Frameless window"; checked: app.frameless; onToggled: app.frameless=checked }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.colors.border }
                Label { text: "CONTROLS"; color: app.muted; font.pixelSize: 11; font.letterSpacing: 1.2 }
                Label { Layout.fillWidth: true; text: "Use portable key names such as Ctrl+O or Shift+Right. Clear a field to unassign it. Duplicate or invalid bindings are rejected."; color: app.muted; wrapMode: Text.Wrap; font.pixelSize: 12 }
                Label { text: app.bindingMessage; visible: text.length > 0; color: theme.colors.primary; Layout.fillWidth: true; wrapMode: Text.Wrap }
                Repeater {
                    model: viewer.commands
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label { text: modelData.label; Layout.fillWidth: true }
                        TextField {
                            Layout.preferredWidth: 170
                            text: modelData.binding
                            Accessible.name: "Binding for " + modelData.label
                            onEditingFinished: {
                                if (text !== modelData.binding) {
                                    const ok=viewer.setBinding(modelData.id,text)
                                    app.bindingMessage=ok ? "Binding saved." : "Binding rejected: use a valid, unassigned key sequence."
                                    if (!ok) text=modelData.binding
                                }
                            }
                        }
                    }
                }
                ActionButton { text: "Restore default bindings"; onClicked: { viewer.resetBindings(); app.bindingMessage="Default bindings restored." } }
                Label { Layout.fillWidth: true; text: "Early alpha · JPEG and PNG · SDR/sRGB output\nMonitor profile calibration and native HDR are not yet qualified."; color: theme.colors.muted; wrapMode: Text.Wrap; font.pixelSize: 12 }
            }
        }
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




