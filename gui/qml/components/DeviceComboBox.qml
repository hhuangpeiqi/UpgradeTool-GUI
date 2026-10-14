import UpgradeTool
import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    implicitHeight: 34
    implicitWidth: 300
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontNormal
    hoverEnabled: true

    // A vertical ListView reports contentWidth == -1, so the popup cannot size
    // itself from the delegate's content; measure the widest label instead.
    function longestLabel() {
        var m = control.model;
        if (!m || m.length === undefined)
            return "";
        var longest = "";
        for (var i = 0; i < m.length; ++i) {
            var s = String(m[i]);
            if (s.length > longest.length)
                longest = s;
        }
        return longest;
    }

    TextMetrics {
        id: labelMetrics
        font: control.font
        text: control.longestLabel()
    }

    // Grow to fit the labels, but never past the window edge.
    readonly property real popupWidth: {
        var limit = Overlay.overlay ? Overlay.overlay.width - 24 : 560;
        return Math.min(Math.max(control.width, limit),
                        Math.max(control.width, labelMetrics.advanceWidth + 24));
    }

    delegate: ItemDelegate {
        id: itemDelegate
        width: control.popup.width - 2
        height: 32

        contentItem: Text {
            id: labelText
            text: modelData
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            leftPadding: 10
            rightPadding: 10
        }

        background: Rectangle {
            radius: 4
            color: itemDelegate.highlighted ? Theme.hover : "transparent"
        }

        highlighted: control.highlightedIndex === index

        ToolTip.visible: itemDelegate.hovered && labelText.truncated
        ToolTip.text: modelData
    }

    indicator: Image {
        x: control.width - width - 10
        y: control.topPadding + (control.availableHeight - height) / 2
        source: Icons.caretGray
        sourceSize.width: 18
        sourceSize.height: 18
        opacity: control.enabled ? 1.0 : 0.4
    }

    contentItem: Text {
        leftPadding: 12
        rightPadding: 26
        text: control.displayText
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.controlRadius
        color: control.enabled ? Theme.card : Theme.fieldDisabled
        border.width: 1
        border.color: control.activeFocus ? Theme.primary
                                          : (control.hovered ? Theme.borderStrong : Theme.border)
    }

    popup: Popup {
        y: control.height + 2
        width: control.popupWidth
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 320)
        padding: 1

        background: Rectangle {
            radius: Theme.controlRadius
            color: Theme.card
            border.width: 1
            border.color: Theme.borderStrong
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
