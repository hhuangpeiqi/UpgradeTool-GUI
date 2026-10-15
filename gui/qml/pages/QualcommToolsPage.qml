import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

QualcommWorkspace {
    id: page
    objectName: "QualcommToolsPage"
    title: qsTr("Maintenance & Offline Tools")
    property int selectedTool: 0
    readonly property bool compact: width < 760 || height < 590
    readonly property var tools: [
        {label: qsTr("Chip info"), group: qsTr("Device maintenance"), context: "chipinfo", operation: Operations.QualcommChipInfo},
        {label: qsTr("Reset"), group: qsTr("Device maintenance"), context: "reset", operation: Operations.QualcommReset},
        {label: qsTr("RAM dump"), group: qsTr("Device maintenance"), context: "ramdump", operation: Operations.QualcommRamdump},
        {label: qsTr("Sahara kickstart"), group: qsTr("Device maintenance"), context: "kickstart", operation: Operations.QualcommKickstart},
        {label: qsTr("Create ZIP"), group: qsTr("Offline tools"), context: "zip", operation: Operations.QualcommCreateZip},
        {label: qsTr("Create Sahara archive"), group: qsTr("Offline tools"), context: "archive", operation: Operations.QualcommCreateArchive},
        {label: qsTr("Create VIP digests"), group: qsTr("Offline tools"), context: "digests", operation: Operations.QualcommCreateDigests},
        {label: qsTr("UFS provisioning"), group: "UFS", context: "provision", operation: Operations.QualcommProvision}
    ]
    readonly property var currentTool: tools[selectedTool]
    context: currentTool.context
    hasNavigation: !compact
    hasHeaderContent: compact
    showEdlSwitch: selectedTool < 4 || selectedTool === 7
    footerMessage: {
        app.qualcomm.settings;
        var error = app.qualcomm.validationError(currentTool.operation);
        if (error) return error;
        return app.qualcomm.needsDevice(currentTool.operation) ? app.qualcommEligibility[currentTool.operation].reason : "";
    }
    function selectTool(index) {
        if (!app.busy && index >= 0 && index < tools.length) selectedTool = index;
    }
    onSelectedToolChanged: resetScroll()

    navigation: ColumnLayout {
        objectName: "qcToolNavigation"
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: !page.compact
        spacing: 4
        Repeater {
            model: page.tools.length
            ColumnLayout {
                required property int index
                Layout.fillWidth: true
                spacing: 4
                Text {
                    visible: index === 0 || index === 4 || index === 7
                    Layout.fillWidth: true
                    Layout.topMargin: index === 0 ? 0 : 14
                    Layout.bottomMargin: 4
                    text: page.tools[index].group
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                }
                ItemDelegate {
                    objectName: "qcToolSelect-" + page.tools[index].context
                    Layout.fillWidth: true
                    implicitHeight: 36
                    focusPolicy: Qt.StrongFocus
                    text: page.tools[index].label
                    highlighted: page.selectedTool === index
                    onClicked: page.selectTool(index)
                    contentItem: Text {
                        text: parent.text
                        color: parent.highlighted ? Theme.primary : Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontNormal
                        font.bold: parent.highlighted
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        radius: Theme.controlRadius
                        color: parent.highlighted ? Theme.selected : parent.hovered ? Theme.hover : "transparent"
                        border.width: parent.visualFocus ? 1 : 0
                        border.color: Theme.primary
                    }
                }
            }
        }
        Item { Layout.fillHeight: true }
    }
    headerContent: QualcommComboBox {
        objectName: "qcToolSelector"
        Layout.fillWidth: true
        visible: page.compact
        enabled: !app.busy
        model: page.tools.map(function(tool) { return tool.group + " / " + tool.label; })
        displayText: page.currentTool.label
        selectedIndex: page.selectedTool
        onActivated: page.selectTool(currentIndex)
    }
    // A stable count keeps each form alive when translated labels change.
    Repeater {
        id: panels
        model: page.tools.length
        QualcommToolPanel {
            required property int index
            toolIndex: index
            toolLabel: page.tools[index].label
            title: page.compact ? "" : toolLabel
            operation: page.tools[index].operation
            context: page.tools[index].context
            visible: index === page.selectedTool
        }
    }
    actions: Repeater {
        model: page.tools.length
        PrimaryButton {
            required property int index
            readonly property int operation: page.tools[index].operation
            readonly property var settings: { app.qualcomm.settings; return app.qualcomm.settingsFor(page.tools[index].context); }
            readonly property bool valid: { app.qualcomm.settings; return app.qualcomm.canRun(operation); }
            readonly property bool deviceRequired: { app.qualcomm.settings; return app.qualcomm.needsDevice(operation); }
            objectName: "qcToolStart-" + page.tools[index].context
            iconSource: Icons.playWhite
            focusPolicy: Qt.StrongFocus
            visible: index === page.selectedTool
            text: index === 7 && settings.dryRun
                  ? qsTranslate("QualcommToolPanel", "Simulate UFS provisioning") : page.tools[index].label
            enabled: !app.busy && valid && (!deviceRequired || app.qualcommEligibility[operation].allowed)
            onClicked: app.qualcomm.run(operation)
            ToolTip.visible: hovered && !enabled && !app.busy
            ToolTip.text: page.footerMessage
        }
    }
}
