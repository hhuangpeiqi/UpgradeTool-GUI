import UpgradeTool
import QtQuick
import QtQuick.Layouts

AppDialog {
    id: dialog
    objectName: "AllwinnerDeviceDialog"
    titleText: qsTr("Allwinner Device Info")
    width: 480
    readonly property var deviceState: app.allwinner.states[3]
    hintText: deviceState.error ? deviceState.hint : ""
    hintError: deviceState.error
    onOpened: app.allwinner.run(Operations.AllwinnerDeviceInfo)
    KeyValueTable { width: parent.width; implicitHeight: 220; model: dialog.deviceState.results }
    footerContent: RowLayout {
        SecondaryButton {
            focusPolicy: Qt.StrongFocus
            text: qsTr("Read Again")
            enabled: !app.interactionLocked && app.vendor === 1 && app.hasDevice
            onClicked: app.allwinner.run(Operations.AllwinnerDeviceInfo)
        }
        SecondaryButton {
            focusPolicy: Qt.StrongFocus; text: qsTr("Close"); onClicked: dialog.close() }
    }
}
