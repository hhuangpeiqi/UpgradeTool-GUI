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
    auto changed = original; changed.instance += "-new";
    CHECK(gui::FindDriverDevice({changed}, original));
    changed.location += "-other";
    CHECK(!gui::FindDriverDevice({changed}, original));
    for (int scenario = 0; scenario < 9; ++scenario) {
        int restarts = 0;
        auto device = original;
        device.service = "usbUDisc"; device.state = gui::DriverState::Bound;
        if (scenario == 0) device.state = gui::DriverState::Ready;
        if (scenario == 2) device.state = gui::DriverState::RebootRequired;
        if (scenario == 3) device.service = "WinUSB";
        if (scenario == 6) device.verifiedInterface = false;
        gui::DriverRecoveryHooks hooks;
        hooks.wait = [](int) {};
        hooks.scan = [&] {
            if (scenario == 4) return gui::DriverScan{};
            if (scenario == 5) return gui::DriverScan{{device}, "scan failed"};
            if (scenario == 7) { auto duplicate = device; duplicate.instance += "-other"; return gui::DriverScan{{device, duplicate}, {}}; }
            return gui::DriverScan{{device}, {}};
        };
        hooks.restart = [&](const auto &current) {
            ++restarts; CHECK(current.fingerprint() == device.fingerprint());
            gui::DriverInstallResult result{true, false, {}};
            if (scenario == 8) result.rebootRequired = true;
            return result;
        };
        const auto result = gui::RecoverAllwinnerDriver(original, hooks);
        CHECK(result.ok && restarts == int(scenario == 1 || scenario == 8));
        CHECK(result.rebootRequired == (scenario == 2 || scenario == 8));
    }
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
