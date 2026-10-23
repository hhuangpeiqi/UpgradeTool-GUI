#include "UsbTransport.h"
#include "platform/FileLog.h"
#include <libusb.h>

namespace core::rockchip {
namespace {
class LibusbTransport final : public UsbTransport {
public:
    libusb_device_handle *handle = nullptr;
    int interfaceNumber = -1;
    ~LibusbTransport() override {
        if (handle) {
            if (interfaceNumber >= 0) libusb_release_interface(handle, interfaceNumber);
            libusb_close(handle);
        }
    }
    int Bulk(uint8_t endpoint, unsigned char *data, int size, int &done, unsigned timeout) override {
        return libusb_bulk_transfer(handle, endpoint, data, size, &done, timeout);
    }
    int Control(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                unsigned char *data, uint16_t size, unsigned timeout) override {
        return libusb_control_transfer(handle, type, request, value, index, data, size, timeout);
    }
    int Reset() override { return libusb_reset_device(handle); }
    std::string Serial() override {
        libusb_device_descriptor descriptor{};
        unsigned char text[256]{};
        if (libusb_get_device_descriptor(libusb_get_device(handle), &descriptor) || !descriptor.iSerialNumber) return {};
        int size = libusb_get_string_descriptor_ascii(handle, descriptor.iSerialNumber, text, sizeof(text));
        return size > 0 ? std::string(reinterpret_cast<char *>(text), size) : std::string{};
    }
};
}
std::vector<std::shared_ptr<const UsbDevice>> EnumerateUsb(libusb_context *context, platform::FileLog *log) {
    std::vector<std::shared_ptr<const UsbDevice>> result;
    libusb_device **list = nullptr;
    const auto count = libusb_get_device_list(context, &list);
    if (count < 0) {
        if (log) log->Record("USB enumeration failed: %s", libusb_error_name(int(count)));
        return result;
    }
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor descriptor{};
        if (libusb_get_device_descriptor(list[i], &descriptor)) continue;
        auto device = std::make_shared<UsbDevice>();
        device->vid = descriptor.idVendor; device->pid = descriptor.idProduct; device->bcdUsb = descriptor.bcdUSB;
        device->location = libusb_get_bus_number(list[i]) & 0xf;
        unsigned char ports[7];
        const int depth = libusb_get_port_numbers(list[i], ports, sizeof(ports));
        for (int j = 0; j < depth; ++j) device->location = (device->location << 4) + (ports[j] & 0xf);
        device->native = {libusb_ref_device(list[i]), [](void *p) { libusb_unref_device(static_cast<libusb_device *>(p)); }};
        result.push_back(std::move(device));
    }
    libusb_free_device_list(list, 1);
    return result;
}
std::unique_ptr<UsbTransport> OpenUsb(const UsbDevice &device, bool massStorage, int &error, platform::FileLog *log) {
    auto usb = std::make_unique<LibusbTransport>();
    error = libusb_open(static_cast<libusb_device *>(device.native.get()), &usb->handle);
    if (error) return {};
    libusb_config_descriptor *config = nullptr;
    error = libusb_get_active_config_descriptor(static_cast<libusb_device *>(device.native.get()), &config);
    if (error) return {};
    for (int i = 0; i < config->bNumInterfaces && usb->interfaceNumber < 0; ++i) {
        const auto &iface = config->interface[i];
        for (int j = 0; j < iface.num_altsetting; ++j) {
            const auto &alt = iface.altsetting[j];
            if (massStorage ? (alt.bInterfaceClass != 8 || alt.bInterfaceSubClass != 6 || alt.bInterfaceProtocol != 0x50)
                            : (alt.bInterfaceClass != 0xff || alt.bInterfaceSubClass != 6 || alt.bInterfaceProtocol != 5)) continue;
            uint8_t in = 0, out = 0;
            for (int k = 0; k < alt.bNumEndpoints; ++k) {
                const auto &ep = alt.endpoint[k];
                if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) continue;
                if (ep.bEndpointAddress & 0x80) in = ep.bEndpointAddress; else out = ep.bEndpointAddress;
            }
            if (in && out) {
                error = libusb_claim_interface(usb->handle, alt.bInterfaceNumber);
                if (!error) { usb->interfaceNumber = alt.bInterfaceNumber; usb->bulkIn = in; usb->bulkOut = out; }
                break;
            }
        }
    }
    libusb_free_config_descriptor(config);
    if (usb->interfaceNumber < 0) { if (!error) error = LIBUSB_ERROR_NOT_FOUND; return {}; }
    if (log) log->Record("USB opened: transport=libusb vid=%04x pid=%04x interface=%d bulk_in=%02x bulk_out=%02x",
                         device.vid, device.pid, usb->interfaceNumber, usb->bulkIn, usb->bulkOut);
    return usb;
}
std::string UsbError(int error) { return libusb_error_name(error); }
}
