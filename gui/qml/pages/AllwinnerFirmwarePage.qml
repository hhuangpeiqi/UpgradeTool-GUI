import UpgradeTool
import QtQuick
import QtQuick.Layouts

AllwinnerWorkspace {
    id: page
    objectName: "AllwinnerFirmwarePage"
    context: 1
    title: qsTr("Flash Firmware")
    property var selectedPartitions: []
    signal toolsRequested(string path)
    onFirmwareChanged: selectedPartitions = []

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 12
        rowSpacing: 10
        enabled: !app.interactionLocked
        Text { text: qsTr("Flash Mode"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppComboBox {
            id: mode
            objectName: "awFlashMode"
            Layout.fillWidth: true
            model: [qsTr("Flash selected partitions"), qsTr("Keep user data"), qsTr("Erase partitions, then flash"), qsTr("Full erase, then flash")]
            currentIndex: Number(page.setting("mode", 3))
            onActivated: page.saveSetting("mode", currentIndex)
        }
        Text { text: qsTr("After Finish"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppComboBox {
            id: postAction
            Layout.fillWidth: true
            model: [qsTr("Reboot"), qsTr("Power off"), qsTr("Shut down")]
            currentIndex: Number(page.setting("postAction", 0))
            onActivated: page.saveSetting("postAction", currentIndex)
        }
    }
    AppCheckBox {
        focusPolicy: Qt.StrongFocus
        id: verify
        objectName: "awVerify"
        text: qsTr("Verify after write")
        enabled: !app.interactionLocked
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
            iconSource: Icons.playWhite
            focusPolicy: Qt.StrongFocus
            objectName: "awFlashStart"
            text: app.batchMode ? qsTr("Batch flash") : qsTr("Start Flashing")
            reserveText: app.batchMode ? qsTr("Start Flashing") : qsTr("Batch flash")
            enabled: !app.interactionLocked && page.valid && app.hasFlashTarget && (mode.currentIndex !== 0 || page.selectedPartitions.length > 0)
            onClicked: page.execute(Operations.AllwinnerFlash, {mode: mode.currentIndex, verify: verify.checked,
                postAction: postAction.currentIndex, partitions: page.selectedPartitions})
        }
    ]
}
