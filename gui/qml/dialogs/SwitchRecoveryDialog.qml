import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dialog
    objectName: "switchRecoveryDialog"
    property bool accessProblem: false
    property string accessMessage: ""
    signal driversRequested

    function openFailure() {
        accessProblem = false;
        accessMessage = "";
        open();
    }
    function openAccess(message) {
        accessProblem = true;
        accessMessage = message;
        open();
    }

    titleText: accessProblem ? qsTr("Device access unavailable") : qsTr("Flash mode switch incomplete")
    width: Math.min(440, Overlay.overlay ? Overlay.overlay.width - 32 : 440)

    Text {
        objectName: "switchRecoveryInstructions"
        width: parent.width
        text: dialog.accessProblem ? (dialog.accessMessage.trim() !== "" ? dialog.accessMessage : qsTr("Check the USB driver, access permissions, and whether another tool is using the device.")) : qsTr("Please enter flash mode manually.")
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
    }
    footerContent: [
        SecondaryButton {
            objectName: "switchRecoveryDriverButton"
            visible: dialog.accessProblem && app.drivers.available
            text: qsTr("View USB Drivers")
            onClicked: {
                dialog.close();
                dialog.driversRequested();
            }
        },
        PrimaryButton {
            objectName: "switchRecoveryCloseButton"
            focusPolicy: Qt.StrongFocus
            text: qsTr("Got it")
            onClicked: dialog.close()
        }
    ]
}
