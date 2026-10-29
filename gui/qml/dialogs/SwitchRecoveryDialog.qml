import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dialog
    objectName: "switchRecoveryDialog"
    property var prompt: null
    signal driversRequested

    function openFailure() {
        prompt = null;
        open();
    }
    function openAccess(value) {
        prompt = value;
        open();
    }

    titleText: prompt ? (prompt.title || qsTr("Device access unavailable")) : qsTr("Flash mode switch incomplete")
    width: Math.min(440, Overlay.overlay ? Overlay.overlay.width - 32 : 440)

    Text {
        objectName: "switchRecoveryInstructions"
        width: parent.width
        text: dialog.prompt ? (dialog.prompt.message.trim() !== "" ? dialog.prompt.message : qsTr("Check the USB driver, access permissions, and whether another tool is using the device.")) : qsTr("Please enter flash mode manually.")
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
    }
    footerContent: [
        SecondaryButton {
            objectName: "switchRecoverySecondaryDriverButton"
            visible: dialog.prompt !== null && dialog.prompt.offersDrivers && dialog.prompt.offersAcknowledgement
            text: qsTr("View USB Drivers")
            onClicked: {
                dialog.close();
                dialog.driversRequested();
            }
        },
        PrimaryButton {
            objectName: "switchRecoveryDriverButton"
            visible: dialog.prompt !== null && dialog.prompt.offersDrivers && !dialog.prompt.offersAcknowledgement
            focusPolicy: Qt.StrongFocus
            text: dialog.prompt && dialog.prompt.installsDriver ? qsTr("Go to driver installation") : qsTr("View USB Drivers")
            onClicked: {
                dialog.close();
                dialog.driversRequested();
            }
        },
        PrimaryButton {
            objectName: "switchRecoveryCloseButton"
            visible: dialog.prompt === null || dialog.prompt.offersAcknowledgement
            focusPolicy: Qt.StrongFocus
            text: qsTr("Got it")
            onClicked: dialog.close()
        }
    ]
}
