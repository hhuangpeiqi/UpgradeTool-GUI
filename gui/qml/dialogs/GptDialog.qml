import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("生成 GPT")
    width: 520

    Column {
        width: parent.width
        spacing: 10

        Row {
            width: parent.width
            spacing: 10

            Text {
                text: qsTr("输入 parameter")
                width: 120
                height: 38
                verticalAlignment: Text.AlignVCenter
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
            }

            AppTextField {
                id: inputField
                width: parent.width - 220
                placeholderText: qsTr("parameter.txt 路径")
            }

            SecondaryButton {
                text: qsTr("选择…")
                onClicked: {
                    picker.onPicked = function (p) { inputField.text = p; };
                    picker.open();
                }
            }
        }

        Row {
            width: parent.width
            spacing: 10

            Text {
                text: qsTr("输出 GPT")
                width: 120
                height: 38
                verticalAlignment: Text.AlignVCenter
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
            }

            AppTextField {
                id: outputField
                width: parent.width - 220
                placeholderText: qsTr("输出 gpt 文件路径")
            }

            SecondaryButton {
                text: qsTr("选择…")
                onClicked: {
                    savePicker.onPicked = function (p) { outputField.text = p; };
                    savePicker.open();
                }
            }
        }
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
            text: qsTr("生成")
            onClicked: {
                app.createGpt(inputField.text, outputField.text);
                dlg.close();
            }
        }
    }

    AppFilePicker {
        id: picker
        mode: AppFilePicker.OpenFile
        property var onPicked: null
        titleText: qsTr("选择输入 parameter")
        persistKey: "parameter"
        onAccepted: {
            var file = app.toLocalPath(selectedFile.toString());
            if (picker.onPicked)
                picker.onPicked(file);
        }
    }

    AppFilePicker {
        id: savePicker
        mode: AppFilePicker.SaveFile
        property var onPicked: null
        titleText: qsTr("选择输出 GPT")
        persistKey: "gpt_output"
        onAccepted: {
            var file = app.toLocalPath(selectedFile.toString());
            if (savePicker.onPicked)
                savePicker.onPicked(file);
        }
    }
}
