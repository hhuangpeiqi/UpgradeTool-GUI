#pragma once
#include "platform/UsbLocation.h"

namespace platform {
// A snapshot keeps topology resolution independent of SetupAPI and testable
// without opening a USB device or requiring a Windows host.
struct WindowsUsbNode {
    std::string instance, parent, location;
    bool uniqueId = false;
};
std::vector<NativeUsbLocation> WindowsUsbLocations(const std::vector<WindowsUsbNode> &nodes);
}
