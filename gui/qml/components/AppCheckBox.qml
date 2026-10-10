import UpgradeTool
import QtQuick
import QtQuick.Controls

CheckBox {
    id: root

    spacing: 8
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontMedium

    background: Rectangle {
        color: "transparent"
        radius: Theme.controlRadius
        border.width: root.activeFocus ? 1 : 0
        border.color: Theme.primary
    }

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: 0
        y: (root.height - height) / 2
        radius: 4
        color: root.checked ? Theme.primary : Theme.card
        border.width: root.checked ? 0 : 1
        border.color: root.hovered && root.enabled ? Theme.primary
                                                    : (root.enabled ? Theme.borderStrong
                                                                    : Theme.border)

        Image {
            anchors.centerIn: parent
            width: 12
            height: 12
            source: Icons.checkWhite
            visible: root.checked
        }
    }

    contentItem: Text {
        leftPadding: 26
        text: root.text
        font: root.font
        color: root.enabled ? Theme.textPrimary : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
