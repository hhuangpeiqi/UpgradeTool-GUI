import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page

    readonly property var simpleOperations: [
        { label: qsTr("Read Flash ID"), operation: Operations.ReadFlashId },
        { label: qsTr("Read Flash Info"), operation: Operations.ReadFlashInfo },
        { label: qsTr("Read Chip Info"), operation: Operations.ReadChipInfo },
        { label: qsTr("Read Capability"), operation: Operations.ReadCapability },
        { label: qsTr("Test Device"), operation: Operations.TestDevice },
        { label: qsTr("Reset Device"), operation: Operations.ResetDevice },
        { label: qsTr("Switch Storage"), operation: Operations.SwitchStorage },
        { label: qsTr("Read Secure Mode"), operation: Operations.ReadSecureMode },
        { label: qsTr("Read OTP"), operation: Operations.ReadOtpChip },
        { label: qsTr("Switch Device"), operation: Operations.SwitchDevice },
        { label: qsTr("Read Serial"), operation: Operations.ReadSn }
    ]

    readonly property var unsupportedOperations: [
        qsTr("Clear Serial"),
        qsTr("Get Current Storage"),
        qsTr("Switch USB3")
    ]

    GptDialog { id: gptDialog; objectName: "rkGptDialog" }

    readonly property bool canRun: !app.interactionLocked && app.hasDevice

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
                    text: qsTr("Advanced")
                    Layout.fillWidth: true
                }

                Text {
                    Layout.fillWidth: true
                    text: app.rockchip.advancedHint
                    color: app.rockchip.advancedHintError ? Theme.danger : Theme.textSecondary
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
                    AppTextField { id: bootField; Layout.fillWidth: true; placeholderText: qsTr("Loader / boot file path") }
                    SecondaryButton { text: qsTr("Choose…"); enabled: !app.interactionLocked; onClicked: { picker.fileKind = "loader"; picker.onPicked = function (p) { bootField.text = p; }; picker.open(); } }

                    Text { text: qsTr("Firmware:"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 64 }
                    AppTextField { id: fwField; Layout.fillWidth: true; placeholderText: qsTr("Firmware file path") }
                    SecondaryButton { text: qsTr("Choose…"); enabled: !app.interactionLocked; onClicked: { picker.fileKind = "firmware"; picker.onPicked = function (p) { fwField.text = p; }; picker.open(); } }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 12

                    SecondaryButton {
                        text: qsTr("Download Boot")
                        enabled: page.canRun && bootField.text !== ""
                        onClicked: app.rockchip.runOperation(Operations.DownloadBoot, {path: bootField.text})
                    }
                    SecondaryButton {
                        text: qsTr("Extract Firmware")
                        enabled: !app.interactionLocked && fwField.text !== ""
                        onClicked: { folderPicker.pending = "extract"; folderPicker.open(); }
                    }
                    SecondaryButton {
                        objectName: "rkGenerateGpt"
                        focusPolicy: Qt.StrongFocus
                        text: qsTr("Generate GPT…")
                        enabled: !app.interactionLocked
                        onClicked: gptDialog.open()
                    }
                    SecondaryButton {
                        text: qsTr("Erase All")
                        enabled: page.canRun
                        onClicked: app.rockchip.runOperation(Operations.EraseFlash, {path: bootField.text})
                    }
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
                            onClicked: app.rockchip.runOperation(modelData.operation)
                        }
                    }

                    SecondaryButton {
                        objectName: "enterMaskromButton"
                        text: qsTr("Enter Maskrom")
                        enabled: !app.interactionLocked && app.rockchipMaskromEligibility.allowed
                        hoverEnabled: true
                        ToolTip.visible: hovered && !app.rockchipMaskromEligibility.allowed
                        ToolTip.text: app.rockchipMaskromEligibility.reason
                        onClicked: app.rockchip.runOperation(Operations.EnterMaskrom)
                    }

                    Repeater {
                        model: page.unsupportedOperations
                        delegate: SecondaryButton {
                            text: modelData
                            enabled: false
                            hoverEnabled: true
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("The current engine does not support this feature")
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    SecondaryButton {
                        text: qsTr("Export COM Log")
                        enabled: page.canRun
                        onClicked: { savePicker.pending = "comlog"; savePicker.open(); }
                    }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text { text: qsTr("Serial:"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                    AppTextField { id: snField; Layout.preferredWidth: 280; placeholderText: qsTr("Serial number to write") }
                    SecondaryButton {
                        text: qsTr("Write Serial")
                        enabled: page.canRun && snField.text.trim() !== ""
                        onClicked: app.rockchip.runOperation(Operations.WriteSn, {serial: snField.text.trim()})
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
                            text: qsTr("LBA / Sector")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMedium
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: qsTr("Start sector"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                            AppTextField { id: beginField; Layout.preferredWidth: 140 }
                            Text { text: qsTr("Sector count"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                            AppTextField { id: countField; Layout.preferredWidth: 140 }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            SecondaryButton {
                                text: qsTr("Export Image")
                                enabled: page.canRun && beginField.text !== "" && countField.text !== ""
                                onClicked: { savePicker.pending = "export"; savePicker.open(); }
                            }
                            SecondaryButton {
                                text: qsTr("Erase Sectors")
                                enabled: page.canRun && beginField.text !== "" && countField.text !== ""
                                onClicked: app.rockchip.runOperation(Operations.EraseLba, {begin: beginField.text, count: countField.text})
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }

                KeyValueTable {
                    Layout.fillWidth: true
                    implicitHeight: Math.min(320, Math.max(120, app.rockchip.results.count * 38 + 44))
                    model: app.rockchip.results
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
                            text: qsTr("Expert Parameters")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMedium
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: qsTr("Write LBA"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 130 }
                            AppTextField { id: wlBegin; Layout.preferredWidth: 140; placeholderText: qsTr("Start sector") }
                            AppTextField { id: wlFile; Layout.fillWidth: true; placeholderText: qsTr("File") }
                            SecondaryButton { text: qsTr("Choose…"); enabled: !app.interactionLocked; onClicked: { picker.fileKind = "image"; picker.onPicked = function (p) { wlFile.text = p; }; picker.open(); } }
                            SecondaryButton {
                                text: qsTr("Execute")
                                enabled: page.canRun && wlBegin.text !== "" && wlFile.text !== ""
                                onClicked: app.rockchip.runOperation(Operations.WriteLba, {begin: wlBegin.text, path: wlFile.text})
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: qsTr("Write Sector"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 130 }
                            AppTextField { id: wsBegin; Layout.preferredWidth: 110; placeholderText: qsTr("Start sector") }
                            AppTextField { id: wsPage; Layout.preferredWidth: 100; placeholderText: qsTr("Page KB") }
                            AppTextField { id: wsSpare; Layout.preferredWidth: 100; placeholderText: qsTr("spare") }
                            AppTextField { id: wsFile; Layout.fillWidth: true; placeholderText: qsTr("File") }
                            SecondaryButton { text: qsTr("Choose…"); enabled: !app.interactionLocked; onClicked: { picker.fileKind = "image"; picker.onPicked = function (p) { wsFile.text = p; }; picker.open(); } }
                            SecondaryButton {
                                text: qsTr("Execute")
                                enabled: page.canRun && wsBegin.text !== "" && wsPage.text !== "" && wsSpare.text !== "" && wsFile.text !== ""
                                onClicked: app.rockchip.runOperation(Operations.WriteSector, {begin: wsBegin.text, pageSize: wsPage.text, spareSize: wsSpare.text, path: wsFile.text})
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: qsTr("Erase Block"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 130 }
                            AppTextField { id: ebCs; Layout.preferredWidth: 90; placeholderText: qsTr("CS") }
                            AppTextField { id: ebBegin; Layout.preferredWidth: 110; placeholderText: qsTr("Start block") }
                            AppTextField { id: ebCount; Layout.preferredWidth: 110; placeholderText: qsTr("Block count") }
                            AppCheckBox { id: ebForce; text: qsTr("Force erase") }
                            Item { Layout.fillWidth: true }
                            SecondaryButton {
                                text: qsTr("Execute")
                                enabled: page.canRun && ebCs.text !== "" && ebBegin.text !== "" && ebCount.text !== ""
                                onClicked: app.rockchip.runOperation(Operations.EraseBlock, {chipSelect: ebCs.text, begin: ebBegin.text, count: ebCount.text, force: ebForce.checked})
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: qsTr("Run System"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 130 }
                            AppTextField { id: runUbootAddr; Layout.preferredWidth: 110; placeholderText: qsTr("U-Boot address") }
                            AppTextField { id: runTrustAddr; Layout.preferredWidth: 110; placeholderText: qsTr("Trust address") }
                            AppTextField { id: runBootAddr; Layout.preferredWidth: 110; placeholderText: qsTr("Boot address") }
                            Item { Layout.fillWidth: true }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Item { Layout.preferredWidth: 130 }
                            AppTextField { id: runUboot; Layout.fillWidth: true; placeholderText: qsTr("U-Boot file") }
                            SecondaryButton { text: qsTr("Choose…"); enabled: !app.interactionLocked; onClicked: { picker.fileKind = "image"; picker.onPicked = function (p) { runUboot.text = p; }; picker.open(); } }
                            AppTextField { id: runTrust; Layout.fillWidth: true; placeholderText: qsTr("Trust file") }
                            SecondaryButton { text: qsTr("Choose…"); enabled: !app.interactionLocked; onClicked: { picker.fileKind = "image"; picker.onPicked = function (p) { runTrust.text = p; }; picker.open(); } }
                            AppTextField { id: runBoot; Layout.fillWidth: true; placeholderText: qsTr("Boot file") }
                            SecondaryButton { text: qsTr("Choose…"); enabled: !app.interactionLocked; onClicked: { picker.fileKind = "image"; picker.onPicked = function (p) { runBoot.text = p; }; picker.open(); } }
                            SecondaryButton {
                                text: qsTr("Execute")
                                enabled: page.canRun && runUbootAddr.text !== "" && runTrustAddr.text !== "" && runBootAddr.text !== ""
                                         && runUboot.text !== "" && runTrust.text !== "" && runBoot.text !== ""
                                onClicked: app.rockchip.runOperation(Operations.RunSystem, {ubootAddress: runUbootAddr.text, trustAddress: runTrustAddr.text, bootAddress: runBootAddr.text, uboot: runUboot.text, trust: runTrust.text, boot: runBoot.text})
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: qsTr("Reset Pipe"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.preferredWidth: 130 }
                            AppTextField { id: rpPipe; Layout.preferredWidth: 160; placeholderText: qsTr("Optional pipe") }
                            Item { Layout.fillWidth: true }
                            SecondaryButton {
                                text: qsTr("Execute")
                                enabled: page.canRun
                                onClicked: app.rockchip.runOperation(Operations.ResetPipe, rpPipe.text.trim() !== "" ? {pipe: rpPipe.text.trim()} : {})
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
        property string fileKind: "image"
        nameFilters: [fileKind === "loader" ? qsTr("Loader files (*.bin)")
                      : fileKind === "firmware" ? qsTr("Firmware (*.img)")
                      : qsTr("Image files (*.img *.bin)"), qsTr("All files (*)")]
        mode: AppFilePicker.OpenFile
        property var onPicked: null
        titleText: qsTr("Select File")
        persistKey: "open_file"
        onAccepted: {
            if (picker.onPicked)
                picker.onPicked(app.toLocalPath(selectedFile.toString()));
        }
    }

    AppFilePicker {
        id: folderPicker
        mode: AppFilePicker.OpenFolder
        titleText: qsTr("Select Output Directory")
        persistKey: "extract_dir"
        onAccepted: {
            var dir = app.toLocalPath(selectedFolder.toString());
            if (folderPicker.pending === "extract")
                app.rockchip.runOperation(Operations.FirmwareExtract, {path: fwField.text, output: dir});
        }
    }

    AppFilePicker {
        id: savePicker
        nameFilters: [pending === "comlog" ? qsTr("Log files (*.log *.txt)")
                      : qsTr("Image files (*.img *.bin)"), qsTr("All files (*)")]
        mode: AppFilePicker.SaveFile
        titleText: qsTr("Select Save File")
        persistKey: "export_file"
        onAccepted: {
            var file = app.toLocalPath(selectedFile.toString());
            if (savePicker.pending === "comlog")
                app.rockchip.exportComLog(file);
            else if (savePicker.pending === "export")
                app.rockchip.exportImage(beginField.text, countField.text, file);
        }
    }
}
