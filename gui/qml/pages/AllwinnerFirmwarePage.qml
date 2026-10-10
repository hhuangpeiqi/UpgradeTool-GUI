import UpgradeTool
import QtQuick
import QtQuick.Layouts

AllwinnerWorkspace {
    id: page
    objectName: "AllwinnerFirmwarePage"
    context: 1
    title: qsTr("固件刷写")
    property var selectedPartitions: []
    signal toolsRequested(string path)
    onFirmwareChanged: selectedPartitions = []

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 12
        rowSpacing: 10
        enabled: !app.busy
        Text { text: qsTr("刷写模式"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppComboBox {
            id: mode
            objectName: "awFlashMode"
            Layout.fillWidth: true
            model: [qsTr("按所选分区刷写"), qsTr("保留用户数据"), qsTr("分区擦除后刷写"), qsTr("全盘擦除后刷写")]
            currentIndex: Number(page.setting("mode", 3))
            onActivated: page.saveSetting("mode", currentIndex)
        }
        Text { text: qsTr("完成动作"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppComboBox {
            id: postAction
            Layout.fillWidth: true
            model: [qsTr("重启"), qsTr("断电"), qsTr("关机")]
            currentIndex: Number(page.setting("postAction", 0))
            onActivated: page.saveSetting("postAction", currentIndex)
        }
    }
    AppCheckBox {
        focusPolicy: Qt.StrongFocus
        id: verify
        objectName: "awVerify"
        text: qsTr("写入后校验")
        enabled: !app.busy
        checked: String(page.setting("verify", true)) === "true"
        onToggled: page.saveSetting("verify", checked)
    }
    AllwinnerPartitions {
        Layout.fillWidth: true
        partitions: page.contextState.partitions
        selected: page.selectedPartitions
        selectable: mode.currentIndex === 0
        onSelectionChanged: (names) => page.selectedPartitions = names
    }
    actions: [
        PrimaryButton {
            focusPolicy: Qt.StrongFocus
            objectName: "awFlashStart"
            text: app.batchMode ? qsTr("批量刷写") : qsTr("开始刷写")
            enabled: !app.busy && page.valid && app.hasDevice && (mode.currentIndex !== 0 || page.selectedPartitions.length > 0)
            onClicked: page.execute(Operations.AllwinnerFlash, {mode: mode.currentIndex, verify: verify.checked,
                postAction: postAction.currentIndex, partitions: page.selectedPartitions})
        }
    ]
}
