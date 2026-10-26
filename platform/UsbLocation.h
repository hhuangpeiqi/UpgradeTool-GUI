#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace platform {
struct NativeUsbLocation {
    std::string serial, location;
    bool operator==(const NativeUsbLocation &) const = default;
};
std::vector<NativeUsbLocation> NativeAdbLocations();
std::string UsbTopologyLocation(int bus, const std::vector<int> &ports);
inline std::string NormalizeUsbLocation(std::string location) {
    // Windows interface suffixes identify siblings, not distinct physical ports.
    if (location.find("#") != std::string::npos) {
        std::transform(location.begin(), location.end(), location.begin(),
                       [](unsigned char c) { return char(std::toupper(c)); });
        const auto suffix = location.find("#USBMI(");
        if (suffix != std::string::npos) location.resize(suffix);
    }
    return location;
}
inline bool IsAdbInterfaceCompatibleId(std::string id) {
    std::transform(id.begin(), id.end(), id.begin(),
                   [](unsigned char c) { return char(std::toupper(c)); });
    return id.find("SUBCLASS_42&PROT_01") != std::string::npos;
}
struct UsbLocation {
    std::string native;
    int bus = 0;
    std::vector<int> ports;
    bool known() const { return !native.empty() || !ports.empty(); }
    bool matches(const UsbLocation &other) const {
        if (!native.empty() && !other.native.empty()) {
            const auto a = NormalizeUsbLocation(native), b = NormalizeUsbLocation(other.native);
            if (a == b) return true;
            // Different enumeration APIs may name the same port differently.
            const bool windows = a.find('#') != std::string::npos && b.find('#') != std::string::npos;
            if (windows || a.substr(0, a.find(':')) == b.substr(0, b.find(':'))) return false;
        }
        return !ports.empty() && !other.ports.empty() && bus == other.bus && ports == other.ports;
    }
};
}
