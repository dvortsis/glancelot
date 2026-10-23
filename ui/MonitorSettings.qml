import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

UiDialog {
    id: dialog
    objectName: "monitorSettings"
    required property var host
    required property var customization
    property var manager: viewer.displayManager
    property string targetPane: ""
    property string pendingEnableDisplay: ""
    title: qsTr("Displays and workspaces")
    width: Math.min(760, parent ? parent.width - 2 * Theme.gapMd : 760)
    height: Math.min(680, parent ? parent.height - 2 * Theme.gapMd : 680)
    anchors.centerIn: parent
    function focusRequestedPane() {
        const card=paneCards.itemAt(["tree","grid","preview","metadata"].indexOf(targetPane))
        if(card){panesScroll.contentItem.contentY=Math.max(0,Math.min(card.y,panesScroll.contentItem.contentHeight-panesScroll.availableHeight));card.detachAction.forceActiveFocus(Qt.OtherFocusReason)}
    }
    function focusPage() {
        if(pages.currentIndex === 3)presetChoice.forceActiveFocus()
        else if(pages.currentIndex === 2){panesScroll.contentItem.contentY=0;paneDisplayChoice.forceActiveFocus()}
        else displayChoice.forceActiveFocus()
    }
    function openPage(index, pane) { pages.currentIndex = index; targetPane = pane || ""; open(); Qt.callLater(dialog.focusPage) }
    function choiceLabel(key,value) {
        const labels={windowMode:{windowed:qsTr("Windowed"),fullscreen:qsTr("Full screen")},fitMode:{shrink:qsTr("Shrink to fit"),fit:qsTr("Fit image"),fill:qsTr("Fill display"),actual:qsTr("Actual size (1:1)")},viewportLink:{linked:qsTr("Linked to controller"),independent:qsTr("Independent")},background:{theme:qsTr("Match controller theme"),solid:qsTr("Solid color"),checker:qsTr("Checkerboard")},cursorPolicy:{auto:qsTr("Hide when idle"),visible:qsTr("Always visible"),hidden:qsTr("Hidden")},folderChange:{follow:qsTr("Follow folder changes"),hold:qsTr("Hold displayed image"),disable:qsTr("Disable output")},slideshowTiming:{shared:qsTr("Follow controller timing"),independent:qsTr("Latest selection every 3 seconds")}}
        return labels[key][value]
    }
    // Pane names as shown to people; route keys stay the stable ids tree/grid/preview/metadata.
    readonly property var paneTitles: ({tree: qsTr("Tree"), grid: qsTr("Grid"), preview: qsTr("Preview"), metadata: qsTr("Metadata")})
    readonly property var paneDestinations: ({tree: qsTr("Destination for tree pane"), grid: qsTr("Destination for grid pane"), preview: qsTr("Destination for preview pane"), metadata: qsTr("Destination for metadata pane")})
    function option(key, value) { const patch = ({}); patch[key] = value; customization.setOutputOptions(patch) }
    function finishEnable() {
        if(!pendingEnableDisplay.length || customization.saving) return
        const target=pendingEnableDisplay; pendingEnableDisplay=""
        if(customization.outputOptions.displayId === target && customization.feedback.length === 0) host.invoke("monitor-enable")
    }
    Connections { target: customization; function onChanged() {
        dialog.finishEnable()
        const pane=host.pendingPaneFocus
        if(pane.length && customization.paneRoutes[pane].detached && customization.feedback.length === 0)dialog.close()
    } }
    onClosed: { if(!host.pendingPaneFocus.length){host.requestActivate();Qt.callLater(host.focusContent)} }
    contentItem: ColumnLayout {
        spacing: Theme.gapMd
        TabBar {
            id: pages
            Layout.fillWidth: true
            Repeater {
                model: [qsTr("Displays"), qsTr("Presentation"), qsTr("Panes"), qsTr("Presets")]
                TabButton {
                    id: tab
                    required property string modelData
                    text: modelData; font.pixelSize: Theme.textBody; implicitHeight: Theme.controlHeight
                    contentItem: Label { text: tab.text; font: tab.font; color: tab.checked ? Theme.textOnPrimary : Theme.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    background: Rectangle { color: tab.checked ? Theme.primary : (tab.hovered ? Theme.hover : "transparent"); radius: Theme.radiusControl; border.width: tab.visualFocus ? Theme.focusWidth : 0; border.color: Theme.focusRing }
                }
            }
        }
        Label { Layout.fillWidth: true; text: customization.feedback; visible: text.length > 0; wrapMode: Text.Wrap; color: Theme.text; Accessible.role: Accessible.AlertMessage }
        StackLayout {
            currentIndex: pages.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                objectName: "monitorDisplaysScroll"
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: UiScrollBar {}
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.gapMd
                    Label { text: qsTr("Presentation display"); color: Theme.text; font.pixelSize: Theme.textTitle }
                    UiComboBox { id: displayChoice; objectName: "monitorDisplayChoice"; Layout.fillWidth: true; model: dialog.manager.displays; textRole: "label"; valueRole: "id"; Accessible.name: qsTr("Presentation display") }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted
                        readonly property var info: dialog.manager.displays[displayChoice.currentIndex] || ({})
                        text: info.name ? qsTr("%1 × %2 · %3× scale · %4 Hz").arg(info.width).arg(info.height).arg(info.devicePixelRatio).arg(Math.round(info.refreshRate)) + "\n"
                                          + (info.identityConfidence === "strong" ? qsTr("Strong serial identity: %1").arg(info.serialNumber) : qsTr("Weak identity: reselect similar displays after reconnect"))
                                          + "\n" + qsTr("Connection type unavailable · Color: %1").arg(info.colorSpace)
                                        : qsTr("No display is available.")
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: dialog.manager.presentationActive; text: qsTr("Output active on %1").arg(dialog.manager.activeDisplayName); color: Theme.text }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: customization.outputOptions.displayId.length>0 && !dialog.manager.displays.some(display => display.id===customization.outputOptions.displayId); text: qsTr("The saved presentation display is unavailable. Choose a connected display to enable output."); color: Theme.muted }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: dialog.manager.displays.some(display => !display.canPosition); text: qsTr("This compositor chooses window positions. Use your window manager to move windows; choosing a destination display here is unavailable."); color: Theme.muted }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: dialog.manager.outputColorStatus; color: Theme.muted }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: customization.outputRoutingStatus; color: Theme.muted }
                    Flow {
                        Layout.fillWidth: true; spacing: Theme.gapSm
                        UiButton { text: qsTr("Identify"); onClicked: host.invoke("monitor-identify") }
                        UiButton { text: qsTr("Enable output"); enabled: displayChoice.currentIndex >= 0; onClicked: { dialog.pendingEnableDisplay=displayChoice.currentValue; if(!customization.setOutputOptions({displayId: displayChoice.currentValue}))dialog.pendingEnableDisplay=""; else dialog.finishEnable() } }
                        UiButton { text: qsTr("Move output"); enabled: dialog.manager.presentationActive && displayChoice.currentIndex >= 0 && dialog.manager.displays[displayChoice.currentIndex].canPosition; onClicked: host.invokeTarget("monitor-move-output",displayChoice.currentValue) }
                        UiButton { text: qsTr("Swap with controller"); enabled: dialog.manager.presentationActive && displayChoice.currentIndex >= 0 && dialog.manager.displays[displayChoice.currentIndex].canPosition; onClicked: host.invoke("monitor-swap") }
                        UiButton { text: qsTr("Disable output"); enabled: dialog.manager.presentationActive; onClicked: host.invoke("monitor-disable") }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Reconnecting a display never enables presentation automatically. Physical color, HDR and refresh accuracy require separate qualification."); color: Theme.muted }
                }
            }
            ScrollView {
                objectName: "monitorPresentationScroll"
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: UiScrollBar {}
                ColumnLayout {
                    width: parent.width; spacing: Theme.gapMd
                    Repeater {
                        model: [
                            {key: "windowMode", label: qsTr("Window mode"), choices: ["windowed", "fullscreen"]},
                            {key: "fitMode", label: qsTr("Image scale"), choices: ["shrink", "fit", "fill", "actual"]},
                            {key: "viewportLink", label: qsTr("Controller viewport"), choices: ["linked", "independent"]},
                            {key: "background", label: qsTr("Background"), choices: ["theme", "solid", "checker"]},
                            {key: "cursorPolicy", label: qsTr("Cursor"), choices: ["auto", "visible", "hidden"]},
                            {key: "folderChange", label: qsTr("On folder change"), choices: ["follow", "hold", "disable"]},
                            {key: "slideshowTiming", label: qsTr("Presentation timing"), choices: ["shared", "independent"]}
                        ]
                        ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Label { text: modelData.label; color: Theme.text }
                            UiComboBox { Layout.fillWidth: true; model: modelData.choices.map(value => ({value:value,label:dialog.choiceLabel(modelData.key,value)})); textRole: "label"; valueRole: "value"; enabled: !(dialog.manager.isPinned && ["viewportLink","folderChange","slideshowTiming"].indexOf(modelData.key)>=0); currentIndex: modelData.choices.indexOf(customization.outputOptions[modelData.key]); Accessible.name: modelData.label; onActivated: dialog.option(modelData.key, currentValue) }
                        }
                    }
                    Flow { Layout.fillWidth: true; spacing: Theme.gapSm
                        UiButton { text: qsTr("Rotate output"); enabled: dialog.manager.presentationActive; onClicked: host.invoke("presentation-view-rotate") }
                        UiButton { text: qsTr("Zoom in"); enabled: dialog.manager.presentationActive; onClicked: host.invoke("presentation-view-zoom-in") }
                        UiButton { text: qsTr("Zoom out"); enabled: dialog.manager.presentationActive; onClicked: host.invoke("presentation-view-zoom-out") }
                    }
                    Label { text: qsTr("Solid/checker background color (#RRGGBB)"); color: Theme.text }
                    UiTextField { Layout.fillWidth: true; text: customization.outputOptions.backgroundColor; Accessible.name: qsTr("Background color"); onEditingFinished: dialog.option("backgroundColor",text) }
                    Repeater {
                        model: [{key:"guides",label:qsTr("Show composition guides")},{key:"imageInfo",label:qsTr("Show image information")},{key:"followSelection",label:qsTr("Follow controller selection")}]
                        UiSwitch { Layout.fillWidth: true; required property var modelData; text: modelData.label; enabled: !dialog.manager.isPinned || modelData.key !== "followSelection"; checked: customization.outputOptions[modelData.key]; onClicked: dialog.option(modelData.key,checked) }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: dialog.manager.isPinned; color: Theme.muted; text: qsTr("Pinned output keeps its reference image. Follow, folder-change and timing controls apply after leaving pinned mode.") }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted; text: qsTr("Linked views require both windows to show the same photo and follow selection. Pinned output keeps its reference. Some compositors prevent window positioning. Actual size shows one image pixel per display pixel. Independent timing presents the latest selection every 3 seconds.") }
                }
            }
            ScrollView {
                objectName: "monitorPanesScroll"
                id: panesScroll
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: UiScrollBar {}
                ColumnLayout {
                    width: parent.width; spacing: Theme.gapLg
                    UiSwitch { objectName: "monitorLayoutLock"; Layout.fillWidth: true; enabled: !customization.saving; text: qsTr("Lock workspace layout"); checked: customization.layoutLocked; onClicked: customization.layoutLocked = checked }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Choose a destination on this page, then detach a pane. Frozen panes remain interactive. Recovery is always available."); color: Theme.muted }
                    Label { objectName: "monitorPaneDestinationLabel"; text: dialog.paneDestinations[dialog.targetPane] || qsTr("Pane destination"); color: Theme.text }
                    UiComboBox { id: paneDisplayChoice; objectName: "monitorPaneDestination"; Layout.fillWidth: true; model: dialog.manager.displays; textRole: "label"; valueRole: "id"; Accessible.name: qsTr("Pane destination display"); onActivated: { if(dialog.targetPane.length)Qt.callLater(dialog.focusRequestedPane) } }
                    Repeater {
                        id: paneCards
                        model: ["tree","grid","preview","metadata"]
                        ColumnLayout {
                            required property string modelData
                            readonly property var route: customization.paneRoutes[modelData] || ({})
                            property alias detachAction: paneDetachButton
                            Layout.fillWidth: true; spacing: Theme.gapSm
                            Label { text: (route.detached ? qsTr("%1 — detached") : qsTr("%1 — controller")).arg(dialog.paneTitles[modelData]); color: Theme.text; font.pixelSize: Theme.textTitle }
                            Flow {
                                Layout.fillWidth: true; spacing: Theme.gapSm
                                UiButton { id: paneDetachButton; objectName: "paneDetach-"+modelData; text: qsTr("Detach to selected display"); enabled: !customization.saving && !customization.layoutLocked && !route.frozen && paneDisplayChoice.currentIndex >= 0 && dialog.manager.displays[paneDisplayChoice.currentIndex].canPosition; onClicked: host.invokeTarget("pane-detach-"+modelData,paneDisplayChoice.currentValue) }
                                UiButton { text: qsTr("Re-dock"); enabled: !customization.saving && route.detached && !customization.layoutLocked && !route.frozen; onClicked: host.invoke("pane-redock-"+modelData) }
                                UiButton { text: route.frozen ? qsTr("Unfreeze") : qsTr("Freeze"); enabled: !customization.saving && !customization.layoutLocked; onClicked: host.invoke("pane-freeze-"+modelData) }
                            }
                            Label { visible: route.recovered === true; text: qsTr("Preferred display unavailable; controls recovered onscreen."); color: Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("Width"); color: Theme.text }
                                UiTextField { Layout.fillWidth: true; text: route.geometry ? route.geometry.width : "800"; Accessible.name: qsTr("%1 pane width").arg(dialog.paneTitles[modelData]); enabled: !customization.saving && !customization.layoutLocked && !route.frozen; onEditingFinished: {const r=Object.assign({},route.geometry);r.width=Number(text);customization.updatePaneGeometry(modelData,r)} }
                                Label { text: qsTr("Height"); color: Theme.text }
                                UiTextField { Layout.fillWidth: true; text: route.geometry ? route.geometry.height : "600"; Accessible.name: qsTr("%1 pane height").arg(dialog.paneTitles[modelData]); enabled: !customization.saving && !customization.layoutLocked && !route.frozen; onEditingFinished: {const r=Object.assign({},route.geometry);r.height=Number(text);customization.updatePaneGeometry(modelData,r)} }
                            }
                        }
                    }
                }
            }
            ScrollView {
                objectName: "monitorPresetsScroll"
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: UiScrollBar {}
                ColumnLayout {
                    width: parent.width; spacing: Theme.gapMd
                    Label { objectName: "monitorPresetHeading"; text: qsTr("Display workspace presets for this window"); color: Theme.text; font.pixelSize: Theme.textTitle }
                    Label { text: qsTr("Saved preset"); color: Theme.text }
                    UiComboBox { id: presetChoice; Layout.fillWidth: true; model: customization.presets; displayText: count > 0 ? currentText : qsTr("No saved presets"); textRole: "name"; valueRole: "id"; Accessible.name: qsTr("Saved preset"); onActivated: { const saved=customization.presets[currentIndex]; if(saved) {presetName.text=saved.name;presetKind.currentIndex=saved.kind === "workspace" ? 1 : 0} } }
                    Label { text: qsTr("Preset name"); color: Theme.text }
                    UiTextField { id: presetName; Layout.fillWidth: true; placeholderText: qsTr("Preset name"); Accessible.name: qsTr("Preset name"); maximumLength: 160 }
                    Label { text: qsTr("Preset type"); color: Theme.text }
                    UiComboBox { id: presetKind; model: [{value:"presentation",label:qsTr("Presentation")},{value:"workspace",label:qsTr("Workspace")}]; textRole: "label"; valueRole: "value"; Accessible.name: qsTr("Preset type") }
                    Flow {
                        Layout.fillWidth: true; spacing: Theme.gapSm
                        UiButton { text: qsTr("Create preset"); onClicked: customization.savePreset("",presetName.text,presetKind.currentValue) }
                        UiButton { text: qsTr("Update snapshot"); enabled: presetChoice.currentIndex >= 0; onClicked: { const saved=customization.presets[presetChoice.currentIndex]; customization.savePreset(saved.id,saved.name,saved.kind) } }
                        UiButton { text: qsTr("Rename"); enabled: presetChoice.currentIndex >= 0; onClicked: customization.renamePreset(presetChoice.currentValue,presetName.text) }
                        UiButton { text: qsTr("Apply"); enabled: presetChoice.currentIndex >= 0; onClicked: customization.applyPreset(presetChoice.currentValue) }
                        UiButton { text: qsTr("Delete preset"); enabled: presetChoice.currentIndex >= 0; onClicked: customization.deletePreset(presetChoice.currentValue) }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Workspace presets include pane placement and lock state; they belong to this window. Pane layout presets in Settings are shared by all windows. Images are never stored in presets. Applying a preset does not automatically enable output."); color: Theme.muted }
                    UiButton { text: qsTr("Reset monitor preferences…"); onClicked: resetConfirm.open() }
                }
            }
        }
        UiButton { objectName: "monitorRecover"; text: qsTr("Recover all panes to controller"); Layout.fillWidth: true; onClicked: host.invoke("monitor-recover") }
    }
    UiDialog {
        id: resetConfirm; title: qsTr("Reset monitor preferences?"); anchors.centerIn: parent
        width: Math.min(440,dialog.width-Theme.gapLg)
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("This removes this slot’s monitor presets, re-docks panes and clears their layout locks. Original images and other slots are unchanged."); color: Theme.text }
            UiButton { text: qsTr("Reset preferences"); onClicked: { customization.resetPreferences(); resetConfirm.close() } }
            UiButton { text: qsTr("Cancel"); onClicked: resetConfirm.close() }
        }
    }
}
