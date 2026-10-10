import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var logModel: null

    signal clearRequested()

    color: Theme.card
    radius: Theme.radius
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: qsTr("Log")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontLarge
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            SecondaryButton {
                text: qsTr("Clear")
                implicitHeight: 32
                hPadding: 14
                onClicked: root.clearRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.card
            radius: Theme.controlRadius
            border.width: 1
            border.color: Theme.border
            clip: true

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 10
                model: root.logModel
                clip: true
                spacing: 4
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Text {
                    width: list.width
                    text: model.text
                    color: model.error ? Theme.danger : Theme.textSecondary
                    font.family: Qt.platform.os === "osx" ? "Menlo" : "DejaVu Sans Mono"
                    font.pixelSize: 13
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                }

                onCountChanged: Qt.callLater(list.positionViewAtEnd)
            }
        }
    }
}
