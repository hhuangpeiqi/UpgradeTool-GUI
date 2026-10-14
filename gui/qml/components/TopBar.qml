import UpgradeTool
import QtQuick
import QtQuick.Layouts

Item {
    id: root

    property int currentIndex: 0

    // Qualcomm reuses the first two tabs for a different purpose (partition
    // work and downloading), so the icons follow the tab's meaning per vendor
    // instead of the slot index.
    readonly property url partitionIcon: root.currentIndex === 0
        ? (app.vendor === 2 ? Icons.chipBlue : Icons.downloadBlue)
        : (app.vendor === 2 ? Icons.chipGray : Icons.downloadGray)
    readonly property url downloadIcon: root.currentIndex === 1
        ? (app.vendor === 2 ? Icons.downloadBlue : Icons.chipBlue)
        : (app.vendor === 2 ? Icons.downloadGray : Icons.chipGray)

    signal tabRequested(int index)
    signal configClicked(Item source)
    signal deviceClicked(Item source)
    signal moreClicked(Item source)

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
            id: vendorSelector
            objectName: "vendorSelector"
            Layout.preferredWidth: 170
            model: [qsTr("Rockchip"), qsTr("Allwinner"), qsTr("Qualcomm")]
            currentIndex: app.vendor
            onModelChanged: Qt.callLater(function() {
                vendorSelector.currentIndex = Qt.binding(function() { return app.vendor; });
            })
            enabled: !app.busy
            onActivated: (index) => app.vendor = index
        }

        NavButton {
            text: app.vendor === 2 ? qsTr("Partition Tools") : app.vendor === 0 ? qsTr("Download Image") : qsTr("Raw Image Flash")
            iconSource: root.partitionIcon
            selected: root.currentIndex === 0
            onClicked: root.tabRequested(0)
        }

        NavButton {
            text: app.vendor === 2 ? qsTr("Download") : app.vendor === 0 ? qsTr("Upgrade Firmware") : qsTr("Flash Firmware")
            iconSource: root.downloadIcon
            selected: root.currentIndex === 1
            onClicked: root.tabRequested(1)
        }

        NavButton {
            text: app.vendor === 2 ? qsTr("Tools") : qsTr("Advanced")
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
            id: moreAction
            text: qsTr("More")
            iconSource: Icons.moreGray
            onClicked: root.moreClicked(moreAction)
        }
    }
}
