import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("Batch Select Devices")
    width: 480

    property var checkedKeys: []

    function openDialog() {
        checkedKeys = [];
        dlg.open();
    }

    Text {
        width: parent.width
        text: qsTr("Check the devices to run the same operation on, in order:")
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
        text: qsTr("Continue on failure")
        checked: true
    }

    footerContent: RowLayout {
        width: parent.width
        spacing: 10

        Item { Layout.fillWidth: true }

        SecondaryButton {
            text: qsTr("Cancel")
            onClicked: dlg.close()
        }

        PrimaryButton {
            text: qsTr("OK")
            onClicked: {
                app.setBatchTargets(dlg.checkedKeys, continueBox.checked);
                dlg.close();
            }
        }
    }
}
