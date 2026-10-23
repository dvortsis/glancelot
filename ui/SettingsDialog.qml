import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Glancelot

// Settings, appearance and the command list in one place. Wide dialogs use a section sidebar; narrow ones
// switch to a scrollable section strip above the content. "Controls" is also the command palette:
// search, run and remap every command from the keyboard.
UiDialog {
    id: dialog
    objectName: "settingsDialog"
    required property var host     // the application window: invoke(), routeAllows(), fuzzyMatch(), frameless
    required property var photoCanvas
    property string section: "appearance"
    property string bindingMessage: ""
    property bool bindingFailed: false
    property string settingsFeedback: ""
    readonly property bool wide: availableWidth >= 600
    readonly property var sections: [
        { id: "appearance", label: qsTr("Appearance"), icon: "appearance" },
        { id: "viewing", label: qsTr("Viewing"), icon: "eye" },
        { id: "browsing", label: qsTr("Browsing"), icon: "folder" },
        { id: "controls", label: qsTr("Controls"), icon: "keyboard" },
        { id: "about", label: qsTr("About"), icon: "info" }
    ]
    readonly property int sectionIndex: Math.max(0, sections.findIndex(item => item.id === section))
    // STACK-F049: pane layout presets obey the one canonical lock/freeze state (paneLayout, shared with the monitor
    // coordinator) and wait while a display transaction is saving it.
    readonly property var monitor: typeof monitorCustomization !== "undefined" ? monitorCustomization : null
    readonly property bool layoutSaving: monitor !== null && monitor.saving
    readonly property var paneNames: ({ tree: qsTr("Folder tree"), preview: qsTr("Image preview"), grid: qsTr("File grid"),
                                         filmstrip: qsTr("Filmstrip"), topbar: qsTr("Top toolbar"), statusbar: qsTr("Status toolbar") })
    readonly property var frozenPanes: {
        const panes = paneLayout.panes, names = []
        for (const id in panes) if (panes[id].frozen) names.push(paneNames[id] || id)
        return names
    }
    // About 70 characters of small text: a readable line length for descriptions and the controls stacked under them.
    readonly property FontMetrics noteMetrics: FontMetrics { font.pixelSize: Theme.textSmall }
    readonly property real measure: Math.round(noteMetrics.averageCharacterWidth * 70)
    readonly property string savingReason: qsTr("Unavailable while display preferences are saving.")
    readonly property string presetBlockedReason: paneLayout.locked ? qsTr("Unlock the workspace layout to apply a pane layout preset.")
        : frozenPanes.length ? qsTr("Frozen: %1. Unfreeze to apply a pane layout preset.").arg(frozenPanes.join(", "))
        : layoutSaving ? qsTr("Display preferences are saving. Apply the preset when they finish.") : ""
    title: qsTr("Settings")

    function cleanPath(url) {
        let path = url.toString()
        if (path.startsWith("file:///")) {
            path = path.slice(7)
            if (Qt.platform.os === "windows" && path.length > 2 && path.charAt(0) === '/' && path.charAt(2) === ':') {
                path = path.slice(1)
            }
        }
        return path
    }

    function rowMatches(label, desc) {
        if (!settingsSearch.text.trim()) return true
        const q = settingsSearch.text.trim().toLowerCase()
        if (label && label.toLowerCase().indexOf(q) >= 0) return true
        if (desc && desc.toLowerCase().indexOf(q) >= 0) return true
        return host.fuzzyMatch(label || "", q) || (desc ? host.fuzzyMatch(desc, q) : false)
    }

    function sectionMatches(secId) {
        if (!settingsSearch.text.trim()) return true
        const q = settingsSearch.text.trim().toLowerCase()
        const sec = sections.find(s => s.id === secId)
        if (sec && (sec.label.toLowerCase().indexOf(q) >= 0 || host.fuzzyMatch(sec.label, q))) return true
        if (secId === "appearance") {
            return rowMatches("Mode", "Follow system matches the light or dark setting of your desktop")
                || rowMatches("Palette", "Palettes colour the interface")
                || rowMatches("Text size", "Scale interface text and controls")
                || rowMatches("Reduced motion", "Disables non-essential animations")
        }
        if (secId === "viewing") {
            return rowMatches("Fit current photo", "Each newly opened photo starts at Shrink only")
                || rowMatches("Mouse wheel", "Ctrl + wheel always zooms at the pointer")
                || rowMatches("Canvas backdrop", "Steps through the backdrop colours")
                || rowMatches("Nearest-neighbour sampling", "Shows individual pixels sharply")
                || rowMatches("Frameless window", "Hides the system title bar")
        }
        if (secId === "browsing") {
            return rowMatches("Workspace layout", "Arrange panes lock")
                || rowMatches("Pane layout presets", "Review Browse Default saved pane layouts")
                || rowMatches("File order", "Order of photos inside a folder")
                || rowMatches("Reverse file order", "")
                || rowMatches("At the end of a folder", "What Next and Previous do")
                || rowMatches("Show filmstrip", "Thumbnails of the current folder")
                || rowMatches("Pin folder tree open", "Keeps the folder panel visible")
        }
        if (secId === "controls") {
            for (const c of viewer.commands) {
                if (host.fuzzyMatch(c.label, q) || c.label.toLowerCase().indexOf(q) >= 0 || c.id.toLowerCase().indexOf(q) >= 0) return true
            }
            return false
        }
        if (secId === "about") {
            return rowMatches("About", "Glancelot local-first photo viewer")
                || rowMatches("Settings management", "Export import backup reset")
                || rowMatches("Early alpha", "")
                || rowMatches("Type", "Geist fonts")
        }
        return true
    }

    function openSection(id, focusSearch) {
        if (sections.findIndex(item => item.id === id) >= 0) section = id
        if (!opened) open()
        if (focusSearch) { commandSearch.text = ""; Qt.callLater(() => commandSearch.forceActiveFocus(Qt.ShortcutFocusReason)) }
        else Qt.callLater(focusSection)
    }
    function focusSection() { const button = nav.itemAtIndex(sectionIndex); if (button) button.forceActiveFocus(Qt.TabFocusReason) }
    // Settings is already open, so its own row is listed for remapping but is not runnable.
    // INPUT-02: the window's scope data (routeAllows) replaced its grid allow-list; behaviour in the grid view is unchanged.
    function commandRunnable(id) { return id !== "settings" && host.routeAllows(id) }
    // Why a command cannot run right now, shown in visible text on its row (empty when it can run).
    function unavailableReason(id) { return id === "settings" ? qsTr("Open now") : (commandRunnable(id) ? "" : host.routeBlockedReason(id)) }
    function run(id) {
        if (!commandRunnable(id)) return
        if (id === "palette") { openSection("controls", true); return }
        close()
        host.invoke(id)
    }
    function runFirstMatch() {
        for (const command of viewer.commands)
            if (host.fuzzyMatch(command.label, commandSearch.text) && commandRunnable(command.id)) { run(command.id); return }
    }
    onClosed: { settingsSearch.text = ""; commandSearch.text = ""; bindingMessage = ""; settingsFeedback = "" }

    component SectionLabel: Label {
        color: Theme.muted
        font.pixelSize: Theme.textSmall
        font.weight: Font.DemiBold
        Layout.topMargin: Theme.gapSm
        Layout.fillWidth: true
        Accessible.role: Accessible.Heading
    }
    component Note: Label {
        color: Theme.muted
        font.pixelSize: Theme.textSmall
        wrapMode: Text.Wrap
        lineHeight: 1.4
        Layout.fillWidth: true
    }
    component Page: ScrollView {
        id: page
        property string heading
        default property alias body: column.data
        clip: true
        contentWidth: availableWidth
        ScrollBar.vertical: UiScrollBar { parent: page; x: page.width - width; height: page.availableHeight }
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: column
            width: page.availableWidth - Theme.gapMd // keeps rows clear of the scroll bar
            spacing: Theme.gapSm
            Label { // the narrow section strip already names the page
                visible: dialog.wide
                text: page.heading
                color: Theme.text
                font.pixelSize: Theme.textHeading
                font.weight: Font.DemiBold
                font.letterSpacing: -0.2
                Layout.bottomMargin: Theme.gapXs
                Accessible.role: Accessible.Heading
            }
        }
    }

    GridLayout {
        anchors.fill: parent
        columns: dialog.wide ? 2 : 1
        rowSpacing: Theme.gapMd
        columnSpacing: Theme.gapXl

        ColumnLayout {
            id: sidebarCol
            Layout.preferredWidth: dialog.wide ? 200 : -1
            Layout.fillWidth: !dialog.wide
            Layout.fillHeight: dialog.wide
            spacing: Theme.gapSm

            UiTextField {
                id: settingsSearch
                objectName: "settingsSearch"
                Layout.fillWidth: true
                searchIcon: true
                placeholderText: qsTr("Search settings...")
                Accessible.name: qsTr("Search settings")
                Accessible.description: qsTr("Filters visible settings across all sections.")
                onTextChanged: {
                    if (text.trim().length > 0 && !dialog.sectionMatches(dialog.section)) {
                        for (const sec of dialog.sections) {
                            if (dialog.sectionMatches(sec.id)) {
                                dialog.section = sec.id
                                break
                            }
                        }
                    }
                }
            }

            ListView {
                id: nav
                objectName: "settingsNav"
                orientation: dialog.wide ? ListView.Vertical : ListView.Horizontal
                Layout.fillWidth: true
                Layout.fillHeight: dialog.wide
                Layout.preferredHeight: dialog.wide ? -1 : Theme.controlHeight
                spacing: Theme.gapXxs
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: dialog.sections
                currentIndex: dialog.sectionIndex
                Accessible.role: Accessible.PageTabList
                Accessible.name: qsTr("Settings sections")
                delegate: UiButton {
                    required property var modelData
                    required property int index
                    visible: dialog.sectionMatches(modelData.id)
                    width: dialog.wide ? ListView.view.width : implicitWidth
                    text: modelData.label
                    iconName: dialog.wide ? modelData.icon : "" // the narrow strip keeps every section label visible
                    alignment: dialog.wide ? Qt.AlignLeft : Qt.AlignHCenter // leading bar in the sidebar, underline in the strip
                    selected: dialog.section === modelData.id // not checkable: clicking the current section must not clear it
                    Accessible.role: Accessible.PageTab
                    Accessible.selected: selected
                    onClicked: dialog.section = modelData.id
                    // Up/Down (sidebar) or Left/Right (strip) move between sections, like a tab list.
                    Keys.onUpPressed: if (dialog.wide && index > 0) { dialog.section = dialog.sections[index - 1].id; dialog.focusSection() }
                    Keys.onDownPressed: if (dialog.wide && index < dialog.sections.length - 1) { dialog.section = dialog.sections[index + 1].id; dialog.focusSection() }
                    Keys.onLeftPressed: if (!dialog.wide && index > 0) { dialog.section = dialog.sections[index - 1].id; dialog.focusSection() }
                    Keys.onRightPressed: if (!dialog.wide && index < dialog.sections.length - 1) { dialog.section = dialog.sections[index + 1].id; dialog.focusSection() }
                }
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: dialog.sectionIndex

            Page {
                heading: qsTr("Appearance")
                SettingRow {
                    label: qsTr("Mode")
                    description: qsTr("Follow system matches the light or dark setting of your desktop.")
                    visible: dialog.rowMatches(label, description)
                    modified: theme.mode !== "system"
                    UiComboBox {
                        model: [qsTr("Follow system"), qsTr("Dark"), qsTr("Light")]
                        currentIndex: ["system", "dark", "light"].indexOf(theme.mode)
                        onActivated: theme.mode = ["system", "dark", "light"][currentIndex]
                        Accessible.name: qsTr("Mode")
                    }
                }
                SettingRow {
                    label: qsTr("Text size")
                    description: qsTr("Scale interface text and controls proportionally.")
                    visible: dialog.rowMatches(label, description)
                    modified: Math.abs(viewer.textScale - 1.0) > 0.01
                    RowLayout {
                        spacing: Theme.gapXs
                        UiButton {
                            text: "100%"
                            selected: viewer.textScale < 1.15
                            onClicked: viewer.textScale = 1.0
                            Accessible.name: qsTr("100% text scale")
                        }
                        UiButton {
                            text: "125%"
                            selected: viewer.textScale >= 1.15 && viewer.textScale < 1.4
                            onClicked: viewer.textScale = 1.25
                            Accessible.name: qsTr("125% text scale")
                        }
                        UiButton {
                            text: "150%"
                            selected: viewer.textScale >= 1.4
                            onClicked: viewer.textScale = 1.5
                            Accessible.name: qsTr("150% text scale")
                        }
                    }
                }
                UiSwitch {
                    Layout.fillWidth: true
                    text: qsTr("Reduced motion")
                    description: qsTr("Disables non-essential animations across the interface.")
                    visible: dialog.rowMatches(text, description)
                    checked: viewer.reducedMotion
                    onToggled: viewer.reducedMotion = checked
                }
                SectionLabel { text: qsTr("Palette"); visible: dialog.rowMatches("Palette", "Themes and color palettes") }
                ThemePicker { Layout.fillWidth: true; visible: dialog.rowMatches("Palette", "Themes and color palettes") }
                Note {
                    text: theme.error || qsTr("Changes apply immediately and are saved. Palettes colour the interface only; photo pixels and the canvas backdrop are unchanged.")
                    color: theme.error ? Theme.danger : Theme.muted
                    visible: dialog.rowMatches("Palette", text)
                }
                UiButton {
                    text: qsTr("Reset appearance to defaults")
                    variant: "outline"
                    Layout.topMargin: Theme.gapSm
                    visible: dialog.rowMatches(text, "Reset appearance settings to default values")
                    onClicked: {
                        viewer.resetSection("appearance")
                        dialog.settingsFeedback = qsTr("Appearance reset to defaults.")
                    }
                }
            }

            Page {
                heading: qsTr("Viewing")
                SettingRow {
                    label: qsTr("Fit current photo")
                    description: viewer.imageWidth > 0 ? qsTr("Each newly opened photo starts at Shrink only.") : qsTr("Open a photo to change how it fits.")
                    visible: dialog.rowMatches(label, description)
                    UiComboBox {
                        readonly property var modes: ["shrink", "fit", "fill", "width", "height", "actual"]
                        model: [qsTr("Shrink only"), qsTr("Fit"), qsTr("Fill"), qsTr("Fit width"), qsTr("Fit height"), qsTr("Actual size")]
                        currentIndex: modes.indexOf(dialog.photoCanvas.fitMode)
                        displayText: currentIndex < 0 ? qsTr("Custom zoom") : currentText
                        enabled: viewer.imageWidth > 0
                        onActivated: { if (modes[currentIndex] === "actual") dialog.photoCanvas.actualSize(); else dialog.photoCanvas.fit(modes[currentIndex]) }
                        Accessible.name: qsTr("Fit current photo")
                    }
                }
                SettingRow {
                    label: qsTr("Mouse wheel")
                    description: qsTr("Ctrl + wheel always zooms at the pointer; Shift + wheel pans sideways.")
                    visible: dialog.rowMatches(label, description)
                    modified: viewer.wheelMode !== "navigate"
                    UiComboBox {
                        model: [qsTr("Next / previous photo"), qsTr("Zoom"), qsTr("Pan")]
                        currentIndex: ["navigate", "zoom", "pan"].indexOf(viewer.wheelMode)
                        onActivated: viewer.wheelMode = ["navigate", "zoom", "pan"][currentIndex]
                        Accessible.name: qsTr("Mouse wheel")
                    }
                }
                SettingRow {
                    label: qsTr("Canvas backdrop")
                    description: dialog.host.gridMode ? qsTr("Switch to photo view to change the canvas backdrop.")
                        : viewer.bindingFor("background").length ? qsTr("Steps through the backdrop colours behind the photo (%1).").arg(viewer.bindingFor("background"))
                        : qsTr("Steps through the backdrop colours behind the photo.")
                    visible: dialog.rowMatches(label, description)
                    UiButton { text: qsTr("Next backdrop"); variant: "outline"; enabled: !dialog.host.gridMode; onClicked: dialog.host.invoke("background") }
                }
                UiSwitch {
                    Layout.fillWidth: true
                    text: qsTr("Nearest-neighbour sampling")
                    description: qsTr("Shows individual pixels sharply when zoomed in.")
                    visible: dialog.rowMatches(text, description)
                    checked: dialog.photoCanvas.nearest
                    onToggled: dialog.photoCanvas.nearest = checked
                }
                UiSwitch {
                    Layout.fillWidth: true
                    text: qsTr("Frameless window")
                    description: qsTr("Hides the system title bar. Drag the top bar to move the window.")
                    visible: dialog.rowMatches(text, description)
                    checked: dialog.host.frameless
                    onToggled: dialog.host.frameless = checked
                }
                Note {
                    text: qsTr("Drag to pan. Hold the right mouse button for a 100% loupe. Double-click toggles full screen.")
                    visible: dialog.rowMatches("Viewing controls", text)
                }
                UiButton {
                    text: qsTr("Reset viewing to defaults")
                    variant: "outline"
                    Layout.topMargin: Theme.gapSm
                    visible: dialog.rowMatches(text, "Reset viewing settings to default values")
                    onClicked: {
                        viewer.resetSection("viewing")
                        dialog.settingsFeedback = qsTr("Viewing settings reset to defaults.")
                    }
                }
            }

            Page {
                heading: qsTr("Browsing")
                SectionLabel { text: qsTr("Workspace layout") }
                UiButton {
                    objectName: "openMonitorSettings"
                    text: qsTr("Displays and workspaces…")
                    Layout.fillWidth: true
                    onClicked: { dialog.close(); host.invoke("monitor-settings") }
                }
                UiButton {
                    text: qsTr("Arrange panes")
                    onClicked: { dialog.close();dialog.host.layoutEditing=true }
                }
                UiSwitch {
                    objectName: "layoutLock"
                    Layout.fillWidth: true
                    text: qsTr("Lock workspace layout")
                    description: dialog.layoutSaving ? dialog.savingReason : qsTr("Keep pane positions and sizes fixed while using their contents.")
                    checked: paneLayout.locked
                    enabled: !dialog.layoutSaving
                    onToggled: paneLayout.locked = checked
                }
                // Reset also clears the lock and every freeze, so it sits with the lock rather than with one pane's placement.
                UiButton {
                    objectName: "layoutReset"
                    text: qsTr("Reset workspace layout")
                    variant: "outline"
                    enabled: !dialog.layoutSaving
                    Accessible.description: resetNote.text
                    onClicked: paneLayout.reset()
                }
                Note {
                    id: resetNote
                    Layout.maximumWidth: dialog.measure
                    text: dialog.layoutSaving ? dialog.savingReason : qsTr("Restores the default pane placement, sizes and view, and turns off the lock and every freeze.")
                }
                // F049 presets: apply a saved arrangement in one step; save the current one under a name. Named and scoped
                // apart from the per-window display workspace presets behind "Displays and workspaces…". The controls stack
                // under the text so the description keeps a readable line length at every width.
                SectionLabel { objectName: "layoutPresetHeading"; text: qsTr("Pane layout presets") }
                Note {
                    objectName: "layoutPresetScope"
                    Layout.maximumWidth: dialog.measure
                    text: qsTr("Saved for every window; applies to this window's docked panes. Never changes lock, freeze or displays.")
                }
                RowLayout {
                    spacing: Theme.gapSm
                    Layout.fillWidth: true
                    Layout.maximumWidth: dialog.measure
                    UiComboBox {
                        id: presetChoice
                        objectName: "layoutPresetChoice"
                        Layout.fillWidth: true
                        model: paneLayout.presetNames
                        Accessible.name: qsTr("Pane layout preset")
                    }
                    UiButton {
                        objectName: "layoutPresetApply"
                        text: qsTr("Apply")
                        enabled: dialog.presetBlockedReason.length === 0 && presetChoice.currentIndex >= 0
                        Accessible.description: dialog.presetBlockedReason
                        onClicked: paneLayout.applyPreset(presetChoice.currentText)
                    }
                    UiButton {
                        objectName: "layoutPresetDelete"
                        text: qsTr("Delete")
                        variant: "outline"
                        enabled: presetChoice.currentIndex >= 0 && !paneLayout.isBuiltInPreset(presetChoice.currentText)
                        onClicked: paneLayout.deletePreset(presetChoice.currentText)
                    }
                }
                Note {
                    objectName: "layoutPresetBlocked"
                    Layout.maximumWidth: dialog.measure
                    visible: dialog.presetBlockedReason.length > 0
                    text: dialog.presetBlockedReason
                }
                SettingRow {
                    label: qsTr("Save pane layout as")
                    Layout.maximumWidth: dialog.measure
                    RowLayout {
                        spacing: Theme.gapSm
                        UiTextField {
                            id: presetName
                            objectName: "layoutPresetName"
                            implicitWidth: 180
                            placeholderText: qsTr("Pane layout name")
                            maximumLength: 40
                            Accessible.name: qsTr("Name for the new pane layout preset")
                            onAccepted: if (paneLayout.savePreset(text.trim())) text = ""
                        }
                        UiButton {
                            objectName: "layoutPresetSave"
                            text: qsTr("Save")
                            enabled: presetName.text.trim().length > 0
                            onClicked: if (paneLayout.savePreset(presetName.text.trim())) presetName.text = ""
                        }
                    }
                }
                SectionLabel { text: qsTr("Pane placement") }
                SettingRow {
                    label: qsTr("Pane")
                    UiComboBox {
                        id: paneChoice
                        objectName: "layoutPaneChoice"
                        property var ids: ["tree","preview","grid","filmstrip","topbar","statusbar"]
                        model: [qsTr("Folder tree"),qsTr("Image preview"),qsTr("File grid"),qsTr("Filmstrip"),qsTr("Top toolbar"),qsTr("Status toolbar")]
                        Accessible.name: qsTr("Pane to customize")
                    }
                }
                SettingRow {
                    label: qsTr("Position")
                    UiComboBox {
                        objectName: "layoutPosition"
                        property string paneId: paneChoice.ids[paneChoice.currentIndex]
                        property var state: paneLayout.panes[paneId]
                        property var regions: ["left","right","top","bottom","center"]
                        model: [qsTr("Left"),qsTr("Right"),qsTr("Top"),qsTr("Bottom"),qsTr("Center")]
                        currentIndex: regions.indexOf(state.region)
                        enabled: !paneLayout.locked && !state.frozen
                        onActivated: paneLayout.move(paneId,regions[currentIndex],state.order)
                        Accessible.name: qsTr("Pane position")
                    }
                }
                SettingRow {
                    label: qsTr("Preferred size")
                    description: qsTr("Logical pixels; smaller windows constrain the displayed size.")
                    UiTextField {
                        objectName: "layoutExtent"
                        property string paneId: paneChoice.ids[paneChoice.currentIndex]
                        property var state: paneLayout.panes[paneId]
                        implicitWidth: 100
                        text: Math.round(state.extent)
                        enabled: !paneLayout.locked && !state.frozen
                        validator: IntValidator { bottom: 32; top: 4096 }
                        onAccepted: paneLayout.resize(paneId,Number(text))
                        Accessible.name: qsTr("Preferred pane size in logical pixels")
                    }
                }
                UiSwitch {
                    objectName: "layoutFreeze"
                    property string paneId: paneChoice.ids[paneChoice.currentIndex]
                    Layout.fillWidth: true
                    text: qsTr("Freeze this pane")
                    description: dialog.layoutSaving ? dialog.savingReason
                        : paneLayout.locked ? qsTr("Unlock the workspace layout to freeze or unfreeze a pane.")
                        : qsTr("Lock its placement and size and keep it visible.")
                    checked: paneLayout.panes[paneId].frozen
                    enabled: !paneLayout.locked && !dialog.layoutSaving
                    onToggled: paneLayout.freeze(paneId,checked)
                }
                RowLayout {
                    UiButton {
                        text: qsTr("Move earlier")
                        enabled: !paneLayout.locked && !paneLayout.panes[paneChoice.ids[paneChoice.currentIndex]].frozen
                        onClicked: {
                            const id=paneChoice.ids[paneChoice.currentIndex], state=paneLayout.panes[id]
                            paneLayout.move(id,state.region,Math.max(0,state.order-1))
                        }
                    }
                    UiButton {
                        text: qsTr("Move later")
                        enabled: !paneLayout.locked && !paneLayout.panes[paneChoice.ids[paneChoice.currentIndex]].frozen
                        onClicked: {
                            const id=paneChoice.ids[paneChoice.currentIndex], state=paneLayout.panes[id]
                            paneLayout.move(id,state.region,Math.min(99,state.order+1))
                        }
                    }
                }
                Note { visible: paneLayout.feedback.length > 0; text: paneLayout.feedback }

                SettingRow {
                    label: qsTr("File order")
                    description: qsTr("Order of photos inside a folder. Folders always keep natural name order.")
                    visible: dialog.rowMatches(label, description)
                    modified: viewer.sortKey !== "name"
                    UiComboBox {
                        model: [qsTr("Name"), qsTr("Date modified"), qsTr("Size"), qsTr("Shuffle")]
                        currentIndex: ["name", "modified", "size", "shuffle"].indexOf(viewer.sortKey)
                        onActivated: viewer.sortKey = ["name", "modified", "size", "shuffle"][currentIndex]
                        Accessible.name: qsTr("File order")
                    }
                }
                UiSwitch {
                    Layout.fillWidth: true
                    text: qsTr("Reverse file order")
                    visible: dialog.rowMatches(text, "")
                    modified: viewer.sortDescending
                    checked: viewer.sortDescending
                    onToggled: viewer.sortDescending = checked
                }
                SettingRow {
                    label: qsTr("At the end of a folder")
                    description: qsTr("What Next and Previous do after the last or first photo.")
                    visible: dialog.rowMatches(label, description)
                    modified: viewer.boundaryMode !== "continue"
                    UiComboBox {
                        model: [qsTr("Stop"), qsTr("Loop this folder"), qsTr("Continue through tree")]
                        currentIndex: ["stop", "loop", "continue"].indexOf(viewer.boundaryMode)
                        onActivated: viewer.boundaryMode = ["stop", "loop", "continue"][currentIndex]
                        Accessible.name: qsTr("At the end of a folder")
                    }
                }
                UiSwitch {
                    Layout.fillWidth: true
                    text: qsTr("Show filmstrip")
                    description: qsTr("Thumbnails of the current folder under the top bar.")
                    visible: dialog.rowMatches(text, description)
                    modified: !viewer.filmstripVisible
                    checked: viewer.filmstripVisible
                    onToggled: viewer.filmstripVisible = checked
                }
                UiSwitch {
                    Layout.fillWidth: true
                    text: qsTr("Pin folder tree open")
                    description: qsTr("Keeps the folder panel visible and makes room for the photo in wider windows.")
                    visible: dialog.rowMatches(text, description)
                    modified: viewer.treePinned
                    checked: viewer.treePinned
                    onToggled: viewer.treePinned = checked
                }
                SectionLabel { text: qsTr("This window"); visible: dialog.rowMatches("This window", "Window position memory") }
                Note {
                    text: viewer.windowSlot > 0 ? qsTr("Window %1 remembers its own last folder and photo, and reopens them the next time you start Glancelot without choosing a file.").arg(viewer.windowSlot)
                                                : qsTr("Position memory is off for this window: the settings folder is busy or unavailable, or it was turned off at launch.")
                    visible: dialog.rowMatches("This window", text)
                }
                UiButton {
                    text: qsTr("Reset browsing to defaults")
                    variant: "outline"
                    Layout.topMargin: Theme.gapSm
                    visible: dialog.rowMatches(text, "Reset browsing settings to default values")
                    onClicked: {
                        viewer.resetSection("browsing")
                        dialog.settingsFeedback = qsTr("Browsing settings reset to defaults.")
                    }
                }
            }

            Page {
                heading: qsTr("Controls")
                UiTextField {
                    id: commandSearch
                    objectName: "commandSearch"
                    Layout.fillWidth: true
                    searchIcon: true
                    placeholderText: qsTr("Find a command")
                    Accessible.name: qsTr("Find a command")
                    Accessible.description: qsTr("Enter runs the first match. Down moves to the list.")
                    onAccepted: dialog.runFirstMatch()
                    Keys.onDownPressed: { const first = commandRows.firstVisible(); if (first) first.forceActiveFocus(Qt.TabFocusReason) }
                }
                Note { text: qsTr("Select a command to run it. To change a shortcut, activate its box and press the new keys (two presses make a chord such as Ctrl+K, Ctrl+C). Escape cancels; Backspace clears it.") }
                Note {
                    visible: dialog.host.gridMode
                    text: qsTr("Commands that act on the open photo are dimmed while the grid view is showing. Their shortcuts can still be changed.")
                }
                Note {
                    visible: dialog.bindingMessage.length > 0
                    text: dialog.bindingMessage
                    color: dialog.bindingFailed ? Theme.danger : Theme.text
                }
                ColumnLayout {
                    id: commandRows
                    Layout.fillWidth: true
                    spacing: Theme.gapXxs
                    function firstVisible() {
                        for (let i = 0; i < children.length; ++i) if (children[i].visible && children[i].runButton) return children[i].runButton
                        return null
                    }
                    Repeater {
                        model: viewer.commands
                        delegate: RowLayout {
                            id: commandRow
                            required property var modelData
                            readonly property string reason: dialog.unavailableReason(modelData.id)
                            readonly property var settings: dialog // for handlers of nested components, which cannot see this id
                            property alias runButton: runButton
                            Layout.fillWidth: true
                            spacing: Theme.gapSm
                            visible: dialog.host.fuzzyMatch(modelData.label, commandSearch.text)
                            // Unavailable rows stay focusable so keyboard and screen-reader users reach the reason;
                            // run() refuses them.
                            UiButton {
                                id: runButton
                                Layout.fillWidth: true
                                alignment: Qt.AlignLeft
                                text: commandRow.modelData.label
                                foreground: commandRow.reason.length ? Theme.muted : Theme.text // muted, not opacity: keeps 4.5:1
                                Accessible.name: commandRow.reason.length ? commandRow.modelData.label : qsTr("Run %1").arg(commandRow.modelData.label)
                                Accessible.description: commandRow.reason
                                onClicked: dialog.run(commandRow.modelData.id)
                            }
                            Label {
                                visible: commandRow.reason.length > 0
                                text: commandRow.reason
                                color: Theme.muted
                                font.pixelSize: Theme.textSmall
                                Accessible.ignored: true // already the button's description
                            }
                            // F051: press the keys to record; a refused shortcut explains why and nothing is saved.
                            ShortcutRecorder {
                                objectName: "shortcutRecorder-" + commandRow.modelData.id
                                Layout.preferredWidth: 168
                                commandId: commandRow.modelData.id
                                commandLabel: commandRow.modelData.label
                                binding: commandRow.modelData.binding
                                // Saved after this handler returns: a saved binding rebuilds the command list, which destroys this
                                // very row, and that must not happen in the middle of its own signal.
                                onCommitted: sequence => {
                                    const id = commandRow.modelData.id, label = commandRow.modelData.label, owner = commandRow.settings
                                    Qt.callLater(() => {
                                        const ok = viewer.setBinding(id, sequence)
                                        owner.bindingMessage = ok ? (sequence.length ? qsTr("Shortcut for “%1” is now %2.").arg(label).arg(sequence) : qsTr("Shortcut for “%1” cleared.").arg(label)) : viewer.status
                                        owner.bindingFailed = !ok
                                    })
                                }
                                onRefused: reason => { commandRow.settings.bindingMessage = reason; commandRow.settings.bindingFailed = true }
                            }
                        }
                    }
                }
                Note {
                    visible: commandSearch.text.length > 0 && !commandRows.firstVisible()
                    text: qsTr("No command matches “%1”.").arg(commandSearch.text)
                }
                UiButton {
                    text: qsTr("Restore default shortcuts")
                    variant: "outline"
                    Layout.topMargin: Theme.gapSm
                    onClicked: { viewer.resetBindings(); dialog.bindingMessage = qsTr("Default shortcuts restored."); dialog.bindingFailed = false }
                }
            }

            Page {
                heading: qsTr("About")
                Label {
                    text: qsTr("Glancelot %1").arg(Qt.application.version)
                    color: Theme.text
                    font.pixelSize: Theme.textBody
                    font.weight: Font.Medium
                    visible: dialog.rowMatches("Glancelot", "About")
                }
                Note {
                    text: qsTr("A local-first photo viewer. Viewing never changes your photos.")
                    visible: dialog.rowMatches("About", text)
                }
                SectionLabel {
                    text: qsTr("Settings management")
                    visible: dialog.rowMatches("Settings management", "Export import backup restore reset")
                }
                Note {
                    text: qsTr("Export settings to a portable JSON file for backup or transfer between computers, or import settings to restore them.")
                    visible: dialog.rowMatches("Settings management", text)
                }
                RowLayout {
                    spacing: Theme.gapSm
                    visible: dialog.rowMatches("Settings management", "Export import backup reset")
                    UiButton {
                        text: qsTr("Export settings...")
                        variant: "outline"
                        onClicked: exportDialog.open()
                    }
                    UiButton {
                        text: qsTr("Import settings...")
                        variant: "outline"
                        onClicked: importDialog.open()
                    }
                }
                UiButton {
                    text: qsTr("Reset all settings to defaults...")
                    variant: "danger-quiet"
                    Layout.topMargin: Theme.gapXs
                    visible: dialog.rowMatches("Reset all settings to defaults", "Restore default settings")
                    onClicked: confirmResetDialog.open()
                }
                Note {
                    visible: dialog.settingsFeedback.length > 0
                    text: dialog.settingsFeedback
                    color: Theme.accent
                }
                SectionLabel {
                    text: qsTr("Early alpha")
                    visible: dialog.rowMatches("Early alpha", "")
                }
                Note {
                    text: qsTr("JPEG and PNG, displayed as SDR in sRGB. Monitor profile calibration and native HDR output are not yet qualified.")
                    visible: dialog.rowMatches("Early alpha", text)
                }
                SectionLabel {
                    text: qsTr("Type")
                    visible: dialog.rowMatches("Type", "Geist fonts")
                }
                Note {
                    text: qsTr("Interface set in Geist and Geist Mono, under the SIL Open Font License 1.1.")
                    visible: dialog.rowMatches("Type", text)
                }
            }
        }
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export Settings")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("JSON settings files (*.json)"), qsTr("All files (*)")]
        defaultSuffix: "json"
        onAccepted: {
            const path = dialog.cleanPath(selectedFile)
            if (viewer.exportSettings(path)) {
                dialog.settingsFeedback = qsTr("Settings exported successfully to %1").arg(path)
            } else {
                dialog.settingsFeedback = viewer.status || qsTr("Could not export settings.")
            }
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import Settings")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("JSON settings files (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            const path = dialog.cleanPath(selectedFile)
            if (viewer.importSettings(path)) {
                dialog.settingsFeedback = qsTr("Settings imported successfully from %1").arg(path)
            } else {
                dialog.settingsFeedback = viewer.status || qsTr("Could not import settings.")
            }
        }
    }

    UiDialog {
        id: confirmResetDialog
        title: qsTr("Reset all settings")
        width: Math.min(480, dialog.width - 2 * Theme.gapLg)
        ColumnLayout {
            spacing: Theme.gapMd
            anchors.fill: parent
            Label {
                text: qsTr("Restore all settings to their original defaults? This will reset your theme, keyboard shortcuts, workspace layout, and preferences.")
                color: Theme.text
                font.pixelSize: Theme.textBody
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.gapSm
                UiButton {
                    text: qsTr("Cancel")
                    variant: "outline"
                    onClicked: confirmResetDialog.close()
                }
                UiButton {
                    text: qsTr("Reset everything")
                    variant: "danger"
                    onClicked: {
                        confirmResetDialog.close()
                        if (viewer.resetAllSettings()) {
                            dialog.settingsFeedback = qsTr("All settings have been reset to defaults.")
                        } else {
                            dialog.settingsFeedback = viewer.status || qsTr("Could not reset settings.")
                        }
                    }
                }
            }
        }
    }
}
