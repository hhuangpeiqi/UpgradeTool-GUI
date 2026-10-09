import UpgradeTool
import QtQuick
import QtQuick.Controls

TextField {
    id: root

    implicitHeight: 38
    leftPadding: 12
    rightPadding: 12
    verticalAlignment: TextInput.AlignVCenter
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontMedium
    color: Theme.textPrimary
    placeholderTextColor: Theme.textDisabled
    selectByMouse: true
    selectionColor: Theme.primary
    selectedTextColor: Theme.textOnPrimary

    background: Rectangle {
        radius: Theme.controlRadius
        color: root.enabled ? Theme.card : Theme.fieldDisabled
        border.width: 1
        border.color: root.activeFocus ? Theme.primary
                                       : (root.hovered ? Theme.borderStrong : Theme.border)
    }
}
