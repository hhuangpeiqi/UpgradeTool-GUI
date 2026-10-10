import UpgradeTool
import QtQuick
import QtQuick.Layouts

AppDialog {
    id: dialog
    objectName: "AllwinnerDeviceDialog"
    titleText: qsTr("全志设备信息")
    width: 480
    readonly property var deviceState: app.allwinnerStates[3]
    hintText: deviceState.error ? deviceState.hint : ""
    hintError: deviceState.error
    onOpened: app.runAllwinner(Operations.AllwinnerDeviceInfo)
    KeyValueTable { width: parent.width; implicitHeight: 220; model: dialog.deviceState.results }
    footerContent: RowLayout {
        SecondaryButton {
            focusPolicy: Qt.StrongFocus
            text: qsTr("重新读取")
            enabled: !app.busy && app.vendor === 1 && app.hasDevice
            onClicked: app.runAllwinner(Operations.AllwinnerDeviceInfo)
        }
        SecondaryButton {
            focusPolicy: Qt.StrongFocus; text: qsTr("关闭"); onClicked: dialog.close() }
    }
}
