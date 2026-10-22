import QtQuick
import QtQuick.Controls
import Glancelot

// The canonical choice list. Shares field geometry with UiTextField; the popup uses menu geometry.
ComboBox {
    id: control
    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(176, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: Theme.gapMd
    rightPadding: Theme.gapSm + Theme.iconSize + Theme.gapSm
    font.pixelSize: Theme.textBody
    opacity: enabled ? 1 : Theme.disabledOpacity
    hoverEnabled: true

    contentItem: Label {
        text: control.displayText
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: UiIcon {
        name: "chevron-down"
        color: Theme.muted
        x: control.width - width - Theme.gapSm - Theme.gapXxs
        y: (control.height - height) / 2
    }
    background: Rectangle {
        radius: Theme.radiusControl
        color: control.down ? Theme.pressed : (control.hovered ? Theme.hover : Theme.background)
        border.width: control.visualFocus ? Theme.focusWidth : Theme.borderWidth
        border.color: control.visualFocus ? Theme.focusRing : Theme.border
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: Theme.controlHeight
        leftPadding: Theme.gapSm
        rightPadding: Theme.gapSm
        highlighted: control.highlightedIndex === index
        readonly property string optionText: control.textAt(index)
        Accessible.name: optionText
        contentItem: Row {
            spacing: Theme.gapSm
            UiIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: "check"
                color: Theme.primary
                opacity: control.currentIndex === option.index ? 1 : 0
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: option.optionText
                font.pixelSize: Theme.textBody
                color: Theme.text
            }
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: option.highlighted || option.hovered ? Theme.hover : "transparent"
        }
    }
    popup: Popup {
        y: control.height + Theme.gapXs
        width: control.width
        padding: Theme.gapXs
        implicitHeight: Math.min(contentItem.implicitHeight + 2 * padding, 320)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: UiScrollBar {}
        }
        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.surface
            border.width: Theme.borderWidth
            border.color: Theme.border
        }
    }
}
