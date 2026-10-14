import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

QualcommWorkspace {
    id: page
    objectName: "QualcommToolsPage"
    property int selectedTool: 0
    title: qsTr("Maintenance & Offline Tools")
    context: ["chipinfo", "reset", "ramdump", "kickstart", "zip", "archive", "digests", "provision"][page.selectedTool]
    readonly property var operationIds: [Operations.QualcommChipInfo, Operations.QualcommReset, Operations.QualcommRamdump,
        Operations.QualcommKickstart, Operations.QualcommCreateZip, Operations.QualcommCreateArchive, Operations.QualcommCreateDigests, Operations.QualcommProvision]
    // Touch settings so the binding recomputes on every controller change.
    readonly property bool valid: { app.qualcomm.settings; return app.qualcomm.canRun(page.operationIds[page.selectedTool]) }
    readonly property bool deviceRequired: { app.qualcomm.settings; return app.qualcomm.needsDevice(page.operationIds[page.selectedTool]) }
    readonly property bool packageBuild: page.selectedTool === 4 || (page.selectedTool === 5 && Boolean(page.settings.archiveContents)) || (page.selectedTool === 6 && Boolean(page.settings.packageBuild))
    readonly property bool showProgrammer: page.selectedTool === 3 || (!page.packageBuild && page.selectedTool >= 5)
    readonly property bool showOutput: page.selectedTool === 2 || page.selectedTool === 4 || page.selectedTool === 5
    readonly property bool showDigestDir: page.selectedTool === 6
    readonly property bool showFilter: page.selectedTool === 2
    readonly property bool showNode: page.selectedTool === 3 && Qt.platform.os !== "windows"
    readonly property bool showInput: page.packageBuild || page.showProgrammer || page.showOutput || page.showDigestDir || page.showFilter || page.showNode
    readonly property bool showXml: (page.selectedTool === 6 && !page.packageBuild) || page.selectedTool === 7
    FormSection {
        title: qsTr("Operation")
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10
            Text {
                text: qsTr("Operation")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                Layout.preferredWidth: Theme.formLabelWidth
                elide: Text.ElideRight
            }
            QualcommComboBox {
                id: tool
                objectName: "qcTool"
                Layout.fillWidth: true
                selectedIndex: page.selectedTool
                onActivated: page.selectedTool = currentIndex
                model: [qsTr("Chip info"), qsTr("Reset"), qsTr("RAM dump"), qsTr("Sahara kickstart"), qsTr("Create ZIP"), qsTr("Create Sahara archive"), qsTr("Create VIP digests"), qsTr("UFS provisioning")]
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10
            visible: page.selectedTool === 5 || page.selectedTool === 6
            Text {
                text: qsTr("Input type")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                Layout.preferredWidth: Theme.formLabelWidth
                elide: Text.ElideRight
            }
            SegmentedControl {
                id: packageType
                model: page.selectedTool === 5 ? [qsTr("Programmer / Sahara images"), "contents.xml"] : [qsTr("Flat Build"), qsTr("Meta / Package Build")]
                selectedIndex: page.packageBuild ? 1 : 0
                onActivated: app.qualcomm.setContextValue(page.context, page.selectedTool === 5 ? "archiveContents" : "packageBuild", currentIndex === 1)
            }
        }
    }
    FormSection {
        visible: page.showInput
        title: qsTr("Input")
        QualcommProgrammer { context: page.context; visible: page.showProgrammer }
        QualcommPathRow {
            context: page.context; visible: page.packageBuild; settingKey: "package"
            caption: page.selectedTool === 6 ? qsTr("Build package") : "contents.xml"
            nameFilters: page.selectedTool === 6 ? [qsTr("Build (*.zip *.json *.xml)")] : ["XML (*.xml)"]
            placeholder: page.selectedTool === 6 ? qsTr("Build package (*.zip, *.json or *.xml)") : qsTr("Path to the contents.xml")
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10
            visible: page.packageBuild
            Text {
                text: qsTr("Selector")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                Layout.preferredWidth: Theme.formLabelWidth
                elide: Text.ElideRight
            }
            AppTextField {
                Layout.fillWidth: true
                text: String(page.settings.selector || "")
                placeholderText: qsTr("Optional selector inside the package")
                onTextEdited: app.qualcomm.setContextValue(page.context, "selector", text.trim())
            }
        }
        QualcommPathRow {
            context: page.context; visible: page.showOutput
            settingKey: "output"; caption: qsTr("Output")
            pickerMode: page.selectedTool === 2 ? AppFilePicker.OpenFolder : AppFilePicker.SaveFile
            placeholder: page.selectedTool === 2 ? qsTr("Output directory for the RAM dump")
                         : page.selectedTool === 4 ? qsTr("Output ZIP file")
                         : qsTr("Output Sahara archive file")
        }
        QualcommPathRow {
            context: page.context; visible: page.showDigestDir; settingKey: "digestDir"; caption: qsTr("Digest directory")
            pickerMode: AppFilePicker.OpenFolder
            placeholder: qsTr("Output directory for the VIP digest tables")
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10
            visible: page.showFilter
            Text {
                text: qsTr("Segment filter")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                Layout.preferredWidth: Theme.formLabelWidth
                elide: Text.ElideRight
            }
            AppTextField {
                Layout.fillWidth: true
                text: String(page.settings.filter || "")
                placeholderText: qsTr("Comma-separated file patterns, e.g. DDR*.bin")
                onTextEdited: app.qualcomm.setContextValue(page.context, "filter", text.trim())
            }
        }
        QualcommPathRow {
            context: page.context; visible: page.showNode; settingKey: "node"; caption: qsTr("Device node")
            placeholder: qsTr("Serial node, e.g. /dev/ttyUSB0 (Auto transport only)")
        }
    }
    FormSection {
        visible: page.showXml
        title: page.context === "provision" ? qsTr("UFS configuration XML") : qsTr("XML")
        headerActions: SecondaryButton {
            objectName: "qcLoadXml"
            text: qsTr("Load XML…")
            onClicked: toolXml.loadXml()
        }
        QualcommXmlList { id: toolXml; context: page.context }
        AppCheckBox { visible: page.selectedTool === 7; text: qsTr("Lock UFS configuration"); checked: Boolean(page.settings.finalize); onToggled: app.qualcomm.setContextValue(page.context, "finalize", checked) }
    }
    QualcommOptions { context: page.context; packageBuild: page.packageBuild }
    KeyValueTable { Layout.fillWidth: true; Layout.preferredHeight: 160; model: page.results; visible: page.results.count > 0 }
    actions: [
        PrimaryButton {
            objectName: "qcToolStart"
            text: page.context === "provision" && page.settings.dryRun ? qsTr("Simulate") : qsTr("Execute")
            enabled: !app.busy && page.valid && (!page.deviceRequired || app.qualcommEligibility[page.operationIds[page.selectedTool]].allowed)
            onClicked: app.qualcomm.run(page.operationIds[page.selectedTool])

            ToolTip.visible: hovered && !enabled && !app.busy
            ToolTip.text: !page.valid ? qsTr("Complete the required inputs first")
                                       : app.qualcommEligibility[page.operationIds[page.selectedTool]].reason
        }
    ]
}
