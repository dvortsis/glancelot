import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Glancelot

// One context-menu row for a registered command (INPUT-02, F055): its label, its live binding and its enablement come
// from the same host predicate the shortcut uses, so a menu and the keyboard can never disagree. Rows a menu does not
// apply to (`available: false`) take no space.
MenuItem {
    id: item
    required property string commandId
    property var host: null            // the window: scopeAllows(), keyHint(), commandLabel() and invoke()
    property string label: ""          // replaces the registered label, e.g. to name the target of a destructive row
    property string iconName: ""
    property bool danger: false
    property bool available: true
    readonly property string keys: host ? host.keyHint(commandId) : ""
    readonly property bool allowed: host ? host.scopeAllows(commandId) : true
    objectName: "menuItem-" + commandId
    text: label.length ? label : (host ? host.commandLabel(commandId) : commandId)
    visible: available
    enabled: allowed
    height: available ? implicitHeight : 0
    implicitHeight: available ? Theme.controlHeight : 0
    leftPadding: Theme.gapSm; rightPadding: Theme.gapSm
    indicator: null
    Accessible.name: text + (keys.length ? ", " + keys : "")
    onTriggered: if (host) host.invoke(commandId)
    contentItem: RowLayout {
        spacing: Theme.gapSm
        opacity: item.enabled ? 1 : Theme.disabledOpacity
        UiIcon { visible: item.iconName.length > 0; name: item.iconName; color: item.danger ? Theme.danger : Theme.text }
        Label {
            Layout.fillWidth: true
            text: item.text
            color: item.danger ? Theme.danger : Theme.text
            font.pixelSize: Theme.textBody
            elide: Text.ElideRight
        }
        Label {
            visible: item.keys.length > 0
            text: item.keys
            color: Theme.muted
            font.family: Theme.monoFamily
            font.pixelSize: Theme.textSmall
        }
    }
    background: Rectangle { radius: Theme.radiusSmall; color: item.highlighted || item.hovered ? Theme.hover : "transparent" }
}
