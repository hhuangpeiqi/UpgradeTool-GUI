import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Card {
    id: root
    property string title: ""
    property string context: "flash"
    readonly property var settings: { app.qualcomm.settings; return app.qualcomm.settingsFor(context); }
    readonly property var xmlFiles: { app.qualcomm.xmlFiles; return app.qualcomm.xmlFilesFor(context); }
    readonly property var preview: { app.qualcomm.preview; return app.qualcomm.previewFor(context); }
    readonly property var results: app.qualcomm.resultsFor(context)
    default property alias content: body.data
    property alias actions: buttons.data
    // One-line readiness/status hint shown at the start of the action bar, so
    // the page reads as a task with a clear next step instead of a form.
    property string summary: ""
    signal switchConfigRequested()
    signal switchEdlRequested()
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12
        SectionTitle { text: root.title; Layout.fillWidth: true }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            // The workspace content is a plain ColumnLayout, whose implicit
            // height is not picked up as the scrollable content by itself, so
            // bind it explicitly (otherwise the form is clipped, not scrolled).
            contentHeight: body.implicitHeight
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: ScrollBar {
                id: vbar
                policy: ScrollBar.AsNeeded
                // Only grab the mouse when there is actually something to scroll,
                // so the bar does not swallow clicks at the content's right edge.
                interactive: vbar.size < 1.0
                background: Rectangle { color: "transparent" }
                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: 3
                    color: Theme.borderStrong
                    opacity: vbar.size < 1.0 ? (vbar.active || vbar.pressed ? 1.0 : 0.5) : 0.0
                }
            }
            ColumnLayout {
                id: body
                // Reserve a gutter for the bar, which overlays the right edge.
                width: scroll.availableWidth - 14
                spacing: 10
                enabled: !app.busy
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        RowLayout {
            id: buttons
            spacing: 10
            Layout.fillWidth: true
            Text {
                id: summaryText
                objectName: "qcActionSummary"
                text: root.summary
                visible: text !== ""
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                HoverHandler { id: summaryHover }
                ToolTip.visible: summaryHover.hovered && summaryText.truncated
                ToolTip.text: root.summary
            }
            SecondaryButton {
                objectName: "qcSwitchConfig"
                text: qsTr("Switch settings…")
                enabled: !app.busy && !app.edl.discovering
                onClicked: root.switchConfigRequested()
            }
            SecondaryButton {
                objectName: "qcSwitchEdl"
                text: qsTr("Switch to EDL")
                iconSource: Icons.switchBlue
                busy: app.edl.running
                enabled: !app.busy && !app.edl.discovering
                onClicked: root.switchEdlRequested()
            }
        }
    }
}
