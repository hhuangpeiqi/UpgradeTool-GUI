#include "application/BackendRegistry.h"
namespace core {
IDeviceBackend *BackendRegistry::Find(Vendor vendor) const { for(auto *b:backends) if(b->vendor()==vendor) return b; return nullptr; }
bool BackendRegistry::Validate(const TaskJob &job) const {
    if(job.steps.empty()) return false;
    auto v=job.steps.front().vendor(); auto *backend=Find(v); if(!backend) return false;
    bool local=!RequiresEnumeratedDevice(job.steps.front());
    for(const auto &r:job.steps) if(r.vendor()!=v||(!RequiresEnumeratedDevice(r))!=local||!backend->Supports(r.operation())) return false;
    for(const auto &d:job.targets) if(d.vendor()!=v) return false;
    if(!local&&job.targets.empty()) return false;
    return !job.rebootAfter || backend->Supports(Operation::ResetDevice);
}
OperationResult BackendRegistry::Execute(const OperationRequest &r,const DeviceRef &d,const Cancellation &c,IUpgradeObserver *o) {
    auto *b=Find(r.vendor());
    if(b&&b->Supports(r.operation())&&(!RequiresEnumeratedDevice(r)||r.vendor()==d.vendor())) return b->Execute(r,d,c,o);
    OperationResult result; result.operation=r.operation(); result.error=ErrorCode::UnsupportedDevice; result.diagnostic="Unsupported backend operation";
    return result;
}
}
