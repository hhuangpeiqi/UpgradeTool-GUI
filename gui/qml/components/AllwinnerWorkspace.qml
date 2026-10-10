import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: workspace
    property int context: 1
    property string title: ""
    property string fileCaption: qsTr("固件包")
    readonly property var contextState: app.allwinnerStates[context]
    readonly property string firmware: contextState.firmware
    readonly property bool valid: contextState.valid
    readonly property string prefix: "page" + context + "/"
    property var saved: app.allwinnerSettings()
    default property alias workspaceContent: body.data
    property alias actions: actionRow.data

    function setting(key, fallback) { return saved[prefix + key] === undefined ? fallback : saved[prefix + key]; }
    function saveSetting(key, value) {
        var values = {};
        values[prefix + key] = value;
        app.saveAllwinnerSettings(values);
    }
    function execute(operation, parameters) {
        var values = parameters || {};
        values.context = context;
        app.runAllwinner(operation, values);
    }
    function ensureLoaded() {
        Qt.callLater(function() {
            if (workspace.visible && app.vendor === 1) app.ensureAllwinnerContext(workspace.context);
        });
    }
    Component.onCompleted: ensureLoaded()
    onVisibleChanged: ensureLoaded()
    onFirmwareChanged: { if (firmwareRow) firmwareRow.text = firmware; }
    Connections {
        target: app
        function onVendorChanged() { workspace.ensureLoaded(); }
        function onBusyChanged() { if (!app.busy) workspace.ensureLoaded(); }
    }

    Card {
        anchors.fill: parent
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12
            SectionTitle { text: workspace.title; Layout.fillWidth: true }
            FilePathRow {
                id: firmwareRow
                Layout.fillWidth: true
                caption: workspace.fileCaption
                placeholder: qsTr("请选择 IMAGEWTY 固件包（.img）")
                text: workspace.firmware
                enabled: !app.busy
                persistKey: "allwinnerFirmware" + workspace.context
                onEdited: (path) => {
                    if (path !== workspace.firmware || !workspace.valid)
                        app.setAllwinnerContextFirmware(workspace.context, path);
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
                            text: qsTr("固件信息")
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
                        text: qsTr("切换方式")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontNormal
                    }
                    AppComboBox {
                        id: switchMethod
                        objectName: "awSwitchMethod"
                        Layout.preferredWidth: 130
                        model: [qsTr("自动"), qsTr("备选")]
                        enabled: !app.busy
                    }
                    SecondaryButton {
                        objectName: "awSwitchButton"
                        text: qsTr("切换")
                        busy: app.switching
                        enabled: !app.busy && app.adbReady
                        onClicked: app.switchToFel(switchMethod.currentIndex)
                    }
                }
            }
        }
    }
}
