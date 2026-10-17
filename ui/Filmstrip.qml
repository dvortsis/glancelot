import QtQuick
import QtQuick.Controls

// Virtualized strip for the current folder. Thumbnails are produced by the decoder worker process via
// image://thumb; delegates only exist near the viewport, so scrolling cancels work for items that left it.
Rectangle {
    id: strip
    property alias hovered: hover.hovered
    readonly property int thumbEdge: 160
    signal itemActivated(string path)
    implicitHeight: 92
    color: theme.colors.surface
    Accessible.role: Accessible.Pane
    Accessible.name: "Filmstrip"
    HoverHandler { id: hover }

    ListView {
        id: list
        objectName: "filmstripList"
        anchors.fill: parent
        anchors.margins: 6
        orientation: ListView.Horizontal
        spacing: 6
        clip: true
        model: filmstrip
        cacheBuffer: 360
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; height: 6 }
        Accessible.role: Accessible.List
        Accessible.name: "Photos in this folder"
        Component.onCompleted: if (filmstrip.currentRow >= 0) positionViewAtIndex(filmstrip.currentRow, ListView.Center)
        Connections {
            target: filmstrip
            function onCurrentRowChanged() { if (filmstrip.currentRow >= 0) list.positionViewAtIndex(filmstrip.currentRow, ListView.Center) }
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
            width: 84
            height: ListView.view.height
            padding: 2
            focusPolicy: Qt.StrongFocus
            Accessible.name: name + (isCurrent ? ", current photo" : "")
            Accessible.description: "Open this photo"
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: name
            onClicked: { viewer.open(path); strip.itemActivated(path) }
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            background: Rectangle {
                radius: 4
                color: cell.hovered || cell.visualFocus ? theme.colors.hover : "transparent"
                border.width: cell.isCurrent || cell.visualFocus ? 2 : 0
                border.color: theme.colors.primary
            }
            contentItem: Item {
                Image {
                    id: thumb
                    anchors.fill: parent
                    anchors.margins: 2
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    source: filmstrip.thumbnailSource(cell.path, strip.thumbEdge, filmstrip.epoch)
                    opacity: status === Image.Ready ? 1 : 0
                }
                Label {
                    anchors.fill: parent
                    anchors.margins: 4
                    visible: thumb.status !== Image.Ready
                    text: thumb.status === Image.Error ? cell.name : ""
                    color: theme.colors.muted
                    font.pixelSize: 10
                    wrapMode: Text.WrapAnywhere
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
