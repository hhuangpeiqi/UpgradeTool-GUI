import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("Usage Help")
    width: 640

    ScrollView {
        width: parent.width
        height: 460
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Column {
            width: parent.width
            spacing: 18

            Text {
                width: parent.width
                text: qsTr("Select a device, then run the operation on the matching page. Rockchip devices must be in Rockusb mode (Loader or Maskrom), and Allwinner devices in FEL / FES mode; make sure the USB connection is good.")
                      + (Qt.platform.os === "linux" ? qsTr("The system must have the matching udev rules installed.") : "")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: app.usageHelp()

                delegate: Column {
                    width: parent.width
                    spacing: 8

                    Row {
                        spacing: 8

                        Rectangle {
                            id: vendorTag
                            visible: modelData.vendor !== undefined && modelData.vendor !== ""
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 3
                            color: Theme.primary
                            implicitWidth: vendorTagText.implicitWidth + 10
                            implicitHeight: vendorTagText.implicitHeight + 2

                            Text {
                                id: vendorTagText
                                anchors.centerIn: parent
                                text: modelData.vendor
                                color: Theme.textOnPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.title
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMedium
                            font.bold: true
                        }
                    }

                    Text {
                        visible: modelData.note !== ""
                        width: parent.width
                        text: modelData.note
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: modelData.items

                        delegate: Row {
                            width: parent.width
                            spacing: 10

                            Text {
                                text: modelData.name
                                width: 150
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontNormal
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                text: modelData.desc
                                width: parent.width - 160
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontNormal
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }

    footerContent: RowLayout {
        width: parent.width
        spacing: 10

        Item { Layout.fillWidth: true }

        PrimaryButton {
            text: qsTr("Close")
            onClicked: dlg.close()
        }
    }
}
