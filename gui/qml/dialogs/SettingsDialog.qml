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
            text: qsTr("Save")
            onClicked: {
                var map = {
                    "ui_log_details": logDetailsBox.checked
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
