#include "backends/allwinner/UsbWire.h"
#include <libusb.h>
#include <stdexcept>

namespace core::allwinner {
namespace {
DeviceIdentity Identity(libusb_device *dev) {
    DeviceIdentity d; d.vendor = Vendor::Allwinner; d.bus = libusb_get_bus_number(dev);
    d.vid = 0x1f3a; d.pid = 0xefe8;
    uint8_t ports[16]; const int n = libusb_get_port_numbers(dev, ports, sizeof(ports));
    if (n > 0) d.ports.assign(ports, ports + n);
    return d;
}
struct TransferResult { bool finished = false; libusb_transfer_status status = LIBUSB_TRANSFER_ERROR; int count = 0; };
void LIBUSB_CALL Completed(libusb_transfer *transfer) {
    auto &result = *static_cast<TransferResult *>(transfer->user_data);
    result.status = transfer->status; result.count = transfer->actual_length; result.finished = true;
}
class LibusbWire final : public UsbWire {
    std::shared_ptr<platform::UsbContext> usb;
    libusb_device_handle *handle = nullptr;
    int interfaceNumber = -1;
    bool detached = false;
public:
    explicit LibusbWire(std::shared_ptr<platform::UsbContext> context) : usb(std::move(context)) {}
    ~LibusbWire() override { Close(); }
    void Close() override {
        if (handle) {
            if (interfaceNumber >= 0) libusb_release_interface(handle, interfaceNumber);
            if (detached) libusb_attach_kernel_driver(handle, interfaceNumber);
            libusb_close(handle);
        }
        handle = nullptr; interfaceNumber = -1; detached = false;
    }
    void Open(const DeviceIdentity &target, int &in, int &out) override {
        if (target.vendor != Vendor::Allwinner || target.ports.empty()) throw std::runtime_error("Missing full USB device identity");
        libusb_device **list = nullptr;
        const ssize_t n = libusb_get_device_list(usb->get(), &list);
        if (n < 0) throw std::runtime_error(libusb_error_name(int(n)));
        int found = 0; libusb_device *selected = nullptr;
        for (ssize_t i = 0; i < n; ++i) {
            libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(list[i], &desc) || desc.idVendor != 0x1f3a || desc.idProduct != 0xefe8) continue;
            const auto d = Identity(list[i]);
            if (d.bus == target.bus && d.ports == target.ports) { ++found; selected = list[i]; }
        }
        int rc = found == 1 ? libusb_open(selected, &handle) : 0;
        libusb_config_descriptor *config = nullptr;
        if (found == 1 && !rc && !libusb_get_active_config_descriptor(selected, &config)) {
            for (unsigned i = 0; i < config->bNumInterfaces && interfaceNumber < 0; ++i) {
                if (!config->interface[i].num_altsetting) continue;
                const auto &alt = config->interface[i].altsetting[0]; int epin = 0, epout = 0;
                for (unsigned j = 0; j < alt.bNumEndpoints; ++j) {
                    const auto &ep = alt.endpoint[j];
                    if ((ep.bmAttributes & 3) != LIBUSB_TRANSFER_TYPE_BULK) continue;
                    if (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN) epin = ep.bEndpointAddress; else epout = ep.bEndpointAddress;
                }
                if (epin && epout) { interfaceNumber = alt.bInterfaceNumber; in = epin; out = epout; }
            }
            libusb_free_config_descriptor(config);
        }
        libusb_free_device_list(list, 1);
        if (found != 1 || rc) throw std::runtime_error(found == 0 ? "Target device disconnected" : found > 1 ? "Ambiguous device identity" : libusb_error_name(rc));
        if (interfaceNumber < 0) throw std::runtime_error("No Allwinner USB bulk interface");
        if (libusb_kernel_driver_active(handle, interfaceNumber) == 1) {
            rc = libusb_detach_kernel_driver(handle, interfaceNumber);
            if (rc) throw std::runtime_error(libusb_error_name(rc));
            detached = true;
        }
        rc = libusb_claim_interface(handle, interfaceNumber);
        if (rc) throw std::runtime_error(libusb_error_name(rc));
    }
    int Transfer(int ep, char *data, int size) override {
        auto *transfer = libusb_alloc_transfer(0);
        if (!transfer) { error = "Cannot allocate USB transfer"; return -1; }
        TransferResult result;
        libusb_fill_bulk_transfer(transfer, handle, uint8_t(ep), reinterpret_cast<unsigned char *>(data), size, Completed, &result, 60000);
        const int rc = libusb_submit_transfer(transfer);
        if (rc) { error = libusb_error_name(rc); libusb_free_transfer(transfer); return -1; }
        bool cancelled = false;
        while (!result.finished) {
            if (!cancelled && Stopped()) { libusb_cancel_transfer(transfer); cancelled = true; }
            timeval timeout = {0, 50000}; libusb_handle_events_timeout(usb->get(), &timeout);
        }
        libusb_free_transfer(transfer);
        if (result.status != LIBUSB_TRANSFER_COMPLETED) {
            error = "USB transfer status=" + std::to_string(result.status); return -1;
        }
        return result.count;
    }
};
}
std::unique_ptr<UsbWire> CreateUsbWire(std::shared_ptr<platform::UsbContext> context) { return std::make_unique<LibusbWire>(std::move(context)); }
std::vector<DeviceDescriptor> ScanUsb(libusb_context *context) {
    libusb_device **list = nullptr;
    const ssize_t n = libusb_get_device_list(context, &list);
    if (n < 0) throw std::runtime_error(libusb_error_name(int(n)));
    std::vector<DeviceDescriptor> result;
    for (ssize_t i = 0; i < n; ++i) {
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) || desc.idVendor != 0x1f3a || desc.idProduct != 0xefe8) continue;
        auto d = Describe(Identity(list[i])); d.index = int(result.size() + 1); d.mode = "FEL/FES";
        if (!d.ports.empty()) result.push_back(d);
    }
    libusb_free_device_list(list, 1); return result;
}
}
