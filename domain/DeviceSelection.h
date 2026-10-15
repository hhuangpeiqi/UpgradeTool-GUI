#ifndef CORE_DEVICE_SELECTION_H
#define CORE_DEVICE_SELECTION_H
#include "domain/Operation.h"
namespace core {
// Returns a unique position, -1 if missing, -2 if ambiguous. Never falls back to an index.
inline int MatchDevice(const std::vector<DeviceRef> &devices, const DeviceRef &target)
{
	int found = -1;
	for (size_t i = 0; i < devices.size(); ++i) {
        if (!target.Matches(devices[i])) continue;
		if (found >= 0) return -2;
		found = static_cast<int>(i);
	}
	return found;
}
}
#endif
