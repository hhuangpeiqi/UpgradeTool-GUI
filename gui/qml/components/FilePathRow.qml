import UpgradeTool
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root
    property alias text: field.text
    property string caption: ""
    property string placeholder: ""
    property string persistKey: "allwinner"
    property int pickerMode: AppFilePicker.OpenFile
    signal edited(string path)
    spacing: 10
    Text {
        text: root.caption
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
        Layout.preferredWidth: 80
    }
    AppTextField {
        id: field
        Layout.fillWidth: true
        placeholderText: root.placeholder !== "" ? root.placeholder
                                               : root.pickerMode === AppFilePicker.OpenFolder ? qsTr("选择输出目录") : qsTr("输入路径或浏览文件")
        selectByMouse: true
        onEditingFinished: root.edited(text.trim())
    }
    SecondaryButton { focusPolicy: Qt.StrongFocus; text: qsTr("浏览…"); onClicked: picker.open() }
    AppFilePicker {
        id: picker
        titleText: root.caption
        mode: root.pickerMode
        persistKey: root.persistKey
        onAccepted: {
            field.text = app.toLocalPath(root.pickerMode === AppFilePicker.OpenFolder ? selectedFolder : selectedFile);
            root.edited(field.text);
        }
    }
}
