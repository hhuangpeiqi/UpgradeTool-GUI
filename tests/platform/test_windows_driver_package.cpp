#include "tests/support/Checks.h"
#include "gui/WindowsUsbDriver.h"
#include "application/AllwinnerDriverPackage.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <windows.h>
#include <cfgmgr32.h>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    QTemporaryDir directory;
    CHECK(gui::AllwinnerDriverPackageError(directory.path()).contains("missing"));
    // Runtime verification must reject modified signed payloads before launch.
    for (const auto &[relative, hash] : gui::allwinnerPackageFiles) {
        CHECK(QByteArray(hash).size() == 64);
        QFile file(QDir(directory.path()).filePath(QString::fromUtf8(relative)));
        CHECK(file.open(QIODevice::WriteOnly)); file.write("modified driver");
    }
    CHECK(gui::AllwinnerDriverPackageError(directory.path()).contains("SHA256"));
    gui::DriverDevice device;
    device.vid = 0x1f3a; device.pid = 0xefe8;
    CHECK(gui::RecognizeWindowsFlashingInterface(device, {"USB\\Class_ff&SubClass_ff&Prot_ff"}));
    device.service = "usbUDisc";
    gui::ClassifyWindowsUsbDriver(device, DN_STARTED, 0);
    CHECK(device.state == gui::DriverState::Bound);
    for (const auto &service : {"WinUSB", "libusbK", "libusb0", "OtherUSB"}) {
        device.service = service;
        gui::ClassifyWindowsUsbDriver(device, DN_STARTED, 0);
        CHECK(device.state == gui::DriverState::Incompatible);
    }
    device.service = "usbUDisc";
    gui::ClassifyWindowsUsbDriver(device, DN_HAS_PROBLEM, CM_PROB_NEED_RESTART);
    CHECK(device.state == gui::DriverState::RebootRequired);
    gui::ClassifyWindowsUsbDriver(device, DN_HAS_PROBLEM, CM_PROB_UNSIGNED_DRIVER);
    CHECK(device.state == gui::DriverState::DeviceError && device.problem == CM_PROB_UNSIGNED_DRIVER);
    return failures ? 1 : 0;
}
