import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: root
    objectName: "qcEdlDialog"
    width: Math.min(680, parent ? parent.width - 24 : 680)
    titleText: qsTr("Select switch target")
    property string selectedTargetId: ""
    property string selectedEndpointId: ""
    readonly property var candidates: app.edl.targets
    readonly property var target: {
        for (var i = 0; i < candidates.length; ++i)
            if (candidates[i].id === selectedTargetId) return candidates[i];
        return null;
    }
    readonly property var endpoints: target ? target.endpoints : []
    readonly property bool ready: {
        app.edl.targets;
        return app.edl.ready && !app.interactionLocked && app.edl.canStart(selectedTargetId, selectedEndpointId);
    }
    function syncSelection() {
        if (!device || !port) return;
        var targets = root.candidates || [];
        var index = -1;
        for (var i = 0; i < targets.length; ++i)
            if (targets[i].id === selectedTargetId) index = i;
        if (index < 0) selectedTargetId = "";
        device.currentIndex = index;
        var ports = root.endpoints || [];
        var portIndex = 0;
        for (var j = 0; j < ports.length; ++j)
            if (ports[j].id === selectedEndpointId) portIndex = j + 1;
        if (portIndex === 0 && selectedEndpointId !== "") {
            // Losing an explicitly chosen interface must not fall back to an
            // automatic interface on the same device.
            selectedTargetId = "";
            selectedEndpointId = "";
            device.currentIndex = -1;
        }
        port.currentIndex = portIndex;
    }
    function openSelection() {
        selectedEndpointId = "";
        selectedTargetId = candidates.length === 1 ? candidates[0].id : "";
        syncSelection();
        open();
    }
    onCandidatesChanged: syncSelection()
    onClosed: { selectedTargetId = ""; selectedEndpointId = ""; }
    Connections {
        target: app
        function onVendorChanged() { root.close(); }
    }
    ColumnLayout {
        width: parent.width
        spacing: 12
        enabled: !app.interactionLocked
        Text { text: qsTr("Device"); color: Theme.textSecondary; font.family: Theme.fontFamily }
        AppComboBox {
            id: device
            objectName: "qcEdlDevice"
            Layout.fillWidth: true
            model: root.candidates.map(function(t) { return t.label; })
            currentIndex: -1
            onActivated: {
                root.selectedEndpointId = "";
                root.selectedTargetId = root.candidates[currentIndex].id;
                root.syncSelection();
            }
        }
        Text {
            visible: app.edl.method === 1
            text: qsTr("Diagnostic interface")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
        }
        AppComboBox {
            id: port
            objectName: "qcEdlPort"
            visible: app.edl.method === 1
            Layout.fillWidth: true
            model: [qsTr("Automatic (recognized interfaces)")].concat(root.endpoints.map(function(e) { return e.label; }))
            onActivated: root.selectedEndpointId = currentIndex > 0 ? root.endpoints[currentIndex - 1].id : ""
        }
    }
    footerContent: RowLayout {
        width: parent.width
        spacing: 10
        SecondaryButton {
            text: qsTr("Refresh")
            enabled: !app.interactionLocked && !app.edl.discovering
            onClicked: app.refreshEdl()
        }
        Item { Layout.fillWidth: true }
        SecondaryButton { text: qsTr("Cancel"); onClicked: root.close() }
        Item {
            implicitWidth: startButton.implicitWidth
            implicitHeight: startButton.implicitHeight
            PrimaryButton {
                id: startButton
                objectName: "qcEdlStart"
                anchors.fill: parent
                text: qsTr("Switch")
                enabled: root.ready && root.target !== null && root.candidates.length > 0
                hoverEnabled: enabled
                onClicked: {
                    var target = root.selectedTargetId;
                    var endpoint = root.selectedEndpointId;
                    var method = app.edl.method;
                    root.close();
                    app.switchToEdl(target, method, endpoint);
                }
            }
            HoverHandler { id: startHover }
            ToolTip {
                objectName: "qcEdlStartTooltip"
                visible: startHover.hovered && !startButton.enabled && !app.interactionLocked
                text: {
                    if (!app.edl.ready) return app.edl.unavailableReason +
                        (app.edl.discoveryError !== "" ? "\n" + app.edl.discoveryError : "");
                    if (root.target === null) return qsTr("Select a device first");
                    return qsTr("Select one diagnostic interface before switching.");
                }
            }
        }
    }
}
