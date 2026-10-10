import UpgradeTool
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var partitions: []
    property var selected: []
    property bool selectable: false
    signal selectionChanged(var names)
    spacing: 8
    Text { text: qsTr("固件分区"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMedium }
    Text {
        visible: root.partitions.length === 0
        text: qsTr("选择固件后显示分区；部分引导包不包含分区表。")
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
    }
    Repeater {
        model: root.partitions
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            AppCheckBox {
                visible: root.selectable
                focusPolicy: Qt.StrongFocus
                objectName: "awPartition_" + modelData.name
                text: modelData.name
                checked: root.selected.indexOf(modelData.name) >= 0
                enabled: !app.busy && modelData.writable
                onToggled: {
                    var names = root.selected.slice();
                    var index = names.indexOf(modelData.name);
                    if (checked && index < 0) names.push(modelData.name);
                    if (!checked && index >= 0) names.splice(index, 1);
                    root.selectionChanged(names);
                }
            }
            Text {
                visible: !root.selectable
                text: modelData.name
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("扇区 %1 · 长度 %2").arg(modelData.begin).arg(modelData.count)
                color: Theme.textSecondary
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
