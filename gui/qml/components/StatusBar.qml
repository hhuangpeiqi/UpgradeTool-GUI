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
    property string progressText: ""
    property string progressStage: ""
    property string progressSpeed: ""
    property bool busy: false
    property bool closing: false
    property bool hasDevice: false
    property bool hasAdbDevice: false
    property bool hasInterface: false
    property bool driverAttention: false
    property bool driverBusy: false
    property string driverText: ""
    property string driverMessage: ""
    property int driverElapsedSeconds: 0
    property bool controlsLocked: busy
    property int cornerRadius: Theme.windowRadius
    readonly property bool progressVisible: root.busy || root.driverBusy
    readonly property string taskMessage: root.closing ? qsTr("Cancelling tasks and closing…")
        : root.driverBusy ? root.driverMessage
        : root.busy ? (root.progressStage || qsTr("Working…"))
        : root.progressText !== "" ? root.progressText
        : root.driverAttention ? qsTr("Check USB driver details to continue")
        : root.hasInterface ? qsTr("Device interface detected; choose a supported operation")
        : !root.hasDevice && !root.hasAdbDevice ? qsTr("Connect a device to begin")
        : qsTr("Waiting for an operation")

    signal refreshClicked()
    signal cancelClicked()
    signal deviceActivated(int index)
    signal driverClicked()

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
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        height: 42
        spacing: 12

        Rectangle {
            id: statusBadge
            objectName: "deviceStatusBadge"
            implicitHeight: 34
            implicitWidth: statusLabel.implicitWidth + 48
            radius: Theme.controlRadius
            color: root.hasDevice ? Theme.toastOkBg
                                  : root.hasAdbDevice ? Theme.primarySoft : Theme.fieldDisabled
            border.width: 1
            border.color: root.hasDevice ? Theme.toastOkBorder
                                         : root.hasAdbDevice ? Theme.primary : Theme.borderStrong
            Layout.preferredWidth: 220
            Layout.minimumWidth: 220
            Layout.maximumWidth: 220
            Layout.alignment: Qt.AlignVCenter

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Image {
                    source: Icons.deviceGray
                    sourceSize.width: 20
                    sourceSize.height: 20
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    id: statusLabel
                    objectName: "statusSummary"
                    text: root.statusText
                    color: {
                        if (root.hasDevice)
                            return Theme.success;
                        if (root.hasAdbDevice)
                            return Theme.dark ? Qt.lighter(Theme.primary, 1.5) : Theme.primary;
                        return Theme.textSecondary;
                    }
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMedium
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            HoverHandler { id: statusHover }
            ToolTip {
                objectName: "statusSummaryTooltip"
                visible: statusHover.hovered && statusLabel.truncated
                text: root.statusText
            }
        }

        DeviceComboBox {
            objectName: "deviceCombo"
            Layout.fillWidth: true
            Layout.minimumWidth: 240
            model: root.deviceLabels
            currentIndex: root.deviceLabels.length > 0 ? root.deviceIndex : -1
            displayText: count > 0 ? currentText : qsTr("No selectable device")
            enabled: root.deviceLabels.length > 0 && !root.controlsLocked
            onActivated: (index) => root.deviceActivated(index)
            ToolTip.visible: hovered && currentIndex >= 0
            ToolTip.text: currentText
        }

        Button {
            id: driverButton
            objectName: "statusDriver"
            visible: root.driverAttention || root.driverBusy
            enabled: !root.closing
            Layout.preferredWidth: 230
            Layout.minimumWidth: 230
            Layout.maximumWidth: 230
            implicitHeight: 34
            leftPadding: 10
            rightPadding: 10
            topPadding: 0
            bottomPadding: 0
            hoverEnabled: true
            Accessible.name: qsTr("USB Drivers") + ": " + root.driverText
            background: Rectangle {
                radius: Theme.controlRadius
                color: root.driverBusy ? Theme.primarySoft : Theme.warningSoft
                border.width: driverButton.visualFocus ? 2 : 1
                border.color: root.driverBusy ? Theme.primary : Theme.warningBorder
                opacity: driverButton.hovered ? 0.8 : 1
            }
            contentItem: RowLayout {
                spacing: 8
                Spinner {
                    visible: root.driverBusy
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    Layout.alignment: Qt.AlignVCenter
                    size: 14
                }
                Text {
                    visible: !root.driverBusy
                    Layout.alignment: Qt.AlignVCenter
                    text: "!"
                    color: Theme.warning
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMedium
                    font.bold: true
                }
                Text {
                    objectName: "statusDriverText"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignVCenter
                    text: root.driverText
                    color: root.driverBusy ? Theme.primary : Theme.warning
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontNormal
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    text: "›"
                    Layout.alignment: Qt.AlignVCenter
                    color: root.driverBusy ? Theme.primary : Theme.warning
                    font.pixelSize: Theme.fontLarge
                }
            }
            ToolTip.visible: hovered
            ToolTip.text: root.driverText + "\n" + qsTr("View USB driver details")
            onClicked: root.driverClicked()
        }

        SecondaryButton {
            objectName: "statusRefresh"
            Layout.preferredWidth: 96
            Layout.minimumWidth: 96
            Layout.maximumWidth: 96
            enabled: !root.controlsLocked
            text: qsTr("Refresh")
            iconSource: Icons.refreshBlue
            implicitHeight: 34
            hPadding: 10
            onClicked: root.refreshClicked()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        y: 42
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        height: 41
        spacing: 12

        Text {
            id: taskLabel
            objectName: "statusProgressMessage"
            text: root.taskMessage
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            elide: Text.ElideRight
            Layout.fillWidth: !root.progressVisible
            Layout.preferredWidth: root.progressVisible ? 220 : implicitWidth
            Layout.minimumWidth: 0
            Layout.maximumWidth: root.progressVisible ? 220 : Number.POSITIVE_INFINITY
            HoverHandler { id: taskHover }
            ToolTip.visible: taskHover.hovered && truncated
            ToolTip.text: root.taskMessage
        }

        AppProgressBar {
            objectName: "statusProgress"
            visible: root.progressVisible
            Layout.fillWidth: true
            Layout.minimumWidth: 200
            Layout.alignment: Qt.AlignVCenter
            value: root.progressValue / 100.0
            indeterminate: root.driverBusy || (root.busy && root.progressValue < 0)
            HoverHandler { id: progressHover }
            ToolTip {
                objectName: "statusProgressTooltip"
                visible: progressHover.hovered && text !== ""
                text: root.driverBusy || root.closing ? root.taskMessage : root.progressText
            }
        }

        Text {
            objectName: "statusDriverElapsed"
            visible: root.driverBusy
            text: qsTr("%1 s elapsed").arg(root.driverElapsedSeconds)
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            Layout.preferredWidth: implicitWidth
        }

        Text {
            objectName: "statusPercentage"
            visible: root.progressVisible && !root.driverBusy && root.progressValue >= 0
            text: visible ? root.progressValue + "%" : ""
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            Layout.preferredWidth: 52
            horizontalAlignment: Text.AlignRight
        }

        Text {
            objectName: "statusProgressSpeed"
            visible: root.busy && !root.driverBusy && root.progressSpeed !== ""
            text: root.progressSpeed
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            elide: Text.ElideRight
            Layout.preferredWidth: 200
            Layout.minimumWidth: 0
            Layout.maximumWidth: 200
            HoverHandler { id: speedHover }
            ToolTip.visible: speedHover.hovered && truncated
            ToolTip.text: root.progressSpeed
        }

        SecondaryButton {
            objectName: "statusCancel"
            visible: root.busy && !root.driverBusy && !root.closing
            text: qsTr("Cancel")
            Layout.preferredWidth: 96
            Layout.minimumWidth: 96
            Layout.maximumWidth: 96
            implicitHeight: 30
            hPadding: 10
            onClicked: root.cancelClicked()
        }
    }
}
