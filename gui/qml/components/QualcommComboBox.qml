import UpgradeTool
import QtQuick

AppComboBox {
    id: root
    property int selectedIndex: 0
    currentIndex: selectedIndex
    // ComboBox resets currentIndex while replacing a translated model. Restore
    // its binding once that model update is complete, without changing the draft.
    function restoreSelection() {
        currentIndex = Qt.binding(function() { return root.selectedIndex; });
    }
    onModelChanged: Qt.callLater(restoreSelection)
}
