import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("About")
    width: Math.min(480, Overlay.overlay ? Overlay.overlay.width - 32 : 800)

    Text {
        width: parent.width
        text: app.toolVersion()
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMedium
        font.bold: true
    }

    Text {
        width: parent.width
        text: qsTr("Firmware flashing tool for embedded devices.")
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
        wrapMode: Text.WordWrap
    }

    footerContent: [
        PrimaryButton {
            text: qsTr("Close")
            onClicked: dlg.close()
        }
    ]
}
