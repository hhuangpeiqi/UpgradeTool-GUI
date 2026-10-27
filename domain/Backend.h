#ifndef CORE_BACKEND_H
#define CORE_BACKEND_H
#include "domain/UpgradeObserver.h"
#include <memory>
namespace core {
enum class DeviceMode { Unknown, Diag, Adb, Download, CrashDump };
struct DeviceDescriptor {
    DeviceRef reference;
    std::string serial,devicePath,deviceInstance,physicalLocation;
    uint32_t location=0;
    int transport=-1, interfaceNumber=-1,alternate=0,inEndpoint=0,outEndpoint=0;
    unsigned usbAddress=0;
    uint8_t bus=0;
    std::vector<uint8_t> ports;
    unsigned vid = 0, pid = 0;
    int index = 0, usbType = 0;
    std::string mode;
    DeviceMode modeCode = DeviceMode::Unknown;
    int accessError = 0;
};
class IDeviceBackend {
public:
    virtual ~IDeviceBackend() {}
    virtual Vendor vendor() const = 0;
    virtual std::vector<DeviceDescriptor> Scan() = 0;
    virtual bool Supports(Operation) const = 0;
    virtual FirmwareInfo InspectDirectory(const std::string &,const Cancellation &) { return {}; }
    virtual OperationResult Execute(const OperationRequest &, const DeviceRef &,
                                    const Cancellation &, IUpgradeObserver *) = 0;
};

}
#endif
