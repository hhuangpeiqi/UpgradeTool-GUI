#pragma once
#include "gui/WindowsUsbDriver.h"

namespace gui {
// All hooks run on the installation worker. Tests substitute time and device operations.
struct DriverRecoveryHooks {
    std::function<DriverScan()> scan;
    std::function<DriverInstallResult(const DriverDevice &)> install;
    std::function<DriverInstallResult(const DriverDevice &)> restart;
    std::function<void(int)> wait;
    DriverProgress progress;
};
DriverInstallResult RecoverWindowsUsbDriver(const DriverDevice &, const QVector<DriverDevice> &, const DriverRecoveryHooks &);
// After a normal assistant close, try one device restart. Readiness is still
// verified by the controller with the user's permissions; install is unused.
DriverInstallResult RecoverRockusbDriver(const DriverDevice &, const DriverRecoveryHooks &);
}
