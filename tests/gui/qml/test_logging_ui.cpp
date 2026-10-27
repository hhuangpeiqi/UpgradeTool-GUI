#include "gui/backend/AppController.h"
#include "gui/AdbRunner.h"
#include "gui/QtObserver.h"
#include "gui/OperationSpec.h"
#include "gui/DiagnosticLog.h"
#include "platform/PlatformPath.h"
#include "tests/support/Checks.h"
#include "tests/support/qt/QmlHelpers.h"
#include <QGuiApplication>
#include <QQuickStyle>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QColor>
#include <QDateTime>
#include <thread>

static int failures = 0;
static QByteArray Logs(const QString &directory) {
    QByteArray result;
    for (const auto &name : QDir(directory).entryList(QDir::Files)) {
        QFile file(QDir(directory).filePath(name));
        if (file.open(QIODevice::ReadOnly)) result += file.readAll();
    }
    return result;
}
static void TestRouting() {
    QTemporaryDir dir;
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    qputenv("XDG_STATE_HOME", dir.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    qputenv("ADB", dir.filePath("missing-adb").toUtf8());
    platform::LogService log(dir.path().toStdString(), "log", true);
    gui::AppController controller(&log);
    controller.findChild<gui::AdbRunner *>()->Stop();
    controller.language()->setMode("zh_CN");
    controller.log()->clear();
    log.write(core::LogLevel::Info, "Application", {"gui::AppController", "Application started", {}});
    CHECK(controller.log()->count() == 1);
    CHECK(controller.log()->data(controller.log()->index(0), gui::LogModel::TextRole).toString() == QString::fromUtf8("应用已启动"));
    CHECK(Logs(dir.path()).contains("Application started"));
    CHECK(!Logs(dir.path()).contains(QString::fromUtf8("应用已启动").toUtf8()));

    gui::QtObserver observer(log);
    std::thread producer([&] {
        core::ScopedLogContext context({"Allwinner", 77, 3, "usb:1-5.3"});
        observer.OnOutput(core::LogLevel::Warn, "recoverable warning");
        observer.OnOutput(core::LogLevel::Error, "unique diagnostic");
        core::OperationResult result;
        result.status = core::OperationStatus::Failed;
        result.diagnostic = "unique diagnostic";
        observer.OnResult(result);
    });
    producer.join();
    controller.setVendor(2);
    CHECK(AwaitUi([&] { return controller.log()->count() >= 4; }));
    const auto index = controller.log()->index(1);
    CHECK(controller.log()->data(index, gui::LogModel::SourceRole) == "Allwinner");
    CHECK(controller.log()->data(index, gui::LogModel::LevelRole) == "WARN");
    CHECK(!controller.log()->data(index, gui::LogModel::ErrorRole).toBool());
    CHECK(controller.log()->data(index, gui::LogModel::ContextRole).toString().contains("task=77 step=3 usb:1-5.3"));
    CHECK(Logs(dir.path()).contains("[WARN] [Allwinner] [session="));
    CHECK(Logs(dir.path()).contains("task=77 step=3 device=usb:1-5.3] recoverable warning"));
    CHECK(Logs(dir.path()).count("unique diagnostic") == 1);
    core::OperationResult failed;
    failed.diagnostic = "unique diagnostic";
    observer.OnResult(failed); // A later result must retain the same error.
    CHECK(Logs(dir.path()).count("unique diagnostic") == 2);

    gui::WriteDiagnostic(log, "ADB", QJsonObject{{"event", "command_finished"}, {"log_task", 88},
        {"command", "shell\nprintf test"}, {"exit_code", 0}, {"output", "one\ntwo"}});
    const auto disk = Logs(dir.path());
    CHECK(disk.contains("task=88] command_finished"));
    CHECK(disk.contains("| command=shell\n"));
    CHECK(disk.contains("| printf test\n"));
    CHECK(disk.contains("| output=one\n"));
    CHECK(disk.contains("| two\n"));
    controller.setVendor(0);
    uint64_t firstTask = 0, secondTask = 0;
    QObject::connect(&controller, &gui::AppController::requestExecute, &controller, [&](const core::TaskJob &job) {
        if (!firstTask) firstTask = job.logId; else secondTask = job.logId;
    });
    controller.rockchip()->runOperation(gui::Operations::FirmwareInfo, {{"path", dir.filePath("missing.img")}});
    CHECK(AwaitUi([&] { return !controller.tasks()->busy(); }));
    controller.rockchip()->runOperation(gui::Operations::FirmwareInfo, {{"path", dir.filePath("missing.img")}});
    CHECK(AwaitUi([&] { return !controller.tasks()->busy(); }));
    CHECK(firstTask && secondTask && firstTask != secondTask);
    CHECK(Logs(dir.path()).contains(QByteArray("task=") + QByteArray::number(firstTask)));
    const auto finalDisk = Logs(dir.path());
    const int count = controller.log()->count();
    log.setEnabled(false);
    log.write(core::LogLevel::Warn, "Rockchip", {{}, "visible only", {}});
    CHECK(controller.log()->count() == count + 1);
    CHECK(Logs(dir.path()) == finalDisk);
    controller.log()->clear();
    CHECK(controller.log()->count() == 0 && Logs(dir.path()) == finalDisk);
    controller.language()->setMode("en");
}
static void TestPanel(QQmlEngine &engine) {
    QTemporaryDir dir;
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    qputenv("XDG_STATE_HOME", dir.path().toUtf8());
    qputenv("APPDATA", dir.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    qputenv("ADB", dir.filePath("missing-adb").toUtf8());
    CHECK(platform::EnsureDirRecursive(platform::GetUserConfigDir()));
    QFile configFile(QString::fromStdString(platform::GetUserConfigDir() + "/config.ini"));
    CHECK(configFile.open(QIODevice::WriteOnly));
    configFile.write("ui_log_details=true\nforce_data_band=6\nfirmware=ignored.img\nloader=ignored.bin\n"); // Old application configuration must be ignored.
    configFile.close();
    gui::AppController controller(nullptr);
    controller.findChild<gui::AdbRunner *>()->Stop();
    controller.language()->setMode("en");
    CHECK(!controller.config()->uiLogDetails());
    engine.rootContext()->setContextProperty("app", &controller);
    gui::LogModel model;
    engine.rootContext()->setContextProperty("testLogModel", &model);
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import UpgradeTool
Window {
    width: 600; height: 300; visible: true
    property color warningColor: Theme.warning
    property color errorColor: Theme.danger
    LogPanel {
        objectName: "testLogPanel"
        anchors.fill: parent
        logModel: testLogModel
        showDetails: app.config.uiLogDetails
    }
    SettingsDialog { objectName: "testSettingsDialog" }
}
)QML", QUrl());
    CHECK(AwaitUi([&] { return component.status() != QQmlComponent::Loading; }));
    std::unique_ptr<QObject> object(component.create());
    if (!object) { std::fprintf(stderr, "%s\n", component.errorString().toUtf8().constData()); CHECK(object); return; }
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    auto *list = FindVisualItem(window->contentItem(), "logList");
    CHECK(list);
    if (!list) return;
    auto *panel = FindVisualItem(window->contentItem(), "testLogPanel");
    auto *settings = object->findChild<QObject *>("testSettingsDialog");
    CHECK(panel && settings);
    if (!panel || !settings) return;

    core::LogEntry sample;
    sample.time = std::chrono::system_clock::time_point(std::chrono::milliseconds(1770000000123LL));
    sample.level = core::LogLevel::Error;
    sample.context = {"Allwinner", 77, 3, "usb:1-5.3"};
    sample.message.key = "Write failed\n<b>plain text</b>";
    model.append(sample);
    CHECK(AwaitUi([&] { return FindVisualItem(list, "logEntry") != nullptr; }));
    auto *sampleRow = FindVisualItem(list, "logEntry");
    CHECK(sampleRow);
    if (!sampleRow) return;
    const auto time = QDateTime::fromMSecsSinceEpoch(1770000000123LL).toString("HH:mm:ss.zzz");
    const auto compact = time.left(8) + " Write failed\n<b>plain text</b>";
    const auto detailed = time + " [ERROR] [Allwinner] [task=77 step=3 usb:1-5.3] Write failed\n<b>plain text</b>";
    CHECK(sampleRow->property("text").toString() == compact);
    CHECK(sampleRow->property("color").value<QColor>() == object->property("errorColor").value<QColor>());
    int detailChanges = 0;
    QObject::connect(controller.config(), &gui::ConfigStore::uiLogDetailsChanged, [&] { ++detailChanges; });
    CHECK(QMetaObject::invokeMethod(settings, "openDialog"));
    CHECK(AwaitUi([&] { return FindVisualItem(window->contentItem(), "logDetailsBox") != nullptr; }));
    auto *details = FindVisualItem(window->contentItem(), "logDetailsBox");
    auto *save = FindVisualItem(window->contentItem(), "saveSettingsButton");
    auto *cancel = FindVisualItem(window->contentItem(), "cancelSettingsButton");
    CHECK(details && save && cancel);
    if (!details || !save || !cancel) return;
    CHECK(!details->property("checked").toBool());
    details->setProperty("checked", true);
    CHECK(!controller.config()->uiLogDetails()); // Edits wait for Save.
    CHECK(QMetaObject::invokeMethod(cancel, "clicked"));
    CHECK(!controller.config()->uiLogDetails() && detailChanges == 0);
    CHECK(QMetaObject::invokeMethod(settings, "openDialog"));
    CHECK(!details->property("checked").toBool());
    details->setProperty("checked", true);
    CHECK(QMetaObject::invokeMethod(save, "clicked"));
    CHECK(controller.config()->uiLogDetails() && detailChanges == 1);
    CHECK(AwaitUi([&] { return sampleRow->property("text").toString() == detailed; }));
    CHECK(sampleRow->property("color").value<QColor>() == object->property("errorColor").value<QColor>());
    gui::ConfigStore restored;
    CHECK(restored.uiLogDetails()); // A new store reads the saved preference at startup.
    QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "upgrade_tool", "upgrade_tool");
    CHECK(preferences.value("ui/logDetails").toBool());
    CHECK(preferences.value("ui/language").toString() == "en");
    CHECK(controller.config()->save({{"ui_log_details", true}}));
    CHECK(detailChanges == 1); // Saving an unchanged preference emits no notification.
    CHECK(controller.rockchip()->firmwarePath().isEmpty());
    CHECK(configFile.open(QIODevice::ReadOnly));
    CHECK(configFile.readAll() == "ui_log_details=true\nforce_data_band=6\nfirmware=ignored.img\nloader=ignored.bin\n");
    configFile.close();
    CHECK(QMetaObject::invokeMethod(settings, "openDialog"));
    CHECK(details->property("checked").toBool());
    details->setProperty("checked", false);
    CHECK(QMetaObject::invokeMethod(save, "clicked"));
    CHECK(!controller.config()->uiLogDetails() && detailChanges == 2);
    CHECK(AwaitUi([&] { return sampleRow->property("text").toString() == compact; }));
    CHECK(!gui::ConfigStore().uiLogDetails());
    // Reloading an externally edited preference also refreshes existing rows.
    preferences.setValue("ui/logDetails", true);
    preferences.sync();
    CHECK(preferences.status() == QSettings::NoError);
    CHECK(controller.config()->load().value("ui_log_details").toBool());
    CHECK(detailChanges == 3);
    CHECK(AwaitUi([&] { return sampleRow->property("text").toString() == detailed; }));
    model.clear();
    for (int i = 0; i < 80; ++i) {
        core::LogEntry entry;
        entry.context.source = "Allwinner";
        entry.message.key = "Line " + std::to_string(i) + " <b>plain text</b> " + std::string(130, 'x');
        entry.level = core::LogLevel::Warn;
        model.append(entry);
    }
    CHECK(AwaitUi([&] { return list->property("atYEnd").toBool() && list->property("contentHeight").toDouble() > list->height(); }));
    QMetaObject::invokeMethod(list, "movementStarted");
    QMetaObject::invokeMethod(list, "positionViewAtBeginning");
    QMetaObject::invokeMethod(list, "movementEnded");
    CHECK(AwaitUi([&] { return list->property("atYBeginning").toBool(); }));
    auto *row = FindVisualItem(list, "logEntry");
    CHECK(row);
    if (row) {
        CHECK(row->property("color").value<QColor>() == object->property("warningColor").value<QColor>());
        CHECK(!row->property("truncated").toBool());
        CHECK(row->height() > 26); // The full long message wraps onto multiple lines.
        CHECK(row->property("text").toString().contains(" [WARN] [Allwinner] Line "));
    }
    auto values = controller.config()->load();
    values["ui_log_details"] = false;
    CHECK(controller.config()->save(values));
    CHECK(!panel->property("showDetails").toBool());
    CHECK(!list->property("following").toBool()); // Changing format while reading history does not follow the tail.
    CHECK(AwaitUi([&] { return !list->property("atYEnd").toBool(); }));
    if (const auto path = qEnvironmentVariable("UPGRADE_LOG_SCREENSHOT"); !path.isEmpty())
        CHECK(window->grabWindow().save(path));
    const auto historyY = list->property("contentY").toDouble();
    core::LogEntry entry;
    entry.context.source = "Rockchip"; entry.message.key = "New entry while reading history";
    model.append(entry);
    QCoreApplication::processEvents();
    CHECK(!list->property("atYEnd").toBool());
    CHECK(qAbs(list->property("contentY").toDouble() - historyY) < 1);
    QMetaObject::invokeMethod(list, "positionViewAtEnd");
    CHECK(AwaitUi([&] { return list->property("atYEnd").toBool(); }));
    model.append(entry);
    CHECK(AwaitUi([&] { return list->property("atYEnd").toBool(); }));
    QMetaObject::invokeMethod(list, "forceLayout");
    auto *last = FindVisualItem(list, "logEntry");
    CHECK(last && !last->property("text").toString().contains("[Rockchip]"));
    for (int i = 0; i < 5020; ++i) model.append(entry);
    CHECK(model.count() == 5000);
    model.clear();
    CHECK(model.count() == 0);
}
static void TestSaveFailure() {
    QTemporaryDir dir;
    const QString blocked = dir.filePath("blocked");
    QFile file(blocked);
    CHECK(file.open(QIODevice::WriteOnly));
    file.close();
    // A regular file as the settings root fails consistently on every platform.
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, blocked);
    gui::ConfigStore store;
    int changes = 0;
    QObject::connect(&store, &gui::ConfigStore::uiLogDetailsChanged, [&] { ++changes; });
    CHECK(!store.save({{"ui_log_details", true}}));
    CHECK(!store.uiLogDetails() && changes == 0);
    CHECK(store.status().contains("Save failed"));
    CHECK(!gui::ConfigStore().uiLogDetails());
    CHECK(!store.load().value("ui_log_details").toBool());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
}
int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle("Basic");
    TestRouting();
    QQmlEngine engine;
    engine.addImportPath("qrc:/qt/qml");
    TestPanel(engine);
    TestSaveFailure();
    return failures ? 1 : 0;
}
