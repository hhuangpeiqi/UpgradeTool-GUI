#include "tests/support/Checks.h"
#include <libefex.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace {
int failures = 0;
using Bytes = std::vector<unsigned char>;
struct Exchange {
    int endpoint;
    Bytes bytes;
};
struct Script {
    std::vector<Exchange> exchanges;
    size_t next = 0;
    size_t failAt = size_t(-1);
};
// Wire offsets are intentionally independent of the protocol's C structs.
Bytes Request(unsigned char command, uint32_t length) {
    Bytes bytes(32);
    std::memcpy(bytes.data(), "AWUC", 4);
    bytes[15] = 0x0c;
    bytes[16] = command;
    for (unsigned i = 0; i < 4; ++i)
        bytes[8 + i] = bytes[18 + i] = static_cast<unsigned char>(length >> (8 * i));
    return bytes;
}
Bytes UsbStatus() {
    Bytes bytes(13);
    std::memcpy(bytes.data(), "AWUS", 4);
    return bytes;
}
Script Handshake(unsigned char mode) {
    Bytes verify(16);
    verify[0] = 1;
    Bytes device(32);
    std::memcpy(device.data(), "AWUSBFEX", 8);
    device[8] = 0x23;
    device[9] = 0x16;
    device[12] = 1;
    device[16] = mode;
    return {{{0x01, Request(0x12, 16)}, {0x01, verify}, {0x81, UsbStatus()},
             {0x01, Request(0x11, 32)}, {0x81, device}, {0x81, UsbStatus()},
             {0x01, Request(0x11, 8)}, {0x81, Bytes(8)}, {0x81, UsbStatus()}}};
}
sunxi_efex_ctx_t Context(Script &script) {
    sunxi_efex_ctx_t context{};
    context.hdl = &script;
    context.epout = 0x01;
    context.epin = 0x81;
    return context;
}
int Transfer(void *handle, int endpoint, char *data, ssize_t length, bool receive) {
    auto &script = *static_cast<Script *>(handle);
    const auto index = script.next++;
    CHECK(index < script.exchanges.size());
    if (index >= script.exchanges.size()) return EFEX_ERR_USB_TRANSFER;
    const auto &expected = script.exchanges[index];
    CHECK(endpoint == expected.endpoint);
    CHECK(length == static_cast<ssize_t>(expected.bytes.size()));
    if (length != static_cast<ssize_t>(expected.bytes.size())) return EFEX_ERR_USB_TRANSFER;
    if (index == script.failAt) return EFEX_ERR_USB_TRANSFER;
    if (receive)
        std::memcpy(data, expected.bytes.data(), expected.bytes.size());
    else
        CHECK(std::equal(expected.bytes.begin(), expected.bytes.end(),
                         reinterpret_cast<unsigned char *>(data)));
    return 0;
}
} // namespace

extern "C" int sunxi_usb_bulk_send(void *handle, int endpoint, const char *data, ssize_t length) {
    return Transfer(handle, endpoint, const_cast<char *>(data), length, false);
}
extern "C" int sunxi_usb_bulk_recv(void *handle, int endpoint, char *data, ssize_t length) {
    return Transfer(handle, endpoint, data, length, true);
}

int main() {
    for (const auto command : {AW_USB_READ, AW_USB_WRITE}) {
        for (const uint32_t length : {8u, 16u, 32u, 65536u}) {
            Script script{{{0x01, Request(static_cast<unsigned char>(command), length)}}};
            auto context = Context(script);
            CHECK(sunxi_send_usb_request(&context, command, length) == EFEX_ERR_SUCCESS);
            CHECK(script.next == script.exchanges.size());
        }
    }
    for (const unsigned char mode : {1, 2}) {
        auto script = Handshake(mode);
        auto context = Context(script);
        CHECK(sunxi_efex_init(&context) == EFEX_ERR_SUCCESS);
        CHECK(script.next == script.exchanges.size());
        CHECK(context.resp.mode == mode);
        CHECK(context.resp.id == 0x1623 && context.resp.firmware == 1);
    }
    for (size_t step = 0; step < 9; ++step) {
        auto script = Handshake(2);
        script.failAt = step;
        auto context = Context(script);
        CHECK(sunxi_efex_init(&context) == EFEX_ERR_USB_TRANSFER);
        CHECK(script.next == step + 1);
    }
    return failures ? 1 : 0;
}
