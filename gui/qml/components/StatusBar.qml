import UpgradeTool
import QtQuick
import QtQuick.Layouts

EdgeRoundedRect {
    id: root

    property string statusText: ""
    property var deviceLabels: []
    property int deviceIndex: -1
    property int progressValue: 0
    property string progressSpeed: ""
    property bool busy: false
    property int cornerRadius: Theme.windowRadius

    signal refreshClicked()
    signal cancelClicked()
    signal deviceActivated(int index)

    implicitHeight: Theme.statusBarHeight
    color: Theme.statusBar
    roundedEdge: "bottom"
    radius: root.cornerRadius

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 14

        Image {
            source: Icons.deviceGray
            sourceSize.width: 22
            sourceSize.height: 22
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            text: root.statusText
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMedium
            Layout.alignment: Qt.AlignVCenter
        }

        Item { Layout.fillWidth: true }

        AppProgressBar {
            Layout.fillWidth: true
            Layout.maximumWidth: 560
            Layout.minimumWidth: 220
            Layout.alignment: Qt.AlignVCenter
            value: root.progressValue / 100.0
        }

        Text {
            text: root.progressValue + "%"
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMedium
            Layout.preferredWidth: 44
            Layout.alignment: Qt.AlignVCenter
            horizontalAlignment: Text.AlignLeft
        }

        Text {
            text: root.progressSpeed
            visible: text.length > 0
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMedium
            elide: Text.ElideRight
            Layout.preferredWidth: 260
            Layout.alignment: Qt.AlignVCenter
            horizontalAlignment: Text.AlignLeft
        }

        SecondaryButton {
            text: qsTr("Cancel")
            enabled: root.busy
            implicitHeight: 34
            hPadding: 16
            onClicked: root.cancelClicked()
        }

        DeviceComboBox {
            Layout.preferredWidth: 300
            model: root.deviceLabels
            currentIndex: root.deviceLabels.length > 0 ? root.deviceIndex : -1
            enabled: root.deviceLabels.length > 0
            onActivated: (index) => root.deviceActivated(index)
        }

        SecondaryButton {
            text: qsTr("Refresh")
            iconSource: Icons.refreshBlue
            implicitHeight: 34
            hPadding: 16
            onClicked: root.refreshClicked()
        }
    }
}
