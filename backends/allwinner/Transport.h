#include "backends/DeviceIdentity.h"
#ifndef ALLWINNER_TRANSPORT_H
#define ALLWINNER_TRANSPORT_H
#include "domain/Backend.h"
#include <chrono>
#include "platform/UsbContext.h"
#include "backends/allwinner/Firmware.h"
namespace core { namespace allwinner {
struct DeviceDetails { uint32_t chip=0, firmware=0; uint16_t mode=0; };
struct VerifyResult { uint32_t flag=0, checksum=0; };
class ITransport {
public:
    virtual ~ITransport() {}
    virtual void Open(const DeviceIdentity &,const Cancellation &) = 0;
    // Rebind a retained connection to the current operation without reopening it.
    virtual void SetIoContext(const Cancellation &, std::chrono::steady_clock::time_point =
        (std::chrono::steady_clock::time_point::max)()) = 0;
    virtual void Close() = 0;
    virtual DeviceDetails Details() const = 0;
    virtual void FelWrite(uint32_t,const Bytes &) = 0;
    virtual Bytes FelRead(uint32_t,size_t) = 0;
    virtual void FelExec(uint32_t) = 0;
    virtual uint32_t QueryStorage() = 0;
    virtual uint32_t QuerySecure() = 0;
    virtual uint32_t QuerySize() = 0;
    virtual void FlashAccess(bool) = 0;
    virtual void Download(uint32_t,uint32_t,const Bytes &) = 0;
    virtual VerifyResult Verify(uint32_t,uint64_t,bool status=false) = 0;
    virtual void Finish(PostAction) = 0;
};
std::unique_ptr<ITransport> UsbTransport(std::shared_ptr<platform::UsbContext> context);
std::vector<DeviceDescriptor> ScanUsb(libusb_context *context);
std::string ModeName(uint16_t);
std::string StorageName(uint32_t);
} }
#endif
