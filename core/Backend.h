#ifndef CORE_BACKEND_H
#define CORE_BACKEND_H
#include "core/UpgradeObserver.h"
#include <memory>
#include "core/qualcomm/DevicePolicy.h"
namespace core {
struct DeviceDescriptor {
    DeviceIdentity identity;
    unsigned vid = 0, pid = 0;
    int index = 0, usbType = 0;
    std::string mode;
    qtq_mode qualcommMode = QTQ_UNKNOWN;
    int accessError = 0;
};
class IDeviceBackend {
public:
    virtual ~IDeviceBackend() {}
    virtual Vendor vendor() const = 0;
    virtual std::vector<DeviceDescriptor> Scan() = 0;
    virtual bool Supports(Operation) const = 0;
    virtual OperationResult Execute(const OperationRequest &, const DeviceIdentity &,
                                    const Cancellation &, IUpgradeObserver *) = 0;
};
class UpgradeEngine;
class RockchipBackend : public IDeviceBackend {
public:
    explicit RockchipBackend(UpgradeEngine &engine) : engine(engine) {}
    Vendor vendor() const override { return Vendor::Rockchip; }
    std::vector<DeviceDescriptor> Scan() override;
    bool Supports(Operation) const override;
    OperationResult Execute(const OperationRequest &,const DeviceIdentity &,const Cancellation &,IUpgradeObserver *) override;
private:
    UpgradeEngine &engine;
};
class BackendRegistry {
public:
    void Add(IDeviceBackend &backend) { backends.push_back(&backend); }
    IDeviceBackend *Find(Vendor) const;
    bool Validate(const TaskJob &) const;
    OperationResult Execute(const OperationRequest &, const DeviceIdentity &, const Cancellation &, IUpgradeObserver *);
private:
    std::vector<IDeviceBackend *> backends;
};
}
#endif
