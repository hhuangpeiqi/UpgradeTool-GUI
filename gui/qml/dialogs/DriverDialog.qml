import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dialog
    objectName: "driverDialog"
    titleText: qsTr("USB Drivers")
    width: Math.min(600, Overlay.overlay ? Overlay.overlay.width - 32 : 600)
    bodyRightInset: 0

    function openDialog() {
        open();
        app.drivers.refresh();
    }

    Text {
        width: parent.width
        visible: app.drivers.devices.length === 0
        text: app.drivers.checking ? qsTr("Checking…") : app.vendor === 2
            ? qsTr("Installation is available when the connected Qualcomm device's driver is missing.")
            : qsTr("No flashing device found")
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
    }

    Repeater {
        model: app.drivers.devices
        delegate: Rectangle {
            id: deviceCard
            required property var modelData
            property bool expanded: false
            width: parent.width
            implicitHeight: deviceContent.implicitHeight + 24
            radius: Theme.controlRadius
            color: Theme.pageBg
            border.color: Theme.border

            ColumnLayout {
                id: deviceContent
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: deviceCard.modelData.label || deviceCard.modelData.identity
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMedium
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                        }
                        Text {
                            text: deviceCard.modelData.stateText + (deviceCard.modelData.service ? " · " + deviceCard.modelData.service : "")
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontNormal
                            color: deviceCard.modelData.attention ? Theme.danger : Theme.textSecondary
                        }
                    }
                    PrimaryButton {
                        objectName: "driverInstallButton"
                        visible: deviceCard.modelData.canInstall
                        text: qsTr("Install…")
                        enabled: !app.interactionLocked && !app.drivers.checking
                        onClicked: app.drivers.prepareInstall(deviceCard.modelData.instance)
                    }
                }

                Text {
                    text: deviceCard.modelData.identity + "  " + (deviceCard.expanded ? "−" : "+")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: Qt.PointingHandCursor
                        onClicked: deviceCard.expanded = !deviceCard.expanded
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: deviceCard.expanded
                    text: [deviceCard.modelData.instance, deviceCard.modelData.location, deviceCard.modelData.diagnostic].filter(function(value) { return value !== ""; }).join("\n")
                    wrapMode: Text.WrapAnywhere
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                }
            }
        }
    }

    RowLayout {
        width: parent.width
        visible: app.drivers.message !== ""
        spacing: 10
        Spinner { visible: app.drivers.busy; running: visible; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
        Text {
            Layout.fillWidth: true
            objectName: "driverProgressText"
            text: app.drivers.progressText
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
        }
    }

    footerContent: RowLayout {
        width: parent.width
        spacing: 10
        SecondaryButton {
            objectName: "driverRefreshButton"
            text: qsTr("Refresh")
            enabled: !app.interactionLocked && !app.drivers.checking
            onClicked: app.drivers.refresh()
        }
        Item { Layout.fillWidth: true }
        PrimaryButton {
            objectName: "qualcommUninstallButton"
            visible: app.vendor === 2
            text: qsTr("Uninstall drivers")
            enabled: !app.interactionLocked && app.drivers.canUninstallQualcomm
            onClicked: app.drivers.prepareQualcommUninstall()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Remove the installed Qualcomm driver package")
        }
        SecondaryButton {
            objectName: "driverCloseButton"
            text: app.drivers.busy ? qsTr("Run in background") : qsTr("Close")
            onClicked: dialog.close()
        }
    }

    Connections {
        target: app.drivers
        function onConfirmationRequested() { confirmation.open(); }
    }

    AppDialog {
        id: confirmation
        parent: dialog.parent
        objectName: "driverConfirmationDialog"
        titleText: app.drivers.confirmationTitle
        width: Math.min(540, Overlay.overlay ? Overlay.overlay.width - 32 : 540)

        Text {
            width: parent.width
            visible: text !== ""
            text: app.drivers.confirmationText
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
        }

        Repeater {
            model: app.drivers.confirmation
            delegate: Text {
                required property var modelData
                width: parent.width
                text: (modelData.label || modelData.identity) + "\n" + modelData.hardwareId
                wrapMode: Text.WrapAnywhere
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
            }
        }
        footerContent: RowLayout {
            width: parent.width
            spacing: 10
            Item { Layout.fillWidth: true }
            SecondaryButton { text: qsTr("Cancel"); onClicked: confirmation.close() }
            PrimaryButton {
                objectName: "driverConfirmButton"
                text: app.drivers.confirmation.length > 0 ? qsTr("Install") : qsTr("Uninstall")
                enabled: !app.interactionLocked && !app.drivers.checking && app.drivers.canConfirm
                onClicked: { confirmation.close(); app.drivers.confirmInstall(); }
            }
        }
    }
}
