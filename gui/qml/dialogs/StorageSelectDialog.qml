import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("Select Storage")
    width: 440

    property var options: []
    property bool handled: false

    function openWith(opts, current) {
        options = opts;
        handled = false;
        dlg.open();
    }

    onClosed: {
        if (!handled)
            app.answerStorage(0);
    }

    Text {
        width: parent.width
        text: qsTr("Select the storage to switch to:")
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
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
                        app.answerStorage(index + 1);
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
            text: qsTr("Cancel")
            onClicked: dlg.close()
        }
    }
}
