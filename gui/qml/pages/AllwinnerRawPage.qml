import UpgradeTool
import QtQuick
import QtQuick.Layouts

AllwinnerWorkspace {
    id: page
    objectName: "AllwinnerRawPage"
    context: 0
    title: qsTr("Raw Image Flash")
    fileCaption: qsTr("Boot package")

    FilePathRow {
        id: rawImage
        objectName: "awRawImage"
        Layout.fillWidth: true
        caption: qsTr("Image to write")
        placeholder: qsTr("Select the image file to write")
        text: page.setting("rawImage", "")
        enabled: !app.interactionLocked
        persistKey: "allwinnerRaw"
        onEdited: page.saveSetting("rawImage", text)
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: qsTr("Storage medium"); color: Theme.textSecondary; font.family: Theme.fontFamily; Layout.preferredWidth: 80 }
        AppComboBox {
            id: storage
            Layout.fillWidth: true
            enabled: !app.interactionLocked
            model: ["SD / eMMC", "UFS", "SPI NOR"]
            currentIndex: Number(page.setting("rawStorage", 0))
            onActivated: page.saveSetting("rawStorage", currentIndex)
        }
    }
    DisclosureSection {
        objectName: "awRawAdvanced"
        title: qsTr("Advanced options")
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 10
            enabled: !app.interactionLocked
            Text { text: qsTr("Write method"); color: Theme.textSecondary; font.family: Theme.fontFamily }
            AppComboBox {
                id: mode
                Layout.fillWidth: true
                model: [qsTr("Write from sector 0 (Command)"), qsTr("Write at logic offset (Logic offset)")]
                currentIndex: Number(page.setting("rawMode", 1))
                onActivated: page.saveSetting("rawMode", currentIndex)
            }
            Text { visible: mode.currentIndex === 1; text: qsTr("Logic offset (sectors)"); color: Theme.textSecondary; font.family: Theme.fontFamily }
            AppTextField {
                id: offset
                visible: mode.currentIndex === 1
                Layout.fillWidth: true
                placeholderText: qsTr("Leave empty to auto-detect (0x hex allowed)")
                text: page.setting("logicOffset", "")
                onEditingFinished: page.saveSetting("logicOffset", text)
            }
        }
    }
    actions: [
        PrimaryButton {
            iconSource: Icons.playWhite
            focusPolicy: Qt.StrongFocus
            objectName: "awRawStart"
            text: app.batchMode ? qsTr("Batch flash") : qsTr("Start Flashing")
            reserveText: app.batchMode ? qsTr("Start Flashing") : qsTr("Batch flash")
            enabled: !app.interactionLocked && page.valid && app.hasFlashTarget && rawImage.text.trim() !== ""
            onClicked: page.execute(Operations.AllwinnerRaw, {rawImage: rawImage.text, rawMode: mode.currentIndex,
                rawStorage: storage.currentIndex, logicOffset: offset.text})
        }
    ]
}
