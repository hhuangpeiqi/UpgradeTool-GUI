#include "platform/UsbContext.h"
#include <libusb.h>
#include <stdexcept>
namespace platform {
UsbContext::UsbContext() {
    int result=libusb_init(&context);
    if(result<0) throw std::runtime_error(libusb_error_name(result));
}
UsbContext::~UsbContext() { libusb_exit(context); }
}
