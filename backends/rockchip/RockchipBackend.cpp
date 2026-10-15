#include "backends/DeviceIdentity.h"
#include "backends/rockchip/RockchipBackend.h"
#include "backends/rockchip/UpgradeEngine.h"
#include "platform/UsbContext.h"
#include "backends/rockchip/FirmwareService.h"
namespace core {
class RockchipRuntime {
public:
    platform::UsbContext usb;
    CRKScan scan;
    UpgradeEngine engine;
    explicit RockchipRuntime(platform::FileLog *log) : scan(30,20,usb.get()) {
        scan.SetVidPid(); engine.SetLogObject(log); engine.SetScan(&scan);
    }
};
RockchipBackend::RockchipBackend(platform::FileLog *log) : runtime(std::make_unique<RockchipRuntime>(log)) {}
RockchipBackend::~RockchipBackend() = default;

void RockchipBackend::Configure(const ConfigSnapshot &config) { runtime->engine.SetCancellation({}); runtime->engine.SetConfig(config); runtime->engine.BeginTask(); }
std::vector<DeviceDescriptor> RockchipBackend::Scan() {
    if(runtime->engine.GetScan()) runtime->engine.GetScan()->Search(RKUSB_MASKROM | RKUSB_LOADER);
    std::vector<DeviceDescriptor> result;
    for(const auto &d:runtime->engine.ScanDevices()) {
        DeviceIdentity identity; identity.location=d.uiLocationID; identity.serial=d.strSerial;
        identity.vid=d.uiVid; identity.pid=d.uiPid;
        auto row=Describe(identity);
        row.index=d.iIndex; row.usbType=d.iUsbType; row.mode=d.strMode;
        result.push_back(row);
    }
    return result;
}
FirmwareInfo RockchipBackend::InspectDirectory(const std::string &path,const Cancellation &cancelled) { return FirmwareService::InspectDirectory(path,cancelled); }
bool RockchipBackend::Supports(Operation op) const {
        switch(op) {
        case Operation::FirmwareUpgrade:
        case Operation::FirmwareExtract:
        case Operation::FirmwareInfo:
        case Operation::LoaderUpgrade:
        case Operation::DownloadBoot:
        case Operation::EraseFlash:
        case Operation::PartitionList:
        case Operation::DownloadImage:
        case Operation::CreateGpt:
        case Operation::SwitchDevice:
        case Operation::ReadSn:
        case Operation::WriteSn:
        case Operation::ReadComLog:
        case Operation::ReadCapability:
        case Operation::ReadFlashId:
        case Operation::ReadFlashInfo:
        case Operation::ReadChipInfo:
        case Operation::ReadOtpChip:
        case Operation::ReadSecureMode:
        case Operation::TestDevice:
        case Operation::ResetDevice:
        case Operation::ResetPipe:
        case Operation::ReadLba:
        case Operation::WriteLba:
        case Operation::EraseLba:
        case Operation::WriteSector:
        case Operation::EraseBlock:
        case Operation::RunSystem:
        case Operation::SwitchStorage:
            return true;
        default: return false;
        }
    }
OperationResult RockchipBackend::Execute(const OperationRequest &r,const DeviceRef &d,const Cancellation &c,IUpgradeObserver *o) {
    runtime->engine.SetObserver(o); runtime->engine.SetTarget(RequiresEnumeratedDevice(r)?DeviceAccess::Identity(d):DeviceIdentity{}); runtime->engine.SetCancellation(c); return runtime->engine.Execute(r);
}
}
