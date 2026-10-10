import UpgradeTool
import QtQuick
import QtQuick.Layouts

AllwinnerWorkspace {
    id: page
    objectName: "AllwinnerRawPage"
    context: 0
    title: qsTr("裸镜像刷写")
    fileCaption: qsTr("引导固件包")

    FilePathRow {
        id: rawImage
        objectName: "awRawImage"
        Layout.fillWidth: true
        caption: qsTr("待写入镜像")
        placeholder: qsTr("请选择待写入的镜像文件")
        text: page.setting("rawImage", "")
        enabled: !app.busy
        persistKey: "allwinnerRaw"
        onEdited: page.saveSetting("rawImage", text)
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: qsTr("存储介质"); color: Theme.textSecondary; font.family: Theme.fontFamily; Layout.preferredWidth: 80 }
        AppComboBox {
            id: storage
            Layout.fillWidth: true
            enabled: !app.busy
            model: ["SD / eMMC", "UFS", "SPI NOR"]
            currentIndex: Number(page.setting("rawStorage", 0))
            onActivated: page.saveSetting("rawStorage", currentIndex)
        }
    }
    AppCheckBox {
        id: advanced
        objectName: "awRawAdvanced"
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
        Text { text: qsTr("写入方式"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppComboBox {
            id: mode
            Layout.fillWidth: true
            model: [qsTr("从扇区 0 写入（Command）"), qsTr("按逻辑偏移写入（Logic offset）")]
            currentIndex: Number(page.setting("rawMode", 1))
            onActivated: page.saveSetting("rawMode", currentIndex)
        }
        Text { visible: mode.currentIndex === 1; text: qsTr("逻辑偏移（扇区）"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppTextField {
            id: offset
            visible: mode.currentIndex === 1
            Layout.fillWidth: true
            placeholderText: qsTr("留空自动匹配（可填 0x 十六进制）")
            text: page.setting("logicOffset", "")
            onEditingFinished: page.saveSetting("logicOffset", text)
        }
    }
    actions: [
        PrimaryButton {
            focusPolicy: Qt.StrongFocus
            objectName: "awRawStart"
            text: app.batchMode ? qsTr("批量刷写") : qsTr("开始刷写")
            enabled: !app.busy && page.valid && app.hasDevice && rawImage.text.trim() !== ""
            onClicked: page.execute(Operations.AllwinnerRaw, {rawImage: rawImage.text, rawMode: mode.currentIndex,
                rawStorage: storage.currentIndex, logicOffset: offset.text})
        }
    ]
}
