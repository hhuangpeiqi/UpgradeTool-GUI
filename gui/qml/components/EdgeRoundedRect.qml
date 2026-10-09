import UpgradeTool
import QtQuick

Rectangle {
    id: root

    property string roundedEdge: "top"

    Rectangle {
        x: 0
        y: root.roundedEdge === "top" ? root.height - root.radius : 0
        width: root.width
        height: root.radius
        color: root.color
    }
}
