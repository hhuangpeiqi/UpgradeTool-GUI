import UpgradeTool
import QtQuick
import QtQuick.Controls

Button {
    id: root

    property url iconSource: ""
    property int iconSize: 18
    property int hPadding: 18

    implicitHeight: 38
    implicitWidth: Math.max(88, contentRow.implicitWidth + hPadding * 2)
    focusPolicy: Qt.NoFocus
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontMedium
    hoverEnabled: true

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight

        Row {
            id: contentRow
            spacing: root.iconSource.toString() !== "" ? 8 : 0
            anchors.centerIn: parent

            Image {
                visible: root.iconSource.toString() !== ""
                source: root.iconSource
                sourceSize.width: root.iconSize
                sourceSize.height: root.iconSize
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: root.text
                color: root.enabled ? Theme.textOnPrimary : Theme.primaryDisabledText
                font: root.font
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    background: Rectangle {
        border.width: root.activeFocus ? 2 : 0
        border.color: Theme.textPrimary
        radius: Theme.controlRadius
        color: {
            if (!root.enabled)
                return Theme.primaryDisabledBg;
            if (root.pressed)
                return Theme.primaryPressed;
            if (root.hovered)
                return Theme.primaryHover;
            return Theme.primary;
        }
    }
}
