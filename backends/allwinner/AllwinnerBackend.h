#ifndef ALLWINNER_BACKEND_H
#define ALLWINNER_BACKEND_H
#include "backends/allwinner/Transport.h"
#include "platform/UsbContext.h"
namespace core { namespace allwinner {
class AllwinnerBackend : public IDeviceBackend {
public:
    using Factory = std::function<std::unique_ptr<ITransport>()>;
    AllwinnerBackend();
    explicit AllwinnerBackend(Factory factory) : factory(std::move(factory)) {}
    Vendor vendor() const override { return Vendor::Allwinner; }
    std::vector<DeviceDescriptor> Scan() override;
    bool Supports(Operation op) const override {
        switch(op) {
        case Operation::AllwinnerFlash:
        case Operation::AllwinnerRaw:
        case Operation::AllwinnerConvert:
        case Operation::AllwinnerInspect:
        case Operation::AllwinnerExtract:
        case Operation::AllwinnerDeviceInfo:
        case Operation::AllwinnerReboot:
            return true;
        default: return false;
        }
    }
    OperationResult Execute(const OperationRequest &,const DeviceRef &,const Cancellation &,IUpgradeObserver *) override;
private:
    std::shared_ptr<platform::UsbContext> usb;
    Factory factory;
};
} }
#endif
