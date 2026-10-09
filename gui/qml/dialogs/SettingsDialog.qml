import UpgradeTool
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dlg

    titleText: qsTr("配置")
    width: 560

    function openDialog() {
        var v = app.config.load();
        parityBox.checked = !!v.lba_parity;
        rbCheckBox.checked = !!v.rb_check_off;
        norBox.checked = !!v.nor_single_idb;
        logOffBox.checked = !!v.log_off;
        stdoutBox.checked = !!v.stdout_buffer_off;
        bandField.text = v.force_data_band !== undefined ? v.force_data_band : "";
        logDirField.text = v.log_dir !== undefined ? v.log_dir : "";
        firmwareField.text = v.firmware !== undefined ? v.firmware : "";
        loaderField.text = v.loader !== undefined ? v.loader : "";
        deviceField.text = v.device !== undefined ? v.device : "";
        themeBox.currentIndex = dlg.themeIndex();
        dlg.open();
    }

    function themeIndex() {
        if (app.theme.mode === "dark")
            return 2;
        return app.theme.mode === "light" ? 1 : 0;
    }

    Connections {
        target: app.theme
        function onModeChanged() {
            themeBox.currentIndex = dlg.themeIndex();
        }
    }

    Text {
        width: parent.width
        text: qsTr("外观")
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMedium
        font.bold: true
    }

    Row {
        width: parent.width
        spacing: 10
        Text { text: qsTr("主题"); width: 140; height: 38; verticalAlignment: Text.AlignVCenter; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
        AppComboBox {
            id: themeBox
            width: 180
            model: [qsTr("跟随系统"), qsTr("浅色"), qsTr("深色")]
            onActivated: app.theme.mode = index === 2 ? "dark" : (index === 1 ? "light" : "auto")
        }
    }

    Text {
        width: parent.width
        text: qsTr("config.ini")
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMedium
        font.bold: true
    }

    Column {
        width: parent.width
        spacing: 8

        AppCheckBox { id: parityBox; text: "lba_parity" }
        AppCheckBox { id: rbCheckBox; text: "rb_check_off" }
        AppCheckBox { id: norBox; text: "nor_single_idb" }
        AppCheckBox { id: logOffBox; text: "log_off" }
        AppCheckBox { id: stdoutBox; text: "stdout_buffer_off" }
    }

    Column {
        width: parent.width
        spacing: 8

        Row {
            width: parent.width
            spacing: 10
            Text { text: "force_data_band"; width: 140; height: 38; verticalAlignment: Text.AlignVCenter; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
            AppTextField { id: bandField; width: parent.width - 150; placeholderText: qsTr("数值，留空则不写入") }
        }
        Row {
            width: parent.width
            spacing: 10
            Text { text: "log_dir"; width: 140; height: 38; verticalAlignment: Text.AlignVCenter; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
            AppTextField { id: logDirField; width: parent.width - 150; placeholderText: qsTr("留空则不写入") }
        }
        Row {
            width: parent.width
            spacing: 10
            Text { text: "firmware"; width: 140; height: 38; verticalAlignment: Text.AlignVCenter; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
            AppTextField { id: firmwareField; width: parent.width - 150; placeholderText: qsTr("留空则不写入") }
        }
        Row {
            width: parent.width
            spacing: 10
            Text { text: "loader"; width: 140; height: 38; verticalAlignment: Text.AlignVCenter; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
            AppTextField { id: loaderField; width: parent.width - 150; placeholderText: qsTr("留空则不写入") }
        }
        Row {
            width: parent.width
            spacing: 10
            Text { text: "device"; width: 140; height: 38; verticalAlignment: Text.AlignVCenter; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontNormal }
            AppTextField { id: deviceField; width: parent.width - 150; placeholderText: qsTr("数值，留空则不写入") }
        }
    }

    Text {
        width: parent.width
        text: qsTr("布尔键写入 true/false；留空的文本框键不会写入文件。重新加载会丢弃未保存的修改。")
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontNormal
        wrapMode: Text.WordWrap
    }

    footerContent: RowLayout {
        width: parent.width
        spacing: 10

        SecondaryButton {
            visible: Qt.platform.os === "linux"
            text: qsTr("安装 udev 规则")
            onClicked: app.config.installUdev()
        }

        Item { Layout.fillWidth: true }

        SecondaryButton {
            text: qsTr("重新加载")
            onClicked: dlg.openDialog()
        }

        SecondaryButton {
            text: qsTr("取消")
            onClicked: dlg.close()
        }

        PrimaryButton {
            text: qsTr("保存")
            onClicked: {
                var map = {
                    "lba_parity": parityBox.checked,
                    "rb_check_off": rbCheckBox.checked,
                    "nor_single_idb": norBox.checked,
                    "log_off": logOffBox.checked,
                    "stdout_buffer_off": stdoutBox.checked,
                    "force_data_band": bandField.text,
                    "log_dir": logDirField.text,
                    "firmware": firmwareField.text,
                    "loader": loaderField.text,
                    "device": deviceField.text
                };
                app.config.save(map);
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
