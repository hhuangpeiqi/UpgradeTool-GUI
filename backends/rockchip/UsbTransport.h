#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct libusb_context;
namespace platform { class FileLog; }
namespace core::rockchip {
// A snapshot owns its enumeration reference, never an open USB handle.
struct UsbDevice {
    uint16_t vid = 0, pid = 0, bcdUsb = 0;
    uint32_t location = 0;
    std::string path, instance, physicalLocation, serial;
    std::shared_ptr<void> native;
};
class UsbTransport {
public:
    virtual ~UsbTransport() = default;
    uint8_t bulkIn = 0, bulkOut = 0;
    virtual int Bulk(uint8_t endpoint, unsigned char *data, int size, int &transferred, unsigned timeout) = 0;
    virtual int Control(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                        unsigned char *data, uint16_t size, unsigned timeout) = 0;
    virtual int Reset() = 0;
    virtual std::string Serial() = 0;
};
std::vector<std::shared_ptr<const UsbDevice>> EnumerateUsb(libusb_context *, platform::FileLog * = nullptr);
std::unique_ptr<UsbTransport> OpenUsb(const UsbDevice &, bool massStorage, int &error, platform::FileLog * = nullptr);
std::string UsbError(int error);
#ifdef _WIN32
// Non-destructive open/configuration check, shared by the driver UI and task preflight.
int ProbeRockusb(const std::string &instance, const std::string &path = {});
#endif
}
