import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("批量选择设备")
    width: 480

    property var checkedKeys: []

    function openDialog() {
        checkedKeys = [];
        dlg.open();
    }

    Text {
        width: parent.width
        text: qsTr("勾选要依次执行同一操作的设备:")
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
    }

    Column {
        width: parent.width
        spacing: 6

        Repeater {
            model: app.devices

            delegate: Row {
                id: deviceRow
                width: parent.width
                height: 34
                spacing: 10

                readonly property string deviceKey: modelData.key
                readonly property string label: "#" + modelData.index + " [" + modelData.mode + "] " + modelData.serial

                CheckSquare {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: dlg.checkedKeys.indexOf(deviceRow.deviceKey) >= 0
                    onToggled: (value) => {
                        var list = dlg.checkedKeys.slice();
                        var i = list.indexOf(deviceRow.deviceKey);
                        if (value && i < 0)
                            list.push(deviceRow.deviceKey);
                        else if (!value && i >= 0)
                            list.splice(i, 1);
                        dlg.checkedKeys = list;
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: deviceRow.label
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontNormal
                }
            }
        }
    }

    AppCheckBox {
        id: continueBox
        text: qsTr("失败继续")
        checked: true
    }

    footerContent: RowLayout {
        width: parent.width
        spacing: 10

        Item { Layout.fillWidth: true }

        SecondaryButton {
            text: qsTr("取消")
            onClicked: dlg.close()
        }

        PrimaryButton {
            text: qsTr("确定")
            onClicked: {
                app.setBatchTargets(dlg.checkedKeys, continueBox.checked);
                dlg.close();
            }
        }
    }
}
