#include "platform/UsbLocation.h"
#include "platform/File.h"
#include "platform/WindowsUsb.h"
#include <cstdio>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#endif
namespace platform {
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
std::vector<NativeUsbLocation> NativeAdbLocations() {
    std::vector<NativeUsbLocation> result;
#ifdef _WIN32
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return result;
    SP_DEVINFO_DATA info{}; info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        bool adb = false;
        for (const auto &id : WindowsUsbProperty(set, info, SPDRP_COMPATIBLEIDS))
            adb |= id.find(L"SUBCLASS_42&PROT_01") != std::wstring::npos;
        if (!adb) continue;
        const auto serial = WindowsUsbSerial(info.DevInst);
        const auto locations = WindowsUsbProperty(set, info, SPDRP_LOCATION_PATHS);
        if (serial.empty() || locations.empty() || locations.front().empty()) continue;
        NativeUsbLocation entry{Utf8(std::filesystem::path(serial)), NormalizeUsbLocation(Utf8(std::filesystem::path(locations.front())))};
        if (std::find(result.begin(), result.end(), entry) == result.end()) result.push_back(std::move(entry));
    }
    SetupDiDestroyDeviceInfoList(set);
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
