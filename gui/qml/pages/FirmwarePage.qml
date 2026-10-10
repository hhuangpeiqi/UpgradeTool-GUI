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
                text: qsTr("Firmware Upgrade")
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                AppTextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Select a firmware file (.img)")
                    text: app.firmwarePath
                    enabled: !app.busy
                    onAccepted: app.setFirmware(text)
                    onEditingFinished: {
                        if (text.trim() !== app.firmwarePath)
                            app.setFirmware(text);
                    }
                }

                PrimaryButton {
                    text: qsTr("Browse...")
                    iconSource: Icons.folderWhite
                    enabled: !app.busy
                    onClicked: firmwareDialog.open()
                }

                SecondaryButton {
                    text: qsTr("Switch")
                    iconSource: Icons.switchBlue
                    busy: app.switching
                    enabled: !app.busy && app.adbReady
                    onClicked: app.switchToLoader()

                    ToolTip.visible: hovered && !enabled && !app.busy
                    ToolTip.text: app.adbAvailable ? qsTr("No ADB device detected")
                                                   : qsTr("ADB unavailable")
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
                        text: qsTr("Firmware Info")
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

                        Text { text: qsTr("Firmware version"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                        Text { text: app.firmwareVersion; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.fillWidth: true }
                        Text { text: qsTr("Loader version"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                        Text { text: app.firmwareLoaderVersion; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.fillWidth: true }
                        Text { text: qsTr("Chip info"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                        Text { text: app.firmwareChip; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.fillWidth: true }
                        Text { text: qsTr("Build time"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                        Text { text: app.firmwareBuildTime; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.fillWidth: true }
                        Text { text: qsTr("Signature"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
                        Text { text: app.firmwareSign; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; Layout.fillWidth: true }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Firmware entries")
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
                    text: qsTr("Reboot device when done")
                    checked: true
                    enabled: !app.busy
                }

                Item { Layout.fillWidth: true }

                SecondaryButton {
                    text: qsTr("View Firmware Info")
                    enabled: !app.busy && app.firmwarePath !== ""
                    onClicked: app.showFirmwareInfo()
                }

                SecondaryButton {
                    text: qsTr("Extract Firmware…")
                    enabled: !app.busy && app.firmwarePath !== ""
                    onClicked: extractDialog.open()
                }

                PrimaryButton {
                    text: qsTr("Upgrade")
                    iconSource: Icons.playWhite
                    enabled: !app.busy && app.firmwareValid && app.hasDevice
                    onClicked: app.executeFirmwareUpgrade(rebootBox.checked)

                    ToolTip.visible: hovered && !enabled && !app.busy
                    ToolTip.text: !app.hasDevice ? qsTr("No device detected. Connect or switch to Loader/Maskrom mode")
                                                 : qsTr("Select a valid firmware first")
                }
            }
        }
    }

    AppFilePicker {
        id: firmwareDialog
        mode: AppFilePicker.OpenFile
        titleText: qsTr("Select Firmware")
        nameFilters: [qsTr("Firmware (*.img)"), qsTr("All files (*)")]
        persistKey: "firmware"
        onAccepted: {
            var file = app.toLocalPath(selectedFile.toString());
            app.setFirmware(file);
        }
    }

    AppFilePicker {
        id: extractDialog
        mode: AppFilePicker.OpenFolder
        titleText: qsTr("Select Extract Output Directory")
        persistKey: "extract_dir"
        onAccepted: {
            var dir = app.toLocalPath(selectedFolder.toString());
            app.extractFirmware(dir);
        }
    }
}
