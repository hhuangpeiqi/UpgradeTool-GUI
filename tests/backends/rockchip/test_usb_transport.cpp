#include "backends/rockchip/legacy/RKComm.h"
#include "backends/rockchip/legacy/RKScan.h"
#include "backends/rockchip/RockusbProtocol.h"
#include "tests/support/Checks.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace {
struct ControlCall { uint8_t type, request; uint16_t value, index; std::vector<unsigned char> data; unsigned timeout; };
std::vector<ControlCall> controls;
int controlResult = std::numeric_limits<int>::max();
bool shortBulk = false;
int bulkCalls = 0;
std::vector<std::shared_ptr<const core::rockchip::UsbDevice>> enumerated;
CBW lastCommand{};
class FakeTransport final : public core::rockchip::UsbTransport {
public:
    FakeTransport() { bulkIn = 0x81; bulkOut = 0x02; }
    int Bulk(uint8_t endpoint, unsigned char *data, int size, int &done, unsigned) override {
        ++bulkCalls; done = shortBulk ? size - 1 : size;
        if (endpoint == bulkOut && size == sizeof(CBW)) std::memcpy(&lastCommand, data, size);
        if (endpoint == bulkIn && size == sizeof(CSW)) {
            CSW response{}; response.dwCSWSignature = CSW_SIGN; response.dwCSWTag = lastCommand.dwCBWTag;
            std::memcpy(data, &response, sizeof(response));
        }
        return 0;
    }
    int Control(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                unsigned char *data, uint16_t size, unsigned timeout) override {
        controls.push_back({type, request, value, index, {}, timeout});
        if (size) controls.back().data.assign(data, data + size);
        if (controlResult != std::numeric_limits<int>::max()) return controlResult;
        return size == 1 ? 0 : size; // BootROM's pending packet completes with zero bytes.
    }
    int Reset() override { return 0; }
    std::string Serial() override { return {}; }
};
}
// Link the actual RK protocol implementation against a deterministic transport.
namespace core::rockchip {
std::unique_ptr<UsbTransport> OpenUsb(const UsbDevice &, bool, int &error, platform::FileLog *) {
    error = 0; return std::make_unique<FakeTransport>();
}
std::string UsbError(int error) { return std::to_string(error); }
std::vector<std::shared_ptr<const UsbDevice>> EnumerateUsb(libusb_context *, platform::FileLog *) { return enumerated; }
}
int main() {
    int failures = 0;
    using namespace core::rockchip::rockusb;
    CHECK(BootRequest(0x471) == 0x8000a000 && BootRequest(0x472) == 0x8000a004 && !BootRequest(0x473));
    std::vector<uint8_t> config{9,2,32,0,1,1,0,0x80,50, 9,4,0,0,2,0xff,6,5,0,
                                7,5,0x81,2,0,2,0, 7,5,0x02,2,0,2,0};
    auto endpoints = ConfigurationEndpoints(config);
    CHECK(endpoints && endpoints->in == 0x81 && endpoints->out == 0x02);
    for (const auto index : {0, 1, 2, 9, 14, 15, 16, 18, 20, 21, 27}) {
        auto malformed = config; malformed[index] = 0;
        CHECK(!ConfigurationEndpoints(malformed));
    }
    auto alternate = config; alternate[12] = 1;
    CHECK(!ConfigurationEndpoints(alternate));
    auto wrongClass = config; wrongClass[14] = 8;
    CHECK(!ConfigurationEndpoints(wrongClass));
    auto duplicateDirection = config; duplicateDirection[27] = 0x82;
    CHECK(!ConfigurationEndpoints(duplicateDirection));
    for (size_t size = 0; size < config.size(); ++size)
        CHECK(!ConfigurationEndpoints(std::span(config).first(size)));
    STRUCT_RKDEVICE_DESC device{};
    device.usbDevice = std::make_shared<core::rockchip::UsbDevice>(); device.emUsbType = RKUSB_MASKROM;
    bool opened = false;
    CRKUsbComm usb(device, nullptr, opened); CHECK(opened);
    const std::string text = "123456789";
    std::vector<BYTE> payload(text.begin(), text.end());
    CHECK(usb.RKU_DeviceRequest(0x471, payload.data(), DWORD(payload.size())) == ERR_SUCCESS);
    CHECK(controls.size() == 1);
    CHECK(controls.front().data == std::vector<BYTE>({'1','2','3','4','5','6','7','8','9',0x29,0xb1}));
    for (const size_t length : {4093, 4094, 4095, 4096, 8190}) {
        controls.clear(); payload.assign(length, 0x5a);
        CHECK(usb.RKU_DeviceRequest(0x472, payload.data(), DWORD(length)) == ERR_SUCCESS);
        const size_t wireSize = length + (length % 4096 == 4095 ? 1 : 0) + 2;
        std::vector<BYTE> wire;
        const bool pending = length % 4096 == 4094;
        CHECK(controls.size() == (wireSize + 4095) / 4096 + size_t(pending));
        for (size_t i = 0; i < controls.size(); ++i) {
            const auto &call = controls[i];
            CHECK(call.type == 0x40 && call.request == 0x0c && call.value == 0 && call.index == 0x472 && call.timeout == 5000);
            CHECK(call.data.size() <= 4096);
            if (!pending || i + 1 < controls.size()) wire.insert(wire.end(), call.data.begin(), call.data.end());
            else CHECK(call.data == std::vector<BYTE>{0});
        }
        CHECK(wire.size() == wireSize && std::equal(payload.begin(), payload.end(), wire.begin()));
        if (length % 4096 == 4095) CHECK(wire[length] == 0);
    }
    for (const int result : {-121, 0, 5}) {
        controls.clear(); controlResult = result; payload.assign(8192, 0);
        CHECK(usb.RKU_DeviceRequest(0x471, payload.data(), DWORD(payload.size())) == ERR_REQUEST_FAIL);
        CHECK(controls.size() == 1); // Never retry partially/possibly transmitted Boot data.
    }
    controlResult = std::numeric_limits<int>::max(); controls.clear();
    CHECK(usb.RKU_DeviceRequest(0x999, payload.data(), DWORD(payload.size())) == ERR_REQUEST_NOT_SUPPORT);
    CHECK(controls.empty());
    CHECK(usb.RKU_ResetPipe(2) == ERR_SUCCESS);
    CHECK(controls.size() == 2 && controls[0].index == 0x81 && controls[1].index == 0x02);
    CHECK(controls[0].type == 2 && controls[0].request == 1 && controls[0].data.empty());
    device.emUsbType = RKUSB_LOADER;
    CHECK(usb.Reset_Usb_Config(device));
    CHECK(usb.RKU_DeviceRequest(0x471, payload.data(), DWORD(payload.size())) == ERR_DEVICE_NOT_SUPPORT);
    CHECK(usb.RKU_TestDeviceReady() == ERR_SUCCESS);
    shortBulk = true; bulkCalls = 0;
    CHECK(usb.RKU_TestDeviceReady() == ERR_DEVICE_WRITE_FAILED);
    CHECK(bulkCalls == 1);
#ifdef _WIN32
    auto unrelated = std::make_shared<core::rockchip::UsbDevice>();
    unrelated->vid = 0x2207; unrelated->pid = 0x350b; unrelated->bcdUsb = 0x201; unrelated->location = 43;
    auto expected = std::make_shared<core::rockchip::UsbDevice>(*unrelated); expected->location = 42;
    enumerated = {unrelated, expected};
    CRKScan scan(1, 1); scan.SetVidPid();
    device.uiLocationID = 42;
    CHECK(scan.Wait(device, RKUSB_LOADER));
    CHECK(device.uiLocationID == 42 && device.usbDevice == expected);
    // The returned snapshot remains valid after both enumeration and scan release their references.
    enumerated.clear(); scan.Search(RKUSB_LOADER);
    CHECK(device.usbDevice && device.usbDevice->location == 42);
#endif
    return failures ? 1 : 0;
}
