#include "platform/UsbLocation.h"
#include "platform/File.h"
#include "platform/WindowsUsb.h"
#include "platform/WindowsUsbLocation.h"
#include <cstdio>
#include <map>
#include <set>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#endif
namespace platform {
namespace {
std::string UpperUsbId(std::string id) {
    std::transform(id.begin(), id.end(), id.begin(),
                   [](unsigned char c) { return char(std::toupper(c)); });
    return id;
}
bool PhysicalUsbDevice(const std::string &id) {
    // Inspect the device portion only; the serial may itself contain "&MI_".
    const auto end = id.find('\\', 4);
    if (end == std::string::npos) return false;
    const auto device = id.substr(0, end);
    if (device.size() < 21 || device.rfind("USB\\VID_", 0) != 0 || device.substr(12, 5) != "&PID_")
        return false;
    for (const auto offset : {8, 17})
        for (int i = offset; i < offset + 4; ++i)
            if (!std::isxdigit(static_cast<unsigned char>(device[i]))) return false;
    return (device.size() == 21 || device[21] == '&') && device.find("&MI_") == std::string::npos;
}
}
std::vector<NativeUsbLocation> WindowsUsbLocations(const std::vector<WindowsUsbNode> &nodes) {
    std::map<std::string, const WindowsUsbNode *> byInstance;
    for (const auto &node : nodes) byInstance.emplace(UpperUsbId(node.instance), &node);
    std::vector<NativeUsbLocation> result;
    std::set<std::string> visited;
    for (const auto &node : nodes) {
        auto *physical = &node;
        for (size_t depth = 0; depth < nodes.size(); ++depth) {
            const auto id = UpperUsbId(physical->instance);
            if (PhysicalUsbDevice(id)) {
                if (!visited.insert(id).second || !physical->uniqueId) break;
                // UNIQUEID distinguishes device serials from Windows-generated
                // per-port instance IDs. Preserve the serial's original case.
                const auto serial = physical->instance.substr(physical->instance.find_last_of('\\') + 1);
                if (!serial.empty())
                    result.push_back({serial, NormalizeUsbLocation(physical->location)});
                break;
            }
            const auto parent = byInstance.find(UpperUsbId(physical->parent));
            if (parent == byInstance.end()) break;
            physical = parent->second;
        }
    }
    return result;
}
std::string UsbTopologyLocation(int bus, const std::vector<int> &ports) {
    if (ports.empty()) return {};
#ifdef __APPLE__
    if (ports.size() > 5 || bus < 0 || bus > 255) return {};
    uint32_t location = uint32_t(bus) << 24;
    for (size_t i = 0; i < ports.size(); ++i) {
        if (ports[i] < 1 || ports[i] > 15) return {};
        location |= uint32_t(ports[i]) << (20 - 4 * i);
    }
    char text[24]; std::snprintf(text, sizeof(text), "iokit:%08x", location);
    return text;
#else
    std::string result = "usb:" + std::to_string(bus) + '-';
    for (size_t i = 0; i < ports.size(); ++i) { if (i) result += '.'; result += std::to_string(ports[i]); }
    return result;
#endif
}
std::vector<NativeUsbLocation> NativeUsbLocations() {
    std::vector<NativeUsbLocation> result;
#ifdef _WIN32
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return result;
    std::vector<WindowsUsbNode> nodes;
    SP_DEVINFO_DATA info{}; info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        WindowsUsbNode node;
        node.instance = Utf8(std::filesystem::path(WindowsUsbInstance(set, info)));
        if (node.instance.empty()) continue;
        DEVINST parent;
        wchar_t parentId[MAX_DEVICE_ID_LEN] = {};
        if (CM_Get_Parent(&parent, info.DevInst, 0) == CR_SUCCESS &&
            CM_Get_Device_IDW(parent, parentId, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS)
            node.parent = Utf8(std::filesystem::path(parentId));
        DWORD capabilities = 0, type = 0;
        node.uniqueId = SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_CAPABILITIES, &type,
            reinterpret_cast<BYTE *>(&capabilities), sizeof(capabilities), nullptr) &&
            type == REG_DWORD && (capabilities & CM_DEVCAP_UNIQUEID);
        const auto locations = WindowsUsbProperty(set, info, SPDRP_LOCATION_PATHS);
        if (!locations.empty()) node.location = Utf8(std::filesystem::path(locations.front()));
        nodes.push_back(std::move(node));
    }
    SetupDiDestroyDeviceInfoList(set);
    result = WindowsUsbLocations(nodes);
#elif defined(__APPLE__)
    io_iterator_t iterator = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOUSBHostDevice"), &iterator) != KERN_SUCCESS) return result;
    io_registry_entry_t entry;
    while ((entry = IOIteratorNext(iterator))) {
        auto serial = IORegistryEntryCreateCFProperty(entry, CFSTR("USB Serial Number"), kCFAllocatorDefault, 0);
        auto location = IORegistryEntryCreateCFProperty(entry, CFSTR("locationID"), kCFAllocatorDefault, 0);
        char text[2048] = {}; int number = 0;
        if (serial && location && CFGetTypeID(serial) == CFStringGetTypeID() && CFGetTypeID(location) == CFNumberGetTypeID() &&
            CFStringGetCString(static_cast<CFStringRef>(serial), text, sizeof(text), kCFStringEncodingUTF8) &&
            CFNumberGetValue(static_cast<CFNumberRef>(location), kCFNumberIntType, &number) && text[0] && number) {
            char path[24]; std::snprintf(path, sizeof(path), "iokit:%08x", uint32_t(number));
            result.push_back({text, path});
        }
        if (serial) CFRelease(serial);
        if (location) CFRelease(location);
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
#endif
    return result;
}
}
