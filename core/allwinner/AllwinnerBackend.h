#ifndef ALLWINNER_BACKEND_H
#define ALLWINNER_BACKEND_H
#include "core/allwinner/Transport.h"
namespace core { namespace allwinner {
class AllwinnerBackend : public IDeviceBackend {
public:
    using Factory = std::function<std::unique_ptr<ITransport>()>;
    explicit AllwinnerBackend(Factory factory = UsbTransport) : factory(factory) {}
    Vendor vendor() const override { return Vendor::Allwinner; }
    std::vector<DeviceDescriptor> Scan() override { return ScanUsb(); }
    bool Supports(Operation op) const override { return op>=Operation::AllwinnerFlash && op<=Operation::AllwinnerReboot; }
    OperationResult Execute(const OperationRequest &,const DeviceIdentity &,const Cancellation &,IUpgradeObserver *) override;
private:
    Factory factory;
};
} }
#endif
