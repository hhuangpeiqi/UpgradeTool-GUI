import UpgradeTool
import QtQuick
import QtQuick.Controls

Popup {
    id: root

    property string titleText: ""
    property string hintText: ""
    property bool hintError: false

    default property alias dialogContent: body.data
    property alias footerContent: footerRow.data

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    padding: 20
    implicitWidth: 440
    readonly property real _maxHeight:
        Math.max(240, (Overlay.overlay && Overlay.overlay.height > 0
                       ? Overlay.overlay.height : 800) - 24)
    readonly property real _chromeHeight: {
        var iGaps = 1;
        var iHeight = titleRow.implicitHeight;
        if (footerRow.visible) {
            iHeight += footerRow.implicitHeight;
            ++iGaps;
        }
        if (hintLabel.visible) {
            iHeight += hintLabel.implicitHeight;
            ++iGaps;
        }
        return iHeight + contentColumn.spacing * iGaps;
    }
    implicitHeight: Math.min(contentColumn.implicitHeight + 40, _maxHeight)

    Overlay.modal: Rectangle { color: Theme.scrim; radius: Theme.windowRadius }

    background: Rectangle {
        radius: 10
        color: Theme.card
        border.width: 1
        border.color: Theme.border
    }

    contentItem: Column {
        id: contentColumn
        spacing: 16

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
            implicitHeight: Math.min(body.implicitHeight,
                                     Math.max(100, root._maxHeight - 40 - root._chromeHeight))

            ScrollBar.vertical: ScrollBar {
                id: vbar
                policy: ScrollBar.AsNeeded
                // Only grab the mouse when there is actually something to scroll,
                // so the overlay bar does not swallow clicks meant for controls
                // (e.g. nested list scroll bars) at the content's right edge.
                interactive: vbar.size < 1.0
                background: Rectangle { color: "transparent" }

                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: 3
                    color: Theme.borderStrong
                    opacity: vbar.size < 1.0 ? (vbar.active || vbar.pressed ? 1.0 : 0.5) : 0.0
                }
            }

            Column {
                id: body
                // Reserve a gutter for the vertical scroll bar so the content
                // never sits underneath it (the bar overlays the flickable's
                // right edge). Constant offset to avoid a binding loop with
                // body.implicitHeight -> vbar.size.
                width: bodyFlick.width - 14
                spacing: 12
            }
        }

        Row {
            id: footerRow
            width: parent.width
            spacing: 10
            visible: children.length > 0
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
