import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Collapsible section header. A disclosure row (not a checkbox) that reveals
// rarely-used options; collapsing never clears the options it contains.
ColumnLayout {
    id: root

    property string title: ""
    property bool expanded: false
    default property alias content: body.data

    Layout.fillWidth: true
    spacing: 10

    Control {
        id: header
        Layout.fillWidth: true
        implicitHeight: 32
        focusPolicy: Qt.TabFocus
        background: Rectangle {
            radius: Theme.controlRadius
            color: headerArea.containsMouse ? Theme.hover : "transparent"
            border.width: header.visualFocus ? 1 : 0
            border.color: Theme.primary
        }

        function toggle() { root.expanded = !root.expanded; }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 4
            spacing: 6
            Image {
                source: Icons.caretGray
                sourceSize.width: 16
                sourceSize.height: 16
                rotation: root.expanded ? 0 : -90
                Behavior on rotation { NumberAnimation { duration: 120; easing.type: Easing.InOutQuad } }
            }
            Text {
                text: root.title
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMedium
                Layout.fillWidth: true
                verticalAlignment: Text.AlignVCenter
            }
        }

        MouseArea {
            id: headerArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                header.focusReason = Qt.MouseFocusReason;
                header.forceActiveFocus(Qt.MouseFocusReason);
                header.toggle();
            }
        }

        Keys.onSpacePressed: header.toggle()
        Keys.onReturnPressed: header.toggle()
        Keys.onEnterPressed: header.toggle()
    }

    ColumnLayout {
        id: body
        Layout.fillWidth: true
        visible: root.expanded
        spacing: 10
    }
}
