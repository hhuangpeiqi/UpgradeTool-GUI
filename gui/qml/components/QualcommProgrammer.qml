import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property string context: "flash"
    readonly property var settings: { app.qualcomm.settings; return app.qualcomm.settingsFor(context); }
    Layout.fillWidth: true
    spacing: 10
    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 10
        rowSpacing: 10
        Text {
            text: qsTr("Programmer input")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            Layout.preferredWidth: Theme.formLabelWidth
            elide: Text.ElideRight
        }
        SegmentedControl {
            id: inputType
            objectName: "qcProgrammerInput"
            model: [qsTr("Programmer file"), qsTr("Sahara image mapping")]
            selectedIndex: root.settings.useImages ? 1 : 0
            onActivated: app.qualcomm.setContextValue(root.context, "useImages", currentIndex === 1)
        }
    }
    QualcommPathRow {
        context: root.context; visible: !root.settings.useImages; settingKey: "programmer"; caption: qsTr("Programmer")
        nameFilters: [qsTr("FireHose Programmer (*firehose*.mbn *firehose*.elf)"), qsTr("All files (*)")]
        placeholder: qsTr("Firehose programmer (*firehose*.mbn or *firehose*.elf)")
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        visible: Boolean(root.settings.useImages)
        Item { Layout.preferredWidth: Theme.formLabelWidth }
        Text { Layout.fillWidth: true; text: qsTr("Sahara images (ID:path)"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
    }
    ScrollView {
        visible: Boolean(root.settings.useImages)
        Layout.fillWidth: true
        Layout.preferredHeight: 100
        TextArea {
            objectName: "qcImages"
            text: String(root.settings.images || "")
            placeholderText: qsTr("One mapping per line - ID:path, e.g. 13:/path/prog_firehose_ddr.elf")
            font.family: "monospace"; font.pixelSize: Theme.fontSmall; color: Theme.textPrimary; selectByMouse: true
            placeholderTextColor: Theme.textDisabled
            background: Rectangle { color: Theme.card; border.color: Theme.border; radius: Theme.controlRadius }
            onTextChanged: if (activeFocus) app.qualcomm.setContextValue(root.context, "images", text)
        }
    }
}
