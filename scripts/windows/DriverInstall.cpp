#include "gui/WindowsUsbDriver.h"
#include "gui/DriverRecovery.h"
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QThread>
#include <windows.h>
#include <setupapi.h>
#include <shlobj.h>
#include <libwdi.h>

namespace gui {
namespace {
DriverInstallResult restartDevice(const DriverDevice &target) {
    DriverInstallResult result;
    const auto current = ScanWindowsUsbDrivers();
    const auto *device = FindDriverDevice(current.devices, target);
    if (!current.error.isEmpty() || !device || device->fingerprint() != target.fingerprint()) {
        result.error = QStringLiteral("Device changed before restart; refresh and select it again."); return result;
    }
    HDEVINFO set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (set == INVALID_HANDLE_VALUE) {
        result.error = QStringLiteral("SetupDiCreateDeviceInfoList: %1").arg(GetLastError()); return result;
    }
    auto cleanup = qScopeGuard([&] { SetupDiDestroyDeviceInfoList(set); });
    SP_DEVINFO_DATA info = {}; info.cbSize = sizeof(info);
    if (!SetupDiOpenDeviceInfoW(set, reinterpret_cast<LPCWSTR>(target.instance.utf16()), nullptr, 0, &info)) {
        result.error = QStringLiteral("SetupDiOpenDeviceInfo: %1").arg(GetLastError()); return result;
    }
    SP_PROPCHANGE_PARAMS change = {};
    change.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    change.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    change.StateChange = DICS_PROPCHANGE;
    change.Scope = DICS_FLAG_CONFIGSPECIFIC;
    if (SetupDiSetClassInstallParamsW(set, &info, &change.ClassInstallHeader, sizeof(change)))
        result.ok = SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, set, &info);
    const DWORD error = result.ok ? ERROR_SUCCESS : GetLastError();
    SP_DEVINSTALL_PARAMS_W params = {}; params.cbSize = sizeof(params);
    if (SetupDiGetDeviceInstallParamsW(set, &info, &params))
        result.rebootRequired = (params.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) != 0;
    if (error == ERROR_SUCCESS_REBOOT_REQUIRED || error == ERROR_SUCCESS_RESTART_REQUIRED) result.rebootRequired = true;
    if (!result.ok) result.error = QStringLiteral("Device restart failed (%1). Close other device tools and reconnect the device.").arg(error);
    return result;
}
}

DriverInstallResult InstallWindowsUsbDriverElevated(const DriverDevice &target, const QVector<DriverDevice> &affected,
                                                   const DriverProgress &progress) {
    DriverInstallResult result;
    if (!IsUserAnAdmin()) { result.error = QStringLiteral("Administrator permission is required."); return result; }
    const auto current = ScanWindowsUsbDrivers();
    QStringList expected, actual;
    QVector<DriverDevice> baseline;
    bool found = false;
    for (const auto &d : affected) expected.append(d.fingerprint());
    for (const auto &d : current.devices) {
        if (d.bindingId() == target.bindingId()) { actual.append(d.fingerprint()); baseline.append(d); }
        if (!d.instance.compare(target.instance, Qt::CaseInsensitive) && d.fingerprint() == target.fingerprint()) found = d.canInstall();
    }
    expected.sort(); actual.sort();
    const auto *selected = FindDriverDevice(current.devices, target);
    if (!current.error.isEmpty() || !found || !selected || expected != actual) {
        result.error = QStringLiteral("Device or driver changed; refresh and select it again."); return result;
    }
    QTemporaryDir directory(QDir::tempPath() + "/upgrade-driver-XXXXXX");
    if (!directory.isValid()) { result.error = QStringLiteral("Cannot create the driver installation directory."); return result; }
    DriverRecoveryHooks hooks;
    hooks.scan = [] { return ScanWindowsUsbDrivers(true); };
    hooks.wait = [](int ms) { QThread::msleep(ms); };
    hooks.progress = progress;
    hooks.restart = restartDevice;
    hooks.install = [&](const DriverDevice &deviceTarget) {
        DriverInstallResult outcome;
        wdi_device_info *list = nullptr;
        wdi_options_create_list enumerate = {}; enumerate.list_all = TRUE;
        int rc = wdi_create_list(&list, &enumerate);
        if (rc != WDI_SUCCESS) { outcome.error = QString::fromUtf8(wdi_strerror(rc)); return outcome; }
        auto cleanup = qScopeGuard([&] { wdi_destroy_list(list); });
        wdi_device_info *device = list;
        for (; device; device = device->next)
            if (device->device_id && !deviceTarget.instance.compare(QString::fromUtf8(device->device_id), Qt::CaseInsensitive)) break;
        if (!device || !device->hardware_id || deviceTarget.hardwareId.compare(QString::fromUtf8(device->hardware_id), Qt::CaseInsensitive)) {
            outcome.error = QStringLiteral("Device changed; refresh and select it again."); return outcome;
        }
        const auto path = directory.path().toUtf8();
        wdi_options_prepare_driver prepare = {}; prepare.driver_type = WDI_WINUSB;
        rc = wdi_prepare_driver(device, path.constData(), "upgrade_winusb.inf", &prepare);
        if (rc == WDI_SUCCESS) {
            const auto statusPath = directory.filePath("upgrade-install-status.json");
            QFile::remove(statusPath);
            wdi_options_install_driver install = {}; install.pending_install_timeout = 10000;
            rc = wdi_install_driver(device, path.constData(), "upgrade_winusb.inf", &install);
            QFile status(statusPath);
            if (status.open(QIODevice::ReadOnly) && status.size() <= 1024) {
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(status.readAll(), &error);
                if (error.error == QJsonParseError::NoError && document.isObject() && document.object()["rebootRequired"].isBool())
                    outcome.rebootRequired = document.object()["rebootRequired"].toBool();
                else if (rc == WDI_SUCCESS) { outcome.error = QStringLiteral("The installer returned an invalid restart status."); return outcome; }
            } else if (rc == WDI_SUCCESS) {
                outcome.error = QStringLiteral("The installer did not return a restart status."); return outcome;
            }
        }
        outcome.ok = rc == WDI_SUCCESS;
        outcome.cancelled = rc == WDI_ERROR_USER_CANCEL;
        outcome.retryable = rc == WDI_ERROR_PENDING_INSTALLATION;
        if (!outcome.ok) outcome.error = QString::fromUtf8(wdi_strerror(rc));
        return outcome;
    };
    return RecoverWindowsUsbDriver(*selected, baseline, hooks);
}
}
