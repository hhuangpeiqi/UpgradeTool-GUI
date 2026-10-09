import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var model: null
    property bool allChecked: false
    property bool allEnabled: false
    property string emptyTitle: ""
    property string emptySubtitle: ""
    property url emptyIcon: Icons.folderGray

    signal rowChecked(int row, bool checked)
    signal allToggled(bool checked)
    signal pathDoubleClicked(int row)

    readonly property int cSel: 52
    readonly property int cNo: 46
    readonly property int cCheck: 78
    readonly property int cStorage: 104
    readonly property int cAddr: 152
    readonly property int cName: 200
    readonly property int fixedWidth: cSel + cNo + cCheck + cStorage + cAddr + cName

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
            implicitHeight: 46
            color: Theme.tableHeader

            Row {
                anchors.fill: parent

                Item {
                    width: root.cSel
                    height: parent.height
                    CheckSquare {
                        anchors.centerIn: parent
                        checked: root.allChecked
                        enabled: root.allEnabled
                        onToggled: root.allToggled(checked)
                    }
                }

                Text { width: root.cNo; height: parent.height; leftPadding: 12; text: qsTr("#"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; font.bold: true; verticalAlignment: Text.AlignVCenter }
                Text { width: root.cCheck; height: parent.height; leftPadding: 12; text: qsTr("选择"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; font.bold: true; verticalAlignment: Text.AlignVCenter }
                Text { width: root.cStorage; height: parent.height; leftPadding: 12; text: qsTr("存储"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; font.bold: true; verticalAlignment: Text.AlignVCenter }
                Text { width: root.cAddr; height: parent.height; leftPadding: 12; text: qsTr("地址"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; font.bold: true; verticalAlignment: Text.AlignVCenter }
                Text { width: root.cName; height: parent.height; leftPadding: 12; text: qsTr("名字"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; font.bold: true; verticalAlignment: Text.AlignVCenter }
                Text { width: parent.width - root.fixedWidth; height: parent.height; leftPadding: 12; text: qsTr("路径"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; font.bold: true; verticalAlignment: Text.AlignVCenter }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                visible: root.model !== null && list.count > 0
                model: root.model
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: rowItem
                width: list.width
                height: 42
                color: rowMouse.containsMouse ? Theme.hover
                                              : (index % 2 === 0 ? Theme.card : Theme.rowAlt)

                Row {
                    anchors.fill: parent

                    Item {
                        width: root.cSel
                        height: parent.height
                    }

                    Text { width: root.cNo; height: parent.height; leftPadding: 12; text: model.no; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; verticalAlignment: Text.AlignVCenter }

                    Item {
                        width: root.cCheck
                        height: parent.height
                        CheckSquare {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 12
                            checked: model.checked
                            enabled: model.selectable
                            onToggled: root.rowChecked(index, checked)
                        }
                    }

                    Text { width: root.cStorage; height: parent.height; leftPadding: 12; text: model.storage; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; verticalAlignment: Text.AlignVCenter }
                    Text { width: root.cAddr; height: parent.height; leftPadding: 12; text: model.addr; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; verticalAlignment: Text.AlignVCenter }
                    Text { width: root.cName; height: parent.height; leftPadding: 12; text: model.name; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }

                    Text {
                        width: list.width - root.fixedWidth
                        height: parent.height
                        leftPadding: 12
                        rightPadding: 12
                        text: model.path
                        color: model.path === "" ? Theme.textDisabled : Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontNormal
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideMiddle

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onDoubleClicked: root.pathDoubleClicked(index)
                        }
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.rowDivider
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }
            }
            }

            EmptyState {
                anchors.fill: parent
                visible: root.model === null || list.count === 0
                icon: root.emptyIcon
                title: root.emptyTitle
                subtitle: root.emptySubtitle
            }
        }
    }
}
