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
    title: "Displays and workspaces"
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
        const labels={windowMode:{windowed:"Windowed",fullscreen:"Full screen"},fitMode:{shrink:"Shrink to fit",fit:"Fit image",fill:"Fill display",actual:"Actual size (1:1)"},viewportLink:{linked:"Linked to controller",independent:"Independent"},background:{theme:"Match controller theme",solid:"Solid color",checker:"Checkerboard"},cursorPolicy:{auto:"Hide when idle",visible:"Always visible",hidden:"Hidden"},folderChange:{follow:"Follow folder changes",hold:"Hold displayed image",disable:"Disable output"},slideshowTiming:{shared:"Follow controller timing",independent:"Latest selection every 3 seconds"}}
        return labels[key][value]
    }
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
                model: ["Displays", "Presentation", "Panes", "Presets"]
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
                    Label { text: "Presentation display"; color: Theme.text; font.pixelSize: Theme.textTitle }
                    UiComboBox { id: displayChoice; objectName: "monitorDisplayChoice"; Layout.fillWidth: true; model: dialog.manager.displays; textRole: "label"; valueRole: "id"; Accessible.name: "Presentation display" }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted
                        readonly property var info: dialog.manager.displays[displayChoice.currentIndex] || ({})
                        text: info.name ? info.width + " × " + info.height + " · " + info.devicePixelRatio + "× scale · " + Math.round(info.refreshRate) + " Hz\n" + (info.identityConfidence === "strong" ? "Strong serial identity: " + info.serialNumber : "Weak identity: reselect similar displays after reconnect") + "\nConnection type unavailable · Color: " + info.colorSpace : "No display is available."
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: dialog.manager.presentationActive; text: "Output active on " + dialog.manager.activeDisplayName; color: Theme.text }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: customization.outputOptions.displayId.length>0 && !dialog.manager.displays.some(display => display.id===customization.outputOptions.displayId); text: "The saved presentation display is unavailable. Choose a connected display to enable output."; color: Theme.muted }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: dialog.manager.displays.some(display => !display.canPosition); text: "This compositor chooses window positions. Use your window manager to move windows; choosing a destination display here is unavailable."; color: Theme.muted }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: dialog.manager.outputColorStatus; color: Theme.muted }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: customization.outputRoutingStatus; color: Theme.muted }
                    Flow {
                        Layout.fillWidth: true; spacing: Theme.gapSm
                        UiButton { text: "Identify"; onClicked: host.invoke("monitor-identify") }
                        UiButton { text: "Enable output"; enabled: displayChoice.currentIndex >= 0; onClicked: { dialog.pendingEnableDisplay=displayChoice.currentValue; if(!customization.setOutputOptions({displayId: displayChoice.currentValue}))dialog.pendingEnableDisplay=""; else dialog.finishEnable() } }
                        UiButton { text: "Move output"; enabled: dialog.manager.presentationActive && displayChoice.currentIndex >= 0 && dialog.manager.displays[displayChoice.currentIndex].canPosition; onClicked: host.invokeTarget("monitor-move-output",displayChoice.currentValue) }
                        UiButton { text: "Swap with controller"; enabled: dialog.manager.presentationActive && displayChoice.currentIndex >= 0 && dialog.manager.displays[displayChoice.currentIndex].canPosition; onClicked: host.invoke("monitor-swap") }
                        UiButton { text: "Disable output"; enabled: dialog.manager.presentationActive; onClicked: host.invoke("monitor-disable") }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Reconnecting a display never enables presentation automatically. Physical color, HDR and refresh accuracy require separate qualification."; color: Theme.muted }
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
                            {key: "windowMode", label: "Window mode", choices: ["windowed", "fullscreen"]},
                            {key: "fitMode", label: "Image scale", choices: ["shrink", "fit", "fill", "actual"]},
                            {key: "viewportLink", label: "Controller viewport", choices: ["linked", "independent"]},
                            {key: "background", label: "Background", choices: ["theme", "solid", "checker"]},
                            {key: "cursorPolicy", label: "Cursor", choices: ["auto", "visible", "hidden"]},
                            {key: "folderChange", label: "On folder change", choices: ["follow", "hold", "disable"]},
                            {key: "slideshowTiming", label: "Presentation timing", choices: ["shared", "independent"]}
                        ]
                        ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Label { text: modelData.label; color: Theme.text }
                            UiComboBox { Layout.fillWidth: true; model: modelData.choices.map(value => ({value:value,label:dialog.choiceLabel(modelData.key,value)})); textRole: "label"; valueRole: "value"; enabled: !(dialog.manager.isPinned && ["viewportLink","folderChange","slideshowTiming"].indexOf(modelData.key)>=0); currentIndex: modelData.choices.indexOf(customization.outputOptions[modelData.key]); Accessible.name: modelData.label; onActivated: dialog.option(modelData.key, currentValue) }
                        }
                    }
                    Flow { Layout.fillWidth: true; spacing: Theme.gapSm
                        UiButton { text: "Rotate output"; enabled: dialog.manager.presentationActive; onClicked: host.invoke("presentation-view-rotate") }
                        UiButton { text: "Zoom in"; enabled: dialog.manager.presentationActive; onClicked: host.invoke("presentation-view-zoom-in") }
                        UiButton { text: "Zoom out"; enabled: dialog.manager.presentationActive; onClicked: host.invoke("presentation-view-zoom-out") }
                    }
                    Label { text: "Solid/checker background color (#RRGGBB)"; color: Theme.text }
                    UiTextField { Layout.fillWidth: true; text: customization.outputOptions.backgroundColor; Accessible.name: "Background color"; onEditingFinished: dialog.option("backgroundColor",text) }
                    Repeater {
                        model: [{key:"guides",label:"Show composition guides"},{key:"imageInfo",label:"Show image information"},{key:"followSelection",label:"Follow controller selection"}]
                        UiSwitch { Layout.fillWidth: true; required property var modelData; text: modelData.label; enabled: !dialog.manager.isPinned || modelData.key !== "followSelection"; checked: customization.outputOptions[modelData.key]; onClicked: dialog.option(modelData.key,checked) }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: dialog.manager.isPinned; color: Theme.muted; text: "Pinned output keeps its reference image. Follow, folder-change and timing controls apply after leaving pinned mode." }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted; text: "Linked views require both windows to show the same photo and follow selection. Pinned output keeps its reference. Some compositors prevent window positioning. Actual size shows one image pixel per display pixel. Independent timing presents the latest selection every 3 seconds." }
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
                    UiSwitch { objectName: "monitorLayoutLock"; Layout.fillWidth: true; enabled: !customization.saving; text: "Lock workspace layout"; checked: customization.layoutLocked; onClicked: customization.layoutLocked = checked }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Choose a destination on this page, then detach a pane. Frozen panes remain interactive. Recovery is always available."; color: Theme.muted }
                    Label { objectName: "monitorPaneDestinationLabel"; text: dialog.targetPane.length ? "Destination for " + dialog.targetPane + " pane" : "Pane destination"; color: Theme.text }
                    UiComboBox { id: paneDisplayChoice; objectName: "monitorPaneDestination"; Layout.fillWidth: true; model: dialog.manager.displays; textRole: "label"; valueRole: "id"; Accessible.name: "Pane destination display"; onActivated: { if(dialog.targetPane.length)Qt.callLater(dialog.focusRequestedPane) } }
                    Repeater {
                        id: paneCards
                        model: ["tree","grid","preview","metadata"]
                        ColumnLayout {
                            required property string modelData
                            readonly property var route: customization.paneRoutes[modelData] || ({})
                            property alias detachAction: paneDetachButton
                            Layout.fillWidth: true; spacing: Theme.gapSm
                            Label { text: modelData.charAt(0).toUpperCase()+modelData.slice(1) + (route.detached ? " — detached" : " — controller"); color: Theme.text; font.pixelSize: Theme.textTitle }
                            Flow {
                                Layout.fillWidth: true; spacing: Theme.gapSm
                                UiButton { id: paneDetachButton; objectName: "paneDetach-"+modelData; text: "Detach to selected display"; enabled: !customization.saving && !customization.layoutLocked && !route.frozen && paneDisplayChoice.currentIndex >= 0 && dialog.manager.displays[paneDisplayChoice.currentIndex].canPosition; onClicked: host.invokeTarget("pane-detach-"+modelData,paneDisplayChoice.currentValue) }
                                UiButton { text: "Re-dock"; enabled: !customization.saving && route.detached && !customization.layoutLocked && !route.frozen; onClicked: host.invoke("pane-redock-"+modelData) }
                                UiButton { text: route.frozen ? "Unfreeze" : "Freeze"; enabled: !customization.saving && !customization.layoutLocked; onClicked: host.invoke("pane-freeze-"+modelData) }
                            }
                            Label { visible: route.recovered === true; text: "Preferred display unavailable; controls recovered onscreen."; color: Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "Width"; color: Theme.text }
                                UiTextField { Layout.fillWidth: true; text: route.geometry ? route.geometry.width : "800"; Accessible.name: modelData+" pane width"; enabled: !customization.saving && !customization.layoutLocked && !route.frozen; onEditingFinished: {const r=Object.assign({},route.geometry);r.width=Number(text);customization.updatePaneGeometry(modelData,r)} }
                                Label { text: "Height"; color: Theme.text }
                                UiTextField { Layout.fillWidth: true; text: route.geometry ? route.geometry.height : "600"; Accessible.name: modelData+" pane height"; enabled: !customization.saving && !customization.layoutLocked && !route.frozen; onEditingFinished: {const r=Object.assign({},route.geometry);r.height=Number(text);customization.updatePaneGeometry(modelData,r)} }
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
                    Label { text: "Named presets for this window slot"; color: Theme.text; font.pixelSize: Theme.textTitle }
                    Label { text: "Saved preset"; color: Theme.text }
                    UiComboBox { id: presetChoice; Layout.fillWidth: true; model: customization.presets; displayText: count > 0 ? currentText : "No saved presets"; textRole: "name"; valueRole: "id"; Accessible.name: "Saved preset"; onActivated: { const saved=customization.presets[currentIndex]; if(saved) {presetName.text=saved.name;presetKind.currentIndex=saved.kind === "workspace" ? 1 : 0} } }
                    Label { text: "Preset name"; color: Theme.text }
                    UiTextField { id: presetName; Layout.fillWidth: true; placeholderText: "Preset name"; Accessible.name: "Preset name"; maximumLength: 160 }
                    Label { text: "Preset type"; color: Theme.text }
                    UiComboBox { id: presetKind; model: [{value:"presentation",label:"Presentation"},{value:"workspace",label:"Workspace"}]; textRole: "label"; valueRole: "value"; Accessible.name: "Preset type" }
                    Flow {
                        Layout.fillWidth: true; spacing: Theme.gapSm
                        UiButton { text: "Create preset"; onClicked: customization.savePreset("",presetName.text,presetKind.currentValue) }
                        UiButton { text: "Update snapshot"; enabled: presetChoice.currentIndex >= 0; onClicked: { const saved=customization.presets[presetChoice.currentIndex]; customization.savePreset(saved.id,saved.name,saved.kind) } }
                        UiButton { text: "Rename"; enabled: presetChoice.currentIndex >= 0; onClicked: customization.renamePreset(presetChoice.currentValue,presetName.text) }
                        UiButton { text: "Apply"; enabled: presetChoice.currentIndex >= 0; onClicked: customization.applyPreset(presetChoice.currentValue) }
                        UiButton { text: "Delete preset"; enabled: presetChoice.currentIndex >= 0; onClicked: customization.deletePreset(presetChoice.currentValue) }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Workspace presets include pane placement and lock state. Images are never stored in presets. Applying a preset does not automatically enable output."; color: Theme.muted }
                    UiButton { text: "Reset monitor preferences…"; onClicked: resetConfirm.open() }
                }
            }
        }
        UiButton { objectName: "monitorRecover"; text: "Recover all panes to controller"; Layout.fillWidth: true; onClicked: host.invoke("monitor-recover") }
    }
    UiDialog {
        id: resetConfirm; title: "Reset monitor preferences?"; anchors.centerIn: parent
        width: Math.min(440,dialog.width-Theme.gapLg)
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "This removes this slot’s monitor presets, re-docks panes and clears their layout locks. Original images and other slots are unchanged."; color: Theme.text }
            UiButton { text: "Reset preferences"; onClicked: { customization.resetPreferences(); resetConfirm.close() } }
            UiButton { text: "Cancel"; onClicked: resetConfirm.close() }
        }
    }
}
