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
    // Rockusb recovery never reinstalls a package and restarts only the selected
    // flashing interface, using its fresh post-install fingerprint.
    auto rk = original;
    rk.vendor = 0; rk.vid = 0x2207; rk.pid = 0x350b;
    rk.instance = "USB\\VID_2207&PID_350B\\RK-BOARD";
    rk.hardwareId = "USB\\VID_2207&PID_350B";
    rk.service.clear(); rk.state = gui::DriverState::Missing;
    enum class RockusbCase { Ready, DelayedReady, Missing, Bound, Inaccessible, RestartDenied,
        Reboot, RestartReboot, Absent, OtherDevice, Ambiguous, OtherDriver, Unverified, WrongVendor, ScanFailure };
    for (auto scenario : {RockusbCase::Ready, RockusbCase::DelayedReady, RockusbCase::Missing,
            RockusbCase::Bound, RockusbCase::Inaccessible, RockusbCase::RestartDenied,
            RockusbCase::Reboot, RockusbCase::RestartReboot, RockusbCase::Absent,
            RockusbCase::OtherDevice, RockusbCase::Ambiguous, RockusbCase::OtherDriver,
            RockusbCase::Unverified, RockusbCase::WrongVendor, RockusbCase::ScanFailure}) {
        int restarts = 0, waited = 0, scans = 0;
        QVector<gui::DriverPhase> phases;
        auto device = rk;
        if (scenario == RockusbCase::Bound || scenario == RockusbCase::Inaccessible) {
            device.service = "Rockusb"; device.problem = 0;
            device.state = scenario == RockusbCase::Bound ? gui::DriverState::Bound : gui::DriverState::AccessFailed;
        }
        if (scenario == RockusbCase::OtherDriver) device.service = "WinUSB";
        if (scenario == RockusbCase::Unverified) device.verifiedInterface = false;
        if (scenario == RockusbCase::WrongVendor) device.vendor = 1;
        if (scenario == RockusbCase::OtherDevice) { device.instance += "-other"; device.location += "-other"; }
        gui::DriverRecoveryHooks hooks;
        hooks.wait = [&](int ms) { waited += ms; };
        hooks.progress = [&](auto phase) { phases.append(phase); };
        hooks.install = [&](const auto &) { CHECK(false); return gui::DriverInstallResult{}; };
        hooks.scan = [&] {
            ++scans;
            auto current = device;
            if (scenario == RockusbCase::Ready || (scenario == RockusbCase::DelayedReady && scans == 3))
                current.state = gui::DriverState::Ready;
            if (scenario == RockusbCase::Reboot) current.state = gui::DriverState::RebootRequired;
            if (scenario == RockusbCase::Absent) return gui::DriverScan{};
            if (scenario == RockusbCase::ScanFailure) return gui::DriverScan{{}, "scan failure"};
            if (scenario == RockusbCase::Ambiguous) {
                auto duplicate = current; duplicate.instance += "-other";
                return gui::DriverScan{{current, duplicate}, {}};
            }
            return gui::DriverScan{{current}, {}};
        };
        hooks.restart = [&](const auto &current) {
            ++restarts;
            CHECK(current.fingerprint() == device.fingerprint());
            CHECK(current.instance == rk.instance);
            gui::DriverInstallResult result{true, false, {}};
            if (scenario == RockusbCase::RestartDenied) { result.ok = false; result.error = "restart denied"; }
            if (scenario == RockusbCase::RestartReboot) result.rebootRequired = true;
            return result;
        };
        const auto result = gui::RecoverRockusbDriver(rk, hooks);
        const bool shouldRestart = scenario == RockusbCase::Missing || scenario == RockusbCase::Bound ||
            scenario == RockusbCase::Inaccessible || scenario == RockusbCase::RestartDenied || scenario == RockusbCase::RestartReboot;
        CHECK(restarts == int(shouldRestart));
        CHECK(result.ok && !result.cancelled && !result.log.isEmpty());
        CHECK(result.rebootRequired == (scenario == RockusbCase::Reboot || scenario == RockusbCase::RestartReboot));
        CHECK(phases.contains(gui::DriverPhase::Recovering) == shouldRestart);
        CHECK(waited == (scenario == RockusbCase::Ready || scenario == RockusbCase::Reboot ? 0 :
            scenario == RockusbCase::DelayedReady ? 1000 : 3000));
        if (scenario == RockusbCase::RestartDenied) CHECK(result.log.contains("restart denied"));
    }
    return failures ? 1 : 0;
}
