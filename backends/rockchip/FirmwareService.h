#ifndef CORE_FIRMWARE_SERVICE_H
#define CORE_FIRMWARE_SERVICE_H
#include <cstdint>
#include <string>
#include <vector>
#include "domain/FirmwareInfo.h"
#include "domain/Operation.h"
class CRKImage;
namespace core {
class FirmwareService {
public:
	static FirmwareInfo InspectDirectory(const std::string &path,const Cancellation &cancelled);
    static std::string ChipName(uint32_t tag);
	static bool Inspect(const std::string &path, FirmwareInfo &info, int entryIndex = -1);
	static bool InspectImage(CRKImage &image, const std::string &path, FirmwareInfo &info);
};
}
#endif
