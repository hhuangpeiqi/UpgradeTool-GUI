import UpgradeTool
import QtQuick
import QtQuick.Controls
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
            id: statusLabel
            objectName: "statusSummary"
            text: root.statusText
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMedium
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: Math.min(implicitWidth, root.width * 0.25)
            Layout.maximumWidth: root.width * 0.25
            Layout.alignment: Qt.AlignVCenter

            HoverHandler { id: statusHover }
            ToolTip {
                objectName: "statusSummaryTooltip"
                visible: statusHover.hovered && statusLabel.truncated
                text: root.statusText
            }
        }

        Item { Layout.fillWidth: true }

        AppProgressBar {
            objectName: "statusProgress"
            Layout.fillWidth: true
            Layout.maximumWidth: 560
            Layout.minimumWidth: root.width >= 1200 ? 220 : 140
            Layout.alignment: Qt.AlignVCenter
            value: root.progressValue / 100.0
            indeterminate: root.busy && root.progressValue < 0
        }

        Text {
            text: root.progressValue < 0 ? "" : root.progressValue + "%"
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMedium
            Layout.preferredWidth: 44
            Layout.alignment: Qt.AlignVCenter
            horizontalAlignment: Text.AlignLeft
        }

        Text {
            text: root.progressSpeed
            visible: root.width >= 1200
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMedium
            elide: Text.ElideRight
            Layout.preferredWidth: 260
            Layout.alignment: Qt.AlignVCenter
            horizontalAlignment: Text.AlignLeft
        }

        SecondaryButton {
            objectName: "statusCancel"
            text: qsTr("Cancel")
            enabled: root.busy
            implicitHeight: 34
            hPadding: 16
            onClicked: root.cancelClicked()
        }

        DeviceComboBox {
            objectName: "deviceCombo"
            Layout.preferredWidth: root.width >= 1200 ? 300 : 220
            model: root.deviceLabels
            currentIndex: root.deviceLabels.length > 0 ? root.deviceIndex : -1
            enabled: root.deviceLabels.length > 0
            onActivated: (index) => root.deviceActivated(index)
        }

        SecondaryButton {
            objectName: "statusRefresh"
            text: qsTr("Refresh")
            iconSource: Icons.refreshBlue
            implicitHeight: 34
            hPadding: 16
            onClicked: root.refreshClicked()
        }
    }
}
