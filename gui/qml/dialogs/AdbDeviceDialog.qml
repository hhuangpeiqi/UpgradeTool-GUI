import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("选择设备")
    width: 460

    property var options: []
    property bool handled: false

    function openWith(opts) {
        options = opts;
        handled = false;
        dlg.open();
    }

    onClosed: {
        if (!handled)
            app.cancelAdbChoice();
    }

    Text {
        width: parent.width
        text: app.vendor === 1 ? qsTr("检测到多台 ADB 设备，请选择要切换到 FEL 的设备:")
                              : qsTr("检测到多台 ADB 设备，请选择要切换到 Loader 的设备:")
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
        wrapMode: Text.WordWrap
    }

    Column {
        width: parent.width
        spacing: 6

        Repeater {
            model: dlg.options

            delegate: Rectangle {
                width: parent.width
                height: 36
                radius: Theme.controlRadius
                color: hover.containsMouse ? Theme.hover : "transparent"

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 12
                    text: modelData
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontNormal
                }

                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        dlg.handled = true;
                        app.chooseAdbDevice(modelData);
                        dlg.close();
                    }
                }
            }
        }
    }

    footerContent: RowLayout {
        width: parent.width
        spacing: 10

        Item { Layout.fillWidth: true }

        SecondaryButton {
            text: qsTr("取消")
            onClicked: dlg.close()
        }
    }
}
