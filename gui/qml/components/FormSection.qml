import UpgradeTool
import QtQuick
import QtQuick.Layouts

// A labelled group inside a workspace card. The card already provides the
// surface, so a section only adds a heading (and an optional caption) above
// its content; this keeps a long form readable without extra frames.
// Actions placed in `headerActions` sit on the heading row, which is where
// the control that fills the section (e.g. "Load XML…") belongs.
ColumnLayout {
    id: root

    property string title: ""
    property string caption: ""
    default property alias content: body.data
    property alias headerActions: headerRow.data

    Layout.fillWidth: true
    spacing: 8

    RowLayout {
        Layout.fillWidth: true
        spacing: 10

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                Layout.fillWidth: true
                visible: root.title !== ""
                text: root.title
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMedium
                font.bold: true
            }

            Text {
                Layout.fillWidth: true
                visible: root.caption !== ""
                text: root.caption
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
        }

        RowLayout {
            id: headerRow
            spacing: 8
            Layout.alignment: Qt.AlignVCenter
        }
    }

    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: 8
    }
}
