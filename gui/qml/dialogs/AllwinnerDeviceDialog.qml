import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dialog
    objectName: "AllwinnerDeviceDialog"
    titleText: qsTr("Allwinner Device Info")
    width: Math.min(480, Overlay.overlay ? Overlay.overlay.width - 32 : 800)
    readonly property var deviceState: app.allwinner.states[3]
    hintText: deviceState.error ? deviceState.hint : ""
    hintError: deviceState.error
    onOpened: app.allwinner.run(Operations.AllwinnerDeviceInfo)
    KeyValueTable {
        width: parent.width
        implicitHeight: 220
        model: dialog.deviceState.results
    }
    footerLeadingContent: [
        SecondaryButton {
            focusPolicy: Qt.StrongFocus
            text: qsTr("Read Again")
            enabled: !app.interactionLocked && app.vendor === 1 && app.hasFlashTarget
            onClicked: app.allwinner.run(Operations.AllwinnerDeviceInfo)
        }
    ]

    footerContent: [
        PrimaryButton {
            focusPolicy: Qt.StrongFocus
            text: qsTr("Close")
            onClicked: dialog.close()
        }
    ]
}
