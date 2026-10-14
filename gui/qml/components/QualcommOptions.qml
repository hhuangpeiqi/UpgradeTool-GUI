import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Qualcomm target (storage/transport) and the common options are all
// rarely changed once set, so the whole group lives behind one disclosure row;
// only each page's own inputs stay visible by default.
DisclosureSection {
    id: root
    objectName: "qcAdvanced"
    title: qsTr("Advanced options")
    property string context: "flash"
    property bool packageBuild: false
    readonly property bool firehose: ["flash", "raw", "digests", "provision"].indexOf(context) >= 0
    readonly property bool provision: context === "provision"
    readonly property bool device: ["zip", "archive"].indexOf(context) < 0
    // Storage type only applies to a plain Firehose flash session; the slot is
    // a rare override and lives next to it under Advanced options.
    readonly property bool storage: root.firehose && !root.provision && !root.packageBuild
    readonly property bool transport: root.device && root.context !== "digests"
    readonly property var settings: { app.qualcomm.settings; return app.qualcomm.settingsFor(context); }
    // QDLoader (QUD) is the Windows COM driver; other platforms only expose USB.
    readonly property var transportModel: Qt.platform.os === "windows" ? ["Auto", "USB", "QDLoader (COM)"] : ["Auto", "USB"]
    function setting(key, fallback) { var v = settings[key]; return v === undefined ? fallback : v; }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 10
        rowSpacing: 10
        Text {
            visible: root.storage
            text: qsTr("Storage")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            Layout.preferredWidth: Theme.formLabelWidth
            elide: Text.ElideRight
        }
        QualcommComboBox {
            visible: root.storage
            Layout.fillWidth: true
            model: ["eMMC", "NAND", "UFS", "NVMe", "SPI NOR"]
            selectedIndex: Number(root.setting("storage", 3)) - 1
            onActivated: app.qualcomm.setContextValue(root.context, "storage", currentIndex + 1)
        }
        Text {
            visible: root.transport
            text: qsTr("Transport")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            Layout.preferredWidth: Theme.formLabelWidth
            elide: Text.ElideRight
        }
        SegmentedControl {
            visible: root.transport
            model: root.transportModel
            selectedIndex: {
                var t = Number(root.setting("transport", -1));
                var index = t === 0 ? 1 : (t === 2 && root.transportModel.length > 2) ? 2 : 0;
                return Math.min(index, root.transportModel.length - 1);
            }
            onActivated: app.qualcomm.setContextValue(root.context, "transport", currentIndex === 0 ? -1 : currentIndex === 1 ? 0 : 2)
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: 14
        AppCheckBox {
            visible: root.firehose
            text: qsTr("Skip reset")
            checked: Boolean(root.setting("skipReset", false))
            focusPolicy: Qt.StrongFocus
            onToggled: app.qualcomm.setContextValue(root.context, "skipReset", checked)
        }
        AppCheckBox {
            objectName: "qcDryRun"
            visible: root.context === "flash" || root.context === "raw" || root.provision
            text: qsTr("Dry run")
            checked: Boolean(root.setting("dryRun", false))
            focusPolicy: Qt.StrongFocus
            onToggled: app.qualcomm.setContextValue(root.context, "dryRun", checked)
        }
        AppCheckBox {
            objectName: "qcDebug"
            text: qsTr("Debug log")
            checked: Boolean(root.setting("debug", false))
            focusPolicy: Qt.StrongFocus
            onToggled: app.qualcomm.setContextValue(root.context, "debug", checked)
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 10
        rowSpacing: 10
        Text {
            visible: root.storage
            text: qsTr("Storage slot")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            Layout.preferredWidth: Theme.formLabelWidth
            elide: Text.ElideRight
        }
        AppTextField {
            visible: root.storage
            Layout.fillWidth: true
            text: String(root.setting("slot", ""))
            placeholderText: qsTr("LUN / slot number; empty uses the device default")
            onTextEdited: app.qualcomm.setContextValue(root.context, "slot", text.trim())
        }
        Text {
            visible: root.device
            text: qsTr("Transfer chunk")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            Layout.preferredWidth: Theme.formLabelWidth
            elide: Text.ElideRight
        }
        AppTextField {
            visible: root.device
            Layout.fillWidth: true
            text: String(root.setting("chunkSize", ""))
            placeholderText: qsTr("Bytes per transfer; empty uses the device default")
            onTextEdited: app.qualcomm.setContextValue(root.context, "chunkSize", text.trim())
        }
    }
    QualcommPathRow {
        context: root.context; visible: root.context === "flash" || root.context === "raw"
        settingKey: "vipTables"; caption: qsTr("VIP tables"); pickerMode: AppFilePicker.OpenFolder
        placeholder: qsTr("Directory holding signed VIP digest tables")
    }
    Flow {
        Layout.fillWidth: true
        spacing: 14
        AppCheckBox {
            visible: root.firehose && !root.provision
            text: qsTr("Skip matching SHA256")
            checked: Boolean(root.setting("skipblock", false))
            focusPolicy: Qt.StrongFocus
            onToggled: app.qualcomm.setContextValue(root.context, "skipblock", checked)
        }
        AppCheckBox {
            visible: (root.context === "flash" || root.context === "digests") && !root.packageBuild
            text: qsTr("Allow missing images")
            checked: Boolean(root.setting("allowMissing", false))
            focusPolicy: Qt.StrongFocus
            onToggled: app.qualcomm.setContextValue(root.context, "allowMissing", checked)
        }
        AppCheckBox {
            visible: root.firehose && !root.provision
            text: qsTr("Allow fusing")
            checked: Boolean(root.setting("allowFusing", false))
            focusPolicy: Qt.StrongFocus
            onToggled: app.qualcomm.setContextValue(root.context, "allowFusing", checked)
        }
    }
}
