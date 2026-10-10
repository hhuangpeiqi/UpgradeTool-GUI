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
    property var vendorPages: [1, 1]
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
                onHelpClicked: (source) => window.toggleActionMenu(helpMenu, source)
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
                                app.setAllwinnerContextFirmware(2, path);
                                window.currentPage = 2;
                            }
                        }
                        AllwinnerToolsPage {}
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
                statusText: app.deviceStatusText
                deviceLabels: app.deviceLabels
                deviceIndex: app.deviceIndex
                progressValue: app.progressValue
                progressSpeed: app.progressSpeed
                busy: app.busy
                onRefreshClicked: app.refresh()
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
        AppMenuItem { text: qsTr("首选项…"); onTriggered: settingsDialog.openDialog() }
        AppMenuItem { text: qsTr("生成 GPT…"); visible: app.vendor === 0; onTriggered: gptDialog.open() }
    }

    AppMenu {
        id: deviceMenu
        AppMenuItem {
            text: qsTr("设备信息…")
            visible: app.vendor === 1
            enabled: !app.busy && app.hasDevice
            onTriggered: allwinnerDeviceDialog.open()
        }
        AppMenuItem {
            text: qsTr("刷新")
            onTriggered: app.refresh()
        }
        AppMenuItem {
            text: qsTr("批量选择…")
            enabled: app.devices.length > 0
            onTriggered: batchDialog.openDialog()
        }
    }

    AppMenu {
        id: helpMenu
        AppMenuItem { text: qsTr("用法"); onTriggered: usageDialog.open() }
        AppMenuItem {
            text: qsTr("日志")
            onTriggered: toast.show(qsTr("日志目录：%1").arg(app.logDir()), false)
        }
        AppMenuItem { text: qsTr("关于"); onTriggered: aboutDialog.open() }
    }

    AllwinnerDeviceDialog { id: allwinnerDeviceDialog }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: (window.nativeWindow ? 0 : Theme.titleBarHeight) + 14
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

        function onBusyChanged() {
            if (!app.busy) storageDialog.close();
        }
        function onStorageSelectionRequested(options, current) {
            storageDialog.openWith(options, current);
        }

        function onAdbDeviceChoiceRequested(devices) {
            adbDialog.openWith(devices);
        }
    }

    SettingsDialog { id: settingsDialog }
    BatchSelectDialog { id: batchDialog }
    StorageSelectDialog { id: storageDialog }
    GptDialog { id: gptDialog }
    AboutDialog { id: aboutDialog }
    UsageDialog { id: usageDialog }
    AdbDeviceDialog { id: adbDialog }
    PermissionDialog {
        id: permissionDialog
        visible: app.permissionBlocked
    }
}
