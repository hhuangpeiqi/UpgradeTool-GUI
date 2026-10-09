import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("使用帮助")
    width: 640

    ScrollView {
        width: parent.width
        height: 460
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Column {
            width: parent.width
            spacing: 18

            Text {
                width: parent.width
                text: qsTr("选择设备后，在对应功能页执行操作。设备需处于 Rockusb 模式（Loader 或 Maskrom），"
                          + "请确认 USB 连接正常。")
                      + (Qt.platform.os === "linux" ? qsTr("系统需安装 udev 规则。") : "")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: app.usageHelp()

                delegate: Column {
                    width: parent.width
                    spacing: 8

                    Text {
                        text: modelData.title
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMedium
                        font.bold: true
                    }

                    Text {
                        visible: modelData.note !== ""
                        width: parent.width
                        text: modelData.note
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: modelData.items

                        delegate: Row {
                            width: parent.width
                            spacing: 10

                            Text {
                                text: modelData.name
                                width: 150
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontNormal
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                text: modelData.desc
                                width: parent.width - 160
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontNormal
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
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
