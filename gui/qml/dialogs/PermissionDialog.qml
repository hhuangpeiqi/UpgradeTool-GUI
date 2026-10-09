import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("需要 USB 设备访问权限")
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
            text: qsTr("当前用户（非 root）没有安装 Rockchip udev 规则，插上 Rockusb 设备后可能无法访问。")
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            text: qsTr("点击下方「安装 udev 规则」，按提示输入密码即可修复；也可在终端手动执行：")
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
                text: qsTr("复制")
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
            text: qsTr("安装后若仍提示无权限，请重新插拔设备，再点「重新检测」。")
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
            text: qsTr("暂时忽略")
            onClicked: dlg.close()
        }

        Item { Layout.fillWidth: true }

        SecondaryButton {
            text: qsTr("重新检测")
            enabled: !dlg.installing
            onClicked: app.retryPermission()
        }

        PrimaryButton {
            text: dlg.installing ? qsTr("安装中…") : qsTr("安装 udev 规则")
            enabled: !dlg.installing
            onClicked: {
                dlg.installing = true;
                app.config.installUdev();
            }
        }
    }
}
