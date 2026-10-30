import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

QualcommWorkspace {
    id: page
    context: "raw"
    objectName: "QualcommPartitionsPage"
    title: qsTr("Partition Tools")
    property var operations: []
    property int selectedAction: 0
    // Touch settings so the binding recomputes on every controller change.
    readonly property bool valid: { app.qualcomm.settings; return app.qualcomm.canRun(Operations.QualcommRaw, {rawOperations: page.operations}) }
    readonly property bool deviceRequired: { app.qualcomm.settings; return app.qualcomm.needsDevice(Operations.QualcommRaw) }
    FormSection {
        title: qsTr("Programmer / Loader")
        QualcommProgrammer { context: page.context }
    }
    FormSection {
        title: qsTr("Operation")
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10
            Text {
                text: qsTr("Action")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                Layout.preferredWidth: Theme.formLabelWidth
                elide: Text.ElideRight
            }
            QualcommComboBox {
                id: action
                objectName: "qcRawAction"
                Layout.fillWidth: true
                model: [qsTr("Read"), qsTr("Write"), qsTr("Erase"), "SHA256", qsTr("Firehose reset")]
                selectedIndex: page.selectedAction
                onActivated: page.selectedAction = currentIndex
            }
            Text {
                text: qsTr("Address")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                Layout.preferredWidth: Theme.formLabelWidth
                elide: Text.ElideRight
            }
            AppTextField { id: address; Layout.fillWidth: true; enabled: page.selectedAction !== 4; placeholderText: qsTr("LUN, LUN/sector, LUN/sector+count, or a partition name") }
        }
        QualcommPathRow {
            context: page.context; visible: page.selectedAction < 2; settingKey: "rawFile"; caption: qsTr("Binary file")
            pickerMode: page.selectedAction === 0 ? AppFilePicker.SaveFile : AppFilePicker.OpenFile
            nameFilters: [qsTr("Image files (*.img *.bin)"), qsTr("All files (*)")]
            placeholder: page.selectedAction === 0 ? qsTr("Output file for the read data") : qsTr("Binary image to write")
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            SecondaryButton {
                text: qsTr("Add")
                iconSource: Icons.plusGray
                enabled: (page.operations.length === 0 || page.operations[page.operations.length - 1].type !== 4) && (address.text.trim().length > 0 || page.selectedAction === 4) && (page.selectedAction > 1 || Boolean(page.settings.rawFile))
                onClicked: {
                    var rows = page.operations.slice();
                    rows.push({ type: page.selectedAction, address: address.text.trim(), file: String(page.settings.rawFile || "") });
                    page.operations = rows;
                }
            }
        }
    }
    FormSection {
        visible: page.operations.length > 0
        title: qsTr("Operations")
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: page.operations
                RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Text { text: [qsTr("Read"), qsTr("Write"), qsTr("Erase"), "SHA256", qsTr("Reset")][modelData.type]; color: Theme.textSecondary; Layout.preferredWidth: 80; font.family: Theme.fontFamily }
                    Text { text: modelData.address + (modelData.type < 2 ? "  ·  " + modelData.file : ""); color: Theme.textPrimary; elide: Text.ElideMiddle; Layout.fillWidth: true; font.family: Theme.fontFamily }
                    SecondaryButton { text: "↑"; enabled: index > 0 && modelData.type !== 4; onClicked: { var rows = page.operations.slice(); var row = rows.splice(index, 1)[0]; rows.splice(index - 1, 0, row); page.operations = rows; } }
                    SecondaryButton { text: "↓"; enabled: index + 1 < page.operations.length && page.operations[index + 1].type !== 4; onClicked: { var rows = page.operations.slice(); var row = rows.splice(index, 1)[0]; rows.splice(index + 1, 0, row); page.operations = rows; } }
                    SecondaryButton { text: qsTr("Remove"); iconSource: Icons.trashGray; onClicked: { var rows = page.operations.slice(); rows.splice(index, 1); page.operations = rows; } }
                }
            }
        }
        Text { visible: page.operations.length > 0 && page.operations[page.operations.length - 1].type === 4; text: qsTr("Firehose reset must be the last operation"); color: Theme.textSecondary; font.family: Theme.fontFamily; Layout.fillWidth: true; wrapMode: Text.Wrap }
    }
    QualcommOptions { context: page.context }
    KeyValueTable { Layout.fillWidth: true; Layout.preferredHeight: 140; model: page.results; visible: page.results.count > 0 }
    actions: [
        PrimaryButton {
            iconSource: Icons.playWhite
            objectName: "qcRawStart"
            text: page.settings.dryRun ? qsTr("Simulate") : qsTr("Execute")
            enabled: !app.interactionLocked && page.valid && (!page.deviceRequired || app.qualcommEligibility[Operations.QualcommRaw].allowed)
            onClicked: app.qualcomm.run(Operations.QualcommRaw, {rawOperations: page.operations})

            ToolTip.visible: hovered && !enabled && !app.interactionLocked
            ToolTip.text: !page.valid ? qsTr("Add a valid programmer and at least one operation first")
                                       : app.qualcommEligibility[Operations.QualcommRaw].reason
        }
    ]
}
