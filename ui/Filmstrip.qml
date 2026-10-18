import QtQuick
import QtQuick.Controls
import Glancelot

// Virtualized strip for the current folder. Thumbnails are produced by the decoder worker process via
// image://thumb; delegates only exist near the viewport, so scrolling cancels work for items that left it.
Rectangle {
    id: strip
    property alias hovered: hover.hovered
    property int focusedCells: 0
    // The HUD must stay open while the pointer is over the strip or one of its cells has keyboard focus.
    readonly property bool keepOpen: hover.hovered || focusedCells > 0
    readonly property int thumbEdge: 160
    // Short windows get a smaller strip so it leaves the photo visible (set by the host layout).
    property bool compact: false
    property bool vertical: false
    signal itemActivated(string path)
    function centerCurrent() { if (filmstrip.currentRow >= 0) list.positionViewAtIndex(filmstrip.currentRow, ListView.Center) }
    // Moves keyboard focus onto the current photo's cell (Tab from the canvas).
    function focusCurrent() {
        const cell = list.itemAtIndex(Math.max(0, filmstrip.currentRow))
        if (cell) cell.forceActiveFocus(Qt.TabFocusReason)
        else retryFocus.restart() // the strip was just revealed; delegates are created on the next layout pass
    }
    Timer {
        id: retryFocus
        interval: 80
        onTriggered: { const cell = list.itemAtIndex(Math.max(0, filmstrip.currentRow)); if (cell) cell.forceActiveFocus(Qt.TabFocusReason) }
    }
    onVisibleChanged: if (visible) Qt.callLater(centerCurrent)
    Rectangle { // edge against the canvas; surface-on-surface alone is too faint
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Theme.borderWidth
        color: Theme.border
    }
    implicitHeight: compact ? 68 : 92
    color: Theme.surface
    Accessible.role: Accessible.Pane
    Accessible.name: "Filmstrip"
    HoverHandler { id: hover }
    MouseArea { // swallow pointer input on blank strip space so it never reaches the canvas behind
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }

    ListView {
        id: list
        objectName: "filmstripList"
        anchors.fill: parent
        leftMargin: Theme.gapMd // content starts on the top bar's leading edge but scrolls to the window edge
        rightMargin: Theme.gapMd
        anchors.topMargin: Theme.gapXs + Theme.gapXxs
        anchors.bottomMargin: Theme.gapXs + Theme.gapXxs
        orientation: strip.vertical ? ListView.Vertical : ListView.Horizontal
        spacing: Theme.gapXs + Theme.gapXxs
        clip: true
        // A hidden strip must not decode: with no model there are no delegates, so no thumbnail requests.
        model: strip.visible ? filmstrip : null
        cacheBuffer: 360
        boundsBehavior: Flickable.StopAtBounds
        Accessible.role: Accessible.List
        Accessible.name: "Photos in this folder"
        Component.onCompleted: strip.centerCurrent()
        Connections {
            target: filmstrip
            enabled: strip.visible
            function onCurrentRowChanged() {
                strip.centerCurrent()
                // Keyboard focus follows the photo: arrow-key navigation must not strand the focus ring on an old cell.
                if (strip.focusedCells > 0) Qt.callLater(strip.focusCurrent)
            }
        }
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: event => {
                const delta = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                list.cancelFlick()
                list.contentX = Math.max(0, Math.min(list.contentWidth - list.width, list.contentX - delta))
            }
        }
        delegate: ItemDelegate {
            id: cell
            required property int index
            required property string name
            required property string path
            required property bool isCurrent
            width: strip.compact ? 68 : 84
            // 3px of vertical room inside the list's clip keeps the outset focus ring whole.
            y: 3
            height: ListView.view.height - 6
            padding: 2
            focusPolicy: Qt.StrongFocus
            Accessible.name: name + (isCurrent ? ", current photo" : "")
            readonly property bool previewFailed: thumb.status === Image.Error
            Accessible.description: previewFailed ? "Preview unavailable. Open this photo" : "Open this photo"
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: previewFailed ? name + " (preview unavailable)" : name
            onClicked: { viewer.open(path); strip.itemActivated(path) }
            onActiveFocusChanged: strip.focusedCells += activeFocus ? 1 : -1
            Component.onDestruction: if (activeFocus) strip.focusedCells -= 1
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            background: Rectangle {
                radius: Theme.radiusControl
                color: cell.hovered || cell.visualFocus ? Theme.hover : "transparent"
                // Current photo = inner primary ring; keyboard focus = outer text-colored ring (different geometry and color).
                border.width: cell.isCurrent ? Theme.focusWidth : 0
                border.color: Theme.primary
                Rectangle { // keyboard focus: a separate outset ring, so it reads even on the current photo
                    anchors.fill: parent; anchors.margins: -3
                    radius: Theme.radiusControl + 3; color: "transparent"
                    border.width: Theme.focusWidth; border.color: Theme.focusRing
                    visible: cell.visualFocus
                }
            }
            contentItem: Item {
                Rectangle { // reserved space while loading, so cells never jump
                    anchors.fill: parent; anchors.margins: 2; radius: Theme.radiusSmall
                    visible: thumb.status !== Image.Ready
                    color: Theme.hover
                }
                Image {
                    id: thumb
                    anchors.fill: parent
                    anchors.margins: 2
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    source: filmstrip.thumbnailSource(cell.path, strip.thumbEdge, filmstrip.epoch)
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
                }
                Label {
                    anchors.fill: parent
                    anchors.margins: 4
                    visible: thumb.status !== Image.Ready
                    text: thumb.status === Image.Error ? cell.name : ""
                    color: Theme.muted
                    font.pixelSize: Theme.textCaption
                    wrapMode: Text.WrapAnywhere
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
