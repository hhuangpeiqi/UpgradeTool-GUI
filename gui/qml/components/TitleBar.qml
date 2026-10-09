import UpgradeTool
import QtQuick
import QtQuick.Layouts
import QtQuick.Window

EdgeRoundedRect {
    id: root

    property string title: ""
    property int cornerRadius: Theme.windowRadius

    roundedEdge: "top"
    implicitHeight: Theme.titleBarHeight
    color: Theme.titleBar
    radius: root.cornerRadius

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.titleBarBorder
    }

    component WinButton: Rectangle {
        id: wb

        property string kind: "min"

        readonly property color glyphColor: (wb.kind === "close" && mouse.containsMouse)
                                            ? Theme.closeGlyphHover : Theme.titleBarGlyph

        width: 44
        height: 30
        radius: 5
        color: mouse.containsMouse
               ? (wb.kind === "close" ? Theme.closeHover : Theme.hover)
               : "transparent"

        function trigger() {
            var win = Window.window;
            if (!win)
                return;
            if (kind === "min")
                win.showMinimized();
            else if (kind === "max")
                win.visibility === Window.Maximized ? win.showNormal() : win.showMaximized();
            else
                win.close();
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: wb.trigger()
        }

        Rectangle {
            visible: wb.kind === "min"
            width: 12
            height: 1.6
            radius: 1
            color: wb.glyphColor
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 4
        }

        Rectangle {
            visible: wb.kind === "max"
            width: 11
            height: 11
            color: "transparent"
            border.width: 1.4
            border.color: wb.glyphColor
            anchors.centerIn: parent
        }

        Item {
            visible: wb.kind === "close"
            width: 12
            height: 12
            anchors.centerIn: parent

            Rectangle {
                width: 12
                height: 1.6
                radius: 1
                color: wb.glyphColor
                anchors.centerIn: parent
                rotation: 45
            }

            Rectangle {
                width: 12
                height: 1.6
                radius: 1
                color: wb.glyphColor
                anchors.centerIn: parent
                rotation: -45
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: {
            var win = Window.window;
            if (win)
                win.startSystemMove();
        }
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 16
        anchors.rightMargin: 156
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

        Image {
            Layout.alignment: Qt.AlignVCenter
            source: Icons.appLogo
            sourceSize.width: 26
            sourceSize.height: 26
            smooth: true
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.title
            color: Theme.titleBarText
            font.family: Theme.fontFamily
            font.pixelSize: 16
            elide: Text.ElideRight
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        WinButton { kind: "min" }
        WinButton { kind: "max" }
        WinButton { kind: "close" }
    }
}
