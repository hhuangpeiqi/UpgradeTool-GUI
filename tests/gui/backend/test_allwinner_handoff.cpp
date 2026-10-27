#include "gui/AdbRunner.h"
#include "gui/backend/AppController.h"
#include "tests/support/Checks.h"
#include "tests/support/qt/Await.h"
#include "tests/support/qt/TestDevices.h"
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <atomic>
#include <cstdio>
#include <libusb.h>

int main(int argc, char **argv) {
    // Fake ADB stops at device choice, so no switch command reaches hardware.
    if (argc > 1) {
        const auto command = QString::fromLocal8Bit(argv[1]);
        if (command == "version") std::puts("Android Debug Bridge version 1.0.41");
        else if (command == "devices") std::puts("List of devices attached\nboard device\nsecond device");
        else if (command != "start-server") return 42;
        return 0;
    }
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    qputenv("XDG_STATE_HOME", temporary.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QGuiApplication application(argc, argv);
    qputenv("ADB", application.applicationFilePath().toUtf8());
    if (libusb_init(nullptr) < 0) return 2;
    int failures = 0;
    {
        gui::DriverDevice board;
        board.vendor = 1; board.vid = 0x1f3a; board.pid = 0xefe8;
        board.instance = "test-allwinner"; board.hardwareId = "USB\\VID_1F3A&PID_EFE8";
        board.location = "PCIROOT(0)#PCI(0801)#USBROOT(0)#USB(2)";
        board.state = gui::DriverState::Missing; board.verifiedInterface = true;
        std::atomic<bool> driverReady{false}, holdInstall{true}, installerEntered{false};
        gui::DriverHooks hooks;
        hooks.scan = [&](bool) {
            auto device = board;
            if (driverReady) { device.state = gui::DriverState::Ready; device.service = "usbUDisc"; }
            return gui::DriverScan{{device}, {}};
        };
        hooks.install = [&](const auto &, const auto &) {
            installerEntered = true;
            while (holdInstall) QThread::msleep(1);
            driverReady = true;
            return gui::DriverInstallResult{true, false, {}};
        };
        gui::AppController controller(nullptr, gui::EdlHooks{}, 10, nullptr, std::move(hooks), true);
        // All backend snapshots below are fixtures; driver maintenance still
        // exercises the real worker barrier and DriverController lifecycle.
        QObject::disconnect(&controller, SIGNAL(requestScan()), nullptr, SLOT(scanDevices()));
        controller.setVendor(1);
        controller.ignorePermission();
        auto *adb = controller.findChild<gui::AdbRunner *>();
        CHECK(adb);
        if (!adb) return 1;
        CHECK(Await([&] { return controller.adbReady() && !controller.drivers()->checking(); }));
        int choices = 0;
        platform::UsbLocation switchLocation{board.location.toStdString(), 0, {}};
        QObject::connect(&controller, &gui::AppController::adbDeviceChoiceRequested, [&] { ++choices; });
        auto update = [&](const QVariantList &rows) {
            CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
                Q_ARG(QVariantList, SnapshotDevices(rows))));
        };
        QVariantMap target{{"vendor", 1}, {"key", "before-install"}, {"mode", "FEL/FES"},
            {"physicalLocation", board.location + "#USBMI(0)"}, {"accessError", 5}};
        auto other = target;
        other["key"] = "unrelated"; other["physicalLocation"] = board.location + "#USB(3)";
        other["accessError"] = 0;
        auto ready = target;
        ready["key"] = "after-install"; ready["physicalLocation"] = board.location.toLower();
        ready["accessError"] = 0;
        auto begin = [&] {
            CHECK(Await([&] { return controller.adbReady(); }));
            const int previous = choices;
            controller.switchToFel();
            CHECK(Await([&] { return choices > previous; }));
            CHECK(controller.switching());
            // Arm then cancel only the synthetic worker monitor. AppController
            // must retain its target even though no FEL handshake succeeded.
            adb->felMonitorRequested({42, switchLocation, {}, 1000});
            adb->felMonitorCancelled(42);
        };
        auto complete = [&](gui::ModeSwitchFailure failure, bool cancelled = false) {
            adb->finished(false, "test FEL access diagnostic", cancelled, failure);
            adb->CancelSwitch();
            CHECK(!controller.switching() && !controller.interactionLocked());
        };
        auto recover = [&] {
            begin();
            update({other, target});
            complete(gui::ModeSwitchFailure::DeviceAccess);
            CHECK(controller.deviceIndex() == -1 && !controller.hasFlashTarget());
        };

        recover();
        update({other}); // Temporary disappearance must not select another board.
        CHECK(controller.deviceIndex() == -1);
        update({other, target});
        CHECK(controller.deviceIndex() == -1);
        CHECK(Await([&] { return !controller.drivers()->checking(); }));
        controller.drivers()->prepareInstall(board.instance);
        CHECK(controller.drivers()->canConfirm());
        controller.drivers()->confirmInstall();
        const bool entered = Await([&] { return installerEntered.load(); });
        CHECK(entered && controller.interactionLocked());
        update({other, ready}); // Fresh ready snapshot arrives before installation ends.
        CHECK(controller.deviceIndex() == -1);
        holdInstall = false;
        CHECK(Await([&] { return !controller.interactionLocked(); }));
        CHECK(controller.deviceIndex() == 1 && controller.hasFlashTarget());
        CHECK(!controller.batchMode());
        update({other}); // A completed recovery must not jump to another target.
        CHECK(controller.deviceIndex() == -1);

        recover();
        auto duplicate = ready;
        duplicate["key"] = "second-interface";
        update({other, ready, duplicate});
        CHECK(controller.deviceIndex() == -1); // Ambiguous physical port.
        controller.setDeviceIndex(0);
        update({other, ready});
        CHECK(controller.deviceIndex() == 0); // Explicit choice wins.

        recover();
        controller.setBatchTargets({other["key"].toString(), target["key"].toString()}, true);
        CHECK(controller.batchMode());
        auto batchReady = ready;
        batchReady["key"] = target["key"];
        update({other, batchReady});
        CHECK(controller.batchMode()); // Explicit batch choice dismisses recovery too.

        recover();
        controller.setDeviceIndex(-1); // Explicit deselection also dismisses recovery.
        update({other, ready});
        CHECK(controller.deviceIndex() == 0);

        recover();
        controller.setVendor(0);
        controller.setVendor(1);
        update({other, ready});
        CHECK(controller.deviceIndex() == 0);

        for (bool cancelled : {false, true}) {
            recover();
            begin(); // Starting a new switch discards the previous recovery target.
            complete(gui::ModeSwitchFailure::Generic, cancelled);
            update({other, ready});
            CHECK(controller.deviceIndex() == 0);
        }

        // Linux/macOS snapshots identify ports by bus and hub chain instead of
        // Windows physical location paths. USB addresses may change on install.
        switchLocation = {"", 1, {2, 3}};
        target["physicalLocation"] = ""; target["bus"] = 1;
        target["ports"] = QVariantList{2, 3}; target["usbAddress"] = 7;
        ready = target; ready["key"] = "new-usb-address";
        ready["accessError"] = 0; ready["usbAddress"] = 9;
        other["physicalLocation"] = ""; other["bus"] = 1;
        other["ports"] = QVariantList{2, 4};
        recover();
        update({other, ready});
        CHECK(controller.deviceIndex() == 1 && controller.hasFlashTarget());
        switchLocation = {platform::UsbTopologyLocation(1, {2, 3}), 0, {}};
        recover();
        update({other, ready});
        CHECK(controller.deviceIndex() == 1 && controller.hasFlashTarget());
    }
    libusb_exit(nullptr);
    return failures ? 1 : 0;
}
