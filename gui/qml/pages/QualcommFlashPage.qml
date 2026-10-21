import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

QualcommWorkspace {
    id: page
    context: "flash"
    objectName: "QualcommFlashPage"
    title: qsTr("Qualcomm Download")
    readonly property bool packageBuild: Boolean(page.settings.packageBuild || false)
    // Touch settings so the binding recomputes on every controller change.
    readonly property bool valid: { app.qualcomm.settings; return app.qualcomm.canRun(Operations.QualcommFlash) }
    readonly property bool deviceRequired: { app.qualcomm.settings; return app.qualcomm.needsDevice(Operations.QualcommFlash) }
    FormSection {
        title: qsTr("Build")
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10
            Text {
                text: qsTr("Build type")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                Layout.preferredWidth: Theme.formLabelWidth
                elide: Text.ElideRight
            }
            SegmentedControl {
                id: buildType
                objectName: "qcBuildType"
                model: [qsTr("Flat Build"), qsTr("Meta / Package Build")]
                selectedIndex: page.packageBuild ? 1 : 0
                onActivated: app.qualcomm.setContextValue(page.context, "packageBuild", currentIndex === 1)
            }
        }
    }
    FormSection {
        visible: !page.packageBuild
        title: qsTr("Programmer / Loader")
        QualcommProgrammer { context: page.context }
    }
    FormSection {
        visible: page.packageBuild
        title: qsTr("Build package")
        QualcommPathRow {
            context: page.context
            settingKey: "package"
            caption: qsTr("Package file")
            nameFilters: [qsTr("Build (*.zip *.json *.xml)")]
            placeholder: qsTr("Build package (*.zip, *.json or *.xml)")
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10
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
    }
    FormSection {
        visible: !page.packageBuild
        title: qsTr("Firmware XML")
        headerActions: PrimaryButton {
            iconSource: Icons.folderWhite
            objectName: "qcLoadXml"
            text: qsTr("Load XML…")
            onClicked: xmlList.loadXml()
        }
        QualcommXmlList { id: xmlList; objectName: "qcFlashXmlList"; context: page.context; showRowActions: false }
        Rectangle {
            visible: page.preview.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 180
            color: Theme.card
            border.color: Theme.border
            radius: Theme.controlRadius
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                Rectangle {
                    Layout.fillWidth: true
                    height: 30
                    color: Theme.tableHeader
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                        Text { text: qsTr("Operation"); Layout.preferredWidth: 70; color: Theme.textSecondary; font.family: Theme.fontFamily }
                        Text { text: qsTr("Partition"); Layout.preferredWidth: 100; color: Theme.textSecondary; font.family: Theme.fontFamily }
                        Text { text: qsTr("Image"); Layout.fillWidth: true; color: Theme.textSecondary; font.family: Theme.fontFamily }
                        Text { text: "LUN"; Layout.preferredWidth: 35; color: Theme.textSecondary; font.family: Theme.fontFamily }
                        Text { text: qsTr("Sector"); Layout.preferredWidth: 100; color: Theme.textSecondary; font.family: Theme.fontFamily }
                    }
                }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: page.preview
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: ListView.view.width; height: 28
                        color: index % 2 ? Theme.rowAlt : Theme.card
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                            Text { text: modelData.type; Layout.preferredWidth: 70; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall }
                            Text { text: modelData.label; Layout.preferredWidth: 100; elide: Text.ElideRight; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall }
                            Text { text: modelData.file; Layout.fillWidth: true; elide: Text.ElideMiddle; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall }
                            Text { text: modelData.lun; Layout.preferredWidth: 35; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall }
                            Text { text: modelData.sector; Layout.preferredWidth: 100; elide: Text.ElideRight; color: Theme.textSecondary; font.family: "monospace"; font.pixelSize: Theme.fontSmall }
                        }
                    }
                }
            }
        }
    }
    QualcommOptions { context: page.context; packageBuild: page.packageBuild }
    actions: [
        PrimaryButton {
            iconSource: Icons.playWhite
            objectName: "qcDownload"
            text: page.settings.dryRun ? qsTr("Simulate download") : qsTr("Download")
            enabled: !app.interactionLocked && page.valid && (!page.deviceRequired || app.qualcommEligibility[Operations.QualcommFlash].allowed)
            onClicked: app.qualcomm.run(Operations.QualcommFlash)

            ToolTip.visible: hovered && !enabled && !app.interactionLocked
            ToolTip.text: !page.valid ? qsTr("Select a valid programmer and XML set first")
                                       : app.qualcommEligibility[Operations.QualcommFlash].reason
        }
    ]
}
