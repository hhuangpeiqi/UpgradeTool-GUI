import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    property string context: "flash"
    property string settingKey: ""
    property string caption: ""
    property string placeholder: ""
    property int pickerMode: AppFilePicker.OpenFile
    property var nameFilters: []
    property bool secondaryBrowse: false
    property string value: { app.qualcomm.settings; return String(app.qualcomm.settingsFor(context)[settingKey] || ""); }
    signal edited(string path)
    Layout.fillWidth: true
    spacing: 10
    Text {
        text: root.caption
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
        Layout.preferredWidth: Theme.formLabelWidth
        elide: Text.ElideRight
    }
    AppTextField {
        id: field
        Layout.fillWidth: true
        font.pixelSize: Theme.fontNormal
        text: root.value
        placeholderText: root.placeholder !== "" ? root.placeholder
                                               : root.pickerMode === AppFilePicker.OpenFolder ? qsTr("Select a directory") : qsTr("Enter a path or browse for a file")
        onTextEdited: { app.qualcomm.setContextValue(root.context, root.settingKey, text.trim()); root.edited(text.trim()); }
    }
    PrimaryButton { visible: !root.secondaryBrowse; text: qsTr("Browse…"); iconSource: Icons.folderWhite; focusPolicy: Qt.StrongFocus; onClicked: picker.open() }
    SecondaryButton { visible: root.secondaryBrowse; text: qsTr("Browse…"); iconSource: Icons.folderGray; focusPolicy: Qt.StrongFocus; onClicked: picker.open() }
    AppFilePicker {
        id: picker
        objectName: "qcPathPicker-" + root.settingKey
        // Bind the request context instead of snapshotting it on visibleChanged:
        // a row that is visible from the start never emits visibleChanged, and a
        // shared row keeps the same visibility while its context changes.
        property string requestContext: root.context
        titleText: root.caption
        mode: root.pickerMode
        nameFilters: root.nameFilters
        persistKey: "qualcomm/" + root.context + "/" + root.settingKey
        onAccepted: {
            var path = app.toLocalPath(root.pickerMode === AppFilePicker.OpenFolder ? selectedFolder : selectedFile);
            app.qualcomm.setContextValue(requestContext, root.settingKey, path);
            root.edited(path);
        }
    }
}
