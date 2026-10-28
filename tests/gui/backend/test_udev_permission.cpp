#include "gui/backend/AppController.h"
#include "gui/backend/ConfigStore.h"
#include "application/BuildFeatures.h"
#include <QGuiApplication>
#include <libusb.h>
#include <cstdio>

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    if (libusb_init(nullptr) < 0) return 2;
    int failures = 0;
    auto check = [&](bool ok) { if (!ok) ++failures; };
    {
        gui::AppController controller(nullptr);
        const QString command = controller.config()->udevCommand();
#ifdef Q_OS_LINUX
        check(command.contains("99-rk-rockusb-all.rules") == bool(UPGRADE_TOOL_ROCKCHIP));
        check(command.contains("99-allwinner.rules") == bool(UPGRADE_TOOL_ALLWINNER));
        check(command.contains("99-qualcomm.rules") == bool(UPGRADE_TOOL_QUALCOMM));
        check(command.count("sudo cp ") == 1);
        check(command.count("sudo udevadm control --reload-rules") == 1);
#else
        check(command.isEmpty());
#endif
        controller.ignorePermission();
        int changes = 0;
        QObject::connect(&controller, &gui::AppController::permissionChanged,
                         [&] { ++changes; });
        for (const auto &vendor : controller.availableVendors()) {
            controller.setVendor(vendor.toInt());
            check(!controller.permissionBlocked());
            check(controller.config()->udevCommand() == command);
        }
        controller.refresh();
        controller.retryPermission();
        // Installation callbacks from Settings (including late completion after
        // dismissal) must not reopen or re-evaluate the startup prompt.
        emit controller.config()->udevInstallFinished(false, "cancelled");
        emit controller.config()->udevInstallFinished(true, "installed");
        check(!controller.permissionBlocked());
        check(changes == 0);
    }
    libusb_exit(nullptr);
    std::printf("test_udev_permission: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
