import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property string context: "flash"
    property var pendingPrograms: []
    property bool openPatchNext: false
    // The Download list is read-only (re-run Load XML to replace); the Tools
    // list still offers reorder and removal.
    property bool showRowActions: true
    function localPaths(urls) { return urls.map(function(url) { return app.toLocalPath(url); }); }
    function loadXml() {
        // Default the picker to the primary input's folder (programmer,
        // package or Sahara image), matching QFIL's programmer-anchored flow.
        var folder = app.qualcomm.xmlFolder(context);
        var initial = folder ? app.pathUrl(folder) : "";
        if (context !== "flash") { picker.initialFolder = initial; picker.open(); return; }
        pendingPrograms = [];
        openPatchNext = false;
        programPicker.initialFolder = initial;
        programPicker.open();
    }
    readonly property var entries: { app.qualcomm.settings; return app.qualcomm.xmlEntriesFor(context); }
    Layout.fillWidth: true
    spacing: 6
    Rectangle {
        visible: root.entries.length === 0
        Layout.fillWidth: true
        implicitHeight: 40
        radius: Theme.controlRadius
        color: "transparent"
        border.width: 1
        border.color: Theme.border
        Text {
            anchors.centerIn: parent
            width: parent.width - 32
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: root.context === "provision"
                  ? qsTr("No XML loaded. Use Load XML… to choose a UFS configuration file.")
                  : qsTr("No XML loaded. Use Load XML… to choose rawprogram and patch files.")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
        }
    }
    Repeater {
        model: root.entries
        RowLayout {
            required property var modelData
            required property int index
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: root.context === "provision" ? "UFS" : modelData.kind === "patch" ? qsTr("Patch") : qsTr("RawProgram")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                Layout.preferredWidth: 92
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: modelData.name
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                elide: Text.ElideMiddle
                ToolTip.visible: hover.hovered
                ToolTip.text: modelData.path
                HoverHandler { id: hover }
            }
            SecondaryButton {
                objectName: "qcXmlRowMoveUp-" + root.context
                visible: root.showRowActions
                text: "↑"; enabled: index > 0
                onClicked: app.qualcomm.moveXml(index, -1, root.context)
            }
            SecondaryButton {
                objectName: "qcXmlRowMoveDown-" + root.context
                visible: root.showRowActions
                text: "↓"; enabled: index + 1 < root.entries.length
                onClicked: app.qualcomm.moveXml(index, 1, root.context)
            }
            SecondaryButton {
                objectName: "qcXmlRowRemove-" + root.context
                visible: root.showRowActions
                text: qsTr("Remove")
                iconSource: Icons.trashGray
                onClicked: app.qualcomm.removeXml(index, root.context)
            }
        }
    }
    AppFilePicker {
        id: programPicker
        objectName: "qcProgramPicker"
        titleText: qsTr("Select rawprogram XML")
        multiple: true
        nameFilters: ["rawprogram (rawprogram*.xml)", "XML (*.xml)"]
        persistKey: "qualcomm/flash/rawprogram"
        validateSelection: function(paths) { return app.qualcomm.validateXmlSelection(paths, false); }
        onAccepted: {
            root.pendingPrograms = root.localPaths(selectedFiles);
            root.openPatchNext = true;
        }
        onClosed: {
            if (!root.openPatchNext) return;
            root.openPatchNext = false;
            patchPicker.initialFolder = app.pathUrl(app.dirOf(root.pendingPrograms[0]));
            patchPicker.open();
        }
    }
    AppFilePicker {
        id: patchPicker
        objectName: "qcPatchPicker"
        titleText: qsTr("Select patch XML")
        multiple: true
        nameFilters: ["patch (patch*.xml)", "XML (*.xml)"]
        persistKey: "qualcomm/flash/patch"
        skipText: qsTr("Skip patch")
        validateSelection: function(paths) {
            var error = app.qualcomm.validateXmlSelection(root.pendingPrograms, false);
            return error || app.qualcomm.validateXmlSelection(paths, true);
        }
        onAccepted: app.qualcomm.replaceFlashXml(root.pendingPrograms, root.localPaths(selectedFiles))
        onSkipped: app.qualcomm.replaceFlashXml(root.pendingPrograms, [])
        onClosed: root.pendingPrograms = []
    }
    AppFilePicker {
        id: picker
        objectName: "qcPathPicker-xml-" + root.context
        // Bind the request context instead of snapshotting it on visibleChanged;
        // see QualcommPathRow.qml.
        property string requestContext: root.context
        titleText: qsTr("Load XML"); nameFilters: ["XML (*.xml)"]; persistKey: "qualcomm/" + root.context + "/xml"
        onAccepted: app.qualcomm.addXml(app.toLocalPath(selectedFile), requestContext)
    }
}
