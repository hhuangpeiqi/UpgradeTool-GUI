#pragma once
#include <cstdint>
#include <optional>
#include <span>

namespace core::rockchip::rockusb {
// User-mode ABI of the original DriverAssistant Rockusb driver.
inline constexpr uint32_t GetConfiguration = 0x220000;
inline constexpr uint32_t ResetDevice = 0x220004;
inline constexpr uint32_t ResetPipe = 0x220008;
constexpr uint32_t BootRequest(uint16_t index) {
    return index == 0x471 ? 0x8000a000 : index == 0x472 ? 0x8000a004 : 0;
}
struct Endpoints { uint8_t in = 0, out = 0; };
inline std::optional<Endpoints> ConfigurationEndpoints(std::span<const uint8_t> bytes) {
    if (bytes.size() < 9 || bytes[0] != 9 || bytes[1] != 2) return {};
    const size_t total = bytes[2] | (uint16_t(bytes[3]) << 8);
    if (total != bytes.size()) return {};
    Endpoints endpoints;
    bool firstInterface = true, selected = false;
    // rockusb.sys selects the first interface and PIPE00/01 select its first
    // IN/OUT pipes. Reject other layouts rather than opening the wrong pipes.
    for (size_t offset = 9; offset < total;) {
        if (offset + 2 > total) return {};
        const size_t length = bytes[offset];
        if (length < 2 || offset + length > total) return {};
        if (bytes[offset + 1] == 4) {
            if (length < 9) return {};
            selected = firstInterface;
            if (firstInterface && (bytes[offset + 3] || bytes[offset + 5] != 0xff ||
                                  bytes[offset + 6] != 6 || bytes[offset + 7] != 5)) return {};
            firstInterface = false;
        } else if (bytes[offset + 1] == 5 && selected) {
            if (length < 7 || (bytes[offset + 3] & 3) != 2) return {};
            const auto address = bytes[offset + 2];
            if (!(address & 0x0f)) return {};
            auto &pipe = address & 0x80 ? endpoints.in : endpoints.out;
            if (pipe) return {}; // The native ABI cannot disambiguate multiple pipes of one direction.
            pipe = address;
        }
        offset += length;
    }
    return endpoints.in && endpoints.out ? std::optional(endpoints) : std::nullopt;
}
}
