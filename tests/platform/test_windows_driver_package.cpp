#include "tests/support/Checks.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <libwdi.h>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    VS_FIXEDFILEINFO version = {};
    CHECK(wdi_is_driver_supported(WDI_WINUSB, &version));
    CHECK(version.dwSignature == 0xfeef04bd);
    CHECK(!wdi_is_driver_supported(WDI_LIBUSB0, nullptr));
    CHECK(!wdi_is_driver_supported(WDI_LIBUSBK, nullptr));
    QTemporaryDir directory;
    wdi_device_info device = {};
    char description[] = "Firmware download interface";
    device.vid = 0x2207; device.pid = 0x350b; device.desc = description;
    wdi_options_prepare_driver prepare = {};
    prepare.driver_type = WDI_WINUSB;
    // Prepare files only; never sign, trust certificates, bind, or install a driver.
    prepare.disable_cat = TRUE; prepare.disable_signing = TRUE;
    const auto path = directory.path().toUtf8();
    CHECK(wdi_prepare_driver(&device, path.constData(), "test_winusb.inf", &prepare) == WDI_SUCCESS);
    QFile inf(directory.filePath("test_winusb.inf"));
    CHECK(inf.open(QIODevice::ReadOnly));
    const auto bytes = inf.readAll();
    const auto text = QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData() + 2), (bytes.size() - 2) / 2);
    CHECK(text.contains("USB\\VID_2207&PID_350B"));
    CHECK(text.contains("Needs = WINUSB.NT.Services"));
    CHECK(text.contains("Needs = WINUSB.NT.HW"));
    CHECK(text.contains("DeviceInterfaceGUIDs"));
    CHECK(text.contains("DeviceName = \"USB Download Device\""));
    CHECK(!text.contains("CoInstaller"));
    CHECK(!text.contains("#DEVICE_"));
    CHECK(QFile::exists(directory.filePath("installer_x64.exe")));
    QFile installer(directory.filePath("installer_x64.exe"));
    CHECK(installer.open(QIODevice::ReadOnly));
    const auto installerBytes = installer.readAll();
    CHECK(installerBytes.contains("upgrade-install-status.json"));
    CHECK(installerBytes.contains("{\"rebootRequired\":%s,"));
    CHECK(installerBytes.contains("\"windowsInstallMs\":%llu"));
    CHECK(installerBytes.contains("\"reenumerationMs\":%llu"));
    return failures ? 1 : 0;
}
