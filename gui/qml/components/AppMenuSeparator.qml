import UpgradeTool
import QtQuick
import QtQuick.Controls

MenuSeparator {
    id: control

    height: visible ? implicitHeight : 0
    padding: 5

    contentItem: Rectangle {
        implicitWidth: 140
        implicitHeight: 1
        color: Theme.border
    }
}
