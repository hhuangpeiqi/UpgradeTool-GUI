#ifndef CORE_FIRMWARE_SERVICE_H
#define CORE_FIRMWARE_SERVICE_H
#include <cstdint>
#include <string>
#include <vector>
class CRKImage;
namespace core {
struct FirmwareEntry {
	std::string partName, fileName, type;
	uint64_t offset = 0;
	uint64_t size = 0;
	uint32_t lba = 0, storage = 0;
	bool hasPart = false, imageLike = false, isLoader = false;
};
struct FirmwareInfo {
	bool valid = false;
	std::string chip, version, loaderVersion, buildTime, sign;
	uint32_t storage = 0;
	std::vector<FirmwareEntry> entries;
};
class FirmwareService {
public:
	static std::string ChipName(uint32_t tag);
	static bool Inspect(const std::string &path, FirmwareInfo &info, int entryIndex = -1);
	static bool InspectImage(CRKImage &image, const std::string &path, FirmwareInfo &info);
};
}
#endif
