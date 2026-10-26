#ifndef ALLWINNER_BACKEND_H
#define ALLWINNER_BACKEND_H
#include "backends/allwinner/Transport.h"
#include "platform/UsbContext.h"
#include <map>
namespace core { namespace allwinner {
class AllwinnerBackend : public IDeviceBackend {
public:
    using Scanner = std::function<std::vector<DeviceDescriptor>()>;
    using Factory = std::function<std::unique_ptr<ITransport>()>;
    AllwinnerBackend();
    explicit AllwinnerBackend(Factory factory, Scanner scanner = {}) : factory(std::move(factory)), scanner(std::move(scanner)) {}
    ~AllwinnerBackend() override { ReleaseSessions(); }
    DeviceDetails Connect(const DeviceRef &, const Cancellation &,
        std::chrono::steady_clock::time_point deadline = (std::chrono::steady_clock::time_point::max)());
    bool HasSession(const DeviceRef &) const;
    void ReleaseSession(const DeviceRef &);
    void ReleaseSessions();
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
    void ReconcileSessions(const std::vector<DeviceDescriptor> &);
    std::shared_ptr<platform::UsbContext> usb;
    Factory factory;
    Scanner scanner;
    struct Session { DeviceIdentity identity; std::unique_ptr<ITransport> transport; };
    std::map<std::string, Session> sessions;
    ITransport &Acquire(const DeviceRef &, const Cancellation &, std::chrono::steady_clock::time_point);
};
} }
#endif
