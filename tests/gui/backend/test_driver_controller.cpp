#include "gui/backend/DriverController.h"
#include "tests/support/Checks.h"
#include "tests/support/qt/Await.h"
#include <QCoreApplication>
#include <atomic>
#include <mutex>
#include <QTemporaryDir>
#include <QSettings>
#include <QFile>
#ifdef Q_OS_WIN
#include <windows.h>
#include <cfgmgr32.h>
#endif

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir settingsDirectory;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    int failures = 0;
#ifdef Q_OS_WIN
    CHECK(!gui::QualcommDriverPackageError(settingsDirectory.filePath("missing.exe")).isEmpty());
    QFile corruptPackage(settingsDirectory.filePath("corrupt.exe"));
    CHECK(corruptPackage.open(QIODevice::WriteOnly));
    corruptPackage.write("not an official package"); corruptPackage.close();
    CHECK(gui::QualcommDriverPackageError(corruptPackage.fileName()).contains("SHA256"));
#endif
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
    CHECK(qualcomm.canInstall());
    for (const auto state : {gui::DriverState::Incompatible, gui::DriverState::Bound, gui::DriverState::Ready,
            gui::DriverState::AccessFailed, gui::DriverState::DeviceError, gui::DriverState::RebootRequired}) {
        qualcomm.state = state; CHECK(!qualcomm.canInstall());
    }
    qualcomm.pid = 0x9091;
    qualcomm.interfaceNumber = 0; qualcomm.state = gui::DriverState::Missing;
    for (const int pid : {0x9091, 0x9092, 0x901d, 0x676c}) {
        qualcomm.pid = pid;
        CHECK(gui::RecognizeWindowsFlashingInterface(qualcomm, {"USB\\Class_ff&SubClass_ff&Prot_30"}));
        CHECK(qualcomm.canInstall());
    }
    qualcomm.pid = 0x9091; qualcomm.interfaceNumber = 2;
    CHECK(gui::RecognizeWindowsFlashingInterface(qualcomm, {"USB\\Class_ff&SubClass_ff&Prot_ff"}));
    CHECK(!qualcomm.canInstall()); // Unknown vendor interface is diagnostic information only.
    CHECK(!gui::RecognizeWindowsFlashingInterface(qualcomm, {"USB\\Class_ff&SubClass_42&Prot_01"}));
    CHECK(!gui::RecognizeWindowsFlashingInterface(qualcomm, {"USB\\COMPOSITE"}));
    qualcomm.vid = 0x2c7c; qualcomm.pid = 0x0125; qualcomm.interfaceNumber = 0;
    CHECK(gui::RecognizeWindowsFlashingInterface(qualcomm, {"USB\\Class_ff&SubClass_ff&Prot_ff"}));
    CHECK(!qualcomm.canInstall()); // OEM bindings never use the Qualcomm installer.
    qualcomm.vid = 0x1234; qualcomm.pid = 0xabcd; qualcomm.label = "OEM Diagnostic port";
    CHECK(gui::RecognizeWindowsFlashingInterface(qualcomm, {"USB\\Class_ff&SubClass_00&Prot_00"}));
    CHECK(!qualcomm.canInstall());

#ifdef Q_OS_WIN
    {
        gui::DriverDevice diag;
        diag.vid = 0x05c6; diag.pid = 0x9091; diag.interfaceNumber = 0;
        diag.instance = "qualcomm-diag"; diag.service = "qcwdfserial"; diag.port = "COM4";
        CHECK(gui::RecognizeWindowsFlashingInterface(diag, {"USB\\Class_ff&SubClass_ff&Prot_30"}));
        CHECK(diag.verifiedInterface);
        auto modem = diag;
        modem.instance = "qualcomm-modem"; modem.interfaceNumber = 1;
        modem.service = "Modem"; modem.port = "COM5";
        CHECK(gui::RecognizeWindowsFlashingInterface(modem, {"USB\\Class_ff&SubClass_00&Prot_00"}));
        auto wwan = diag;
        wwan.instance = "qualcomm-wwan"; wwan.interfaceNumber = 2;
        wwan.service = "qcusbwwan"; wwan.port.clear();
        CHECK(gui::RecognizeWindowsFlashingInterface(wwan, {"USB\\Class_ff&SubClass_ff&Prot_ff"}));
        CHECK(!modem.verifiedInterface && !wwan.verifiedInterface);

        gui::DriverScan outcome;
        gui::DriverHooks auxiliaryHooks;
        auxiliaryHooks.scan = [&](bool) { return outcome; };
        gui::DriverController controller(auxiliaryHooks, true);
        controller.setVendor(2);
        CHECK(Await([&] { return !controller.checking(); }));
        auto refresh = [&](bool probe = true) {
            controller.refresh(probe); CHECK(Await([&] { return !controller.checking(); }));
        };
        gui::ClassifyWindowsUsbDriver(diag, DN_STARTED, 0);
        for (int i = 0; i < 6; ++i) {
            const unsigned long status = i % 2 ? DN_STARTED : 0;
            gui::ClassifyWindowsUsbDriver(modem, status, 0);
            gui::ClassifyWindowsUsbDriver(wwan, status, CM_PROB_FAILED_INSTALL);
            CHECK(wwan.problem == 0); // Problem output is undefined without DN_HAS_PROBLEM.
            CHECK(modem.state == gui::DriverState::Bound && wwan.state == gui::DriverState::Bound);
            CHECK(!modem.canInstall() && !wwan.canInstall());
            outcome = {{diag, modem, wwan}, {}}; refresh(i % 2);
            CHECK(controller.devices().size() == 3 && controller.attentionCount() == 0);
            CHECK(controller.summaryLevel() == "neutral" && controller.summaryText().isEmpty());
            for (const auto &row : controller.devices()) {
                CHECK(!row.toMap()["attention"].toBool());
                CHECK(row.toMap()["stateText"].toString() == "Driver installed");
            }
        }
        for (auto device : {diag, modem, wwan}) {
            gui::ClassifyWindowsUsbDriver(device, DN_HAS_PROBLEM | DN_STARTED, CM_PROB_DISABLED);
            CHECK(device.state == gui::DriverState::DeviceError && device.problem == CM_PROB_DISABLED);
            gui::ClassifyWindowsUsbDriver(device, DN_HAS_PROBLEM, CM_PROB_NEED_RESTART);
            CHECK(device.state == gui::DriverState::RebootRequired);
            gui::ClassifyWindowsUsbDriver(device, DN_HAS_PROBLEM, CM_PROB_FAILED_INSTALL);
            CHECK(device.state == gui::DriverState::Missing);
            gui::ClassifyWindowsUsbDriver(device, DN_HAS_PROBLEM, 0);
            CHECK(device.state == gui::DriverState::DeviceError);
            device.service.clear();
            gui::ClassifyWindowsUsbDriver(device, 0, 0);
            CHECK(device.state == gui::DriverState::Missing);
        }
        gui::ClassifyWindowsUsbDriver(wwan, DN_HAS_PROBLEM, CM_PROB_DISABLED);
        outcome = {{diag, modem, wwan}, {}}; refresh();
        CHECK(controller.attentionCount() == 1 && controller.summaryText() == "Device error (22)");
        // A temporary query/probe failure must not stick to a healthy auxiliary interface.
        wwan.problem = 0; wwan.state = gui::DriverState::AccessFailed;
        outcome = {{diag, modem, wwan}, {}}; refresh();
        gui::ClassifyWindowsUsbDriver(wwan, 0, 0);
        outcome = {{diag, modem, wwan}, {}}; refresh(false);
        CHECK(controller.attentionCount() == 0 && controller.summaryText().isEmpty());

        gui::ClassifyWindowsUsbDriver(diag, 0, 0);
        CHECK(diag.state == gui::DriverState::DeviceError);
        diag.port.clear();
        gui::ClassifyWindowsUsbDriver(diag, DN_STARTED, 0);
        CHECK(diag.state == gui::DriverState::Incompatible);
        diag.service = "WinUSB";
        gui::ClassifyWindowsUsbDriver(diag, DN_STARTED, 0);
        CHECK(diag.state == gui::DriverState::Bound);
        auto edl = diag; edl.pid = 0x9008; edl.interfaceNumber = -1;
        CHECK(gui::RecognizeWindowsFlashingInterface(edl, {"USB\\Class_ff&SubClass_ff&Prot_ff"}));
        edl.service = "UnsupportedUSB";
        gui::ClassifyWindowsUsbDriver(edl, DN_STARTED, 0);
        CHECK(edl.state == gui::DriverState::Incompatible);
        auto rk = rockchip; rk.service = "UnsupportedUSB";
        gui::ClassifyWindowsUsbDriver(rk, DN_STARTED, 0);
        CHECK(rk.state == gui::DriverState::Incompatible);
    }
#endif

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
    // The official RK GUI can close successfully without installing anything.
    // Only a fresh, accessible Rockusb target can complete setup.
    {
        auto rk = allwinner;
        rk.vendor = 0; rk.vid = 0x2207; rk.pid = 0x350b;
        rk.instance = "USB\\VID_2207&PID_350B\\RK-BOARD";
        rk.hardwareId = "USB\\VID_2207&PID_350B";
        rk.service.clear(); rk.state = gui::DriverState::Missing;
        gui::DriverScan outcome{{rk}, {}};
        enum Scenario { NoAction, Cancelled, Failed, Disconnected, Inaccessible, Reboot, Installed } scenario = NoAction;
        int calls = 0;
        gui::DriverHooks rkHooks;
        rkHooks.verificationTimeoutMs = 0;
        rkHooks.scan = [&](bool) { return outcome; };
        rkHooks.installWithProgress = [&](const auto &, const auto &, const auto &) {
            CHECK(false); return gui::DriverInstallResult{}; // RK must never use libwdi.
        };
        rkHooks.driverOperation = [&](auto operation, const auto &target, const auto &, const auto &progress) {
            CHECK(operation == gui::DriverOperation::RockusbAssistant && target.instance == rk.instance);
            ++calls; progress(gui::DriverPhase::Installing);
            if (scenario == Cancelled) return gui::DriverInstallResult{false, true, {}};
            if (scenario == Failed) return gui::DriverInstallResult{false, false, "assistant failed"};
            if (scenario == Disconnected) outcome = {};
            if (scenario == Inaccessible || scenario == Reboot || scenario == Installed) {
                auto bound = rk; bound.service = "Rockusb";
                bound.state = scenario == Inaccessible ? gui::DriverState::AccessFailed :
                              scenario == Reboot ? gui::DriverState::RebootRequired : gui::DriverState::Ready;
                outcome = {{bound}, {}};
            }
            return gui::DriverInstallResult{true, false, {}};
        };
        gui::DriverController controller(rkHooks, true);
        auto refresh = [&] { controller.refresh(); CHECK(Await([&] { return !controller.checking(); })); };
        refresh();
        CHECK(controller.devices().size() == 1 && controller.devices().first().toMap()["canInstall"].toBool());
        for (auto next : {NoAction, Cancelled, Failed, Disconnected, Inaccessible, Reboot, Installed}) {
            scenario = next; outcome = {{rk}, {}};
            controller.forgetPending(); refresh();
            controller.prepareInstall(rk.instance);
            CHECK(controller.confirmationTitle() == "Install Rockchip drivers?");
            CHECK(controller.confirmationText().contains("close its window"));
            controller.confirmInstall(); controller.beginInstall();
            CHECK(Await([&] { return !controller.busy(); }));
            CHECK(controller.hasPending() == (next != Cancelled && next != Installed));
            CHECK((controller.message() == "Driver installed") == (next == Installed));
        }
        CHECK(calls == 7);
        for (const auto state : {gui::DriverState::Incompatible, gui::DriverState::Bound, gui::DriverState::Ready,
                                gui::DriverState::AccessFailed, gui::DriverState::DeviceError}) {
            auto changed = rk; changed.state = state;
            outcome = {{changed}, {}}; refresh();
            CHECK(!controller.devices().first().toMap()["canInstall"].toBool());
        }
        outcome = {}; refresh(); controller.prepareInstall(rk.instance);
        CHECK(!controller.canConfirm() && calls == 7);
#ifdef Q_OS_WIN
        rk.service = "Rockusb";
        gui::ClassifyWindowsUsbDriver(rk, DN_STARTED, 0); CHECK(rk.state == gui::DriverState::Bound);
        rk.service = "WinUSB";
        gui::ClassifyWindowsUsbDriver(rk, DN_STARTED, 0); CHECK(rk.state == gui::DriverState::Incompatible);
        rk.service = "libusbK";
        gui::ClassifyWindowsUsbDriver(rk, DN_STARTED, 0); CHECK(rk.state == gui::DriverState::Incompatible);
        CHECK(!gui::RockusbDriverPackageError(settingsDirectory.path()).isEmpty());
#endif
    }
    for (const int pid : {0x9008, 0x9092}) {
        auto qc = qualcomm;
        qc.vid = 0x05c6; qc.pid = pid; qc.vendor = 2; qc.verifiedInterface = true;
        qc.state = gui::DriverState::Missing;
        qc.hardwareId = QString("USB\\VID_05C6&PID_%1").arg(pid, 4, 16, QChar('0'));
        qc.interfaceNumber = pid == 0x9092 ? 0 : -1;
        if (pid == 0x9092) qc.hardwareId += "&MI_00";
        qc.instance = qc.hardwareId + "\\BOARD"; qc.location = "port-qc";
        gui::DriverScan outcome{{qc}, {}};
        bool installed = false, cancelOperation = false, failOperation = false, holdOperation = false;
        int calls = 0, confirmations = 0;
        gui::DriverHooks qcHooks;
        qcHooks.verificationTimeoutMs = 0;
        qcHooks.scan = [&](bool) { return outcome; };
        qcHooks.qualcommInstalled = [&] { return installed; };
        qcHooks.driverOperation = [&](auto operation, const auto &target, const auto &, const auto &progress) {
            ++calls;
            progress(gui::DriverPhase::Installing);
            if (cancelOperation) return gui::DriverInstallResult{false, true, {}};
            if (failOperation) return gui::DriverInstallResult{false, false, "exit code 7"};
            if (operation == gui::DriverOperation::QualcommUninstall) {
                CHECK(gui::QualcommDriverArguments(operation) == QStringList{"-u"});
                installed = false; outcome = {};
            } else {
                CHECK(operation == gui::DriverOperation::QualcommInstall && target.instance == qc.instance);
                CHECK(gui::QualcommDriverArguments(operation).isEmpty());
                installed = true;
                if (!holdOperation) {
                    auto bound = qc; bound.state = gui::DriverState::Bound;
                    bound.service = "QUSB_BULK"; bound.port = "COM7";
                    outcome = {{bound}, {}};
                }
            }
            return gui::DriverInstallResult{true, false, {}};
        };
        gui::DriverController controller(qcHooks, true);
        QObject::connect(&controller, &gui::DriverController::confirmationRequested, [&] { ++confirmations; });
        controller.setVendor(2); CHECK(Await([&] { return !controller.checking(); }));
        auto refresh = [&] { controller.refresh(); CHECK(Await([&] { return !controller.checking(); })); };
        auto run = [&] {
            controller.confirmInstall(); controller.confirmInstall(); controller.beginInstall();
            CHECK(Await([&] { return !controller.busy(); }));
            CHECK(Await([&] { return !controller.checking(); }));
        };
        CHECK(!controller.canUninstallQualcomm());
        for (const auto state : {gui::DriverState::Incompatible, gui::DriverState::Bound, gui::DriverState::Ready,
                gui::DriverState::AccessFailed, gui::DriverState::DeviceError, gui::DriverState::RebootRequired}) {
            auto d = qc; d.state = state; outcome = {{d}, {}}; refresh();
            controller.prepareInstall(qc.instance); CHECK(confirmations == 0 && calls == 0);
        }
        outcome = {}; refresh(); controller.prepareInstall(qc.instance); CHECK(confirmations == 0);
        outcome = {{qc}, "scan failed"}; refresh();
        controller.prepareInstall(qc.instance); CHECK(confirmations == 0);
        outcome = {{qc}, {}}; refresh(); controller.prepareInstall(qc.instance);
        CHECK(confirmations == 1 && controller.confirmationTitle() == "Install Qualcomm drivers?");
        // Unplugging after confirmation must not invoke the official tool.
        outcome = {}; run(); CHECK(calls == 0 && controller.message().contains("changed"));
        outcome = {{qc}, {}}; refresh(); controller.prepareInstall(qc.instance);
        cancelOperation = true; run(); CHECK(calls == 1 && !controller.hasPending());
        cancelOperation = false; failOperation = true;
        refresh(); controller.prepareInstall(qc.instance); run();
        CHECK(calls == 2 && controller.message().contains("exit code 7") && controller.hasPending());
        failOperation = false; holdOperation = true;
        refresh(); controller.prepareInstall(qc.instance); run();
        CHECK(calls == 3 && controller.message().contains("still missing"));
        holdOperation = false;
        refresh(); controller.prepareInstall(qc.instance); run();
        CHECK(calls == 4 && controller.message() == "Driver installed" && !controller.hasPending());
        CHECK(controller.canUninstallQualcomm());
        CHECK(controller.attentionCount() == 0);
        // Package removal is possible without a connected device.
        outcome = {}; refresh(); controller.prepareQualcommUninstall();
        CHECK(controller.canConfirm() && controller.confirmation().isEmpty());
        CHECK(controller.confirmationTitle() == "Uninstall Qualcomm drivers?");
        run(); CHECK(calls == 5 && controller.message() == "Qualcomm drivers uninstalled");
        CHECK(!controller.canUninstallQualcomm() && !controller.hasPending());
        controller.prepareInstall(qc.instance); CHECK(!controller.canConfirm());
    }
    return failures ? 1 : 0;
}
