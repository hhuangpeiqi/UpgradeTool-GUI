import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var model: null
    property bool showHeader: true
    property string labelHeader: qsTr("Item")
    property string valueHeader: qsTr("Value")
    property int labelWidth: 190

    color: Theme.card
    radius: Theme.controlRadius
    border.width: 1
    border.color: Theme.border
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            color: Theme.tableHeader
            visible: root.showHeader

            Row {
                anchors.fill: parent

                Text {
                    width: root.labelWidth
                    height: parent.height
                    leftPadding: 14
                    text: root.labelHeader
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontNormal
                    font.bold: true
                    verticalAlignment: Text.AlignVCenter
                }

                Text {
                    width: parent.width - root.labelWidth
                    height: parent.height
                    leftPadding: 14
                    text: root.valueHeader
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontNormal
                    font.bold: true
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
                visible: root.showHeader
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.model
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                width: list.width
                height: Math.max(38, valueText.implicitHeight + 16)
                color: index % 2 === 0 ? Theme.card : Theme.rowAlt

                Text {
                    id: labelText
                    x: 14
                    width: root.labelWidth - 24
                    anchors.verticalCenter: parent.verticalCenter
                    text: model.label
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontNormal
                    elide: Text.ElideRight
                }

                Text {
                    id: valueText
                    x: root.labelWidth
                    width: list.width - root.labelWidth - 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: model.value
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontNormal
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.rowDivider
                }
            }
        }
    }
}
