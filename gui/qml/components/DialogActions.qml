import QtQuick
import UpgradeTool

// Owns button spacing and falls back to a vertical group when space is tight.
Grid {
    id: root
    property bool alignRight: false
    readonly property real naturalWidth: {
        var total = 0;
        var count = 0;
        for (var i = 0; i < children.length; ++i) {
            var child = children[i];
            if (!child.visible)
                continue;
            total += child.implicitWidth;
            ++count;
        }
        return total + Math.max(0, count - 1) * spacing;
    }
    readonly property int visibleCount: {
        var count = 0;
        for (var i = 0; i < children.length; ++i)
            if (children[i].visible)
                ++count;
        return count;
    }
    readonly property real widestButton: {
        var widest = 0;
        for (var i = 0; i < children.length; ++i)
            if (children[i].visible)
                widest = Math.max(widest, children[i].implicitWidth);
        return widest;
    }
    width: parent.width >= naturalWidth ? naturalWidth : Math.min(widestButton, parent.width)
    columns: parent.width >= naturalWidth ? Math.max(1, visibleCount) : 1
    spacing: Theme.dialogButtonSpacing
    horizontalItemAlignment: alignRight ? Grid.AlignRight : Grid.AlignLeft
}
