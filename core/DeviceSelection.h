#ifndef CORE_DEVICE_SELECTION_H
#define CORE_DEVICE_SELECTION_H
#include "core/Operation.h"
namespace core {
// Returns a unique position, -1 if missing, -2 if ambiguous. Never falls back to an index.
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
        if (!target.devicePath.empty() && devices[i].devicePath != target.devicePath) continue;
        if (target.transport != QualcommTransport::Auto && devices[i].transport != target.transport) continue;
        if (!target.ports.empty() && (devices[i].bus != target.bus || devices[i].ports != target.ports)) continue;
		if (target.location && devices[i].location != target.location) continue;
		if (!target.serial.empty() && devices[i].serial != target.serial) continue;
		if (found >= 0) return -2;
		found = static_cast<int>(i);
	}
	return found;
}
}
#endif
