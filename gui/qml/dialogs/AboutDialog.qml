import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("关于")
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
        text: qsTr("Rockchip 设备固件烧录工具。\n协议/设备核心派生自 rockchip-linux/rkdeveloptool (GPL-2.0)。")
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
            text: qsTr("关闭")
            onClicked: dlg.close()
        }
    }
}
