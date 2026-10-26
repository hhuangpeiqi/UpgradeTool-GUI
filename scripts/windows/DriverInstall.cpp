#include "gui/WindowsUsbDriver.h"
#include "gui/DriverRecovery.h"
#include <QScopeGuard>
#include <windows.h>
#include <setupapi.h>

namespace gui {
DriverInstallResult RestartWindowsUsbDevice(const DriverDevice &target) {
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
