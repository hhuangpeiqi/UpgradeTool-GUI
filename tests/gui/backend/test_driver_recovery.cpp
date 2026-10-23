#include "gui/DriverRecovery.h"
#include "tests/support/Checks.h"
#include <QCoreApplication>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    gui::DriverDevice original;
    original.instance = "USB\\VID_1F3A&PID_EFE8\\BOARD";
    original.hardwareId = "USB\\VID_1F3A&PID_EFE8&REV_0100";
    original.location = "USBROOT(0)#USB(1)";
    original.vendor = 1; original.interfaceNumber = -1;
    original.verifiedInterface = true;
    original.service = "VendorUSB"; original.state = gui::DriverState::Incompatible;
    enum Scenario { First, Restart, Retry, Reboot, RestartReboot, Cancel, Busy, BusyExhausted,
                    Denied, Disconnect, ExtraDevice, OtherDriver, ScanFailure, AccessFailed, DeviceReboot, RestartDenied };
    for (auto scenario : {First, Restart, Retry, Reboot, RestartReboot, Cancel, Busy, BusyExhausted,
                          Denied, Disconnect, ExtraDevice, OtherDriver, ScanFailure, AccessFailed, DeviceReboot, RestartDenied}) {
        QVector<gui::DriverDevice> devices{original};
        int installs = 0, restarts = 0, waited = 0;
        QVector<gui::DriverPhase> phases;
        gui::DriverRecoveryHooks hooks;
        hooks.wait = [&](int ms) { waited += ms; };
        hooks.progress = [&](auto phase) { phases.append(phase); };
        hooks.scan = [&] { return gui::DriverScan{devices, scenario == ScanFailure && installs ? "scan failure" : QString{}}; };
        auto ready = [&] { devices[0].service = "WinUSB"; devices[0].state = gui::DriverState::Ready; };
        hooks.install = [&](const auto &) {
            ++installs;
            gui::DriverInstallResult result{true, false, {}};
            if (scenario == First || (scenario == Retry && installs == 2) || (scenario == Busy && installs == 2)) ready();
            if (scenario == Reboot) result.rebootRequired = true;
            if (scenario == DeviceReboot) { devices[0].state = gui::DriverState::RebootRequired; devices[0].problem = 14; }
            if (scenario == Cancel) { result.ok = false; result.cancelled = true; }
            if (scenario == Denied) { result.ok = false; result.error = "signature rejected"; }
            if (scenario == BusyExhausted || (scenario == Busy && installs == 1)) {
                result.ok = false; result.retryable = true; result.error = "installer busy";
            }
            if (scenario == Disconnect) devices.clear();
            if (scenario == ExtraDevice) { auto extra = original; extra.instance += "-other"; extra.location += "-other"; devices.append(extra); }
            if (scenario == OtherDriver) devices[0].service = "UnexpectedDriver";
            if (scenario == AccessFailed) { devices[0].service = "WinUSB"; devices[0].state = gui::DriverState::AccessFailed; }
            return result;
        };
        hooks.restart = [&](const auto &target) {
            ++restarts; CHECK(target.instance == original.instance);
            gui::DriverInstallResult result{true, false, {}};
            if (scenario == Restart) {
                ready(); devices[0].instance += "-new";
                devices[0].hardwareId = "USB\\VID_1F3A&PID_EFE8";
            }
            if (scenario == RestartReboot) result.rebootRequired = true;
            if (scenario == RestartDenied) { result.ok = false; result.error = "device restart denied"; }
            return result;
        };
        const auto result = gui::RecoverWindowsUsbDriver(original, {original}, hooks);
        CHECK(!result.log.isEmpty());
        CHECK(installs == ((scenario == Retry || scenario == Busy || scenario == BusyExhausted) ? 2 : 1));
        CHECK(restarts == ((scenario == Restart || scenario == Retry || scenario == RestartReboot || scenario == AccessFailed || scenario == RestartDenied) ? 1 : 0));
        CHECK(result.rebootRequired == (scenario == Reboot || scenario == RestartReboot || scenario == DeviceReboot));
        CHECK(result.cancelled == (scenario == Cancel));
        if (scenario == Denied || scenario == Cancel || scenario == BusyExhausted || scenario == Disconnect ||
            scenario == ExtraDevice || scenario == OtherDriver || scenario == ScanFailure || scenario == RestartDenied) CHECK(!result.ok);
        else CHECK(result.ok);
        CHECK(waited <= 51000);
    }
    auto changed = original; changed.instance += "-new";
    CHECK(gui::FindDriverDevice({changed}, original));
    changed.location.clear();
    CHECK(!gui::FindDriverDevice({changed}, original));
    changed = original; changed.instance += "-new";
    CHECK(!gui::FindDriverDevice({original, changed}, original));
    changed = original; changed.interfaceNumber = 1;
    CHECK(!gui::FindDriverDevice({changed}, original));
    return failures ? 1 : 0;
}
