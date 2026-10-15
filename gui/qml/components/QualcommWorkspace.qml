import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Card {
    id: root
    property string title: ""
    property string context: "flash"
    property bool showEdlSwitch: true
    property string footerMessage: ""
    property bool hasNavigation: false
    property bool hasHeaderContent: false
    readonly property var settings: { app.qualcomm.settings; return app.qualcomm.settingsFor(context); }
    readonly property var xmlFiles: { app.qualcomm.xmlFiles; return app.qualcomm.xmlFilesFor(context); }
    readonly property var preview: { app.qualcomm.preview; return app.qualcomm.previewFor(context); }
    readonly property var results: app.qualcomm.resultsFor(context)
    default property alias content: body.data
    property alias actions: pageActions.data
    property alias navigation: navigationSlot.data
    property alias headerContent: headerSlot.data
    function resetScroll() { scroll.contentItem.contentY = 0; }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12
        SectionTitle { text: root.title; Layout.fillWidth: true }
        ColumnLayout {
            id: headerSlot
            Layout.fillWidth: true
            visible: root.hasHeaderContent
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 18
            ColumnLayout {
                id: navigationSlot
                Layout.preferredWidth: 176
                Layout.fillHeight: true
                visible: root.hasNavigation
                enabled: !app.busy
            }
            Rectangle {
                visible: navigationSlot.visible
                Layout.fillHeight: true
                implicitWidth: 1
                color: Theme.border
            }
            ScrollView {
                id: scroll
                objectName: "qcWorkspaceScroll"
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
        }
        Text {
            objectName: "qcWorkspaceMessage"
            Layout.fillWidth: true
            Layout.leftMargin: root.hasNavigation ? scroll.x : 0
            visible: root.footerMessage !== ""
            text: root.footerMessage
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
            ToolTip.visible: messageHover.hovered && truncated
            ToolTip.text: root.footerMessage
            HoverHandler { id: messageHover }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        RowLayout {
            spacing: 10
            Layout.fillWidth: true
            QualcommComboBox {
                objectName: "qcSwitchMethod"
                visible: root.showEdlSwitch
                Layout.preferredWidth: 100
                model: ["DIAG", "ADB"]
                selectedIndex: app.edl.method - 1
                enabled: !app.busy
                onActivated: app.edl.method = currentIndex + 1
            }
            Item {
                visible: root.showEdlSwitch
                implicitWidth: switchButton.implicitWidth
                implicitHeight: switchButton.implicitHeight
                SecondaryButton {
                    id: switchButton
                    objectName: "qcSwitchEdl"
                    anchors.fill: parent
                    text: qsTr("Switch to EDL")
                    iconSource: Icons.switchBlue
                    busy: app.edl.running
                    enabled: !app.busy && app.edl.ready
                    onClicked: app.requestEdlSwitch()
                }
                // Observe the enabled wrapper so a disabled button still gets a tooltip.
                HoverHandler { id: switchHover }
                ToolTip {
                    objectName: "qcSwitchTooltip"
                    visible: switchHover.hovered && !switchButton.enabled && !app.busy
                    text: app.edl.unavailableReason +
                        (app.edl.discoveryError !== "" ? "\n" + app.edl.discoveryError : "")
                }
            }
            Item { Layout.fillWidth: true }
            RowLayout {
                id: pageActions
                spacing: 10
            }
        }
    }
}
