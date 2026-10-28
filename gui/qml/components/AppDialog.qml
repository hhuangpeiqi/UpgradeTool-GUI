import UpgradeTool
import QtQuick
import QtQuick.Controls

Popup {
    id: root

    property string titleText: ""
    property string hintText: ""
    property bool hintError: false
    property real bodyRightInset: 0

    default property alias dialogContent: body.data
    property alias footerContent: actions.data
    property alias footerLeadingContent: leadingActions.data
    property alias footerFormContent: footerForm.data

    // Settle positioners before callers focus or click a newly opened dialog.
    onOpened: {
        leadingActions.forceLayout();
        actions.forceLayout();
        footerForm.forceLayout();
        footerRow.forceLayout();
        body.forceLayout();
        contentColumn.forceLayout();
    }

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    padding: Theme.dialogPadding
    implicitWidth: 440
    width: Math.min(implicitWidth, Overlay.overlay ? Overlay.overlay.width - 32 : implicitWidth)
    readonly property real _maxHeight: Math.max(240, (Overlay.overlay && Overlay.overlay.height > 0 ? Overlay.overlay.height : 800) - 24)
    readonly property real _chromeHeight: {
        var iGaps = 1;
        var iHeight = titleRow.implicitHeight;
        if (footerRow.implicitHeight > 0) {
            iHeight += footerRow.implicitHeight;
            ++iGaps;
        }
        if (hintLabel.visible) {
            iHeight += hintLabel.implicitHeight;
            ++iGaps;
        }
        return iHeight + contentColumn.spacing * iGaps;
    }
    implicitHeight: Math.min(contentColumn.implicitHeight + topPadding + bottomPadding, _maxHeight)

    Overlay.modal: Rectangle {
        color: Theme.scrim
        radius: Theme.windowRadius
    }

    background: Rectangle {
        radius: Theme.windowRadius
        color: Theme.card
        border.width: 1
        border.color: Theme.border
    }

    contentItem: Column {
        id: contentColumn
        spacing: Theme.dialogSectionSpacing

        Row {
            id: titleRow
            width: parent.width

            Text {
                text: root.titleText
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontLarge
                font.bold: true
                width: parent.width - 28
                elide: Text.ElideRight
            }

            Text {
                text: "\u2715"
                color: Theme.textSecondary
                font.pixelSize: 16
                anchors.verticalCenter: parent.verticalCenter
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }
            }
        }

        Flickable {
            id: bodyFlick
            width: parent.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentHeight: body.implicitHeight
            implicitHeight: Math.min(body.implicitHeight, Math.max(0, root._maxHeight - root.topPadding - root.bottomPadding - root._chromeHeight))

            ScrollBar.vertical: ScrollBar {
                id: vbar
                parent: root.background
                x: root.leftPadding + bodyFlick.width + Theme.dialogControlSpacing
                y: root.topPadding + bodyFlick.y
                height: bodyFlick.height
                policy: ScrollBar.AsNeeded
                // Only grab the mouse when there is actually something to scroll,
                // so the overlay bar does not swallow clicks meant for controls
                // (e.g. nested list scroll bars) at the content's right edge.
                interactive: vbar.size < 1.0
                background: Rectangle {
                    color: "transparent"
                }

                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: 3
                    color: Theme.borderStrong
                    opacity: vbar.size < 1.0 ? (vbar.active || vbar.pressed ? 1.0 : 0.5) : 0.0
                }
            }

            Column {
                id: body
                // The scrollbar lives in the outer padding, so body width stays
                // stable and aligns with the title and footer.
                width: bodyFlick.width - root.bodyRightInset
                spacing: Theme.dialogBodySpacing
            }
        }

        Column {
            id: footerRow
            width: parent.width
            spacing: Theme.dialogBodySpacing

            Column {
                id: footerForm
                width: parent.width
                spacing: Theme.dialogControlSpacing
                visible: children.length > 0
            }

            Item {
                width: parent.width
                readonly property bool stacked: leadingActions.naturalWidth + actions.naturalWidth + Theme.dialogSectionSpacing > width
                implicitHeight: stacked ? ((leadingActions.visibleCount > 0) ? leadingActions.implicitHeight : 0) + ((leadingActions.visibleCount > 0) && (actions.visibleCount > 0) ? Theme.dialogBodySpacing : 0) + ((actions.visibleCount > 0) ? actions.implicitHeight : 0) : Math.max((leadingActions.visibleCount > 0) ? leadingActions.implicitHeight : 0, (actions.visibleCount > 0) ? actions.implicitHeight : 0)

                DialogActions {
                    id: leadingActions
                    objectName: "dialogLeadingActions"
                }
                DialogActions {
                    id: actions
                    objectName: "dialogActions"
                    alignRight: true
                    x: parent.width - width
                    y: parent.stacked && (leadingActions.visibleCount > 0) ? leadingActions.implicitHeight + Theme.dialogBodySpacing : 0
                }
            }
        }

        Text {
            id: hintLabel
            width: parent.width
            visible: root.hintText !== ""
            text: root.hintText
            color: root.hintError ? Theme.danger : Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            wrapMode: Text.WordWrap
        }
    }
}
