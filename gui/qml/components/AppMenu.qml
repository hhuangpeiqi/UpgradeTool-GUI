import UpgradeTool
import QtQuick
import QtQuick.Controls

Menu {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 6
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontNormal

    delegate: AppMenuItem {}

    background: Rectangle {
        implicitWidth: 190
        implicitHeight: 40
        radius: Theme.radius
        color: Theme.card
        border.width: 1
        border.color: Theme.borderStrong
    }

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        currentIndex: control.currentIndex
        interactive: Window.window
                     ? contentHeight + control.topPadding + control.bottomPadding > control.height
                     : false
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollIndicator.vertical: ScrollIndicator {
            id: indicator
            width: 6
            background: Rectangle { color: "transparent" }

            contentItem: Rectangle {
                implicitWidth: 6
                radius: 3
                color: Theme.borderStrong
                visible: indicator.size < 1.0
                opacity: indicator.active || indicator.pressed ? 1.0 : 0.5
            }
        }
    }
}
