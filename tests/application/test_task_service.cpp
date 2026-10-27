#include "application/TaskService.h"
#include "backends/DeviceIdentity.h"
#include <cstdio>
static int failures=0;
#define CHECK(c) do { if(!(c)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); ++failures; } } while(0)
class Observer : public core::IUpgradeObserver {
public:
    int results=0;
    core::OperationResult last;
    void OnOutput(core::LogLevel,const std::string &) override {}
    void OnProgress(core::ProgressPrompt,long long,long long,core::CallStep) override {}
    void OnTransferProgress(core::TransferStage,long long,long long) override {}
    void OnUpgradePrompt(core::UpgradePrompt,unsigned) override {}
    int OnSelectStorage(const std::vector<std::string> &,int) override { return 0; }
    void OnResult(const core::OperationResult &result) override { ++results; last=result; }
};
class Backend : public core::IDeviceBackend {
public:
    int executions=0;
    bool crash=false;
    std::vector<core::DeviceDescriptor> devices;
    core::Vendor vendor() const override { return core::Vendor::Rockchip; }
    std::vector<core::DeviceDescriptor> Scan() override { return devices; }
    bool Supports(core::Operation op) const override { return op==core::Operation::TestDevice || op==core::Operation::FirmwareInfo; }
    core::OperationResult Execute(const core::OperationRequest &request,const core::DeviceRef &,const core::Cancellation &,core::IUpgradeObserver *) override {
        ++executions; if(crash) throw std::runtime_error("Fake transport failure");
        core::OperationResult result; result.operation=request.operation(); result.status=core::OperationStatus::Succeeded; return result;
    }
};
int main() {
    core::BackendRegistry registry; Backend backend; registry.Add(backend); core::TaskService service(registry); Observer observer;
    core::TaskJob job; job.steps={core::OperationRequest(core::FirmwareInfoRequest{})};
    CHECK(service.Run(job,&observer).succeeded==1); CHECK(observer.results==1 && backend.executions==1);
    job.steps={core::OperationRequest(core::TestDeviceRequest{})};
    core::DeviceIdentity original; original.location=2; original.serial="same";
    job.targets={original}; original.location=9; // The selected snapshot is immutable.
    backend.devices={core::Describe(original)};
    CHECK(service.Run(job,&observer).status==core::OperationStatus::Failed); CHECK(observer.last.error==core::ErrorCode::DeviceMissing); CHECK(backend.executions==1);
    core::DeviceIdentity expected; expected.location=2; expected.serial="same";
    backend.devices={core::Describe(original),core::Describe(expected)};
    CHECK(service.Run(job,&observer).succeeded==1);
    backend.devices.push_back(core::Describe(expected)); CHECK(service.Run(job,&observer).status==core::OperationStatus::Failed); CHECK(observer.last.error==core::ErrorCode::DeviceAmbiguous);
    backend.devices={core::Describe(expected)}; backend.crash=true;
    int before=observer.results; CHECK(service.Run(job,&observer).failed==1); CHECK(observer.results==before+1); CHECK(observer.last.diagnostic=="Fake transport failure");
    job.cancelled->request_stop(); before=backend.executions; CHECK(service.Run(job,&observer).status==core::OperationStatus::Cancelled); CHECK(backend.executions==before);
    core::OperationRequest qualcomm(core::QualcommFlashRequest{}); CHECK(qualcomm.vendor()==core::Vendor::Qualcomm);
    try { qualcomm.allwinner(); CHECK(false); } catch(const std::logic_error &) {}
    return failures?1:0;
}
