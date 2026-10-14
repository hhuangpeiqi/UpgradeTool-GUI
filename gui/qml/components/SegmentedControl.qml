import UpgradeTool
import QtQuick
import QtQuick.Layouts

// A compact single-choice control for two or three mutually exclusive options.
// It replaces the dropdown where a popup would only add a click: the choices
// stay visible, so the mode reads as a decision instead of a form field.
// It borrows the app's field styling (white surface, one 1px border, one
// radius) and marks the active option with the shared Theme.selected tint, so
// it sits in a form row without looking like a different design system.
// The API mirrors QualcommComboBox (model / selectedIndex / activated) so the
// call sites and the controller wiring are unchanged.
Rectangle {
    id: root

    property var model: []
    property int selectedIndex: 0
    // Driven by selectedIndex; a click reassigns it and the Qualcomm page then
    // updates the controller, which feeds selectedIndex back through its own
    // binding. A model swap (language change) is handled the same way as in
    // QualcommComboBox: currentIndex is restored from selectedIndex afterwards.
    property int currentIndex: selectedIndex
    signal activated(int index)

    // Horizontal padding inside one segment; the control is only as wide as its
    // labels need.
    readonly property int segmentPadding: 16

    function restoreSelection() {
        currentIndex = Qt.binding(function() { return root.selectedIndex; });
    }
    onModelChanged: Qt.callLater(restoreSelection)

    function select(index) {
        if (index < 0 || index >= model.length || index === currentIndex)
            return;
        currentIndex = index;
        root.activated(index);
    }

    implicitHeight: 38
    // The inner layout is inset by the 1px border on each side.
    implicitWidth: segmentRow.implicitWidth + 2
    radius: Theme.controlRadius
    color: root.enabled ? Theme.card : Theme.fieldDisabled
    border.width: 1
    border.color: !root.enabled ? Theme.border
                                : activeFocus ? Theme.primary
                                              : rootHover.hovered ? Theme.borderStrong : Theme.border
    // Clip the flat segment fills to the container's rounded corners: the
    // control uses a single radius instead of nesting rounded segments.
    clip: true

    activeFocusOnTab: true
    Keys.onLeftPressed: root.select(root.currentIndex - 1)
    Keys.onRightPressed: root.select(root.currentIndex + 1)

    HoverHandler { id: rootHover }

    RowLayout {
        id: segmentRow
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        Repeater {
            model: root.model

            Rectangle {
                id: segment
                required property var modelData
                required property int index

                Layout.fillHeight: true
                Layout.preferredWidth: label.implicitWidth + root.segmentPadding * 2
                color: root.currentIndex === index ? Theme.selected
                                                   : (segmentArea.containsMouse && root.enabled ? Theme.hover : "transparent")

                // A hairline between options keeps them readable; it is hidden
                // next to the active one, where the tint already separates them.
                Rectangle {
                    visible: segment.index > 0 && root.currentIndex !== segment.index
                             && root.currentIndex !== segment.index - 1
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 1
                    height: parent.height - 12
                    color: Theme.border
                }

                Text {
                    id: label
                    anchors.centerIn: parent
                    width: Math.max(0, Math.min(implicitWidth, parent.width - root.segmentPadding))
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: segment.modelData
                    color: root.currentIndex === index ? Theme.primary
                                                       : (root.enabled ? Theme.textPrimary : Theme.textDisabled)
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMedium
                }

                MouseArea {
                    id: segmentArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.forceActiveFocus();
                        root.select(segment.index);
                    }
                }
            }
        }
    }
}
