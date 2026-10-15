#include "BackendTests.h"
#include <libusb.h>
#include <cstdio>

int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);
    if (libusb_init(nullptr) < 0) return 2;
    int failures = 0;
    failures += TestDeviceStatus(application);
    failures += TestAllwinnerSwitching(application);
    failures += TestTasks(application);
    failures += TestAllwinnerState(application);
    failures += TestQualcommDevices(application);
    failures += TestLanguage(application);
    libusb_exit(nullptr);
    std::printf("test_gui_backend: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
