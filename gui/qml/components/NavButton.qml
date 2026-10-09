import UpgradeTool
import QtQuick

Item {
    id: root

    property string text: ""
    property url iconSource: ""
    property bool selected: false

    signal clicked()

    implicitWidth: row.implicitWidth + 36
    implicitHeight: Theme.navBarHeight

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 6
        anchors.bottomMargin: 8
        radius: 6
        color: (hoverArea.containsMouse && !root.selected) ? Theme.hover : "transparent"
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 9

        Image {
            source: root.iconSource
            sourceSize.width: 21
            sourceSize.height: 21
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: root.text
            color: root.selected ? Theme.primary : Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMedium
            font.bold: root.selected
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 2
        color: Theme.primary
        visible: root.selected
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
