import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: workspace
    property int context: 1
    property string title: ""
    property string fileCaption: qsTr("Firmware package")
    readonly property var contextState: app.allwinner.states[context]
    readonly property string firmware: contextState.firmware
    readonly property bool valid: contextState.valid
    readonly property string prefix: "page" + context + "/"
    property var saved: app.allwinner.settings()
    default property alias workspaceContent: body.data
    property alias actions: actionRow.data

    function setting(key, fallback) { return saved[prefix + key] === undefined ? fallback : saved[prefix + key]; }
    function saveSetting(key, value) {
        var values = {};
        values[prefix + key] = value;
        app.allwinner.saveSettings(values);
    }
    function execute(operation, parameters) {
        var values = parameters || {};
        values.context = context;
        app.allwinner.run(operation, values);
    }
    function ensureLoaded() {
        Qt.callLater(function() {
            if (workspace.visible && app.vendor === 1) app.allwinner.ensureContext(workspace.context);
        });
    }
    Component.onCompleted: ensureLoaded()
    onVisibleChanged: ensureLoaded()
    onFirmwareChanged: { if (firmwareRow) firmwareRow.text = firmware; }
    Connections {
        target: app
        function onVendorChanged() { workspace.ensureLoaded(); }
    }

    Connections {
        target: app.tasks
        function onBusyChanged() { if (!app.interactionLocked) workspace.ensureLoaded(); }
    }

    Card {
        anchors.fill: parent
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12
            SectionTitle { text: workspace.title; Layout.fillWidth: true }
            FilePathRow {
                nameFilters: [qsTr("Firmware (*.img)"), qsTr("All files (*)")]
                id: firmwareRow
                Layout.fillWidth: true
                caption: workspace.fileCaption
                placeholder: qsTr("Select an IMAGEWTY firmware package (.img)")
                text: workspace.firmware
                enabled: !app.interactionLocked
                persistKey: "allwinnerFirmware" + workspace.context
                onEdited: (path) => {
                    if (path !== workspace.firmware || !workspace.valid)
                        app.allwinner.setContextFirmware(workspace.context, path);
                }
            }
            ScrollView {
                id: scroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: scroll.availableWidth
                    spacing: 14
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: workspace.contextState.summary.count > 0
                        Text {
                            text: qsTr("Firmware Info")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMedium
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: width >= 480 ? 3 : 2
                            columnSpacing: 20
                            rowSpacing: 10
                            Repeater {
                                model: workspace.contextState.summary
                                delegate: ColumnLayout {
                                    required property string label
                                    required property string value
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    spacing: 3
                                    Text {
                                        text: label
                                        color: Theme.textSecondary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSmall
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: value
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontNormal
                                        elide: Text.ElideMiddle
                                    }
                                }
                            }
                        }
                    }
                    ColumnLayout { id: body; Layout.fillWidth: true; spacing: 14 }
                }
            }
            RowLayout {
                id: actionRow
                Layout.fillWidth: true
                spacing: 10
                Item {
                    visible: workspace.context === 0 || workspace.context === 1
                    Layout.fillWidth: true
                }
                RowLayout {
                    visible: workspace.context === 0 || workspace.context === 1
                    Text {
                        text: qsTr("Switch Method")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontNormal
                    }
                    AppComboBox {
                        id: switchMethod
                        objectName: "awSwitchMethod"
                        Layout.preferredWidth: 130
                        model: [qsTr("Auto"), qsTr("Alternate")]
                        enabled: !app.interactionLocked
                    }
                    SecondaryButton {
                        objectName: "awSwitchButton"
                        text: qsTr("Switch")
                        iconSource: Icons.switchBlue
                        busy: app.switching
                        enabled: !app.interactionLocked && app.adbReady
                        onClicked: app.switchToFel(switchMethod.currentIndex)
                    }
                }
            }
        }
    }
}
