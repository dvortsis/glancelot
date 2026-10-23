import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import QtQuick.Window
import Glancelot

Window {
    id: window
    required property var appHost
    required property var customization
    required property string paneId
    property var route: customization.paneRoutes[paneId]
    property alias paneContent: body
    property bool applyingGeometry: false
    property bool movingGeometry: false
    property string appliedGeometry: ""
    objectName: "detached-" + paneId
    // STACK-F054: pane names are translatable; the route keys stay the stable ids.
    readonly property string paneTitle: ({tree: qsTr("Tree"), grid: qsTr("Grid"), preview: qsTr("Preview"), metadata: qsTr("Metadata")})[paneId] || paneId
    title: qsTr("Glancelot — %1").arg(paneTitle)
    // Right-to-left languages mirror the detached window like the controller (Main.qml); the resize grip stays bottom-right.
    readonly property bool rtl: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.enabled: rtl
    LayoutMirroring.childrenInherit: true
    minimumWidth: 360; minimumHeight: 280
    color: Theme.background
    flags: Qt.Window | Qt.FramelessWindowHint
    function restoreGeometry() {
        const r = route.effectiveGeometry
        if(!r || movingGeometry || customization.saving) return
        const signature=JSON.stringify(r)
        if(signature === appliedGeometry && x===r.x && y===r.y && width===r.width && height===r.height)return
        appliedGeometry=signature
        applyingGeometry = true
        x=r.x; y=r.y; width=r.width; height=r.height
        applyingGeometry = false
    }
    onRouteChanged: restoreGeometry()
    Component.onCompleted: restoreGeometry()
    onActiveChanged: {
        if(active) appHost.focusRegion = paneId === "preview" ? "canvas" : paneId
        else appHost.chordCancel()
    }
    onClosing: close => { close.accepted=false; appHost.invoke("pane-redock-"+paneId) }
    function commitUserGeometry() {
        movingGeometry=false
        if(!customization.updatePaneGeometry(paneId,{x:window.x,y:window.y,width:window.width,height:window.height})) restoreGeometry()
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        ColumnLayout {
            Layout.fillWidth: true; Layout.margins: Theme.gapSm; spacing: Theme.gapSm
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: window.paneTitle; color: Theme.text; Layout.fillWidth: true
                    Accessible.name: qsTr("%1 pane; move with the title or choose a display in Settings").arg(text)
                    MouseArea {
                        anchors.fill: parent; enabled: !customization.saving && !customization.layoutLocked && !route.frozen
                        cursorShape: Qt.SizeAllCursor
                        property point origin
                        property point start
                        onPressed: mouse => { window.movingGeometry=true; origin=mapToGlobal(mouse.x,mouse.y); start=Qt.point(window.x,window.y) }
                        onPositionChanged: mouse => { if(pressed){const point=mapToGlobal(mouse.x,mouse.y);window.x=start.x+point.x-origin.x;window.y=start.y+point.y-origin.y} }
                        onReleased: window.commitUserGeometry()
                        onCanceled: { window.movingGeometry=false; window.restoreGeometry() }
                    }
                }
                UiButton { text: qsTr("Settings"); onClicked: appHost.invoke("monitor-settings") }
            }
            Flow {
                Layout.fillWidth: true; spacing: Theme.gapXs
                UiButton { text: route.frozen ? qsTr("Unfreeze") : qsTr("Freeze"); enabled: !customization.layoutLocked; onClicked: appHost.invoke("pane-freeze-"+paneId) }
                UiButton { text: qsTr("Move to display…"); enabled: !customization.saving && !customization.layoutLocked && !route.frozen; onClicked: appHost.invoke("pane-detach-"+paneId) }
                UiButton { text: qsTr("Re-dock"); enabled: !customization.saving && !customization.layoutLocked && !route.frozen; onClicked: appHost.invoke("pane-redock-"+paneId) }
                UiButton { text: qsTr("Recover all"); onClicked: appHost.invoke("monitor-recover") }
            }
        }
        FocusScope { id: body; Layout.fillWidth: true; Layout.fillHeight: true; clip: true }
        Label { Layout.fillWidth: true; Layout.margins: Theme.gapSm; text: customization.feedback || appHost.splitMessage; visible: text.length > 0; color: Theme.muted; wrapMode: Text.Wrap }
    }
    MouseArea {
        LayoutMirroring.enabled: false // the drag grows width and height from the bottom-right corner in every direction
        anchors.right: parent.right; anchors.bottom: parent.bottom
        width: Theme.iconSize+Theme.gapSm; height: width
        enabled: !customization.saving && !customization.layoutLocked && !route.frozen
        cursorShape: Qt.SizeFDiagCursor
        property point origin
        property size start
        Accessible.name: qsTr("Resize pane; exact width and height are also available in Settings")
        onPressed: mouse => {window.movingGeometry=true;origin=mapToGlobal(mouse.x,mouse.y);start=Qt.size(window.width,window.height)}
        onPositionChanged: mouse => {if(pressed){const point=mapToGlobal(mouse.x,mouse.y);window.width=Math.max(window.minimumWidth,start.width+point.x-origin.x);window.height=Math.max(window.minimumHeight,start.height+point.y-origin.y)}}
        onReleased: window.commitUserGeometry()
        onCanceled: { window.movingGeometry=false; window.restoreGeometry() }
    }
    Instantiator {
        model: Object.keys(appHost.keyTable.firsts)
        delegate: Shortcut {
            required property string modelData
            sequence: modelData; context: Qt.WindowShortcut; autoRepeat: false
            enabled: window.active && appHost.keysLive && !appHost.chordPending && appHost.resolveKey(appHost.keyTable.firsts[modelData]).length > 0
            onActivated: {
                const id=appHost.resolveKey(appHost.keyTable.firsts[modelData])
                if(appHost.commandInfo[id].steps.length === 2) appHost.beginChord(modelData); else appHost.pressKey(id)
            }
        }
    }
    Instantiator {
        model: Object.keys(appHost.keyTable.seconds)
        delegate: Shortcut {
            required property string modelData
            sequence: modelData; context: Qt.WindowShortcut; autoRepeat: false
            enabled: window.active && appHost.keysLive && appHost.chordPending && appHost.resolveKey(appHost.keyTable.seconds[modelData],appHost.chordFirst).length > 0
            onActivated: appHost.pressKey(appHost.resolveKey(appHost.keyTable.seconds[modelData],appHost.chordFirst))
        }
    }
    Shortcut { sequence: "Escape"; context: Qt.WindowShortcut; enabled: window.active && !appHost.modalOpen && !appHost.editingText; onActivated: appHost.escapeContent() }
    Shortcut { sequence: "?"; context: Qt.WindowShortcut; enabled: window.active && appHost.keysLive; onActivated: { appHost.requestActivate(); appHost.openShortcutsHelp() } }
}
