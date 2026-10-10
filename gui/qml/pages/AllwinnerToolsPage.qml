import UpgradeTool
import QtQuick
import QtQuick.Layouts

AllwinnerWorkspace {
    id: page
    objectName: "AllwinnerToolsPage"
    context: 2
    title: qsTr("高级功能")

    AppCheckBox {
        id: showFiles
        focusPolicy: Qt.StrongFocus
        text: qsTr("查看分区与嵌入文件")
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: showFiles.checked
        AllwinnerPartitions { Layout.fillWidth: true; partitions: page.contextState.partitions }
        Text { text: qsTr("嵌入文件"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMedium }
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
        Text { text: qsTr("解包固件"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMedium; Layout.fillWidth: true }
        SecondaryButton {
            focusPolicy: Qt.StrongFocus
            text: qsTr("选择目录并解包…")
            enabled: !app.busy && page.valid
            onClicked: extractPicker.open()
        }
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
    RowLayout {
        Layout.fillWidth: true
        Text { text: qsTr("设备操作"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMedium; Layout.fillWidth: true }
        SecondaryButton {
            focusPolicy: Qt.StrongFocus
            objectName: "awReboot"
            text: qsTr("重启设备")
            enabled: !app.busy && app.vendor === 1 && app.hasDevice
            onClicked: page.execute(Operations.AllwinnerReboot)
        }
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
    Text { text: qsTr("程序器镜像转换"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMedium }
    FilePathRow {
        id: output
        objectName: "awConvertOutput"
        Layout.fillWidth: true
        caption: qsTr("输出镜像")
        pickerMode: AppFilePicker.SaveFile
        placeholder: qsTr("选择或输入输出镜像路径")
        text: page.setting("output", "")
        enabled: !app.busy
        persistKey: "allwinnerConvert"
        onEdited: page.saveSetting("output", text)
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: qsTr("目标介质"); color: Theme.textSecondary; font.family: Theme.fontFamily; Layout.preferredWidth: 80 }
        AppComboBox {
            id: target
            Layout.fillWidth: true
            enabled: !app.busy
            model: ["SPI NOR", qsTr("SD 卡"), "eMMC", "SD NAND", "UFS"]
            currentIndex: Number(page.setting("target", 2))
            onActivated: page.saveSetting("target", currentIndex)
        }
    }
    AppCheckBox {
        id: advanced
        objectName: "awConvertAdvanced"
        focusPolicy: Qt.StrongFocus
        text: qsTr("高级选项")
    }
    GridLayout {
        visible: advanced.checked
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 12
        rowSpacing: 10
        enabled: !app.busy
        Text { text: qsTr("逻辑偏移（扇区）"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppTextField {
            id: offset
            Layout.fillWidth: true
            text: page.setting("logicOffset", "")
            placeholderText: qsTr("留空自动匹配（可填 0x 十六进制）")
            onEditingFinished: page.saveSetting("logicOffset", text)
        }
        Text { visible: target.currentIndex === 0; text: qsTr("U-Boot 偏移（扇区）"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppTextField {
            id: uboot
            visible: target.currentIndex === 0
            Layout.fillWidth: true
            text: page.setting("ubootOffset", "")
            placeholderText: qsTr("留空自动匹配")
            onEditingFinished: page.saveSetting("ubootOffset", text)
        }
        Text { visible: target.currentIndex === 0; text: qsTr("NOR 容量（MiB）"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppTextField {
            id: nor
            visible: target.currentIndex === 0
            Layout.fillWidth: true
            text: page.setting("norMiB", "16")
            placeholderText: qsTr("默认 16")
            onEditingFinished: page.saveSetting("norMiB", text)
        }
        Text { visible: target.currentIndex !== 0; text: qsTr("GPT 目标容量"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppTextField {
            id: capacity
            visible: target.currentIndex !== 0
            Layout.fillWidth: true
            text: page.setting("storageSize", "auto")
            placeholderText: qsTr("auto 或 8GB / 512MB")
            onEditingFinished: page.saveSetting("storageSize", text)
        }
        Text { visible: target.currentIndex !== 0; text: qsTr("安全启动组件"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppComboBox {
            id: secure
            visible: target.currentIndex !== 0
            Layout.fillWidth: true
            model: [qsTr("自动检测"), "TOC0 / TOC1", "Boot0 / BOOTPKG"]
            currentIndex: Number(page.setting("secure", 0))
            onActivated: page.saveSetting("secure", currentIndex)
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
            text: qsTr("重新读取信息")
            enabled: !app.busy && page.firmware !== ""
            onClicked: page.execute(Operations.AllwinnerInspect)
        },
        Item { Layout.fillWidth: true },
        PrimaryButton {
            focusPolicy: Qt.StrongFocus
            objectName: "awConvertStart"
            text: qsTr("转换镜像")
            enabled: !app.busy && page.valid && output.text.trim() !== ""
            onClicked: page.execute(Operations.AllwinnerConvert, {output: output.text, target: target.currentIndex,
                logicOffset: offset.text, ubootOffset: uboot.text, norMiB: nor.text,
                storageSize: capacity.text, secure: secure.currentIndex})
        }
    ]
    AppFilePicker {
        id: extractPicker
        mode: AppFilePicker.OpenFolder
        titleText: qsTr("选择解包输出目录")
        persistKey: "allwinnerExtract"
        defaultFolder: app.pathUrl(page.setting("extractOutput", page.saved["page1/output"] || ""))
        onAccepted: {
            var directory = app.toLocalPath(selectedFolder.toString());
            page.saveSetting("extractOutput", directory);
            page.execute(Operations.AllwinnerExtract, {output: directory});
        }
    }
}
