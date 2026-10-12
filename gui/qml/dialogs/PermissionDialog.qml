import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("USB device access permission required")
    width: 540
    closePolicy: Popup.NoAutoClose

    property bool installing: false

    onClosed: app.ignorePermission()

    Connections {
        target: app.config
        function onUdevInstallFinished(ok, message) {
            dlg.installing = false;
        }
    }

    Column {
        width: parent.width
        spacing: 12

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            text: app.vendor === 0 ? qsTr("The current user (non-root) has not installed the Rockchip udev rules; the device may be inaccessible.") : qsTr("The current user (non-root) has not installed the Allwinner FEL/FES udev rules; the device may be inaccessible.")
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            text: qsTr("Click \"Install udev rules\" below and enter the password when prompted; or run manually in a terminal:")
        }

        Row {
            width: parent.width
            spacing: 8

            Rectangle {
                width: parent.width - copyBtn.width - 8
                implicitHeight: cmdEdit.implicitHeight + 16
                radius: Theme.controlRadius
                color: Theme.card
                border.width: 1
                border.color: Theme.border

                TextEdit {
                    id: cmdEdit
                    anchors.fill: parent
                    anchors.margins: 8
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    text: app.config.udevCommand()
                    color: Theme.textPrimary
                    selectionColor: Theme.primary
                    selectedTextColor: Theme.textOnPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                }
            }

            SecondaryButton {
                id: copyBtn
                text: qsTr("Copy")
                onClicked: {
                    cmdEdit.selectAll();
                    cmdEdit.copy();
                }
            }
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            text: qsTr("If permission errors persist after installing, reconnect the device and click \"Retry\".")
        }

        Text {
            width: parent.width
            visible: app.config.status !== ""
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            text: app.config.status
        }
    }

    footerContent: RowLayout {
        width: parent.width
        spacing: 10

        SecondaryButton {
            text: qsTr("Ignore for now")
            onClicked: dlg.close()
        }

        Item { Layout.fillWidth: true }

        SecondaryButton {
            text: qsTr("Retry")
            enabled: !dlg.installing
            onClicked: app.retryPermission()
        }

        PrimaryButton {
            text: dlg.installing ? qsTr("Installing…") : qsTr("Install udev rules")
            reserveText: dlg.installing ? qsTr("Install udev rules") : qsTr("Installing…")
            enabled: !dlg.installing
            onClicked: {
                dlg.installing = true;
                app.config.installUdev();
            }
        }
    }
}
