#include "gui/EdlDeviceSelection.h"
#include "gui/backend/AppController.h"
#include "tests/support/Checks.h"
#include "tests/support/qt/Await.h"
#include "tests/support/qt/TestDevices.h"
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <libusb.h>

int main(int argc, char **argv) {
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    qputenv("XDG_STATE_HOME", temporary.path().toUtf8());
    qputenv("ADB", temporary.filePath("missing-adb").toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QGuiApplication application(argc, argv);
    if (libusb_init(nullptr) < 0) return 2;
    int failures = 0;
    const QString location = "PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(2)";
    gui::EdlEndpoint expected;
    expected.vid = 0x05c6; expected.pid = 0x9008; expected.downloadInterface = true;
    expected.kind = gui::EdlEndpoint::Usb;
    expected.instance = "temporary-instance"; expected.location = location;
    QVariantMap com{{"vendor", 2}, {"index", 2}, {"key", "com"}, {"vid", 0x05c6}, {"pid", 0x9008},
        {"qualcommMode", QTQ_EDL}, {"transport", int(core::QualcommTransport::Qud)},
        {"devicePath", "\\\\.\\COM8"}, {"deviceInstance", "qdloader-instance"}, {"physicalLocation", location}};
    auto other = com; other["key"] = "other"; other["devicePath"] = "COM9";
    other["deviceInstance"] = "other-instance"; other["physicalLocation"] = location + "#USB(3)";
    auto denied = com; denied["accessError"] = LIBUSB_ERROR_NOT_SUPPORTED;
    auto duplicate = com; duplicate["key"] = "duplicate";
    auto rows = [](const QVariantList &input) { return SnapshotDevices(input); };
    CHECK(gui::SelectEdlDevice(expected, rows({other, com})) == 1);
    CHECK(gui::SelectEdlDevice(expected, rows({denied, other})) == -1);
    CHECK(gui::SelectEdlDevice(expected, rows({com, duplicate})) == -1);
    auto suffix = com; suffix["physicalLocation"] = location.toLower() + "#usbmi(0)";
    CHECK(gui::SelectEdlDevice(expected, rows({suffix})) == 0);
    auto wrongMode = com; wrongMode["qualcommMode"] = QTQ_DUMP;
    CHECK(gui::SelectEdlDevice(expected, rows({wrongMode})) == -1);
    auto noReference = com; noReference["reference"] = QVariant::fromValue(core::DeviceRef{});
    CHECK(gui::SelectEdlDevice(expected, rows({noReference})) == -1);
    // Matching native instances remains possible before a topology is available.
    auto exact = expected; exact.location.clear(); exact.instance = "QDLOADER-INSTANCE";
    CHECK(gui::SelectEdlDevice(exact, rows({com})) == 0);
    exact.instance.clear(); exact.path = "com8"; exact.serial = "board";
    auto serial = com; serial["serial"] = "BOARD";
    CHECK(gui::SelectEdlDevice(exact, rows({serial})) == 0);
    serial["serial"] = "replacement";
    CHECK(gui::SelectEdlDevice(exact, rows({serial})) == -1);
    auto conflict = expected; conflict.instance = "qdloader-instance";
    conflict.location += "#USB(9)";
    CHECK(gui::SelectEdlDevice(conflict, rows({com})) == -1);
#ifdef Q_OS_WIN
    auto usb = com; usb["key"] = "usb"; usb["transport"] = 0;
    CHECK(gui::SelectEdlDevice(expected, rows({usb, com})) == 1);
#endif

    for (int scenario = 0; scenario < 6; ++scenario) {
        gui::EdlHooks hooks;
        hooks.discover = [](const QString &, const gui::EdlCancel &, QString &) { return QVector<gui::EdlEndpoint>{}; };
        gui::AppController controller(nullptr, hooks, 100, nullptr, {}, false);
        CHECK(QObject::disconnect(&controller, SIGNAL(requestScan()), nullptr, nullptr));
        controller.setVendor(2);
        controller.ignorePermission();
        int successes = 0, recoveries = 0, scans = 0;
        QObject::connect(&controller, &gui::AppController::toastRequested, [&](const QString &message, bool error) {
            if (!error && message == gui::AppController::tr("Download device ready.")) {
                ++successes;
                CHECK(controller.hasFlashTarget());
                CHECK(!controller.switching() && !controller.tasks()->busy());
            }
        });
        QObject::connect(&controller, &gui::AppController::switchRecoveryRequested, [&] { ++recoveries; });
        QObject::connect(&controller, &gui::AppController::requestScan, [&] { ++scans; });
        auto update = [&](const QVariantList &devices) {
            CHECK(QMetaObject::invokeMethod(&controller, "onDevicesReady", Qt::DirectConnection,
                Q_ARG(QVariantList, SnapshotDevices(devices))));
        };
        QVariantMap diag{{"vendor", 2}, {"key", "diag"}, {"qualcommMode", QTQ_DIAG}};
        update({diag}); controller.setDeviceIndex(0);
        if (scenario == 5) {
            update({diag, other});
            controller.setBatchTargets({"diag", "other"}, true);
            CHECK(controller.batchMode());
        }
        controller.edl()->started();
        controller.edl()->finished(true, "9008 detected", expected);
        CHECK(controller.switching() && controller.tasks()->busy());
        CHECK(controller.tasks()->progressValue() == -1 && successes == 0);
        CHECK(controller.deviceIndex() == -1);
        const int previousScans = scans;
        controller.refresh(); CHECK(scans > previousScans); // Polling must work while locked.
        update({denied, other});
        CHECK(controller.deviceIndex() == -1 && !controller.hasFlashTarget());
        update({com, duplicate}); CHECK(controller.deviceIndex() == -1);
        if (scenario == 0) {
            controller.setBatchTargets({"diag", "other"}, true); // Locked: ignored.
            update({other, com});
            CHECK(successes == 1 && controller.deviceIndex() == 1);
            CHECK(controller.deviceModel()->referenceAt(1) == rows({com}).first().toMap()["reference"].value<core::DeviceRef>());
            update({com, other}); CHECK(controller.deviceIndex() == 0);
            controller.edl()->finished(false, "late failure", {});
            CHECK(recoveries == 0 && successes == 1);
        } else if (scenario == 1) {
            CHECK(Await([&] { return !controller.switching(); }));
            CHECK(recoveries == 1 && !controller.tasks()->busy());
            update({other}); CHECK(controller.deviceIndex() == -1);
            controller.edl()->finished(true, "late success", expected);
            update({other, com}); CHECK(controller.deviceIndex() == -1);
            CHECK(successes == 0 && recoveries == 1);
        } else if (scenario == 2) {
            controller.cancel();
            CHECK(!controller.switching() && !controller.tasks()->busy() && recoveries == 0);
            controller.edl()->finished(true, "late success", expected);
            update({other, com}); CHECK(controller.deviceIndex() == -1 && successes == 0);
        } else if (scenario == 3) {
            controller.requestQuit(); controller.confirmQuit(true);
            CHECK(controller.shuttingDown());
            controller.edl()->finished(true, "late success", expected);
            update({com}); CHECK(successes == 0);
        } else {
            controller.edl()->finished(false, "duplicate runner result", {});
            CHECK(controller.switching() && recoveries == 0);
            update({com}); CHECK(successes == 1);
            CHECK(!controller.batchMode());
        }
    }
    libusb_exit(nullptr);
    return failures ? 1 : 0;
}
