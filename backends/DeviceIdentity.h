#ifndef BACKENDS_DEVICE_IDENTITY_H
#define BACKENDS_DEVICE_IDENTITY_H
#include "domain/Backend.h"
#include <sstream>
#include <stdexcept>
namespace core {
struct DeviceIdentity {
    operator DeviceRef() const;
    // Qualcomm interface incarnation. Zero IDs retain unspecified-target semantics.
    unsigned int vid = 0, pid = 0, usbAddress = 0;
    int interfaceNumber = -1, alternate = 0, inEndpoint = 0, outEndpoint = 0;
	std::string serial;
	uint32_t location = 0;
	Vendor vendor = Vendor::Rockchip;
	std::string devicePath, deviceInstance;
	QualcommTransport transport = QualcommTransport::Auto;
	uint8_t bus = 0;
	std::vector<uint8_t> ports;
};

inline int MatchDevice(const std::vector<DeviceIdentity> &devices, const DeviceIdentity &target)
{
	int found = -1;
	for (size_t i = 0; i < devices.size(); ++i) {
		if (devices[i].vendor != target.vendor) continue;
        if (target.vendor == Vendor::Qualcomm) {
            if (!target.deviceInstance.empty() && devices[i].deviceInstance != target.deviceInstance) continue;
            if (target.vid && (devices[i].vid != target.vid || devices[i].pid != target.pid)) continue;
            if (target.usbAddress && (devices[i].usbAddress != target.usbAddress || devices[i].bus != target.bus)) continue;
            if (target.interfaceNumber >= 0 && (devices[i].interfaceNumber != target.interfaceNumber ||
                devices[i].alternate != target.alternate || devices[i].inEndpoint != target.inEndpoint ||
                devices[i].outEndpoint != target.outEndpoint)) continue;
        }
        // RK mode transitions replace the native interface path; location remains authoritative.
        if (target.vendor != Vendor::Rockchip && !target.devicePath.empty() && devices[i].devicePath != target.devicePath) continue;
        if (target.transport != QualcommTransport::Auto && devices[i].transport != target.transport) continue;
        if (!target.ports.empty() && (devices[i].bus != target.bus || devices[i].ports != target.ports)) continue;
		if (target.location && devices[i].location != target.location) continue;
		if (!target.serial.empty() && devices[i].serial != target.serial) continue;
		if (found >= 0) return -2;
		found = static_cast<int>(i);
	}
	return found;
}
class IdentityToken final : public DeviceToken {
public:
    explicit IdentityToken(DeviceIdentity value) : identity(std::move(value)) {}
    bool Matches(const DeviceToken &other) const override {
        const auto *candidate=dynamic_cast<const IdentityToken *>(&other);
        return candidate && MatchDevice(std::vector<DeviceIdentity>{candidate->identity},identity)==0;
    }
    const DeviceIdentity identity;
};
class DeviceAccess {
public:
    static DeviceRef Reference(const DeviceIdentity &identity) {
        DeviceRef ref; ref.m_vendor=identity.vendor; ref.m_serial=identity.serial;
        std::ostringstream key;
        if(identity.vendor==Vendor::Rockchip) key<<std::hex<<identity.location<<":"<<identity.serial;
        else {
            if(identity.vendor==Vendor::Qualcomm) key<<"qualcomm:"<<static_cast<int>(identity.transport)<<":"<<identity.devicePath<<":";
            else key<<"allwinner:";
            key<<int(identity.bus)<<"-";
            for(size_t i=0;i<identity.ports.size();++i) { if(i) key<<"."; key<<int(identity.ports[i]); }
            if(identity.vendor==Vendor::Qualcomm) key<<":if"<<identity.interfaceNumber<<":alt"<<identity.alternate;
        }
        ref.m_key=key.str(); ref.m_token=std::make_shared<const IdentityToken>(identity); return ref;
    }
    static const DeviceIdentity &Identity(const DeviceRef &ref) {
        const auto *token=dynamic_cast<const IdentityToken *>(ref.m_token.get());
        if(!token) throw std::invalid_argument("Invalid device reference");
        return token->identity;
    }
};
inline DeviceIdentity::operator DeviceRef() const { return DeviceAccess::Reference(*this); }
inline DeviceDescriptor Describe(const DeviceIdentity &identity) {
    DeviceDescriptor d; d.reference=identity;
    d.vid=identity.vid; d.pid=identity.pid; d.serial=identity.serial;
    d.location=identity.location; d.devicePath=identity.devicePath; d.deviceInstance=identity.deviceInstance;
    d.transport=static_cast<int>(identity.transport); d.bus=identity.bus; d.ports=identity.ports;
    d.usbAddress=identity.usbAddress; d.interfaceNumber=identity.interfaceNumber; d.alternate=identity.alternate;
    d.inEndpoint=identity.inEndpoint; d.outEndpoint=identity.outEndpoint;
    return d;
}
}
#endif
