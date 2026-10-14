pragma Singleton

import UpgradeTool
import QtQuick

QtObject {
    readonly property url appLogo: Qt.resolvedUrl("icons/upgrade_tool.svg")
    readonly property url downloadGray: Theme.dark
                                         ? Qt.resolvedUrl("icons/download-gray-dark.svg")
                                         : Qt.resolvedUrl("icons/download-gray.svg")
    readonly property url downloadBlue: Qt.resolvedUrl("icons/download-blue.svg")
    readonly property url chipGray: Theme.dark
                                    ? Qt.resolvedUrl("icons/chip-gray-dark.svg")
                                    : Qt.resolvedUrl("icons/chip-gray.svg")
    readonly property url chipBlue: Qt.resolvedUrl("icons/chip-blue.svg")
    readonly property url gearGray: Theme.dark
                                    ? Qt.resolvedUrl("icons/gear-gray-dark.svg")
                                    : Qt.resolvedUrl("icons/gear-gray.svg")
    readonly property url gearBlue: Qt.resolvedUrl("icons/gear-blue.svg")
    readonly property url monitorGray: Theme.dark
                                       ? Qt.resolvedUrl("icons/monitor-gray-dark.svg")
                                       : Qt.resolvedUrl("icons/monitor-gray.svg")
    readonly property url moreGray: Theme.dark
                                    ? Qt.resolvedUrl("icons/more-gray-dark.svg")
                                    : Qt.resolvedUrl("icons/more-gray.svg")
    readonly property url refreshBlue: Qt.resolvedUrl("icons/refresh-blue.svg")
    readonly property url folderWhite: Qt.resolvedUrl("icons/folder-white.svg")
    readonly property url folderGray: Theme.dark
                                      ? Qt.resolvedUrl("icons/folder-gray-dark.svg")
                                      : Qt.resolvedUrl("icons/folder-gray.svg")
    readonly property url fileGray: Theme.dark
                                    ? Qt.resolvedUrl("icons/file-gray-dark.svg")
                                    : Qt.resolvedUrl("icons/file-gray.svg")
    readonly property url searchGray: Theme.dark
                                      ? Qt.resolvedUrl("icons/search-gray-dark.svg")
                                      : Qt.resolvedUrl("icons/search-gray.svg")
    readonly property url playWhite: Qt.resolvedUrl("icons/play-white.svg")
    readonly property url switchBlue: Qt.resolvedUrl("icons/switch-blue.svg")
    readonly property url plusGray: Theme.dark
                                    ? Qt.resolvedUrl("icons/plus-gray-dark.svg")
                                    : Qt.resolvedUrl("icons/plus-gray.svg")
    readonly property url trashGray: Theme.dark
                                     ? Qt.resolvedUrl("icons/trash-gray-dark.svg")
                                     : Qt.resolvedUrl("icons/trash-gray.svg")
    readonly property url deviceGray: Theme.dark
                                      ? Qt.resolvedUrl("icons/device-gray-dark.svg")
                                      : Qt.resolvedUrl("icons/device-gray.svg")
    readonly property url inboxGray: Theme.dark
                                     ? Qt.resolvedUrl("icons/inbox-gray-dark.svg")
                                     : Qt.resolvedUrl("icons/inbox-gray.svg")
    readonly property url checkWhite: Qt.resolvedUrl("icons/check-white.svg")
    readonly property url caretGray: Theme.dark
                                     ? Qt.resolvedUrl("icons/caret-gray-dark.svg")
                                     : Qt.resolvedUrl("icons/caret-gray.svg")
}
