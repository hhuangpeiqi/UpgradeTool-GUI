import UpgradeTool
import QtQuick

Item {
    id: root

    property real value: 0

    implicitHeight: 10

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Theme.track
    }

    Rectangle {
        width: Math.max(0, Math.min(1, root.value)) * parent.width
        height: parent.height
        radius: height / 2
        color: Theme.primary
        Behavior on width {
            NumberAnimation { duration: 120 }
        }
    }
}
