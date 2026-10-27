import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var logModel: null
    property bool showDetails: false

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
                objectName: "logList"
                property bool following: true
                property bool followScheduled: false
                function followTail() {
                    if (!following || followScheduled) return
                    followScheduled = true
                    Qt.callLater(function() {
                        followScheduled = false
                        if (list.following) {
                            list.forceLayout()
                            list.positionViewAtEnd()
                        }
                    })
                }
                onMovementStarted: following = false
                onMovementEnded: following = atYEnd
                onAtYEndChanged: { if (atYEnd && !moving && !logScrollBar.pressed) following = true }
                onContentHeightChanged: followTail()
                onCountChanged: { if (count === 0) following = true }
                anchors.fill: parent
                anchors.margins: 10
                model: root.logModel
                clip: true
                spacing: 4
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    id: logScrollBar
                    policy: ScrollBar.AsNeeded
                    onPressedChanged: list.following = pressed ? false : list.atYEnd
                }

                delegate: Text {
                    objectName: "logEntry"
                    width: list.width
                    text: root.showDetails
                          ? model.time + " [" + model.level + "] [" + model.source + "] "
                            + (model.context ? "[" + model.context + "] " : "") + model.text
                          : model.time.slice(0, 8) + " " + model.text
                    textFormat: Text.PlainText
                    color: model.error ? Theme.danger : model.level === "WARN" ? Theme.warning : Theme.textSecondary
                    font.family: Qt.platform.os === "osx" ? "Menlo" : "DejaVu Sans Mono"
                    font.pixelSize: 13
                    wrapMode: Text.WrapAnywhere
                }

                onContentYChanged: {
                    if (moving || dragging || flicking) following = atYEnd
                }
                Connections {
                    target: root.logModel
                    function onRowsInserted() { list.followTail() }
                }
            }
        }
    }
}
