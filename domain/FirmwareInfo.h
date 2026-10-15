#ifndef DOMAIN_FIRMWARE_INFO_H
#define DOMAIN_FIRMWARE_INFO_H
#include <cstdint>
#include <string>
#include <vector>
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
}
#endif
