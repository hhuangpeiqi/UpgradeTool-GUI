#include "gui/WindowsUsbDriver.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QElapsedTimer>
#include <libwdi.h>
#include <cstdio>
#include <exception>
#include <io.h>

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 2) return 2;
    QDir directory(application.arguments().at(1));
    QFile request(directory.filePath("request.json"));
    if (!request.open(QIODevice::ReadOnly) || request.size() > 1024 * 1024) return 2;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(request.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return 2;
    const auto object = document.object();
    const auto action = object.value("operation").toString("winusb");
    if (action != "winusb" && action != "qualcomm-install" && action != "qualcomm-uninstall") return 2;
    const auto target = gui::DriverDevice::fromJson(object["target"].toObject());
    QVector<gui::DriverDevice> affected;
    for (const auto &value : object["affected"].toArray()) affected.append(gui::DriverDevice::fromJson(value.toObject()));
    const auto logPath = QDir::toNativeSeparators(directory.filePath("libwdi.log"));
    _wfreopen(reinterpret_cast<const wchar_t *>(logPath.utf16()), L"w", stdout);
    _dup2(_fileno(stdout), _fileno(stderr));
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    wdi_set_log_level(WDI_LOG_LEVEL_DEBUG);
    QElapsedTimer elapsed; elapsed.start();
    gui::DriverInstallResult result;
    try {
        const gui::DriverProgress progressCallback = [&](gui::DriverPhase phase) {
            std::fprintf(stdout, "[%lld ms] phase=%d\n", static_cast<long long>(elapsed.elapsed()), int(phase));
            QSaveFile progress(directory.filePath("progress.json"));
            if (progress.open(QIODevice::WriteOnly)) {
                progress.write(QJsonDocument(QJsonObject{{"phase", int(phase)}}).toJson(QJsonDocument::Compact));
                progress.commit();
            }
        };
        if (action == "winusb") result = gui::InstallWindowsUsbDriverElevated(target, affected, progressCallback);
        else result = gui::RunQualcommDriverElevated(action == "qualcomm-install" ?
            gui::DriverOperation::QualcommInstall : gui::DriverOperation::QualcommUninstall, target, progressCallback);
    }
    catch (const std::exception &exception) { result.error = QString::fromUtf8(exception.what()); }
    std::fprintf(stdout, "[%lld ms] installation finished ok=%d reboot=%d\n",
        static_cast<long long>(elapsed.elapsed()), result.ok, result.rebootRequired);
    std::fflush(stdout); std::fflush(stderr);
    QFile log(directory.filePath("libwdi.log"));
    if (log.open(QIODevice::ReadOnly)) result.log += QString::fromUtf8(log.read(1024 * 1024));
    QSaveFile output(directory.filePath("result.json"));
    if (!output.open(QIODevice::WriteOnly)) return 3;
    output.write(QJsonDocument(QJsonObject{{"ok", result.ok}, {"cancelled", result.cancelled}, {"error", result.error}, {"log", result.log}, {"rebootRequired", result.rebootRequired}}).toJson());
    if (!output.commit()) return 3;
    return result.ok ? 0 : 1;
}
