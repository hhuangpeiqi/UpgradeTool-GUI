import UpgradeTool
import QtQuick
import QtQuick.Layouts

AllwinnerWorkspace {
    id: page
    objectName: "AllwinnerToolsPage"
    context: 2
    title: qsTr("Advanced")

    AppCheckBox {
        id: showFiles
        focusPolicy: Qt.StrongFocus
        text: qsTr("View partitions and embedded files")
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: showFiles.checked
        AllwinnerPartitions { Layout.fillWidth: true; partitions: page.contextState.partitions }
        Text { text: qsTr("Embedded files"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMedium }
        ColumnLayout {
            Layout.fillWidth: true
            Repeater {
                model: page.contextState.files
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.name + " · " + modelData.type + " · " + modelData.size + " B"
                    elide: Text.ElideMiddle
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                }
            }
        }
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
    RowLayout {
        Layout.fillWidth: true
        Text { text: qsTr("Extract Firmware"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMedium; Layout.fillWidth: true }
        SecondaryButton {
            focusPolicy: Qt.StrongFocus
            text: qsTr("Choose a directory and extract…")
            iconSource: Icons.folderGray
            enabled: !app.busy && page.valid
            onClicked: extractPicker.open()
        }
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
    RowLayout {
        Layout.fillWidth: true
        Text { text: qsTr("Device Operations"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMedium; Layout.fillWidth: true }
        SecondaryButton {
            focusPolicy: Qt.StrongFocus
            objectName: "awReboot"
            text: qsTr("Reboot Device")
            iconSource: Icons.refreshBlue
            enabled: !app.busy && app.vendor === 1 && app.hasDevice
            onClicked: page.execute(Operations.AllwinnerReboot)
        }
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
    Text { text: qsTr("Programmer Image Convert"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMedium }
    FilePathRow {
        id: output
        objectName: "awConvertOutput"
        Layout.fillWidth: true
        caption: qsTr("Output image")
        pickerMode: AppFilePicker.SaveFile
        placeholder: qsTr("Choose or enter the output image path")
        text: page.setting("output", "")
        enabled: !app.busy
        persistKey: "allwinnerConvert"
        onEdited: page.saveSetting("output", text)
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: qsTr("Target medium"); color: Theme.textSecondary; font.family: Theme.fontFamily; Layout.preferredWidth: 80 }
        AppComboBox {
            id: target
            Layout.fillWidth: true
            enabled: !app.busy
            model: ["SPI NOR", qsTr("SD card"), "eMMC", "SD NAND", "UFS"]
            currentIndex: Number(page.setting("target", 2))
            onActivated: page.saveSetting("target", currentIndex)
        }
    }
    DisclosureSection {
        objectName: "awConvertAdvanced"
        title: qsTr("Advanced options")
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 10
            enabled: !app.busy
            Text { text: qsTr("Logic offset (sectors)"); color: Theme.textSecondary; font.family: Theme.fontFamily }
            AppTextField {
                id: offset
                Layout.fillWidth: true
                text: page.setting("logicOffset", "")
                placeholderText: qsTr("Leave empty to auto-detect (0x hex allowed)")
                onEditingFinished: page.saveSetting("logicOffset", text)
            }
            Text { visible: target.currentIndex === 0; text: qsTr("U-Boot offset (sectors)"); color: Theme.textSecondary; font.family: Theme.fontFamily }
            AppTextField {
                id: uboot
                visible: target.currentIndex === 0
                Layout.fillWidth: true
                text: page.setting("ubootOffset", "")
                placeholderText: qsTr("Leave empty to auto-detect")
                onEditingFinished: page.saveSetting("ubootOffset", text)
            }
            Text { visible: target.currentIndex === 0; text: qsTr("NOR capacity (MiB)"); color: Theme.textSecondary; font.family: Theme.fontFamily }
            AppTextField {
                id: nor
                visible: target.currentIndex === 0
                Layout.fillWidth: true
                text: page.setting("norMiB", "16")
                placeholderText: qsTr("Default 16")
                onEditingFinished: page.saveSetting("norMiB", text)
            }
            Text { visible: target.currentIndex !== 0; text: qsTr("GPT target capacity"); color: Theme.textSecondary; font.family: Theme.fontFamily }
            AppTextField {
                id: capacity
                visible: target.currentIndex !== 0
                Layout.fillWidth: true
                text: page.setting("storageSize", "auto")
                placeholderText: qsTr("auto or 8GB / 512MB")
                onEditingFinished: page.saveSetting("storageSize", text)
            }
            Text { visible: target.currentIndex !== 0; text: qsTr("Secure boot components"); color: Theme.textSecondary; font.family: Theme.fontFamily }
            AppComboBox {
                id: secure
                visible: target.currentIndex !== 0
                Layout.fillWidth: true
                model: [qsTr("Auto detect"), "TOC0 / TOC1", "Boot0 / BOOTPKG"]
                currentIndex: Number(page.setting("secure", 0))
                onActivated: page.saveSetting("secure", currentIndex)
            }
        }
    }
    KeyValueTable {
        visible: page.contextState.results.count > 0
        Layout.fillWidth: true
        implicitHeight: 150
        model: page.contextState.results
    }
    actions: [
        SecondaryButton {
            focusPolicy: Qt.StrongFocus
            text: qsTr("Read Info Again")
            iconSource: Icons.refreshBlue
            enabled: !app.busy && page.firmware !== ""
            onClicked: page.execute(Operations.AllwinnerInspect)
        },
        Item { Layout.fillWidth: true },
        PrimaryButton {
            iconSource: Icons.playWhite
            focusPolicy: Qt.StrongFocus
            objectName: "awConvertStart"
            text: qsTr("Convert Image")
            enabled: !app.busy && page.valid && output.text.trim() !== ""
            onClicked: page.execute(Operations.AllwinnerConvert, {output: output.text, target: target.currentIndex,
                logicOffset: offset.text, ubootOffset: uboot.text, norMiB: nor.text,
                storageSize: capacity.text, secure: secure.currentIndex})
        }
    ]
    AppFilePicker {
        id: extractPicker
        mode: AppFilePicker.OpenFolder
        titleText: qsTr("Select Extract Output Directory")
        persistKey: "allwinnerExtract"
        defaultFolder: app.pathUrl(page.setting("extractOutput", page.saved["page1/output"] || ""))
        onAccepted: {
            var directory = app.toLocalPath(selectedFolder.toString());
            page.saveSetting("extractOutput", directory);
            page.execute(Operations.AllwinnerExtract, {output: directory});
        }
    }
}
