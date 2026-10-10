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
                text: qsTr("Split package directory")
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                AppTextField {
                    id: dirField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Select the split package directory (with parameter.txt, per-partition .img and Loader.bin)")
                    text: app.imageDir
                    enabled: !app.busy
                    onAccepted: app.setImageDir(text)
                    onEditingFinished: {
                        if (text.trim() !== app.imageDir)
                            app.setImageDir(text);
                    }
                }

                PrimaryButton {
                    text: qsTr("Browse...")
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
                emptyTitle: qsTr("No split package directory selected")
                emptySubtitle: qsTr("Click \"Browse\" above to choose a directory")
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
                    text: qsTr("Reboot device when done")
                    checked: true
                    enabled: !app.busy
                }

                AppCheckBox {
                    text: qsTr("Force write by address")
                    enabled: false
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("The current engine does not support this option")
                }

                Item { Layout.fillWidth: true }

                PrimaryButton {
                    text: qsTr("Execute")
                    iconSource: Icons.playWhite
                    enabled: !app.busy && app.imageEntries.checkedCount > 0 && app.hasDevice
                    onClicked: app.executeImageDownload(rebootBox.checked)

                    ToolTip.visible: hovered && !enabled && !app.busy
                    ToolTip.text: !app.hasDevice ? qsTr("No device detected. Connect or switch to Loader/Maskrom mode")
                                                 : qsTr("Select at least one partition")
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

                SecondaryButton {
                    text: qsTr("Clear")
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
        titleText: qsTr("Select Split Package Directory")
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
        titleText: qsTr("Select Image File")
        nameFilters: [qsTr("Image files (*.img *.bin)"), qsTr("All files (*)")]
        persistKey: "open_file"
        onAccepted: {
            var file = app.toLocalPath(selectedFile.toString());
            app.imageEntries.setPath(pathDialog.targetRow, file);
        }
    }
}
