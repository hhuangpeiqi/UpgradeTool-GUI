#ifndef ROCKCHIP_BACKEND_H
#define ROCKCHIP_BACKEND_H
#include "domain/Backend.h"
namespace platform { class LogService; }
namespace core {
class UpgradeEngine;
class RockchipRuntime;
class RockchipBackend : public IDeviceBackend {
public:
    explicit RockchipBackend(platform::LogService *log=nullptr);
    ~RockchipBackend() override;
    Vendor vendor() const override { return Vendor::Rockchip; }
    void Configure(const ConfigSnapshot &) override;
    std::vector<DeviceDescriptor> Scan() override;
    bool Supports(Operation) const override;
    FirmwareInfo InspectDirectory(const std::string &,const Cancellation &) override;
    OperationResult Execute(const OperationRequest &,const DeviceRef &,const Cancellation &,IUpgradeObserver *) override;
private:
    std::unique_ptr<RockchipRuntime> runtime;
};
}
#endif
