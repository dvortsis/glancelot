import QtQuick
import QtQuick.Controls
import Glancelot

// Arrangement is explicit: content keeps its own input outside handles. Only completed gestures persist.
Item {
    id: arrangement
    required property var host
    property string dropRegion: ""
    property bool dragging: false
    function destination(x,y,id) {
        const edge=Math.min(host.width,host.height)*0.22
        if(y<edge)return "top"
        if(y>host.height-edge)return "bottom"
        if((id === "preview" || id === "grid") && x>host.width*0.3 && x<host.width*0.7)return "center"
        if(x<host.width/2)return "left"
        if(x>=host.width/2)return "right"
        return id === "preview" || id === "grid" ? "center" : "left"
    }
    Connections {
        target: paneLayout
        function onChanged() { arrangement.dragging=false;arrangement.dropRegion="";arrangement.host.resizePreview=({}) }
    }
    onVisibleChanged: if(!visible) { dragging=false;dropRegion="";host.resizePreview=({}) }
    Rectangle {
        visible: arrangement.dragging
        z: 2
        color: Qt.alpha(Theme.primary,0.18)
        border.color: Theme.primary; border.width: Theme.focusWidth
        x: arrangement.dropRegion === "right" ? arrangement.width*0.75 : 0
        y: arrangement.dropRegion === "bottom" ? arrangement.height*0.75 : 0
        width: ["left","right"].indexOf(arrangement.dropRegion)>=0 ? arrangement.width*0.25 : arrangement.width
        height: ["top","bottom"].indexOf(arrangement.dropRegion)>=0 ? arrangement.height*0.25 : arrangement.height
    }
    Repeater {
        model: ["tree","preview","grid","filmstrip","topbar","statusbar"]
        delegate: Item {
            id: frame
            required property string modelData
            readonly property var state: paneLayout.panes[modelData]
            readonly property var rect: arrangement.host.paneRect(modelData)
            readonly property bool horizontalEdge: state.region === "top" || state.region === "bottom"
            readonly property bool immutable: paneLayout.locked || state.frozen
            x: rect.x; y: rect.y; width: rect.width; height: rect.height
            visible: width>0 && height>0
            Rectangle { anchors.fill: parent; color: "transparent";border.color: frame.immutable ? Theme.muted : Theme.primary; border.width: Theme.focusWidth }
            Rectangle {
                width: Math.min(parent.width,164);height: Theme.controlHeight
                color: Theme.surface;z: 3
                Label { anchors.centerIn: parent; text: frame.modelData + (frame.immutable ? " · locked" : " · move");color: Theme.text;font.pixelSize: Theme.textSmall }
                MouseArea {
                    id: mover
                    objectName: "paneMove-"+frame.modelData
                    anchors.fill: parent
                    enabled: !frame.immutable
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    property point origin
                    property bool moved: false
                    onPressed: mouse => { origin=mapToItem(arrangement,mouse.x,mouse.y);moved=false }
                    onPositionChanged: mouse => {
                        if(!pressed)return
                        const point=mapToItem(arrangement,mouse.x,mouse.y)
                        if(Math.abs(point.x-origin.x)+Math.abs(point.y-origin.y)>8)moved=true
                        if(moved) { arrangement.dragging=true;arrangement.dropRegion=arrangement.destination(point.x,point.y,frame.modelData) }
                    }
                    onReleased: {
                        if(moved && arrangement.visible && !frame.immutable)
                            paneLayout.move(frame.modelData,arrangement.dropRegion,frame.state.order)
                        arrangement.dragging=false;arrangement.dropRegion=""
                    }
                    onCanceled: { arrangement.dragging=false;arrangement.dropRegion="" }
                }
            }
            MouseArea {
                id: resizer
                objectName: "paneResize-"+frame.modelData
                enabled: !frame.immutable && frame.state.region !== "center"
                x: frame.horizontalEdge ? 0 : (frame.state.region === "right" ? -4 : parent.width-4)
                y: frame.horizontalEdge ? (frame.state.region === "bottom" ? -4 : parent.height-4) : 0
                width: frame.horizontalEdge ? parent.width : 8
                height: frame.horizontalEdge ? 8 : parent.height
                z: 4
                cursorShape: frame.horizontalEdge ? Qt.SizeVerCursor : Qt.SizeHorCursor
                property point origin
                property real initialExtent
                property real pendingExtent
                onPressed: mouse => {
                    origin=mapToItem(arrangement,mouse.x,mouse.y)
                    initialExtent=frame.state.extent;pendingExtent=initialExtent
                }
                onPositionChanged: mouse => {
                    if(!pressed)return
                    const point=mapToItem(arrangement,mouse.x,mouse.y)
                    const delta=frame.horizontalEdge ? point.y-origin.y : point.x-origin.x
                    const sign=frame.state.region === "right" || frame.state.region === "bottom" ? -1 : 1
                    pendingExtent=Math.max(32,Math.min(4096,initialExtent+delta*sign))
                    const preview=Object.assign({},arrangement.host.resizePreview)
                    preview[frame.modelData]=pendingExtent;arrangement.host.resizePreview=preview
                }
                onReleased: {
                    if(arrangement.visible && !frame.immutable)paneLayout.resize(frame.modelData,pendingExtent)
                    arrangement.host.resizePreview=({})
                }
                onCanceled: arrangement.host.resizePreview=({})
                onDoubleClicked: { arrangement.host.resizePreview=({});paneLayout.resize(frame.modelData,frame.horizontalEdge?92:280) }
            }
        }
    }
    UiButton {
        anchors { right: parent.right;top: parent.top;margins: Theme.gapSm }
        z: 10
        text: "Done arranging · Esc"
        onClicked: arrangement.host.layoutEditing=false
        Accessible.name: "Finish arranging panes"
    }
}
