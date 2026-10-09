import UpgradeTool
import QtQuick

Item {
    id: root

    property url icon: Icons.folderGray
    property string title: ""
    property string subtitle: ""

    Column {
        anchors.centerIn: parent
        width: Math.min(root.width - 20, 360)
        spacing: 6

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 156
            height: 108
            radius: 54
            color: Theme.thumbBg

            Image {
                anchors.centerIn: parent
                source: root.icon
                sourceSize.width: 58
                sourceSize.height: 58
            }
        }

        Item { width: 1; height: 8 }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMedium
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.subtitle
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
        }
    }
}
