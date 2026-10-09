import UpgradeTool
import QtQuick
import QtQuick.Layouts

Item {
    id: root

    property int currentIndex: 0

    signal tabRequested(int index)
    signal configClicked(Item source)
    signal deviceClicked(Item source)
    signal helpClicked(Item source)

    implicitHeight: Theme.navBarHeight

    Rectangle {
        anchors.fill: parent
        color: Theme.card
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 4

        NavButton {
            text: qsTr("下载镜像")
            iconSource: root.currentIndex === 0 ? Icons.downloadBlue : Icons.downloadGray
            selected: root.currentIndex === 0
            onClicked: root.tabRequested(0)
        }

        NavButton {
            text: qsTr("升级固件")
            iconSource: root.currentIndex === 1 ? Icons.chipBlue : Icons.chipGray
            selected: root.currentIndex === 1
            onClicked: root.tabRequested(1)
        }

        NavButton {
            text: qsTr("高级功能")
            iconSource: root.currentIndex === 2 ? Icons.gearBlue : Icons.gearGray
            selected: root.currentIndex === 2
            onClicked: root.tabRequested(2)
        }

        Item { Layout.fillWidth: true }

        NavAction {
            id: configAction
            text: qsTr("配置")
            iconSource: Icons.gearGray
            onClicked: root.configClicked(configAction)
        }

        NavAction {
            id: deviceAction
            text: qsTr("设备")
            iconSource: Icons.monitorGray
            onClicked: root.deviceClicked(deviceAction)
        }

        NavAction {
            id: helpAction
            text: qsTr("帮助")
            iconSource: Icons.helpGray
            onClicked: root.helpClicked(helpAction)
        }
    }
}
