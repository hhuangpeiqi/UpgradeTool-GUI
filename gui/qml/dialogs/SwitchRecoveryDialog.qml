import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dialog
    objectName: "switchRecoveryDialog"
    titleText: qsTr("Flash mode switch incomplete")
    width: Math.min(440, Overlay.overlay ? Overlay.overlay.width - 32 : 440)

    Text {
        objectName: "switchRecoveryInstructions"
        width: parent.width
        text: qsTr("Please enter flash mode manually.")
        wrapMode: Text.Wrap
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
    }
    footerContent: RowLayout {
        width: parent.width
        Item { Layout.fillWidth: true }
        PrimaryButton {
            objectName: "switchRecoveryCloseButton"
            focusPolicy: Qt.StrongFocus
            text: qsTr("Got it")
            onClicked: dialog.close()
        }
    }
}
