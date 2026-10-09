import UpgradeTool
import QtQuick

Rectangle {
    id: root

    property string text: ""
    property bool error: false

    function show(message, isError) {
        root.text = message;
        root.error = isError;
        root.opacity = 1;
        hideTimer.restart();
    }

    visible: opacity > 0.01
    opacity: 0
    width: label.implicitWidth + 40
    height: 40
    radius: 6
    color: root.error ? Theme.toastErrorBg : Theme.toastOkBg
    border.width: 1
    border.color: root.error ? Theme.toastErrorBorder : Theme.toastOkBorder

    Behavior on opacity {
        NumberAnimation { duration: 180 }
    }

    Timer {
        id: hideTimer
        interval: 2600
        onTriggered: root.opacity = 0
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.error ? Theme.danger : Theme.success
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
    }
}
