import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
    function openPage(index, pane) { pages.currentIndex = index; targetPane = pane || ""; open(); Qt.callLater(() => displayChoice.forceActiveFocus()) }
    function option(key, value) { const patch = ({}); patch[key] = value; customization.setOutputOptions(patch) }
    function finishEnable() {
        if(!pendingEnableDisplay.length || customization.feedback.indexOf("Saving monitor") === 0) return
        const target=pendingEnableDisplay; pendingEnableDisplay=""
        if(customization.outputOptions.displayId === target && customization.feedback.length === 0) host.invoke("monitor-enable")
    }
    Connections { target: customization; function onChanged() { dialog.finishEnable() } }
    onClosed: { host.requestActivate(); Qt.callLater(host.focusContent) }
    contentItem: ColumnLayout {
        spacing: Theme.gapMd
        TabBar {
            id: pages
            Layout.fillWidth: true
            Repeater { model: ["Displays", "Presentation", "Panes", "Presets"]; TabButton { required property string modelData; text: modelData; font.pixelSize: Theme.textBody } }
        }
        Label { Layout.fillWidth: true; text: customization.feedback; visible: text.length > 0; wrapMode: Text.Wrap; color: Theme.text; Accessible.role: Accessible.AlertMessage }
        StackLayout {
            currentIndex: pages.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.gapMd
                    Label { text: "Presentation display"; color: Theme.text; font.pixelSize: Theme.textTitle }
                    UiComboBox { id: displayChoice; objectName: "monitorDisplayChoice"; Layout.fillWidth: true; model: dialog.manager.displays; textRole: "name"; valueRole: "id"; Accessible.name: "Presentation display" }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted
                        readonly property var info: dialog.manager.displays[displayChoice.currentIndex] || ({})
                        text: info.name ? info.width + " × " + info.height + " · " + info.devicePixelRatio + "× scale · " + Math.round(info.refreshRate) + " Hz\n" + (info.serialNumber ? "Serial identity: " + info.serialNumber : "Identity may change when similar displays reconnect") + "\n" + info.colorSpace : "No display is available."
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: dialog.manager.outputColorStatus; color: Theme.muted }
                    Flow {
                        Layout.fillWidth: true; spacing: Theme.gapSm
                        UiButton { text: "Identify"; onClicked: host.invoke("monitor-identify") }
                        UiButton { text: "Enable output"; enabled: displayChoice.currentIndex >= 0; onClicked: { dialog.pendingEnableDisplay=displayChoice.currentValue; if(!customization.setOutputOptions({displayId: displayChoice.currentValue}))dialog.pendingEnableDisplay=""; else dialog.finishEnable() } }
                        UiButton { text: "Move output"; enabled: dialog.manager.presentationActive && displayChoice.currentIndex >= 0; onClicked: customization.moveOutput(displayChoice.currentValue) }
                        UiButton { text: "Swap with controller"; enabled: dialog.manager.presentationActive; onClicked: host.invoke("monitor-swap") }
                        UiButton { text: "Disable output"; enabled: dialog.manager.presentationActive; onClicked: host.invoke("monitor-disable") }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Reconnecting a display never enables presentation automatically. Physical color, HDR and refresh accuracy require separate qualification."; color: Theme.muted }
                }
            }
            ScrollView {
                clip: true
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
                            UiComboBox { Layout.fillWidth: true; model: modelData.choices; currentIndex: modelData.choices.indexOf(customization.outputOptions[modelData.key]); Accessible.name: modelData.label; onActivated: dialog.option(modelData.key, currentText) }
                        }
                    }
                    Label { text: "Solid/checker background color (#RRGGBB)"; color: Theme.text }
                    UiTextField { Layout.fillWidth: true; text: customization.outputOptions.backgroundColor; Accessible.name: "Background color"; onEditingFinished: dialog.option("backgroundColor",text) }
                    Repeater {
                        model: [{key:"guides",label:"Show composition guides"},{key:"imageInfo",label:"Show image information"},{key:"followSelection",label:"Follow controller selection"}]
                        UiSwitch { required property var modelData; text: modelData.label; checked: customization.outputOptions[modelData.key]; onClicked: dialog.option(modelData.key,checked) }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted; text: "Actual size is one image pixel per physical output pixel. Independent timing presents the latest selected image every 3 seconds. Pinned images remain fixed." }
                }
            }
            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width; spacing: Theme.gapLg
                    UiSwitch { text: "Lock workspace layout"; checked: customization.layoutLocked; onClicked: customization.layoutLocked = checked }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Choose a display above, then detach a pane. Frozen panes remain interactive. Recovery is always available."; color: Theme.muted }
                    Repeater {
                        model: ["tree","grid","preview","metadata"]
                        ColumnLayout {
                            required property string modelData
                            readonly property var route: customization.paneRoutes[modelData] || ({})
                            Layout.fillWidth: true; spacing: Theme.gapSm
                            Label { text: modelData.charAt(0).toUpperCase()+modelData.slice(1) + (route.detached ? " — detached" : " — controller"); color: Theme.text; font.pixelSize: Theme.textTitle }
                            Flow {
                                Layout.fillWidth: true; spacing: Theme.gapSm
                                UiButton { text: "Detach to selected display"; enabled: !customization.layoutLocked && !route.frozen && displayChoice.currentIndex >= 0; onClicked: customization.detachPane(modelData,displayChoice.currentValue) }
                                UiButton { text: "Re-dock"; enabled: route.detached && !customization.layoutLocked && !route.frozen; onClicked: host.invoke("pane-redock-"+modelData) }
                                UiButton { text: route.frozen ? "Unfreeze" : "Freeze"; enabled: !customization.layoutLocked; onClicked: host.invoke("pane-freeze-"+modelData) }
                            }
                            Label { visible: route.recovered === true; text: "Preferred display unavailable; controls recovered onscreen."; color: Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "Width"; color: Theme.text }
                                UiTextField { Layout.fillWidth: true; text: route.geometry ? route.geometry.width : "800"; Accessible.name: modelData+" pane width"; enabled: !customization.layoutLocked && !route.frozen; onEditingFinished: {const r=Object.assign({},route.geometry);r.width=Number(text);customization.updatePaneGeometry(modelData,r)} }
                                Label { text: "Height"; color: Theme.text }
                                UiTextField { Layout.fillWidth: true; text: route.geometry ? route.geometry.height : "600"; Accessible.name: modelData+" pane height"; enabled: !customization.layoutLocked && !route.frozen; onEditingFinished: {const r=Object.assign({},route.geometry);r.height=Number(text);customization.updatePaneGeometry(modelData,r)} }
                            }
                        }
                    }
                }
            }
            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width; spacing: Theme.gapMd
                    Label { text: "Named presets for this window slot"; color: Theme.text; font.pixelSize: Theme.textTitle }
                    UiComboBox { id: presetChoice; Layout.fillWidth: true; model: customization.presets; textRole: "name"; valueRole: "id"; Accessible.name: "Saved preset" }
                    UiTextField { id: presetName; Layout.fillWidth: true; placeholderText: "Preset name"; Accessible.name: "Preset name"; maximumLength: 160 }
                    UiComboBox { id: presetKind; model: ["presentation","workspace"]; Accessible.name: "Preset type" }
                    Flow {
                        Layout.fillWidth: true; spacing: Theme.gapSm
                        UiButton { text: "Create preset"; onClicked: customization.savePreset("",presetName.text,presetKind.currentText) }
                        UiButton { text: "Update snapshot"; enabled: presetChoice.currentIndex >= 0; onClicked: customization.savePreset(presetChoice.currentValue,presetName.text,presetKind.currentText) }
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
