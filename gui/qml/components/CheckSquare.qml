import UpgradeTool
import QtQuick

Item {
    id: root

    property bool checked: false
    property bool enabled: true

    signal toggled(bool checked)

    implicitWidth: 18
    implicitHeight: 18

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: root.checked ? Theme.primary : Theme.card
        border.width: root.checked ? 0 : 1
        border.color: root.enabled ? (mouse.containsMouse ? Theme.primary : Theme.borderStrong)
                                   : Theme.border
        opacity: root.enabled ? 1.0 : 0.45
    }

    Image {
        anchors.centerIn: parent
        width: 12
        height: 12
        source: Icons.checkWhite
        visible: root.checked
        opacity: root.enabled ? 1.0 : 0.45
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
