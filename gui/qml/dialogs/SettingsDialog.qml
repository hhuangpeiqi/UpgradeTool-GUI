import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("Settings")
    width: 560

    function openDialog() {
        var v = app.config.load();
        logDetailsBox.checked = !!v.ui_log_details;
        fileLimit.value = v.log_file_mib;
        totalLimit.value = v.log_total_mib;
        app.config.refreshLogStorage();
        themeBox.currentIndex = dlg.themeIndex();
        languageBox.currentIndex = dlg.languageIndex();
        dlg.open();
    }

    function themeIndex() {
        if (app.theme.mode === "dark")
            return 2;
        return app.theme.mode === "light" ? 1 : 0;
    }

    function languageIndex() {
        if (app.language.mode === "en")
            return 2;
        return app.language.mode === "zh_CN" ? 1 : 0;
    }

    Connections {
        target: app.theme
        function onModeChanged() {
            themeBox.currentIndex = dlg.themeIndex();
        }
    }

    Connections {
        target: app.language
        function onModeChanged() {
            languageBox.currentIndex = dlg.languageIndex();
        }
    }

    Text {
        width: parent.width
        text: qsTr("Appearance")
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMedium
        font.bold: true
    }

    Row {
        width: parent.width
        spacing: 10
        Text { text: qsTr("Theme"); width: 140; height: 38; verticalAlignment: Text.AlignVCenter; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
        AppComboBox {
            id: themeBox
            width: 180
            model: [qsTr("Follow system"), qsTr("Light"), qsTr("Dark")]
            onActivated: (index) => app.theme.mode = index === 2 ? "dark" : (index === 1 ? "light" : "auto")
        }
    }

    Text {
        width: parent.width
        text: qsTr("Language")
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMedium
        font.bold: true
    }

    Row {
        width: parent.width
        spacing: 10
        Text { text: qsTr("Interface language"); width: 140; height: 38; verticalAlignment: Text.AlignVCenter; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
        AppComboBox {
            id: languageBox
            width: 180
            model: [qsTr("Follow system"), qsTr("Chinese"), qsTr("English")]
            onActivated: (index) => app.language.mode = index === 2 ? "en" : (index === 1 ? "zh_CN" : "auto")
        }
    }

    Text {
        width: parent.width
        text: qsTr("Logging")
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMedium
        font.bold: true
    }

    Column {
        width: parent.width
        spacing: 8

        AppCheckBox { id: logDetailsBox; objectName: "logDetailsBox"; text: qsTr("Show detailed logs") }
    }

    Timer {
        interval: 2000
        repeat: true
        running: dlg.visible
        onTriggered: app.config.refreshLogStorage()
    }
    RowLayout {
        Text { text: qsTr("Single file limit (MiB)"); color: Theme.textSecondary }
        SpinBox { id: fileLimit; objectName: "logFileLimit"; from: 1; to: 100; value: 5; editable: true }
    }
    RowLayout {
        Text { text: qsTr("Total limit (MiB)"); color: Theme.textSecondary }
        SpinBox { id: totalLimit; objectName: "logTotalLimit"; from: 1; to: 10240; value: 50; editable: true }
    }
    Text {
        width: parent.width
        text: qsTr("Used: %1 MiB / %2 MiB").arg((app.config.logBytes / 1048576).toFixed(1)).arg(app.config.logTotalMiB)
        color: Theme.textSecondary
    }
    Text {
        width: parent.width
        text: app.config.logError
        visible: text.length > 0
        color: Theme.danger
        wrapMode: Text.WordWrap
    }
    Row {
        spacing: 10
        SecondaryButton { text: qsTr("Open log folder"); onClicked: app.openLogDir() }
        SecondaryButton {
            objectName: "clearLogFilesButton"
            text: app.config.logBusy ? qsTr("Working…") : qsTr("Clear log files")
            enabled: !app.config.logBusy
            onClicked: clearConfirmation.open()
        }
    }
    AppDialog {
        id: clearConfirmation
        titleText: qsTr("Clear log files?")
        Text {
            width: parent.width
            text: qsTr("Delete all saved log files?")
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
        }
        footerContent: RowLayout {
            width: parent.width
            spacing: 10
            Item { Layout.fillWidth: true }
            SecondaryButton { text: qsTr("Cancel"); onClicked: clearConfirmation.close() }
            PrimaryButton { text: qsTr("Confirm"); onClicked: { app.config.clearLogFiles(); clearConfirmation.close(); } }
        }
    }

    footerContent: RowLayout {
        width: parent.width
        spacing: 10

        SecondaryButton {
            visible: Qt.platform.os === "linux"
            text: qsTr("Install udev rules")
            onClicked: app.config.installUdev()
        }

        Item { Layout.fillWidth: true }

        SecondaryButton {
            objectName: "cancelSettingsButton"
            text: qsTr("Cancel")
            onClicked: dlg.close()
        }

        PrimaryButton {
            objectName: "saveSettingsButton"
            enabled: !app.config.logBusy
            text: qsTr("Save")
            onClicked: {
                var map = {
                    "ui_log_details": logDetailsBox.checked,
                    "log_file_mib": fileLimit.value,
                    "log_total_mib": totalLimit.value
                };
                if (app.config.save(map))
                    dlg.close();
            }
        }
    }

    Text {
        width: parent.width
        text: app.config.status
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.WordWrap
    }
}
