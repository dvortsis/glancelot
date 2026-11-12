import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// Palette gallery for Settings > Appearance. It does not scroll by itself: the settings page scrolls.
// Arrow keys move between cards, Enter or Space applies one; a click applies immediately.
// E6 (F052/F053): the high-contrast choice leads the gallery, because high contrast replaces the palette's colours.
ColumnLayout {
    id: picker
    property alias searchField: search
    readonly property bool viewerAvailable: typeof viewer !== "undefined" && viewer !== null
    readonly property int columns: width >= 600 ? 4 : (width >= 420 ? 3 : 2)
    readonly property var shown: theme.families.filter(family => family.name.toLowerCase().indexOf(search.text.trim().toLowerCase()) >= 0)
    spacing: Theme.gapMd

    function revealCurrent() {
        if (!cards.currentItem) return
        // The gallery expands to all its rows; its enclosing settings page owns scrolling.
        // GridView cannot reveal its keyboard cursor inside that outer viewport by itself.
        for (let ancestor = parent; ancestor; ancestor = ancestor.parent) {
            if (typeof ancestor.contentY !== "number" || !ancestor.contentItem) continue
            const top = cards.currentItem.mapToItem(ancestor.contentItem, 0, 0).y
            const bottom = top + cards.currentItem.height
            let offset = ancestor.contentY
            if (top < offset) offset = top
            else if (bottom > offset + ancestor.height) offset = bottom - ancestor.height
            ancestor.contentY = Math.max(0, Math.min(offset, Math.max(0, ancestor.contentHeight - ancestor.height)))
            break
        }
    }

    SettingRow {
        label: qsTr("High contrast")
        description: Theme.systemHighContrast
            ? qsTr("Your desktop's high-contrast setting is on, so the interface uses high contrast.")
            : qsTr("Follow system turns high contrast on and off with your desktop's setting.")
        modified: picker.viewerAvailable && viewer.highContrast
        RowLayout {
            id: contrastChoice
            objectName: "highContrastChoice"
            spacing: Theme.gapXs
            enabled: picker.viewerAvailable
            readonly property bool always: picker.viewerAvailable && viewer.highContrast
            UiButton {
                objectName: "highContrastFollowSystem"
                text: qsTr("Follow system")
                selected: !contrastChoice.always
                onClicked: viewer.highContrast = false
                Accessible.name: qsTr("High contrast: follow system")
                Accessible.description: Theme.systemHighContrast ? qsTr("Your desktop's high-contrast setting is on") : qsTr("Your desktop's high-contrast setting is off")
            }
            UiButton {
                objectName: "highContrastAlwaysOn"
                text: qsTr("Always on")
                selected: contrastChoice.always
                onClicked: viewer.highContrast = true
                Accessible.name: qsTr("High contrast: always on")
            }
        }
    }
    Label {
        objectName: "highContrastPaletteNote"
        visible: Theme.highContrast
        Layout.fillWidth: true
        text: qsTr("High contrast is on, so the interface uses black, white and one strong accent. The palette you choose applies when it is off.")
        color: Theme.muted
        font.pixelSize: Theme.textSmall
        wrapMode: Text.Wrap
    }
    UiTextField {
        id: search
        Layout.fillWidth: true
        searchIcon: true
        placeholderText: qsTr("Search %1 palettes").arg(theme.families.length)
        Accessible.name: qsTr("Search palettes")
        Keys.onDownPressed: cards.forceActiveFocus(Qt.TabFocusReason)
    }
    GridView {
        id: cards
        objectName: "paletteGrid"
        Layout.fillWidth: true
        Layout.rightMargin: -Theme.gapSm // each cell carries a trailing gutter; the last one falls outside so edges align
        Layout.preferredHeight: contentHeight
        interactive: false
        activeFocusOnTab: true
        keyNavigationEnabled: true
        cellWidth: width / picker.columns
        cellHeight: 88 + Theme.gapSm
        model: picker.shown
        currentIndex: Math.max(0, picker.shown.findIndex(family => family.id === theme.family))
        onCurrentIndexChanged: if (activeFocus) Qt.callLater(picker.revealCurrent)
        onActiveFocusChanged: if (activeFocus) Qt.callLater(picker.revealCurrent)
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Palettes")
        Keys.onReturnPressed: if (currentItem) currentItem.apply()
        Keys.onEnterPressed: if (currentItem) currentItem.apply()
        Keys.onSpacePressed: if (currentItem) currentItem.apply()
        delegate: Item {
            id: card
            required property int index
            required property var modelData
            // The family's catalog roles plus its derived ones (controlBorder), as the interface would draw them.
            readonly property var preview: theme.preview(modelData.id, Theme.dark)
            readonly property bool selected: theme.family === modelData.id
            readonly property bool cursor: GridView.isCurrentItem && cards.activeFocus
            function apply() { theme.family = modelData.id }
            width: cards.cellWidth
            height: cards.cellHeight
            Accessible.role: Accessible.ListItem
            Accessible.name: selected ? qsTr("%1, selected").arg(modelData.name) : modelData.name
            Accessible.selected: selected
            Accessible.onPressAction: apply()
            Rectangle {
                anchors { fill: parent; rightMargin: Theme.gapSm; bottomMargin: Theme.gapSm }
                radius: Theme.radiusCard
                color: card.preview.background
                border.width: card.selected || card.cursor ? Theme.focusWidth : Theme.borderWidth
                border.color: card.cursor ? Theme.focusRing : (card.selected ? Theme.primary : card.preview.controlBorder)
                HoverHandler { id: cardHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: { cards.currentIndex = card.index; card.apply() } }
                ColumnLayout {
                    anchors { fill: parent; margins: Theme.gapMd }
                    spacing: Theme.gapSm
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gapXs
                        Label {
                            text: card.modelData.name
                            color: card.preview.text
                            font.pixelSize: Theme.textBody
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        UiIcon { visible: card.selected; name: "check"; color: card.preview.primary }
                    }
                    Item { Layout.fillHeight: true }
                    Row {
                        spacing: Theme.gapXs
                        Repeater {
                            model: [card.preview.surface, card.preview.text, card.preview.primary, card.preview.accent, card.preview.muted]
                            delegate: Rectangle {
                                required property string modelData
                                width: 20; height: 20
                                radius: Theme.radiusSmall
                                color: modelData
                                border.width: Theme.borderWidth
                                border.color: card.preview.controlBorder // a swatch is a graphic the user compares: 3:1
                            }
                        }
                    }
                }
                Rectangle { // hover wash in the card's own text colour, legible on any palette
                    anchors.fill: parent
                    radius: parent.radius
                    color: Qt.alpha(card.preview.text, cardHover.hovered ? 0.06 : 0)
                }
            }
        }
    }
    Label {
        visible: picker.shown.length === 0
        text: qsTr("No palettes match “%1”.").arg(search.text.trim())
        color: Theme.muted
        font.pixelSize: Theme.textSmall
    }
}
