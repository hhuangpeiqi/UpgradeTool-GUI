import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page

    Card {
        anchors.fill: parent

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 14

            SectionTitle {
                text: qsTr("固件升级")
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                AppTextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("请选择固件文件（.img）")
                    text: app.firmwarePath
                    enabled: !app.busy
                    onAccepted: app.setFirmware(text)
                    onEditingFinished: {
                        if (text.trim() !== app.firmwarePath)
                            app.setFirmware(text);
                    }
                }

                PrimaryButton {
                    text: qsTr("浏览...")
                    iconSource: Icons.folderWhite
                    enabled: !app.busy
                    onClicked: firmwareDialog.open()
                }

                SecondaryButton {
                    text: qsTr("切换")
                    iconSource: Icons.switchBlue
                    enabled: !app.busy && app.adbReady
                    onClicked: app.switchToLoader()

                    ToolTip.visible: hovered && !enabled && !app.busy
                    ToolTip.text: app.adbAvailable ? qsTr("未检测到 ADB 设备")
                                                   : qsTr("ADB 不可用")
                }
            }

            Text {
                Layout.fillWidth: true
                text: app.firmwareHint
                color: app.firmwareHintError ? Theme.danger : Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                wrapMode: Text.WordWrap
            }

            Card {
                Layout.fillWidth: true
                implicitHeight: metaColumn.implicitHeight + 32

                ColumnLayout {
                    id: metaColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Text {
                        text: qsTr("固件信息")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMedium
                        font.bold: true
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 28
                        rowSpacing: 10

                        Text { text: qsTr("固件版本"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                        Text { text: app.firmwareVersion; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.fillWidth: true }
                        Text { text: qsTr("Loader版本"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                        Text { text: app.firmwareLoaderVersion; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.fillWidth: true }
                        Text { text: qsTr("芯片信息"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                        Text { text: app.firmwareChip; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.fillWidth: true }
                        Text { text: qsTr("构建时间"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                        Text { text: app.firmwareBuildTime; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.fillWidth: true }
                        Text { text: qsTr("签名"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                        Text { text: app.firmwareSign; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.fillWidth: true }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("固件条目")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMedium
                font.bold: true
            }

            KeyValueTable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: app.firmwareEntries
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                AppCheckBox {
                    id: rebootBox
                    text: qsTr("烧录完成后重启设备")
                    checked: true
                    enabled: !app.busy
                }

                Item { Layout.fillWidth: true }

                SecondaryButton {
                    text: qsTr("查看固件信息")
                    enabled: !app.busy && app.firmwarePath !== ""
                    onClicked: app.showFirmwareInfo()
                }

                SecondaryButton {
                    text: qsTr("解包固件…")
                    enabled: !app.busy && app.firmwarePath !== ""
                    onClicked: extractDialog.open()
                }

                PrimaryButton {
                    text: qsTr("升级")
                    iconSource: Icons.playWhite
                    enabled: !app.busy && app.firmwareValid && app.hasDevice
                    onClicked: app.executeFirmwareUpgrade(rebootBox.checked)

                    ToolTip.visible: hovered && !enabled && !app.busy
                    ToolTip.text: !app.hasDevice ? qsTr("未检测到设备，请连接或切换到 Loader/Maskrom 模式")
                                                 : qsTr("请先选择有效的固件")
                }
            }
        }
    }

    AppFilePicker {
        id: firmwareDialog
        mode: AppFilePicker.OpenFile
        titleText: qsTr("选择固件")
        nameFilters: ["固件 (*.img)", "所有文件 (*)"]
        persistKey: "firmware"
        onAccepted: {
            var file = app.toLocalPath(selectedFile.toString());
            app.setFirmware(file);
        }
    }

    AppFilePicker {
        id: extractDialog
        mode: AppFilePicker.OpenFolder
        titleText: qsTr("选择解包输出目录")
        persistKey: "extract_dir"
        onAccepted: {
            var dir = app.toLocalPath(selectedFolder.toString());
            app.extractFirmware(dir);
        }
    }
}
