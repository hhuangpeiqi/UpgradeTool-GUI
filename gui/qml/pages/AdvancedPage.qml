import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page

    readonly property var simpleOperations: [
        { label: qsTr("读取FlashID"), operation: Operations.ReadFlashId },
        { label: qsTr("读取Flash信息"), operation: Operations.ReadFlashInfo },
        { label: qsTr("读取Chip信息"), operation: Operations.ReadChipInfo },
        { label: qsTr("读取Capability"), operation: Operations.ReadCapability },
        { label: qsTr("测试设备"), operation: Operations.TestDevice },
        { label: qsTr("重启设备"), operation: Operations.ResetDevice },
        { label: qsTr("切换存储"), operation: Operations.SwitchStorage },
        { label: qsTr("检测安全模式"), operation: Operations.ReadSecureMode },
        { label: qsTr("读取OTP"), operation: Operations.ReadOtpChip },
        { label: qsTr("切换设备"), operation: Operations.SwitchDevice },
        { label: qsTr("读取序列号"), operation: Operations.ReadSn }
    ]

    readonly property var unsupportedOperations: [
        qsTr("进入Maskrom"),
        qsTr("清空序列号"),
        qsTr("获取当前存储"),
        qsTr("切换USB3")
    ]

    readonly property bool canRun: !app.busy && app.hasDevice

    Card {
        anchors.fill: parent

        ScrollView {
            id: scroll
            anchors.fill: parent
            anchors.margins: 18
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: scroll.availableWidth
                spacing: 14

                SectionTitle {
                    text: qsTr("高级功能")
                    Layout.fillWidth: true
                }

                Text {
                    Layout.fillWidth: true
                    text: app.advancedHint
                    color: app.advancedHintError ? Theme.danger : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontNormal
                    wrapMode: Text.WordWrap
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: 10
                    rowSpacing: 10

                    Text { text: qsTr("Boot:"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 64 }
                    AppTextField { id: bootField; Layout.fillWidth: true; placeholderText: qsTr("Loader / boot 文件路径") }
                    SecondaryButton { text: qsTr("选择…"); enabled: !app.busy; onClicked: { picker.onPicked = function (p) { bootField.text = p; }; picker.open(); } }

                    Text { text: qsTr("固件:"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 64 }
                    AppTextField { id: fwField; Layout.fillWidth: true; placeholderText: qsTr("固件文件路径") }
                    SecondaryButton { text: qsTr("选择…"); enabled: !app.busy; onClicked: { picker.onPicked = function (p) { fwField.text = p; }; picker.open(); } }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    SecondaryButton {
                        text: qsTr("下载 Boot")
                        enabled: page.canRun && bootField.text !== ""
                        onClicked: app.runOperation(Operations.DownloadBoot, {path: bootField.text})
                    }
                    SecondaryButton {
                        text: qsTr("解包固件")
                        enabled: !app.busy && fwField.text !== ""
                        onClicked: { folderPicker.pending = "extract"; folderPicker.open(); }
                    }
                    SecondaryButton {
                        text: qsTr("擦除所有")
                        enabled: page.canRun
                        onClicked: app.runOperation(Operations.EraseFlash, {path: bootField.text})
                    }
                    Item { Layout.fillWidth: true }
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

                Flow {
                    Layout.fillWidth: true
                    spacing: 10

                    Repeater {
                        model: page.simpleOperations
                        delegate: SecondaryButton {
                            text: modelData.label
                            enabled: page.canRun
                            onClicked: app.runOperation(modelData.operation)
                        }
                    }

                    Repeater {
                        model: page.unsupportedOperations
                        delegate: SecondaryButton {
                            text: modelData
                            enabled: false
                            hoverEnabled: true
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("当前引擎不支持该功能")
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    SecondaryButton {
                        text: qsTr("导出串口日志")
                        enabled: page.canRun
                        onClicked: { savePicker.pending = "comlog"; savePicker.open(); }
                    }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text { text: qsTr("序列号:"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                    AppTextField { id: snField; Layout.preferredWidth: 280; placeholderText: qsTr("要写入的序列号") }
                    SecondaryButton {
                        text: qsTr("写入序列号")
                        enabled: page.canRun && snField.text.trim() !== ""
                        onClicked: app.runOperation(Operations.WriteSn, {serial: snField.text.trim()})
                    }
                    Item { Layout.fillWidth: true }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: sectorColumn.implicitHeight + 32
                    color: Theme.card
                    radius: Theme.controlRadius
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        id: sectorColumn
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        Text {
                            text: qsTr("LBA / 扇区")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMedium
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: qsTr("起始扇区"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                            AppTextField { id: beginField; Layout.preferredWidth: 140 }
                            Text { text: qsTr("扇区数"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                            AppTextField { id: countField; Layout.preferredWidth: 140 }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            SecondaryButton {
                                text: qsTr("导出镜像")
                                enabled: page.canRun && beginField.text !== "" && countField.text !== ""
                                onClicked: { savePicker.pending = "export"; savePicker.open(); }
                            }
                            SecondaryButton {
                                text: qsTr("擦除扇区")
                                enabled: page.canRun && beginField.text !== "" && countField.text !== ""
                                onClicked: app.runOperation(Operations.EraseLba, {begin: beginField.text, count: countField.text})
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }

                KeyValueTable {
                    Layout.fillWidth: true
                    implicitHeight: Math.min(320, Math.max(120, app.results.count * 38 + 44))
                    model: app.results
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: proColumn.implicitHeight + 32
                    color: Theme.card
                    radius: Theme.controlRadius
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        id: proColumn
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 14

                        Text {
                            text: qsTr("专业操作参数")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMedium
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: qsTr("写入 LBA"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 130 }
                            AppTextField { id: wlBegin; Layout.preferredWidth: 140; placeholderText: qsTr("起始扇区") }
                            AppTextField { id: wlFile; Layout.fillWidth: true; placeholderText: qsTr("文件") }
                            SecondaryButton { text: qsTr("选择…"); enabled: !app.busy; onClicked: { picker.onPicked = function (p) { wlFile.text = p; }; picker.open(); } }
                            SecondaryButton {
                                text: qsTr("执行")
                                enabled: page.canRun && wlBegin.text !== "" && wlFile.text !== ""
                                onClicked: app.runOperation(Operations.WriteLba, {begin: wlBegin.text, path: wlFile.text})
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: qsTr("写入扇区"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 130 }
                            AppTextField { id: wsBegin; Layout.preferredWidth: 110; placeholderText: qsTr("起始扇区") }
                            AppTextField { id: wsPage; Layout.preferredWidth: 100; placeholderText: qsTr("页KB") }
                            AppTextField { id: wsSpare; Layout.preferredWidth: 100; placeholderText: qsTr("spare") }
                            AppTextField { id: wsFile; Layout.fillWidth: true; placeholderText: qsTr("文件") }
                            SecondaryButton { text: qsTr("选择…"); enabled: !app.busy; onClicked: { picker.onPicked = function (p) { wsFile.text = p; }; picker.open(); } }
                            SecondaryButton {
                                text: qsTr("执行")
                                enabled: page.canRun && wsBegin.text !== "" && wsPage.text !== "" && wsSpare.text !== "" && wsFile.text !== ""
                                onClicked: app.runOperation(Operations.WriteSector, {begin: wsBegin.text, pageSize: wsPage.text, spareSize: wsSpare.text, path: wsFile.text})
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: qsTr("擦除块"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 130 }
                            AppTextField { id: ebCs; Layout.preferredWidth: 90; placeholderText: qsTr("CS") }
                            AppTextField { id: ebBegin; Layout.preferredWidth: 110; placeholderText: qsTr("起始块") }
                            AppTextField { id: ebCount; Layout.preferredWidth: 110; placeholderText: qsTr("块数") }
                            AppCheckBox { id: ebForce; text: qsTr("强制擦除") }
                            Item { Layout.fillWidth: true }
                            SecondaryButton {
                                text: qsTr("执行")
                                enabled: page.canRun && ebCs.text !== "" && ebBegin.text !== "" && ebCount.text !== ""
                                onClicked: app.runOperation(Operations.EraseBlock, {chipSelect: ebCs.text, begin: ebBegin.text, count: ebCount.text, force: ebForce.checked})
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: qsTr("运行系统"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 130 }
                            AppTextField { id: runUbootAddr; Layout.preferredWidth: 110; placeholderText: qsTr("uboot地址") }
                            AppTextField { id: runTrustAddr; Layout.preferredWidth: 110; placeholderText: qsTr("trust地址") }
                            AppTextField { id: runBootAddr; Layout.preferredWidth: 110; placeholderText: qsTr("boot地址") }
                            Item { Layout.fillWidth: true }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Item { Layout.preferredWidth: 130 }
                            AppTextField { id: runUboot; Layout.fillWidth: true; placeholderText: qsTr("uboot 文件") }
                            SecondaryButton { text: qsTr("选择…"); enabled: !app.busy; onClicked: { picker.onPicked = function (p) { runUboot.text = p; }; picker.open(); } }
                            AppTextField { id: runTrust; Layout.fillWidth: true; placeholderText: qsTr("trust 文件") }
                            SecondaryButton { text: qsTr("选择…"); enabled: !app.busy; onClicked: { picker.onPicked = function (p) { runTrust.text = p; }; picker.open(); } }
                            AppTextField { id: runBoot; Layout.fillWidth: true; placeholderText: qsTr("boot 文件") }
                            SecondaryButton { text: qsTr("选择…"); enabled: !app.busy; onClicked: { picker.onPicked = function (p) { runBoot.text = p; }; picker.open(); } }
                            SecondaryButton {
                                text: qsTr("执行")
                                enabled: page.canRun && runUbootAddr.text !== "" && runTrustAddr.text !== "" && runBootAddr.text !== ""
                                         && runUboot.text !== "" && runTrust.text !== "" && runBoot.text !== ""
                                onClicked: app.runOperation(Operations.RunSystem, {ubootAddress: runUbootAddr.text, trustAddress: runTrustAddr.text, bootAddress: runBootAddr.text, uboot: runUboot.text, trust: runTrust.text, boot: runBoot.text})
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: qsTr("重置管道"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 130 }
                            AppTextField { id: rpPipe; Layout.preferredWidth: 160; placeholderText: qsTr("可选 pipe") }
                            Item { Layout.fillWidth: true }
                            SecondaryButton {
                                text: qsTr("执行")
                                enabled: page.canRun
                                onClicked: app.runOperation(Operations.ResetPipe, rpPipe.text.trim() !== "" ? {pipe: rpPipe.text.trim()} : {})
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true; implicitHeight: 4 }
            }
        }
    }

    AppFilePicker {
        id: picker
        mode: AppFilePicker.OpenFile
        property var onPicked: null
        titleText: qsTr("选择文件")
        persistKey: "open_file"
        onAccepted: {
            if (picker.onPicked)
                picker.onPicked(app.toLocalPath(selectedFile.toString()));
        }
    }

    AppFilePicker {
        id: folderPicker
        mode: AppFilePicker.OpenFolder
        titleText: qsTr("选择输出目录")
        persistKey: "extract_dir"
        onAccepted: {
            var dir = app.toLocalPath(selectedFolder.toString());
            if (folderPicker.pending === "extract")
                app.runOperation(Operations.FirmwareExtract, {path: fwField.text, output: dir});
        }
    }

    AppFilePicker {
        id: savePicker
        mode: AppFilePicker.SaveFile
        titleText: qsTr("选择保存文件")
        persistKey: "export_file"
        onAccepted: {
            var file = app.toLocalPath(selectedFile.toString());
            if (savePicker.pending === "comlog")
                app.exportComLog(file);
            else if (savePicker.pending === "export")
                app.exportImage(beginField.text, countField.text, file);
        }
    }
}
