import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg
    objectName: "adbDeviceDialog"

    titleText: qsTr("Select Device")
    width: Math.min(460, Overlay.overlay ? Overlay.overlay.width - 32 : 800)

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
        text: app.vendor === 1 ? qsTr("Multiple ADB devices detected. Select the one to switch to FEL:") : qsTr("Multiple ADB devices detected. Select the one to switch to Loader:")
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

    footerContent: [
        SecondaryButton {
            text: qsTr("Cancel")
            onClicked: dlg.close()
        }
    ]
}
