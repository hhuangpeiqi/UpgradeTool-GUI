import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import UpgradeTool

Item {
    id: page

    Card {
        anchors.fill: parent

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 14

            SectionTitle {
                text: qsTr("分体包目录")
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                AppTextField {
                    id: dirField
                    Layout.fillWidth: true
                    placeholderText: qsTr("请选择分体包目录（内含 parameter.txt、各分区 .img 与 Loader.bin）")
                    text: app.imageDir
                    enabled: !app.busy
                    onAccepted: app.setImageDir(text)
                    onEditingFinished: {
                        if (text.trim() !== app.imageDir)
                            app.setImageDir(text);
                    }
                }

                PrimaryButton {
                    text: qsTr("浏览...")
                    iconSource: Icons.folderWhite
                    enabled: !app.busy
                    onClicked: folderDialog.open()
                }
            }

            Text {
                Layout.fillWidth: true
                visible: app.imageDir !== "" || app.imageHintError
                text: app.imageHint
                color: app.imageHintError ? Theme.danger : Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                wrapMode: Text.WordWrap
            }

            ImageTable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: app.imageEntries
                emptyIcon: Icons.folderGray
                emptyTitle: qsTr("还未选择分体包目录")
                emptySubtitle: qsTr("请点击上方「浏览」按钮选择目录")
                allChecked: app.imageEntries.checkedCount > 0
                            && app.imageEntries.checkedCount === app.imageEntries.checkableCount
                allEnabled: app.imageEntries.checkableCount > 0 && !app.busy
                onRowChecked: (row, checked) => app.imageEntries.setChecked(row, checked)
                onAllToggled: (checked) => app.imageEntries.setAllChecked(checked)
                onPathDoubleClicked: (row) => {
                    pathDialog.targetRow = row;
                    pathDialog.open();
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 20

                AppCheckBox {
                    id: rebootBox
                    text: qsTr("烧录完成后重启设备")
                    checked: true
                    enabled: !app.busy
                }

                AppCheckBox {
                    text: qsTr("强制按地址写")
                    enabled: false
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("当前引擎不支持该选项")
                }

                Item { Layout.fillWidth: true }

                PrimaryButton {
                    text: qsTr("执行")
                    iconSource: Icons.playWhite
                    enabled: !app.busy && app.imageEntries.checkedCount > 0 && app.hasDevice
                    onClicked: app.executeImageDownload(rebootBox.checked)

                    ToolTip.visible: hovered && !enabled && !app.busy
                    ToolTip.text: !app.hasDevice ? qsTr("未检测到设备，请连接或切换到 Loader/Maskrom 模式")
                                                 : qsTr("请至少勾选一个分区")
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

                SecondaryButton {
                    text: qsTr("清空")
                    iconSource: Icons.trashGray
                    enabled: !app.busy
                    onClicked: app.clearImage()
                }
            }
        }
    }

    AppFilePicker {
        id: folderDialog
        mode: AppFilePicker.OpenFolder
        titleText: qsTr("选择分体包目录")
        persistKey: "image_dir"
        defaultFolder: app.imageDir !== "" ? app.pathUrl(app.imageDir) : ""
        onAccepted: {
            var dir = app.toLocalPath(selectedFolder.toString());
            app.setImageDir(dir);
        }
    }

    AppFilePicker {
        id: pathDialog
        mode: AppFilePicker.OpenFile
        property int targetRow: -1
        titleText: qsTr("选择镜像文件")
        nameFilters: ["镜像文件 (*.img *.bin)", "所有文件 (*)"]
        persistKey: "open_file"
        onAccepted: {
            var file = app.toLocalPath(selectedFile.toString());
            app.imageEntries.setPath(pathDialog.targetRow, file);
        }
    }
}
