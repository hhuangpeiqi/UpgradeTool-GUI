#ifndef PLATFORM_USB_CONTEXT_H
#define PLATFORM_USB_CONTEXT_H
struct libusb_context;
namespace platform {
class UsbContext {
public:
    UsbContext();
    ~UsbContext();
    UsbContext(const UsbContext &) = delete;
    UsbContext &operator=(const UsbContext &) = delete;
    libusb_context *get() const { return context; }
private:
    libusb_context *context=nullptr;
};
}
#endif
