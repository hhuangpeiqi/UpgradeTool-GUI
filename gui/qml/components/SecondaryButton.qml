import UpgradeTool
import QtQuick
import QtQuick.Controls

Button {
    id: root

    property url iconSource: ""
    property int iconSize: 18
    property color textColor: Theme.textPrimary
    property int hPadding: 18

    implicitHeight: 38
    implicitWidth: Math.max(76, contentRow.implicitWidth + hPadding * 2)
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
                color: root.enabled ? root.textColor : Theme.textDisabled
                font: root.font
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    background: Rectangle {
        radius: Theme.controlRadius
        color: {
            if (!root.enabled)
                return Theme.buttonFaceDisabled;
            if (root.pressed)
                return Theme.buttonFacePressed;
            if (root.hovered)
                return Theme.buttonFaceHover;
            return Theme.buttonFace;
        }
        border.width: 1
        border.color: root.enabled ? Theme.borderStrong : Theme.border
    }
}
