import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// Settings, appearance and the command list in one place. Wide dialogs use a section sidebar; narrow ones
// switch to a scrollable section strip above the content. "Controls" is also the command palette:
// search, run and remap every command from the keyboard.
UiDialog {
    id: dialog
    objectName: "settingsDialog"
    required property var host     // the application window: invoke(), gridAllowsCommand(), fuzzyMatch(), frameless
    required property var photoCanvas
    property string section: "appearance"
    property string bindingMessage: ""
    property bool bindingFailed: false
    readonly property bool wide: availableWidth >= 600
    readonly property var sections: [
        { id: "appearance", label: "Appearance", icon: "appearance" },
        { id: "viewing", label: "Viewing", icon: "eye" },
        { id: "browsing", label: "Browsing", icon: "folder" },
        { id: "controls", label: "Controls", icon: "keyboard" },
        { id: "about", label: "About", icon: "info" }
    ]
    readonly property int sectionIndex: Math.max(0, sections.findIndex(item => item.id === section))
    title: "Settings"

    function openSection(id, focusSearch) {
        if (sections.findIndex(item => item.id === id) >= 0) section = id
        if (!opened) open()
        if (focusSearch) { commandSearch.text = ""; Qt.callLater(() => commandSearch.forceActiveFocus(Qt.ShortcutFocusReason)) }
        else Qt.callLater(focusSection)
    }
    function focusSection() { const button = nav.itemAtIndex(sectionIndex); if (button) button.forceActiveFocus(Qt.TabFocusReason) }
    function commandAvailable(id) { return !host.gridMode || host.gridAllowsCommand(id) }
    function run(id) {
        if (!commandAvailable(id) || id === "settings") return
        if (id === "palette") { openSection("controls", true); return }
        close()
        host.invoke(id)
    }
    function runFirstMatch() {
        for (const command of viewer.commands)
            if (host.fuzzyMatch(command.label, commandSearch.text) && commandAvailable(command.id)) { run(command.id); return }
    }
    onClosed: { commandSearch.text = ""; bindingMessage = "" }

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

        ListView {
            id: nav
            objectName: "settingsNav"
            orientation: dialog.wide ? ListView.Vertical : ListView.Horizontal
            Layout.preferredWidth: dialog.wide ? 200 : -1
            Layout.fillWidth: !dialog.wide
            Layout.fillHeight: dialog.wide
            Layout.preferredHeight: dialog.wide ? -1 : Theme.controlHeight
            spacing: Theme.gapXxs
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: dialog.sections
            currentIndex: dialog.sectionIndex
            Accessible.role: Accessible.PageTabList
            Accessible.name: "Settings sections"
            delegate: UiButton {
                required property var modelData
                required property int index
                width: dialog.wide ? ListView.view.width : implicitWidth
                text: modelData.label
                iconName: dialog.wide ? modelData.icon : "" // the narrow strip keeps every section label visible
                alignment: Qt.AlignLeft
                checkable: true
                checked: dialog.section === modelData.id
                Accessible.role: Accessible.PageTab
                Accessible.selected: checked
                onClicked: dialog.section = modelData.id
                // Up/Down (sidebar) or Left/Right (strip) move between sections, like a tab list.
                Keys.onUpPressed: if (dialog.wide && index > 0) { dialog.section = dialog.sections[index - 1].id; dialog.focusSection() }
                Keys.onDownPressed: if (dialog.wide && index < dialog.sections.length - 1) { dialog.section = dialog.sections[index + 1].id; dialog.focusSection() }
                Keys.onLeftPressed: if (!dialog.wide && index > 0) { dialog.section = dialog.sections[index - 1].id; dialog.focusSection() }
                Keys.onRightPressed: if (!dialog.wide && index < dialog.sections.length - 1) { dialog.section = dialog.sections[index + 1].id; dialog.focusSection() }
            }
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: dialog.sectionIndex

            Page {
                heading: "Appearance"
                SettingRow {
                    label: "Mode"
                    description: "Follow system matches the light or dark setting of your desktop."
                    UiComboBox {
                        model: ["Follow system", "Dark", "Light"]
                        currentIndex: ["system", "dark", "light"].indexOf(theme.mode)
                        onActivated: theme.mode = ["system", "dark", "light"][currentIndex]
                        Accessible.name: "Mode"
                    }
                }
                SectionLabel { text: "Palette" }
                ThemePicker { Layout.fillWidth: true }
                Note {
                    text: theme.error || "Changes apply immediately and are saved. Palettes colour the interface only; photo pixels and the canvas backdrop are unchanged."
                    color: theme.error ? Theme.danger : Theme.muted
                }
            }

            Page {
                heading: "Viewing"
                SettingRow {
                    label: "Fit current photo"
                    description: "Each newly opened photo starts at Shrink only."
                    UiComboBox {
                        readonly property var modes: ["shrink", "fit", "fill", "width", "height", "actual"]
                        model: ["Shrink only", "Fit", "Fill", "Fit width", "Fit height", "Actual size"]
                        currentIndex: modes.indexOf(dialog.photoCanvas.fitMode)
                        displayText: currentIndex < 0 ? "Custom zoom" : currentText
                        enabled: viewer.imageWidth > 0
                        onActivated: { if (modes[currentIndex] === "actual") dialog.photoCanvas.actualSize(); else dialog.photoCanvas.fit(modes[currentIndex]) }
                        Accessible.name: "Fit current photo"
                    }
                }
                SettingRow {
                    label: "Mouse wheel"
                    description: "Ctrl + wheel always zooms at the pointer; Shift + wheel pans sideways."
                    UiComboBox {
                        model: ["Next / previous photo", "Zoom", "Pan"]
                        currentIndex: ["navigate", "zoom", "pan"].indexOf(viewer.wheelMode)
                        onActivated: viewer.wheelMode = ["navigate", "zoom", "pan"][currentIndex]
                        Accessible.name: "Mouse wheel"
                    }
                }
                SettingRow {
                    label: "Canvas backdrop"
                    description: "Steps through the backdrop colours behind the photo" + (viewer.bindingFor("background").length ? " (" + viewer.bindingFor("background") + ")." : ".")
                    UiButton { text: "Next backdrop"; variant: "outline"; onClicked: dialog.host.invoke("background") }
                }
                UiSwitch {
                    Layout.fillWidth: true
                    text: "Nearest-neighbour sampling"
                    description: "Shows individual pixels sharply when zoomed in."
                    checked: dialog.photoCanvas.nearest
                    onToggled: dialog.photoCanvas.nearest = checked
                }
                UiSwitch {
                    Layout.fillWidth: true
                    text: "Frameless window"
                    description: "Hides the system title bar. Drag the top bar to move the window."
                    checked: dialog.host.frameless
                    onToggled: dialog.host.frameless = checked
                }
                Note { text: "Drag to pan. Hold the right mouse button for a 100% loupe. Double-click toggles full screen." }
            }

            Page {
                heading: "Browsing"
                SettingRow {
                    label: "File order"
                    description: "Order of photos inside a folder. Folders always keep natural name order."
                    UiComboBox {
                        model: ["Name", "Date modified", "Size", "Shuffle"]
                        currentIndex: ["name", "modified", "size", "shuffle"].indexOf(viewer.sortKey)
                        onActivated: viewer.sortKey = ["name", "modified", "size", "shuffle"][currentIndex]
                        Accessible.name: "File order"
                    }
                }
                UiSwitch {
                    Layout.fillWidth: true
                    text: "Reverse file order"
                    checked: viewer.sortDescending
                    onToggled: viewer.sortDescending = checked
                }
                SettingRow {
                    label: "At the end of a folder"
                    description: "What Next and Previous do after the last or first photo."
                    UiComboBox {
                        model: ["Stop", "Loop this folder", "Continue through tree"]
                        currentIndex: ["stop", "loop", "continue"].indexOf(viewer.boundaryMode)
                        onActivated: viewer.boundaryMode = ["stop", "loop", "continue"][currentIndex]
                        Accessible.name: "At the end of a folder"
                    }
                }
                UiSwitch {
                    Layout.fillWidth: true
                    text: "Show filmstrip"
                    description: "Thumbnails of the current folder under the top bar."
                    checked: viewer.filmstripVisible
                    onToggled: viewer.filmstripVisible = checked
                }
                UiSwitch {
                    Layout.fillWidth: true
                    text: "Pin folder tree open"
                    description: "Keeps the folder panel visible and makes room for the photo in wider windows."
                    checked: viewer.treePinned
                    onToggled: viewer.treePinned = checked
                }
                SectionLabel { text: "This window" }
                Note {
                    text: viewer.windowSlot > 0 ? "Window " + viewer.windowSlot + " remembers its own last folder and photo, and reopens them the next time you start Glancelot without choosing a file."
                                                : "Position memory is off for this window: the settings folder is busy or unavailable, or it was turned off at launch."
                }
            }

            Page {
                heading: "Controls"
                UiTextField {
                    id: commandSearch
                    objectName: "commandSearch"
                    Layout.fillWidth: true
                    searchIcon: true
                    placeholderText: "Find a command"
                    Accessible.name: "Find a command"
                    Accessible.description: "Enter runs the first match. Down moves to the list."
                    onAccepted: dialog.runFirstMatch()
                    Keys.onDownPressed: { const first = commandRows.firstVisible(); if (first) first.forceActiveFocus(Qt.TabFocusReason) }
                }
                Note { text: "Select a command to run it. Type a shortcut such as Ctrl+O or Shift+Right to remap it; clear a shortcut to unassign it." }
                Note {
                    visible: dialog.host.gridMode
                    text: "Commands that act on the open photo are dimmed while the grid view is showing. Their shortcuts can still be changed."
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
                            readonly property bool available: dialog.commandAvailable(modelData.id)
                            property alias runButton: runButton
                            Layout.fillWidth: true
                            spacing: Theme.gapSm
                            visible: dialog.host.fuzzyMatch(modelData.label, commandSearch.text)
                            UiButton {
                                id: runButton
                                Layout.fillWidth: true
                                alignment: Qt.AlignLeft
                                text: commandRow.modelData.label
                                enabled: commandRow.available && commandRow.modelData.id !== "settings"
                                tip: !commandRow.available ? "Not available in the grid view" : (commandRow.modelData.id === "settings" ? "You are here" : "")
                                Accessible.name: "Run " + commandRow.modelData.label
                                Accessible.description: commandRow.available ? "" : "Not available in the grid view"
                                onClicked: dialog.run(commandRow.modelData.id)
                            }
                            UiTextField {
                                Layout.preferredWidth: 148
                                mono: true
                                text: commandRow.modelData.binding
                                placeholderText: "Unassigned"
                                Accessible.name: "Shortcut for " + commandRow.modelData.label
                                onEditingFinished: {
                                    if (text !== commandRow.modelData.binding) {
                                        const ok = viewer.setBinding(commandRow.modelData.id, text)
                                        dialog.bindingMessage = viewer.status
                                        dialog.bindingFailed = !ok
                                        if (!ok) text = commandRow.modelData.binding
                                    }
                                }
                            }
                        }
                    }
                }
                Note {
                    visible: commandSearch.text.length > 0 && !commandRows.firstVisible()
                    text: "No command matches “" + commandSearch.text + "”."
                }
                UiButton {
                    text: "Restore default shortcuts"
                    variant: "outline"
                    Layout.topMargin: Theme.gapSm
                    onClicked: { viewer.resetBindings(); dialog.bindingMessage = "Default shortcuts restored."; dialog.bindingFailed = false }
                }
            }

            Page {
                heading: "About"
                Label {
                    text: "Glancelot " + Qt.application.version
                    color: Theme.text
                    font.pixelSize: Theme.textBody
                    font.weight: Font.Medium
                }
                Note { text: "A local-first photo viewer. Viewing never changes your photos." }
                SectionLabel { text: "Early alpha" }
                Note { text: "JPEG and PNG, displayed as SDR in sRGB. Monitor profile calibration and native HDR output are not yet qualified." }
                SectionLabel { text: "Type" }
                Note { text: "Interface set in Geist and Geist Mono, under the SIL Open Font License 1.1." }
            }
        }
    }
}
