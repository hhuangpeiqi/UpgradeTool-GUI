import UpgradeTool
import QtQuick
import QtQuick.Controls

MenuItem {
    id: control

    implicitHeight: 34
    height: visible ? implicitHeight : 0
    padding: 8
    spacing: 8
    hoverEnabled: true

    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontNormal

    contentItem: Text {
        readonly property real arrowPadding:
            control.subMenu && control.arrow ? control.arrow.width + control.spacing : 0
        readonly property real indicatorPadding:
            control.checkable && control.indicator ? control.indicator.width + control.spacing : 0

        leftPadding: control.mirrored ? arrowPadding : indicatorPadding
        rightPadding: control.mirrored ? indicatorPadding : arrowPadding
        text: control.text
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Rectangle {
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 18
        implicitHeight: 18
        visible: control.checked
        radius: 4
        color: Theme.primary

        Image {
            anchors.centerIn: parent
            width: 12
            height: 12
            source: Icons.checkWhite
        }
    }

    arrow: Image {
        x: control.mirrored ? control.leftPadding : control.width - width - control.rightPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        source: Icons.caretGray
        sourceSize.width: 18
        sourceSize.height: 18
        visible: control.subMenu
        rotation: control.mirrored ? 90 : -90
    }

    background: Rectangle {
        width: control.width
        height: control.height
        radius: Theme.controlRadius
        color: control.down ? Theme.buttonFacePressed
                            : (control.highlighted ? Theme.hover : "transparent")
    }
}
