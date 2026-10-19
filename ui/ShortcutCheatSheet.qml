import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// In-app keyboard shortcut cheat sheet and discovery modal (SHORTCUTS-HELP-01 / F056).
// Displays categorized cards of core viewer workflows with dynamic keybindings
// queried directly from viewer.bindingFor(). Includes instant live search and a
// one-click bridge to the Settings dialog shortcuts remapper.
UiDialog {
    id: dialog
    objectName: "shortcutCheatSheet"
    required property var host

    title: "Keyboard Shortcuts"
    implicitWidth: Math.min(parent ? parent.width - 48 : 820, 840)
    implicitHeight: Math.min(parent ? parent.height - 48 : 640, 680)

    property string filterText: searchField.text.trim().toLowerCase()

    readonly property var categories: [
        {
            name: "Navigation",
            icon: "chevron-right",
            items: [
                { id: "next", label: "Next image", defaultKeys: "Right" },
                { id: "previous", label: "Previous image", defaultKeys: "Left" },
                { id: "next-folder", label: "Next folder", defaultKeys: "PgDown" },
                { id: "previous-folder", label: "Previous folder", defaultKeys: "PgUp" },
                { id: "navigate-back", label: "Back in folder history", defaultKeys: "Alt+Left" },
                { id: "navigate-forward", label: "Forward in folder history", defaultKeys: "Alt+Right" },
                { id: "open", label: "Open image file", defaultKeys: "Ctrl+O" }
            ]
        },
        {
            name: "Canvas & Viewing",
            icon: "eye",
            items: [
                { id: "fit", label: "Fit image (shrink only)", defaultKeys: "F" },
                { id: "actual", label: "Actual size (1:1)", defaultKeys: "1" },
                { id: "zoom-in", label: "Zoom in", defaultKeys: "+" },
                { id: "zoom-out", label: "Zoom out", defaultKeys: "-" },
                { id: "rotate", label: "Rotate clockwise", defaultKeys: "R" },
                { id: "background", label: "Cycle background tone", defaultKeys: "B" },
                { id: "fullscreen", label: "Toggle fullscreen", defaultKeys: "F11" },
                { id: "frameless", label: "Toggle window frame", defaultKeys: "Ctrl+F11" }
            ]
        },
        {
            name: "Panes & Workspaces",
            icon: "split",
            items: [
                { id: "grid", label: "Toggle folder grid", defaultKeys: "G" },
                { id: "filmstrip", label: "Toggle filmstrip", defaultKeys: "T" },
                { id: "tree", label: "Toggle folder tree", defaultKeys: "Ctrl+B" },
                { id: "view-combined", label: "Combined grid and preview", defaultKeys: "Ctrl+Shift+G" },
                { id: "sort", label: "Cycle sort order", defaultKeys: "S" },
                { id: "sort-reverse", label: "Reverse sort order", defaultKeys: "Shift+S" },
                { id: "palette", label: "Command palette", defaultKeys: "Ctrl+Shift+P" },
                { id: "settings", label: "Settings dialog", defaultKeys: "Ctrl+," }
            ]
        },
        {
            name: "Selection & Files",
            icon: "folder",
            items: [
                { id: "select-all", label: "Select all in grid", defaultKeys: "Ctrl+A" },
                { id: "clear-selection", label: "Clear selection", defaultKeys: "Ctrl+D" },
                { id: "invert-selection", label: "Invert selection", defaultKeys: "Ctrl+I" },
                { id: "grid-zoom-in", label: "Zoom in grid thumbnails", defaultKeys: "Ctrl+=" },
                { id: "grid-zoom-out", label: "Zoom out grid thumbnails", defaultKeys: "Ctrl+-" },
                { id: "delete-selection", label: "Delete selection", defaultKeys: "Delete" },
                { id: "delete-current-file", label: "Delete current photo", defaultKeys: "Ctrl+Delete" },
                { id: "delete-current-folder", label: "Delete current folder", defaultKeys: "Ctrl+Shift+Delete" },
                { id: "copy-path", label: "Copy file path", defaultKeys: "Ctrl+C" },
                { id: "copy-file", label: "Copy file URL", defaultKeys: "Ctrl+Shift+C" },
                { id: "copy-image", label: "Copy image bitmap", defaultKeys: "Ctrl+Alt+C" },
                { id: "reveal-in-file-manager", label: "Reveal in File Manager", defaultKeys: "Ctrl+Alt+R" },
                { id: "open-terminal", label: "Open Terminal", defaultKeys: "Ctrl+Alt+T" }
            ]
        }
    ]

    function activeBinding(cmdId, defaultKey) {
        if (typeof viewer !== "undefined" && viewer && viewer.bindingFor) {
            const b = viewer.bindingFor(cmdId)
            if (b && b.length > 0) return b
        }
        return defaultKey || ""
    }

    function itemMatches(item) {
        if (!dialog.filterText) return true
        const labelMatch = item.label.toLowerCase().indexOf(dialog.filterText) >= 0
        const idMatch = item.id.toLowerCase().indexOf(dialog.filterText) >= 0
        const key = activeBinding(item.id, item.defaultKeys).toLowerCase()
        const keyMatch = key.indexOf(dialog.filterText) >= 0
        return labelMatch || idMatch || keyMatch
    }

    function categoryMatchCount(cat) {
        let count = 0
        for (let i = 0; i < cat.items.length; ++i) {
            if (itemMatches(cat.items[i])) count++
        }
        return count
    }

    function totalMatchCount() {
        let total = 0
        for (let i = 0; i < categories.length; ++i) {
            total += categoryMatchCount(categories[i])
        }
        return total
    }

    onOpened: {
        searchField.text = ""
        Qt.callLater(() => searchField.forceActiveFocus())
    }

    contentItem: ColumnLayout {
        spacing: Theme.gapMd

        // Top Search Bar & Subtitle
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapSm

            UiTextField {
                id: searchField
                searchIcon: true
                placeholderText: "Search shortcuts (e.g. zoom, grid, copy, delete)..."
                Layout.fillWidth: true
                Accessible.name: "Search shortcuts"

                Keys.onEscapePressed: {
                    if (text.length > 0) {
                        text = ""
                    } else {
                        dialog.close()
                    }
                }

                UiButton {
                    visible: searchField.text.length > 0
                    iconName: "close"
                    iconOnly: true
                    tip: "Clear search"
                    anchors {
                        right: parent.right
                        rightMargin: Theme.gapXs
                        verticalCenter: parent.verticalCenter
                    }
                    implicitWidth: 24
                    implicitHeight: 24
                    onClicked: {
                        searchField.text = ""
                        searchField.forceActiveFocus()
                    }
                }
            }
        }

        // Main Scrollable Area with Cards
        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical: UiScrollBar {}
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Flickable {
                contentWidth: width
                contentHeight: cardLayout.implicitHeight

                ColumnLayout {
                    id: cardLayout
                    width: parent.width
                    spacing: Theme.gapLg

                    // Empty state when filter matches nothing
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: 180
                        visible: dialog.totalMatchCount() === 0

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: Theme.gapSm

                            UiIcon {
                                name: "search"
                                size: 32
                                color: Theme.muted
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Label {
                                text: "No shortcuts match \"" + searchField.text + "\""
                                color: Theme.text
                                font.pixelSize: Theme.textTitle
                                font.weight: Font.DemiBold
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Label {
                                text: "Try searching for an action like 'zoom', 'folder', or a key like 'Ctrl'."
                                color: Theme.muted
                                font.pixelSize: Theme.textBody
                                Layout.alignment: Qt.AlignHCenter
                            }

                            UiButton {
                                text: "Clear filter"
                                Layout.alignment: Qt.AlignHCenter
                                Layout.topMargin: Theme.gapSm
                                onClicked: {
                                    searchField.text = ""
                                    searchField.forceActiveFocus()
                                }
                            }
                        }
                    }

                    // 2-Column Responsive Grid of Category Cards
                    GridLayout {
                        id: catGrid
                        visible: dialog.totalMatchCount() > 0
                        Layout.fillWidth: true
                        columns: dialog.width >= 620 ? 2 : 1
                        columnSpacing: Theme.gapMd
                        rowSpacing: Theme.gapMd

                        Repeater {
                            model: dialog.categories

                            delegate: Rectangle {
                                required property var modelData
                                readonly property int matches: dialog.categoryMatchCount(modelData)
                                visible: matches > 0
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                implicitHeight: cardCol.implicitHeight + Theme.gapLg * 2
                                radius: Theme.radiusCard
                                color: Theme.background
                                border.width: Theme.borderWidth
                                border.color: Theme.border

                                ColumnLayout {
                                    id: cardCol
                                    anchors {
                                        top: parent.top
                                        left: parent.left
                                        right: parent.right
                                        margins: Theme.gapLg
                                    }
                                    spacing: Theme.gapSm

                                    // Category Header
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.gapSm

                                        UiIcon {
                                            name: modelData.icon
                                            size: 16
                                            color: Theme.primary
                                        }

                                        Label {
                                            text: modelData.name
                                            color: Theme.text
                                            font.pixelSize: Theme.textTitle
                                            font.weight: Font.DemiBold
                                            Layout.fillWidth: true
                                        }

                                        Label {
                                            visible: dialog.filterText.length > 0
                                            text: matches + " match" + (matches === 1 ? "" : "es")
                                            color: Theme.muted
                                            font.pixelSize: Theme.textCaption
                                        }
                                    }

                                    // Category Divider
                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: 1
                                        color: Theme.border
                                        Layout.topMargin: 2
                                        Layout.bottomMargin: 4
                                    }

                                    // Item List
                                    Repeater {
                                        model: modelData.items

                                        delegate: RowLayout {
                                            required property var modelData
                                            visible: dialog.itemMatches(modelData)
                                            Layout.fillWidth: true
                                            spacing: Theme.gapSm
                                            implicitHeight: Theme.rowHeight

                                            Label {
                                                text: modelData.label
                                                color: Theme.text
                                                font.pixelSize: Theme.textBody
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                                Layout.alignment: Qt.AlignVCenter
                                            }

                                            // Key badge sequence
                                            RowLayout {
                                                spacing: 4
                                                Layout.alignment: Qt.AlignVCenter

                                                readonly property string binding: dialog.activeBinding(modelData.id, modelData.defaultKeys)
                                                readonly property var keys: binding.length > 0 ? binding.split("+") : ["unbound"]

                                                Repeater {
                                                    model: keys

                                                    delegate: RowLayout {
                                                        required property var modelData
                                                        required property int index
                                                        spacing: 4

                                                        Rectangle {
                                                            implicitHeight: 22
                                                            implicitWidth: keyLabel.implicitWidth + 12
                                                            radius: Theme.radiusSmall
                                                            color: Theme.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.05)
                                                            border.width: Theme.borderWidth
                                                            border.color: Theme.border

                                                            Label {
                                                                id: keyLabel
                                                                anchors.centerIn: parent
                                                                text: modelData
                                                                color: modelData === "unbound" ? Theme.muted : Theme.text
                                                                font.family: Theme.monoFamily
                                                                font.pixelSize: Theme.textCaption
                                                                font.weight: Font.Medium
                                                            }
                                                        }

                                                        Label {
                                                            visible: index < keys.length - 1
                                                            text: "+"
                                                            color: Theme.muted
                                                            font.pixelSize: Theme.textCaption
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Footer Row: Hints & Customization Bridge
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.gapXs
            spacing: Theme.gapMd

            UiIcon {
                name: "info"
                size: 14
                color: Theme.muted
            }

            Label {
                text: "Press ? or F1 anytime to open • Esc to close"
                color: Theme.muted
                font.pixelSize: Theme.textCaption
                Layout.fillWidth: true
            }

            UiButton {
                text: "Customize shortcuts in Settings…"
                iconName: "keyboard"
                tip: "Open full shortcut customization table in Settings (Ctrl+,)"
                onClicked: {
                    dialog.close()
                    if (host && host.openSettings) {
                        host.openSettings("controls", true)
                    } else if (host && host.handleViewCommand) {
                        host.handleViewCommand("settings")
                    }
                }
            }

            UiButton {
                text: "Done"
                onClicked: dialog.close()
            }
        }
    }
}

