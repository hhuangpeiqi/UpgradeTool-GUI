import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("Generate GPT")
    width: Math.min(520, Overlay.overlay ? Overlay.overlay.width - 32 : 800)
    property bool validationFailed: false
    onOpened: validationFailed = false
    hintText: validationFailed ? qsTr("Enter the input parameter and output GPT") : ""
    hintError: true

    Column {
        width: parent.width
        spacing: Theme.dialogControlSpacing

        Row {
            width: parent.width
            spacing: Theme.dialogControlSpacing

            Text {
                text: qsTr("Input parameter")
                width: 120
                height: 38
                verticalAlignment: Text.AlignVCenter
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
            }

            AppTextField {
                id: inputField
                objectName: "gptInputField"
                width: parent.width - 220
                placeholderText: qsTr("parameter.txt path")
            }

            SecondaryButton {
                text: qsTr("Choose…")
                onClicked: {
                    picker.onPicked = function (p) {
                        inputField.text = p;
                    };
                    picker.open();
                }
            }
        }

        Row {
            width: parent.width
            spacing: Theme.dialogControlSpacing

            Text {
                text: qsTr("Output GPT")
                width: 120
                height: 38
                verticalAlignment: Text.AlignVCenter
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
            }

            AppTextField {
                id: outputField
                objectName: "gptOutputField"
                width: parent.width - 220
                placeholderText: qsTr("Output gpt file path")
            }

            SecondaryButton {
                text: qsTr("Choose…")
                onClicked: {
                    savePicker.onPicked = function (p) {
                        outputField.text = p;
                    };
                    savePicker.open();
                }
            }
        }
    }

    footerContent: [
        SecondaryButton {
            text: qsTr("Cancel")
            onClicked: dlg.close()
        },
        PrimaryButton {
            objectName: "gptGenerateButton"
            text: qsTr("Generate")
            onClicked: {
                dlg.validationFailed = !inputField.text.trim() || !outputField.text.trim();
                if (dlg.validationFailed)
                    return;
                app.rockchip.createGpt(inputField.text, outputField.text);
                dlg.close();
            }
        }
    ]

    AppFilePicker {
        id: picker
        mode: AppFilePicker.OpenFile
        property var onPicked: null
        titleText: qsTr("Select Input Parameter")
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
        titleText: qsTr("Select Output GPT")
        persistKey: "gpt_output"
        onAccepted: {
            var file = app.toLocalPath(selectedFile.toString());
            if (savePicker.onPicked)
                savePicker.onPicked(file);
        }
    }
}
