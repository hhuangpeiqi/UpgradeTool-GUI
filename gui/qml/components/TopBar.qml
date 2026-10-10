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

        AppComboBox {
            Layout.preferredWidth: 170
            model: [qsTr("Rockchip"), qsTr("Allwinner")]
            currentIndex: app.vendor
            enabled: !app.busy
            onActivated: (index) => app.vendor = index
        }

        NavButton {
            text: app.vendor === 0 ? qsTr("Download Image") : qsTr("Raw Image Flash")
            iconSource: root.currentIndex === 0 ? Icons.downloadBlue : Icons.downloadGray
            selected: root.currentIndex === 0
            onClicked: root.tabRequested(0)
        }

        NavButton {
            text: app.vendor === 0 ? qsTr("Upgrade Firmware") : qsTr("Flash Firmware")
            iconSource: root.currentIndex === 1 ? Icons.chipBlue : Icons.chipGray
            selected: root.currentIndex === 1
            onClicked: root.tabRequested(1)
        }

        NavButton {
            text: qsTr("Advanced")
            iconSource: root.currentIndex === 2 ? Icons.gearBlue : Icons.gearGray
            selected: root.currentIndex === 2
            onClicked: root.tabRequested(2)
        }

        Item { Layout.fillWidth: true }

        NavAction {
            id: configAction
            text: qsTr("Config")
            iconSource: Icons.gearGray
            onClicked: root.configClicked(configAction)
        }

        NavAction {
            id: deviceAction
            text: qsTr("Device")
            iconSource: Icons.monitorGray
            onClicked: root.deviceClicked(deviceAction)
        }

        NavAction {
            id: helpAction
            text: qsTr("Help")
            iconSource: Icons.helpGray
            onClicked: root.helpClicked(helpAction)
        }
    }
}
