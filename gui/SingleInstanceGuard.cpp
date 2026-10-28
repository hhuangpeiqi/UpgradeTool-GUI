#include "gui/SingleInstanceGuard.h"
#include "gui/backend/LanguageManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>

namespace gui {
SingleInstanceGuard::SingleInstanceGuard(const QString &directory)
    : m_directory(directory.isEmpty() ? QDir::homePath() + "/.upgrade_tool" : directory),
      m_lock(m_directory + "/instance.lock") {
    m_lock.setStaleLockTime(0);
}
SingleInstanceGuard::Result SingleInstanceGuard::acquire() {
    if (!QDir().mkpath(m_directory)) return Result::Error;
    if (m_lock.tryLock(0)) return Result::Acquired;
    return m_lock.error() == QLockFile::LockFailedError ? Result::AlreadyRunning : Result::Error;
}
int ShowStartupNotice(bool alreadyRunning) {
    LanguageManager language;
    const QString message = alreadyRunning
        ? QCoreApplication::translate("StartupNotice", "UpgradeTool-GUI is already running. Please do not start it again.")
        : QCoreApplication::translate("StartupNotice", "Cannot check whether UpgradeTool-GUI is running. Please check your user directory permissions.");
    const int result = alreadyRunning ? 0 : 1;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("startupMessage", message);
    engine.loadData(R"(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ApplicationWindow {
    visible: true
    title: "UpgradeTool-GUI"
    width: 440
    height: content.implicitHeight + 48
    minimumWidth: 320
    minimumHeight: height
    ColumnLayout {
        id: content
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 24 }
        spacing: 24
        Label { text: startupMessage; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Button { text: "OK"; Layout.alignment: Qt.AlignRight; onClicked: Qt.quit() }
    }
}
)");
    if (engine.rootObjects().isEmpty()) return 1;
    QCoreApplication::exec();
    return result;
}
}
