import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FormSection {
    id: page
    required property int toolIndex
    required property int operation
    required property string context
    objectName: "qcToolPanel-" + context
    readonly property string toolLabel: [qsTranslate("QualcommToolsPage", "Chip info"), qsTranslate("QualcommToolsPage", "Reset"),
        qsTranslate("QualcommToolsPage", "RAM dump"), qsTranslate("QualcommToolsPage", "Sahara kickstart"),
        qsTranslate("QualcommToolsPage", "Create ZIP"), qsTranslate("QualcommToolsPage", "Create Sahara archive"),
        qsTranslate("QualcommToolsPage", "Create VIP digests"), qsTranslate("QualcommToolsPage", "UFS provisioning")][toolIndex]
    title: toolIndex < 2 ? "" : toolLabel
    readonly property var settings: { app.qualcomm.settings; return app.qualcomm.settingsFor(context); }
    readonly property var results: app.qualcomm.resultsFor(context)
    // Reevaluate validation and device requirements when any settings change.
    readonly property bool valid: { app.qualcomm.settings; return app.qualcomm.canRun(operation); }
    readonly property bool deviceRequired: { app.qualcomm.settings; return app.qualcomm.needsDevice(operation); }
    readonly property bool packageBuild: page.toolIndex === 4 || (page.toolIndex === 5 && Boolean(page.settings.archiveContents)) || (page.toolIndex === 6 && Boolean(page.settings.packageBuild))
    readonly property bool showProgrammer: (page.toolIndex === 1 && page.settings.resetMethod === "firehose") || page.toolIndex === 3 || (!page.packageBuild && page.toolIndex >= 5)
    readonly property bool showOutput: page.toolIndex === 2 || page.toolIndex === 4 || page.toolIndex === 5
    readonly property bool showDigestDir: page.toolIndex === 6
    readonly property bool showFilter: page.toolIndex === 2
    readonly property bool showNode: page.toolIndex === 3 && Qt.platform.os !== "windows"
    readonly property bool showInput: page.packageBuild || page.showProgrammer || page.showOutput || page.showDigestDir || page.showFilter || page.showNode
    readonly property bool showXml: (page.toolIndex === 6 && !page.packageBuild) || page.toolIndex === 7
    GridLayout {
        visible: page.toolIndex === 1
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 10
        rowSpacing: 10
        Text {
            text: qsTranslate("QualcommToolsPage", "Reset method")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            Layout.preferredWidth: Theme.formLabelWidth
            elide: Text.ElideRight
        }
        SegmentedControl {
            objectName: "qcResetMethod"
            model: ["Sahara", "Firehose"]
            selectedIndex: page.settings.resetMethod === "firehose" ? 1 : 0
            onSelectedIndexChanged: restoreSelection()
            onActivated: app.qualcomm.setContextValue(page.context, "resetMethod", currentIndex === 1 ? "firehose" : "sahara")
        }
    }
    FormSection {
        visible: page.toolIndex === 5 || page.toolIndex === 6
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10
            visible: page.toolIndex === 5 || page.toolIndex === 6
            Text {
                text: qsTranslate("QualcommToolsPage", "Input type")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                Layout.preferredWidth: Theme.formLabelWidth
                elide: Text.ElideRight
            }
            SegmentedControl {
                id: packageType
                model: page.toolIndex === 5 ? [qsTranslate("QualcommToolsPage", "Programmer / Sahara images"), "contents.xml"] : [qsTranslate("QualcommToolsPage", "Flat Build"), qsTranslate("QualcommToolsPage", "Meta / Package Build")]
                selectedIndex: page.packageBuild ? 1 : 0
                onActivated: app.qualcomm.setContextValue(page.context, page.toolIndex === 5 ? "archiveContents" : "packageBuild", currentIndex === 1)
            }
        }
    }
    FormSection {
        visible: page.showInput
        title: qsTranslate("QualcommToolsPage", "Input")
        QualcommProgrammer { context: page.context; visible: page.showProgrammer }
        QualcommPathRow {
            context: page.context; visible: page.packageBuild; settingKey: "package"
            caption: page.toolIndex === 6 ? qsTranslate("QualcommToolsPage", "Build package") : "contents.xml"
            nameFilters: page.toolIndex === 6 ? [qsTranslate("QualcommToolsPage", "Build (*.zip *.json *.xml)")] : ["XML (*.xml)"]
            placeholder: page.toolIndex === 6 ? qsTranslate("QualcommToolsPage", "Build package (*.zip, *.json or *.xml)") : qsTranslate("QualcommToolsPage", "Path to the contents.xml")
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10
            visible: page.packageBuild
            Text {
                text: qsTranslate("QualcommToolsPage", "Selector")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                Layout.preferredWidth: Theme.formLabelWidth
                elide: Text.ElideRight
            }
            AppTextField {
                Layout.fillWidth: true
                text: String(page.settings.selector || "")
                placeholderText: qsTranslate("QualcommToolsPage", "Optional selector inside the package")
                onTextEdited: app.qualcomm.setContextValue(page.context, "selector", text.trim())
            }
        }
        QualcommPathRow {
            context: page.context; visible: page.showOutput
            settingKey: "output"; caption: qsTranslate("QualcommToolsPage", "Output")
            pickerMode: page.toolIndex === 2 ? AppFilePicker.OpenFolder : AppFilePicker.SaveFile
            placeholder: page.toolIndex === 2 ? qsTranslate("QualcommToolsPage", "Output directory for the RAM dump")
                         : page.toolIndex === 4 ? qsTranslate("QualcommToolsPage", "Output ZIP file")
                         : qsTranslate("QualcommToolsPage", "Output Sahara archive file")
        }
        QualcommPathRow {
            context: page.context; visible: page.showDigestDir; settingKey: "digestDir"; caption: qsTranslate("QualcommToolsPage", "Digest directory")
            pickerMode: AppFilePicker.OpenFolder
            placeholder: qsTranslate("QualcommToolsPage", "Output directory for the VIP digest tables")
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10
            visible: page.showFilter
            Text {
                text: qsTranslate("QualcommToolsPage", "Segment filter")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                Layout.preferredWidth: Theme.formLabelWidth
                elide: Text.ElideRight
            }
            AppTextField {
                Layout.fillWidth: true
                text: String(page.settings.filter || "")
                placeholderText: qsTranslate("QualcommToolsPage", "Comma-separated file patterns, e.g. DDR*.bin")
                onTextEdited: app.qualcomm.setContextValue(page.context, "filter", text.trim())
            }
        }
        QualcommPathRow {
            context: page.context; visible: page.showNode; settingKey: "node"; caption: qsTranslate("QualcommToolsPage", "Device node")
            placeholder: qsTranslate("QualcommToolsPage", "Serial node, e.g. /dev/ttyUSB0 (Auto transport only)")
        }
    }
    FormSection {
        visible: page.showXml
        title: page.context === "provision" ? qsTranslate("QualcommToolsPage", "UFS configuration XML") : qsTranslate("QualcommToolsPage", "XML")
        headerActions: PrimaryButton {
            iconSource: Icons.folderWhite
            objectName: "qcLoadXml"
            text: qsTranslate("QualcommToolsPage", "Load XML…")
            onClicked: toolXml.loadXml()
        }
        QualcommXmlList { id: toolXml; context: page.context }
        AppCheckBox { visible: page.toolIndex === 7; text: qsTranslate("QualcommToolsPage", "Lock UFS configuration"); checked: Boolean(page.settings.finalize); onToggled: app.qualcomm.setContextValue(page.context, "finalize", checked) }
    }
    Loader {
        Layout.fillWidth: true
        active: page.toolIndex < 2
        visible: active
        sourceComponent: runAction
    }
    QualcommOptions { context: page.context; packageBuild: page.packageBuild }
    Loader {
        Layout.fillWidth: true
        active: page.toolIndex >= 2
        visible: active
        sourceComponent: runAction
    }
    KeyValueTable { Layout.fillWidth: true; Layout.preferredHeight: 160; model: page.results; visible: page.results.count > 0 }
    Component {
        id: runAction
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true; visible: page.toolIndex >= 2 }
            PrimaryButton {
                iconSource: Icons.playWhite
                objectName: "qcToolStart-" + page.context
                focusPolicy: Qt.StrongFocus
                text: page.context === "provision" && page.settings.dryRun ? qsTr("Simulate UFS provisioning") : page.toolLabel
                enabled: !app.busy && page.valid && (!page.deviceRequired || app.qualcommEligibility[page.operation].allowed)
                onClicked: app.qualcomm.run(page.operation)

                ToolTip.visible: hovered && !enabled && !app.busy
                ToolTip.text: !page.valid ? qsTranslate("QualcommToolsPage", "Complete the required inputs first")
                                           : app.qualcommEligibility[page.operation].reason
            }
        }
    }
}
