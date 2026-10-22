#include "gui/backend/DriverController.h"
#include "tests/support/Checks.h"
#include "tests/support/qt/Await.h"
#include <QCoreApplication>
#include <atomic>
#include <mutex>
#include <QTemporaryDir>
#include <QSettings>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir settingsDirectory;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    int failures = 0;
    gui::DriverDevice rockchip;
    rockchip.vid = 0x2207; rockchip.pid = 0x350b;
    CHECK(gui::RecognizeWindowsFlashingInterface(rockchip, {"USB\\Class_ff&SubClass_06&Prot_05"}));
    rockchip.state = gui::DriverState::Missing;
    CHECK(rockchip.canInstall());
    CHECK(!gui::RecognizeWindowsFlashingInterface(rockchip, {"USB\\Class_ff&SubClass_42&Prot_01"}));
    CHECK(!rockchip.canInstall());
    CHECK(!gui::RecognizeWindowsFlashingInterface(rockchip, {"USB\\Class_08&SubClass_06&Prot_50"}));
    CHECK(!gui::RecognizeWindowsFlashingInterface(rockchip, {"USB\\COMPOSITE"}));
    gui::DriverDevice allwinner;
    allwinner.vid = 0x1f3a; allwinner.pid = 0xefe8;
    CHECK(gui::RecognizeWindowsFlashingInterface(allwinner, {"USB\\Class_ff&SubClass_ff&Prot_ff"}));
    allwinner.state = gui::DriverState::AccessFailed;
    CHECK(!allwinner.canInstall());
    allwinner.state = gui::DriverState::Incompatible; allwinner.service = "VendorUSB";
    CHECK(allwinner.canInstall());
    gui::DriverDevice qualcomm;
    qualcomm.vid = 0x05c6; qualcomm.pid = 0x9008;
    CHECK(gui::RecognizeWindowsFlashingInterface(qualcomm, {"USB\\Class_ff&SubClass_ff&Prot_ff"}));
    qualcomm.state = gui::DriverState::Missing;
    CHECK(!qualcomm.canInstall());
    qualcomm.pid = 0x9091;
    CHECK(!gui::RecognizeWindowsFlashingInterface(qualcomm, {"USB\\Class_ff&SubClass_ff&Prot_ff"}));

    allwinner.instance = "USB\\VID_1F3A&PID_EFE8\\board";
    allwinner.hardwareId = "USB\\VID_1F3A&PID_EFE8"; allwinner.location = "port-1";
    auto second = allwinner; second.instance += "-second";
    second.location += "-second";
    second.hardwareId += "&REV_0100";
    CHECK(allwinner.bindingId() == second.bindingId());
    CHECK(gui::DriverDevice::fromJson(allwinner.toJson()).fingerprint() == allwinner.fingerprint());
    std::atomic<int> scans{0}, installs{0};
    std::atomic<bool> ready{false}, cancel{false}, hold{false}, entered{false};
    gui::DriverHooks hooks;
    hooks.scan = [&](bool probe) {
        ++scans; entered = true;
        while (hold) QThread::msleep(1);
        auto a = allwinner, b = second;
        if (ready) { a.service = b.service = "WinUSB"; a.state = b.state = probe ? gui::DriverState::Ready : gui::DriverState::Bound; }
        return gui::DriverScan{{a, b}, {}};
    };
    hooks.install = [&](const auto &target, const auto &affected) {
        ++installs;
        CHECK(target.instance == allwinner.instance);
        CHECK(affected.size() == 2);
        if (cancel) return gui::DriverInstallResult{false, true, {}};
        ready = true;
        return gui::DriverInstallResult{true, false, {}};
    };
    {
        gui::DriverController controller(hooks, true);
        controller.setVendor(1);
        CHECK(Await([&] { return !controller.checking(); }));
        CHECK(controller.attentionCount() == 2 && installs == 0);
        int deviceChanges = 0;
        QObject::connect(&controller, &gui::DriverController::devicesChanged, [&] { ++deviceChanges; });
        controller.refresh(false);
        CHECK(Await([&] { return !controller.checking(); }));
        CHECK(deviceChanges == 0);
        int confirmations = 0, maintenance = 0;
        QObject::connect(&controller, &gui::DriverController::confirmationRequested, [&] { ++confirmations; });
        QObject::connect(&controller, &gui::DriverController::maintenanceRequested, [&] { ++maintenance; });
        controller.setSuspended(true);
        const int before = scans;
        controller.refresh(); controller.prepareInstall(allwinner.instance);
        CHECK(scans == before && confirmations == 0);
        controller.setSuspended(false);
        CHECK(Await([&] { return !controller.checking(); }));
        controller.prepareInstall("stale-instance"); CHECK(confirmations == 0);
        controller.prepareInstall(allwinner.instance);
        CHECK(confirmations == 1 && controller.confirmation().size() == 2 && installs == 0);
        controller.confirmInstall();
        CHECK(controller.busy() && maintenance == 1 && installs == 0);
        CHECK(controller.summaryLevel() == "busy" && !controller.summaryText().isEmpty());
        controller.confirmInstall(); CHECK(maintenance == 1);
        controller.beginInstall();
        CHECK(Await([&] { return !controller.busy(); }));
        CHECK(installs == 1 && controller.attentionCount() == 0);
        CHECK(controller.summaryLevel() == "neutral" && controller.summaryText().isEmpty());
        controller.prepareInstall(allwinner.instance); CHECK(confirmations == 1);
    }
    ready = false; cancel = true;
    {
        gui::DriverController controller(hooks, true);
        controller.setVendor(1); CHECK(Await([&] { return !controller.checking(); }));
        controller.prepareInstall(allwinner.instance); controller.confirmInstall(); controller.beginInstall();
        CHECK(Await([&] { return !controller.busy(); }));
        CHECK(controller.message().contains("cancelled"));
        CHECK(installs == 2 && controller.attentionCount() == 2);
        controller.refresh(); CHECK(Await([&] { return !controller.checking(); }));
        CHECK(installs == 2);
    }
    {
        gui::DriverController controller(hooks, true);
        hold = true; entered = false;
        controller.refresh(); CHECK(Await([&] { return entered.load(); }));
        controller.setSuspended(true); hold = false;
        CHECK(Await([&] { return !controller.checking(); }));
        CHECK(controller.devices().isEmpty());
        controller.setSuspended(false);
        CHECK(Await([&] { return !controller.checking(); }));
        controller.shutdown(); CHECK(controller.stopped());
    }
    {
        gui::DriverController unavailable(hooks, false);
        const int before = scans;
        unavailable.refresh(); unavailable.poll();
        CHECK(scans == before && !unavailable.available());
        CHECK(unavailable.summaryLevel() == "neutral" && unavailable.summaryText().isEmpty());
    }
    {
        std::atomic<bool> installerHeld{true}, verificationHeld{true}, reachedBinding{false}, verificationEntered{false};
        std::atomic<int> calls{0};
        gui::DriverHooks progressHooks;
        progressHooks.scan = [&](bool probe) {
            auto d = allwinner;
            if (reachedBinding) {
                d.service = "WinUSB";
                d.state = gui::DriverState::Bound;
                if (probe) {
                    verificationEntered = true;
                    while (verificationHeld) QThread::msleep(1);
                    d.state = gui::DriverState::Ready;
                }
            }
            return gui::DriverScan{{d}, {}};
        };
        progressHooks.installWithProgress = [&](const auto &, const auto &, const gui::DriverProgress &progress) {
            ++calls;
            progress(gui::DriverPhase::Preparing);
            progress(gui::DriverPhase::Installing);
            reachedBinding = true;
            progress(gui::DriverPhase::Bound);
            while (installerHeld) QThread::msleep(1);
            return gui::DriverInstallResult{true, false, {}};
        };
        gui::DriverController controller(progressHooks, true);
        controller.setVendor(1); CHECK(Await([&] { return !controller.checking(); }));
        controller.prepareInstall(allwinner.instance); controller.confirmInstall(); controller.beginInstall();
        CHECK(Await([&] { return controller.message().contains("Waiting for Windows"); }));
        CHECK(controller.busy() && controller.locked() && controller.hasPending());
        CHECK(controller.progressText().contains("elapsed"));
        CHECK(!controller.devices().first().toMap()["canInstall"].toBool());
        controller.confirmInstall(); controller.beginInstall();
        CHECK(calls == 1);
        CHECK(Await([&] { return controller.elapsedSeconds() >= 1; }));
        installerHeld = false;
        CHECK(Await([&] { return verificationEntered.load(); }));
        CHECK(controller.busy() && controller.locked() && controller.message() == "Checking driver…");
        verificationHeld = false;
        CHECK(Await([&] { return !controller.busy(); }));
        CHECK(!controller.hasPending() && controller.message() == "Driver installed");
        CHECK(controller.progressText() == controller.message() && calls == 1);
    }
    {
        gui::DriverScan outcome;
        gui::DriverHooks summaryHooks;
        summaryHooks.scan = [&](bool) { return outcome; };
        gui::DriverController controller(summaryHooks, true);
        CHECK(controller.summaryLevel() == "neutral" && controller.summaryText().isEmpty());
        auto refresh = [&] { controller.refresh(); CHECK(Await([&] { return !controller.checking(); })); };
        for (const auto state : {gui::DriverState::Missing, gui::DriverState::Incompatible,
                gui::DriverState::AccessFailed, gui::DriverState::DeviceError}) {
            auto device = allwinner; device.vendor = 0; device.state = state;
            outcome = {{device}, {}}; refresh();
            CHECK(controller.summaryLevel() == "warning");
            CHECK(controller.summaryText() == controller.devices().first().toMap()["stateText"].toString());
        }
        auto missing = allwinner; missing.vendor = 0; missing.state = gui::DriverState::Missing;
        auto incompatible = missing; incompatible.instance += "-second"; incompatible.state = gui::DriverState::Incompatible;
        outcome = {{missing, incompatible}, {}}; refresh();
        CHECK(controller.summaryText() == "Drivers need attention (2)");
        controller.setVendor(1);
        CHECK(Await([&] { return !controller.checking(); }));
        CHECK(controller.summaryLevel() == "neutral" && controller.summaryText().isEmpty());
        controller.setVendor(0);
        CHECK(Await([&] { return !controller.checking(); }));
        auto readyDevice = missing; readyDevice.state = gui::DriverState::Ready;
        outcome = {{readyDevice, incompatible}, {}}; refresh();
        CHECK(controller.attentionCount() == 1 && controller.summaryText() == "Incompatible driver");
        for (const auto state : {gui::DriverState::Bound, gui::DriverState::Ready}) {
            readyDevice.state = state; outcome = {{readyDevice}, {}}; refresh();
            CHECK(controller.summaryLevel() == "neutral" && controller.summaryText().isEmpty());
        }
        outcome.error = "scan failed"; refresh();
        CHECK(controller.summaryLevel() == "warning" && controller.summaryText() == "Driver detection failed");
        outcome = {}; refresh();
        CHECK(controller.summaryLevel() == "neutral" && controller.summaryText().isEmpty());
        CHECK(controller.message().isEmpty());
    }
    {
        gui::DriverHooks pendingHooks;
        auto device = allwinner;
        bool scanFailed = false;
        pendingHooks.verificationTimeoutMs = 0;
        pendingHooks.scan = [&](bool probe) {
            auto d = device;
            if (!probe && d.state == gui::DriverState::Ready) d.state = gui::DriverState::Bound;
            return gui::DriverScan{{d}, scanFailed ? "scan failed" : QString{}};
        };
        pendingHooks.install = [&](const auto &, const auto &) {
            gui::DriverInstallResult result{true, false, {}};
            result.rebootRequired = true;
            return result;
        };
        {
            gui::DriverController controller(pendingHooks, true);
            controller.setVendor(1); CHECK(Await([&] { return !controller.checking(); }));
            controller.prepareInstall(device.instance); controller.confirmInstall(); controller.beginInstall();
            CHECK(Await([&] { return !controller.busy(); }));
            CHECK(controller.hasPending() && !controller.canResume());
            CHECK(controller.message().contains("Restart Windows"));
            // A later scan in this session must not erase Windows' restart requirement.
            device.state = gui::DriverState::Ready;
            controller.refresh(); CHECK(Await([&] { return !controller.checking(); }));
            CHECK(controller.hasPending());
        }
        device.state = gui::DriverState::Incompatible;
        {
            gui::DriverController controller(pendingHooks, true);
            CHECK(controller.hasPending());
            controller.refresh(false); CHECK(Await([&] { return !controller.checking(); }));
            CHECK(controller.canResume());
            int confirmations = 0;
            QObject::connect(&controller, &gui::DriverController::confirmationRequested, [&] { ++confirmations; });
            controller.resumePending();
            CHECK(confirmations == 1 && controller.confirmation().size() == 1 && !controller.busy());
            scanFailed = true;
            controller.refresh(); CHECK(Await([&] { return !controller.checking(); }));
            CHECK(controller.hasPending() && !controller.canResume());
            scanFailed = false; device.state = gui::DriverState::Ready;
            controller.refresh(false); CHECK(Await([&] { return !controller.checking(); }));
            CHECK(!controller.hasPending());
        }
        // Verification failures cannot use a cached Ready snapshot to report success.
        device.state = gui::DriverState::Incompatible;
        pendingHooks.install = [&](const auto &, const auto &) {
            scanFailed = true; return gui::DriverInstallResult{true, false, {}};
        };
        {
            gui::DriverController controller(pendingHooks, true);
            controller.setVendor(1); CHECK(Await([&] { return !controller.checking(); }));
            controller.prepareInstall(device.instance); controller.confirmInstall(); controller.beginInstall();
            CHECK(Await([&] { return !controller.busy(); }));
            CHECK(controller.hasPending() && controller.message().contains("detection failed"));
            scanFailed = false;
            device.instance += "-changed"; device.location.clear();
            controller.refresh(); CHECK(Await([&] { return !controller.checking(); }));
            CHECK(controller.hasPending() && !controller.canResume());
            controller.forgetPending(); CHECK(!controller.hasPending());
        }
    }
    return failures ? 1 : 0;
}
