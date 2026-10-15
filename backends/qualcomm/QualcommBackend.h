#ifndef CORE_QUALCOMM_BACKEND_H
#define CORE_QUALCOMM_BACKEND_H
#include "domain/Backend.h"
namespace core { namespace qualcomm {
class QualcommBackend : public IDeviceBackend {
public:
    Vendor vendor() const override { return Vendor::Qualcomm; }
    std::vector<DeviceDescriptor> Scan() override;
    bool Supports(Operation) const override;
    OperationResult Execute(const OperationRequest &, const DeviceRef &,
                            const Cancellation &, IUpgradeObserver *) override;
};
}}
#endif
