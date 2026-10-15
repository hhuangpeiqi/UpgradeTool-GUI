#ifndef APPLICATION_BACKEND_REGISTRY_H
#define APPLICATION_BACKEND_REGISTRY_H
#include "domain/Backend.h"
namespace core {
class BackendRegistry {
public:
    void Add(IDeviceBackend &backend) { backends.push_back(&backend); }
    IDeviceBackend *Find(Vendor) const;
    bool Validate(const TaskJob &) const;
    OperationResult Execute(const OperationRequest &, const DeviceRef &, const Cancellation &, IUpgradeObserver *);
private:
    std::vector<IDeviceBackend *> backends;
};
}
#endif
