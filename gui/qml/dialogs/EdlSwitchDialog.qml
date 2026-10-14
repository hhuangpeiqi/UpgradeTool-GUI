import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: root
    objectName: "qcEdlDialog"
    width: 680
    titleText: qsTr("Switch to EDL")
    // Last configuration confirmed from this dialog; the main window reuses it
    // so the toolbar "Switch" button can switch without reopening the dialog.
    property string confirmedTargetId: ""
    property int confirmedMethod: 0
    property string confirmedEndpointId: ""
    property var candidates: app.edl.targets
    property var target: candidates && candidates.length && device && device.currentIndex >= 0 ? candidates[device.currentIndex] : null
    property var diagEndpoints: target && target.endpoints ? target.endpoints.filter(function(e) { return !e.adb; }) : []
    property bool hasAdb: target && target.endpoints ? target.endpoints.some(function(e) { return e.adb; }) : false
    property string targetId: target ? target.id : ""
    onTargetIdChanged: port.currentIndex = 0
    onCandidatesChanged: restoreSelection()
    onOpened: restoreSelection()
    function restoreSelection() {
        if (!device || !method || !port) return;
        var targets = root.candidates || [];
        var index = 0;
        for (var i = 0; i < targets.length; ++i)
            if (targets[i].id === root.confirmedTargetId) { index = i; break; }
        method.currentIndex = root.confirmedMethod;
        device.currentIndex = targets.length ? index : -1;
        var ports = root.diagEndpoints || [];
        var portIndex = 0;
        for (var j = 0; j < ports.length; ++j)
            if (ports[j].id === root.confirmedEndpointId) { portIndex = j + 1; break; }
        port.currentIndex = portIndex;
    }
    readonly property string endpointId: port.currentIndex > 0 && port.currentIndex <= diagEndpoints.length ? diagEndpoints[port.currentIndex - 1].id : ""
    readonly property int suggestedCount: diagEndpoints.filter(function(e) { return e.suggested; }).length
    readonly property bool diagReady: endpointId !== "" || suggestedCount === 1
    readonly property bool ready: target !== null && (method.currentIndex === 2 ? hasAdb : method.currentIndex === 1 ? diagReady : (diagReady || (suggestedCount === 0 && hasAdb)))
    ColumnLayout {
        width: parent.width
        spacing: 12
        enabled: !app.busy && !app.edl.discovering
        Text { text: qsTr("Device"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppComboBox {
            id: device
            objectName: "qcEdlDevice"
            Layout.fillWidth: true
            model: root.candidates.map(function(t) { return t.label; })
        }
        Text { text: qsTr("Switch method"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppComboBox {
            id: method
            objectName: "qcEdlMethod"
            Layout.fillWidth: true
            model: [qsTr("Automatic"), qsTr("Diagnostic port (DIAG)"), "ADB"]
        }
        Text {
            visible: method.currentIndex !== 2
            text: qsTr("Diagnostic interface")
            color: Theme.textSecondary; font.family: Theme.fontFamily
        }
        AppComboBox {
            id: port
            objectName: "qcEdlPort"
            visible: method.currentIndex !== 2
            Layout.fillWidth: true
            model: [qsTr("Automatic (recognized interfaces)")].concat(root.diagEndpoints.map(function(e) { return e.label; }))
        }
    }
    footerContent: RowLayout {
        width: parent.width
        SecondaryButton { text: qsTr("Refresh"); enabled: !app.busy && !app.edl.discovering; onClicked: app.refreshEdl() }
        Item { Layout.fillWidth: true }
        SecondaryButton {
            text: app.edl.running ? qsTr("Cancel switch") : qsTr("Close")
            onClicked: { if (app.edl.running) app.cancel(); else root.close(); }
        }
        PrimaryButton {
            objectName: "qcEdlStart"
            text: qsTr("Switch")
            enabled: !app.busy && !app.edl.discovering && root.ready
            onClicked: {
                root.confirmedTargetId = root.targetId;
                root.confirmedMethod = method.currentIndex;
                root.confirmedEndpointId = root.endpointId;
                app.switchToEdl(root.targetId, method.currentIndex, root.endpointId);
            }
        }
    }
}
