import UpgradeTool
import QtQuick

Item {
    id: root

    property real value: 0
    property bool indeterminate: false

    implicitHeight: 10

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Theme.track
    }

    Rectangle {
        width: (root.indeterminate ? 0.22 : Math.max(0, Math.min(1, root.value))) * parent.width
        property real motion: 0
        SequentialAnimation on motion {
            running: root.indeterminate && root.visible
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: root.width * 0.78; duration: 900 }
            NumberAnimation { from: root.width * 0.78; to: 0; duration: 900 }
        }
        x: root.indeterminate ? motion : 0
        height: parent.height
        radius: height / 2
        color: Theme.primary
        Behavior on width {
            NumberAnimation { duration: 120 }
        }
    }
}
