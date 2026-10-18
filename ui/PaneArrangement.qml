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
            readonly property var state: arrangement.host.paneState(modelData)
            readonly property var rect: arrangement.host.paneRect(modelData)
            readonly property bool horizontalEdge: state.region === "top" || state.region === "bottom"
            readonly property bool immutable: paneLayout.locked || state.frozen
            x: rect.x; y: rect.y; width: rect.width; height: rect.height
            visible: width>0 && height>0
            Rectangle { anchors.fill: parent; color: "transparent";border.color: frame.immutable ? Theme.muted : Theme.primary; border.width: Theme.focusWidth }
            Rectangle {
                id: grip
                objectName: "paneGrip-"+frame.modelData
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Arrange "+frame.modelData
                Accessible.description: "Arrow keys move to an edge. Control and arrow keys resize. Shift and arrow keys change order. Space freezes or unfreezes. Home returns image or grid to the center. Escape exits arrangement."
                Accessible.onPressAction: if(!paneLayout.locked)paneLayout.freeze(frame.modelData,!frame.state.frozen)
                border.width: activeFocus ? Theme.focusWidth : 0
                border.color: Theme.focusRing
                Keys.onShortcutOverride: event => {
                    if([Qt.Key_Left,Qt.Key_Right,Qt.Key_Up,Qt.Key_Down,Qt.Key_Space,Qt.Key_Home,Qt.Key_Escape].indexOf(event.key)>=0)event.accepted=true
                }
                Keys.onPressed: event => {
                    if(event.key===Qt.Key_Escape){arrangement.host.layoutEditing=false;event.accepted=true;return}
                    if(event.key===Qt.Key_Space && !paneLayout.locked){paneLayout.freeze(frame.modelData,!frame.state.frozen);event.accepted=true;return}
                    if(frame.immutable)return
                    if(event.key===Qt.Key_Home&&(frame.modelData==="preview"||frame.modelData==="grid")) {
                        paneLayout.move(frame.modelData,"center",frame.state.order);event.accepted=true;return
                    }
                    const directions={}
                    directions[Qt.Key_Left]="left";directions[Qt.Key_Right]="right";directions[Qt.Key_Up]="top";directions[Qt.Key_Down]="bottom"
                    if(directions[event.key]===undefined)return
                    const delta=event.key===Qt.Key_Left||event.key===Qt.Key_Up?-1:1
                    if(event.modifiers & Qt.ControlModifier)paneLayout.resize(frame.modelData,Math.max(32,Math.min(4096,frame.state.extent+delta*16)))
                    else if(event.modifiers & Qt.ShiftModifier)paneLayout.move(frame.modelData,frame.state.region,Math.max(0,Math.min(99,frame.state.order+delta)))
                    else paneLayout.move(frame.modelData,directions[event.key],frame.state.order)
                    event.accepted=true
                }
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
                    onPressed: mouse => { grip.forceActiveFocus();origin=mapToItem(arrangement,mouse.x,mouse.y);moved=false }
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
                    grip.forceActiveFocus()
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
