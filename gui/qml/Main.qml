import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: window

    visible: true
    width: 1300
    height: 780
    minimumWidth: 1024
    minimumHeight: 640
    color: "transparent"
    title: app.toolVersion()
    readonly property bool nativeWindow: Qt.platform.os === "osx"
    flags: nativeWindow ? Qt.Window : (Qt.Window | Qt.FramelessWindowHint)

    property int currentPage: 1
    property var vendorPages: [1, 1, 1]
    property int previousVendor: 0

    Binding {
        target: Theme
        property: "dark"
        value: app.theme.dark
    }

    palette.window: Theme.pageBg
    palette.windowText: Theme.textPrimary
    palette.base: Theme.card
    palette.alternateBase: Theme.rowAlt
    palette.text: Theme.textPrimary
    palette.button: Theme.card
    palette.buttonText: Theme.textPrimary
    palette.brightText: Theme.textOnPrimary
    palette.highlight: Theme.primary
    palette.highlightedText: Theme.textOnPrimary
    palette.mid: Theme.borderStrong
    palette.midlight: Theme.buttonFacePressed
    palette.light: Theme.hover
    palette.dark: Theme.border
    palette.shadow: Theme.scrim
    palette.placeholderText: Theme.textDisabled
    palette.toolTipBase: Theme.card
    palette.toolTipText: Theme.textPrimary

    readonly property int cornerRadius:
        (nativeWindow || visibility === Window.Maximized || visibility === Window.FullScreen)
        ? 0 : Theme.windowRadius

    function toggleActionMenu(menu, source) {
        if (menu.visible) {
            menu.close();
            return;
        }
        var point = source.mapToItem(null, 0, source.height + 4);
        menu.x = Math.round(point.x);
        menu.y = Math.round(point.y);
        menu.open();
    }

    Component.onCompleted: {
        if (typeof startTab !== "undefined")
            window.currentPage = startTab;
        window.previousVendor = app.vendor;
        window.vendorPages[app.vendor] = window.currentPage;
    }

    MouseArea {
        visible: !window.nativeWindow
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 6
        cursorShape: Qt.SizeHorCursor
        onPressed: window.startSystemResize(Qt.LeftEdge)
    }

    MouseArea {
        visible: !window.nativeWindow
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 6
        cursorShape: Qt.SizeHorCursor
        onPressed: window.startSystemResize(Qt.RightEdge)
    }

    MouseArea {
        visible: !window.nativeWindow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 6
        cursorShape: Qt.SizeVerCursor
        onPressed: window.startSystemResize(Qt.BottomEdge)
    }

    MouseArea {
        visible: !window.nativeWindow
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: 12
        height: 12
        cursorShape: Qt.SizeFDiagCursor
        onPressed: window.startSystemResize(Qt.LeftEdge | Qt.BottomEdge)
    }

    MouseArea {
        visible: !window.nativeWindow
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 12
        height: 12
        cursorShape: Qt.SizeBDiagCursor
        onPressed: window.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
    }

    Rectangle {
        id: shell
        enabled: !app.shuttingDown
        anchors.fill: parent
        radius: window.cornerRadius
        color: Theme.pageBg

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            TitleBar {
                visible: !window.nativeWindow
                Layout.fillWidth: true
                title: app.toolVersion()
                cornerRadius: window.cornerRadius
            }

            TopBar {
                Layout.fillWidth: true
                currentIndex: window.currentPage
                onTabRequested: (index) => window.currentPage = index
                onConfigClicked: (source) => window.toggleActionMenu(configMenu, source)
                onDeviceClicked: (source) => window.toggleActionMenu(deviceMenu, source)
                onMoreClicked: (source) => window.toggleActionMenu(moreMenu, source)
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: app.vendor * 3 + window.currentPage

                        DownloadImagePage {}
                        FirmwarePage {}
                        AdvancedPage {}
                        AllwinnerRawPage {}
                        AllwinnerFirmwarePage {
                            onToolsRequested: (path) => {
                                app.allwinner.setContextFirmware(2, path);
                                window.currentPage = 2;
                            }
                        }
                        AllwinnerToolsPage {}
                        QualcommPartitionsPage {}
                        QualcommFlashPage {}
                        QualcommToolsPage {}
                    }

                    LogPanel {
                        Layout.fillHeight: true
                        Layout.preferredWidth: 390
                        Layout.minimumWidth: 300
                        Layout.maximumWidth: 560
                        logModel: app.log
                        onClearRequested: app.log.clear()
                    }
                }
            }

            StatusBar {
                Layout.fillWidth: true
                cornerRadius: window.cornerRadius
                statusText: !app.hasDevice && app.drivers.attentionCount > 0 ? qsTr("Device detected") : app.deviceStatusText
                hasDevice: app.hasDevice
                hasAdbDevice: app.vendor !== 2 && app.adbReady
                deviceLabels: app.deviceLabels
                deviceIndex: app.deviceIndex
                progressValue: app.shuttingDown ? -1 : app.tasks.progressValue
                progressText: app.tasks.progressText
                progressStage: app.tasks.progressStage
                // Qualcomm reports the stage through progressSpeed as well.
                progressSpeed: app.vendor === 2 ? "" : app.tasks.progressSpeed
                busy: app.tasks.busy || app.shuttingDown
                closing: app.shuttingDown
                controlsLocked: app.interactionLocked
                driverAttention: app.drivers.summaryLevel === "warning"
                driverBusy: app.drivers.busy
                driverText: app.drivers.summaryText
                driverMessage: app.drivers.message
                onDriverClicked: driverDialog.openDialog()
                onRefreshClicked: { app.refresh(); app.drivers.refresh(); }
                onCancelClicked: app.cancel()
                onDeviceActivated: (index) => app.deviceIndex = index
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: shell.radius
        border.width: window.nativeWindow ? 0 : 1
        border.color: Theme.border
        z: 100
    }

    AppMenu {
        id: configMenu
        AppMenuItem { text: qsTr("Preferences…"); onTriggered: settingsDialog.openDialog() }
        AppMenuItem { text: qsTr("Generate GPT…"); visible: app.vendor === 0; onTriggered: gptDialog.open() }
    }

    AppMenu {
        id: deviceMenu
        AppMenuItem {
            text: qsTr("USB Drivers…")
            visible: app.drivers.available
            onTriggered: driverDialog.openDialog()
        }
        AppMenuItem {
            text: qsTr("Device Info…")
            visible: app.vendor === 1
            enabled: !app.interactionLocked && app.hasDevice
            onTriggered: allwinnerDeviceDialog.open()
        }
        AppMenuItem {
            text: qsTr("Refresh")
            enabled: !app.interactionLocked
            onTriggered: { app.refresh(); app.drivers.refresh(); }
        }
        AppMenuItem {
            text: qsTr("Batch Select…")
            enabled: !app.interactionLocked && app.devices.length > 0
            onTriggered: batchDialog.openDialog()
        }
    }

    AppMenu {
        id: moreMenu
        AppMenuItem {
            text: qsTr("Log")
            onTriggered: app.openLogDir()
        }
        AppMenuItem { text: qsTr("About"); onTriggered: aboutDialog.open() }
    }

    AllwinnerDeviceDialog { id: allwinnerDeviceDialog }
    DriverDialog { id: driverDialog }
    EdlSwitchDialog { id: edlDialog }
    Connections {
        target: app.edl
        function onSelectionRequested() { edlDialog.openSelection(); }
    }

    Connections {
        target: app.tasks
        function onBusyChanged() { if (!app.interactionLocked) storageDialog.close(); }
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: (window.nativeWindow ? 0 : Theme.titleBarHeight) + Theme.navBarHeight + 12
        z: 2000
    }

    Connections {
        target: app

        function onVendorChanged() {
            window.vendorPages[window.previousVendor] = window.currentPage;
            window.previousVendor = app.vendor;
            window.currentPage = window.vendorPages[app.vendor];
        }
        function onToastRequested(text, error) {
            toast.show(text, error);
        }

        function onStorageSelectionRequested(options, current) {
            storageDialog.openWith(options, current);
        }

        function onAdbDeviceChoiceRequested(devices) {
            adbDialog.openWith(devices);
        }
    }

    SettingsDialog { id: settingsDialog }
    AppDialog {
        id: quitDialog
        objectName: "quitDialog"
        titleText: qsTr("Quit UpgradeTool-GUI?")
        onClosed: app.confirmQuit(false)
        Text {
            width: parent.width
            text: app.drivers.busy ? qsTr("Driver installation is running. Wait for it to finish and quit?")
                : qsTr("A task is running. Cancelling a flash operation may leave the device unable to boot. Cancel the task and quit?")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontNormal
            wrapMode: Text.WordWrap
        }
        footerContent: RowLayout {
            width: parent.width
            spacing: 10

            Item { Layout.fillWidth: true }

            SecondaryButton {
                objectName: "confirmQuitButton"
                text: app.drivers.busy ? qsTr("Wait and quit") : qsTr("Cancel task and quit")
                onClicked: {
                    app.confirmQuit(true);
                    quitDialog.close();
                }
            }
            PrimaryButton {
                objectName: "continueRunningButton"
                text: qsTr("Continue running")
                onClicked: quitDialog.close()
            }
        }
    }
    Connections {
        target: app
        function onQuitConfirmationRequested() {
            window.show();
            window.raise();
            window.requestActivate();
            quitDialog.open();
        }
    }
    BatchSelectDialog { id: batchDialog }
    StorageSelectDialog { id: storageDialog }
    GptDialog { id: gptDialog }
    AboutDialog { id: aboutDialog }
    AdbDeviceDialog { id: adbDialog }
    PermissionDialog {
        id: permissionDialog
        visible: app.permissionBlocked
    }
}
