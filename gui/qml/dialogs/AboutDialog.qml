import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("About")
    width: 480

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
        text: qsTr("Firmware flashing tool for Rockchip and Allwinner devices.\nCredits:\nrockchip-linux/rkdeveloptool (GPL-2.0).\nOpenixCLI libefex (MIT).")
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
        wrapMode: Text.WordWrap
    }

    footerContent: RowLayout {
        width: parent.width
        spacing: 10

        Item { Layout.fillWidth: true }

        PrimaryButton {
            text: qsTr("Close")
            onClicked: dlg.close()
        }
    }
}
