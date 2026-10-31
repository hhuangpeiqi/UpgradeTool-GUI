import UpgradeTool
import QtQuick
import QtQuick.Layouts

Item {
    id: root

    property int currentIndex: 0

    // Use the same page icons for every vendor.
    readonly property url partitionIcon: root.currentIndex === 0
        ? Icons.chipBlue : Icons.chipGray
    readonly property url downloadIcon: root.currentIndex === 1
        ? Icons.downloadBlue : Icons.downloadGray

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
            model: app.availableVendors.map(function(vendor) { return [qsTr("Rockchip"), qsTr("Allwinner"), qsTr("Qualcomm")][vendor]; })
            currentIndex: app.availableVendors.indexOf(app.vendor)
            onModelChanged: Qt.callLater(function() {
                vendorSelector.currentIndex = Qt.binding(function() { return app.availableVendors.indexOf(app.vendor); });
            })
            enabled: !app.interactionLocked
            onActivated: (index) => app.vendor = app.availableVendors[index]
        }

        // Keep page IDs stable for --tab and per-vendor page history.
        NavButton {
            text: qsTr("Flash Firmware")
            iconSource: root.downloadIcon
            selected: root.currentIndex === 1
            onClicked: root.tabRequested(1)
        }

        NavButton {
            text: app.vendor === 2 ? qsTr("Partition Tools") : qsTr("Raw Image Flash")
            iconSource: root.partitionIcon
            selected: root.currentIndex === 0
            onClicked: root.tabRequested(0)
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
            id: moreAction
            text: qsTr("More")
            iconSource: Icons.moreGray
            onClicked: root.moreClicked(moreAction)
        }
    }
}
