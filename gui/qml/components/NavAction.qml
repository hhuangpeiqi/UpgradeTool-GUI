import UpgradeTool
import QtQuick

Item {
    id: root

    property string text: ""
    property url iconSource: ""

    signal clicked()

    implicitWidth: row.implicitWidth + 26
    implicitHeight: 38

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: hoverArea.containsMouse ? Theme.hover : "transparent"
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7

        Image {
            source: root.iconSource
            sourceSize.width: 21
            sourceSize.height: 21
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: root.text
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMedium
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
