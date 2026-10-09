#include "core/FirmwareService.h"
#include "core/UpgradeEngine.h"
#include "RKImage.h"
#include "RKBoot.h"
#include "crc.h"
#include <cstdio>
#include <cstring>
#include <vector>
CRKLog *g_pLogObject = NULL;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static void Write32(unsigned char *p, uint32_t value) { for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (i * 8)); }
static bool Save(const char *path, const std::vector<unsigned char> &bytes) {
	FILE *file = std::fopen(path, "wb"); if (!file) return false;
	bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size(); return std::fclose(file) == 0 && ok;
}
int main() {
	const char *loaderPath = "firmware_test_loader.bin", *imagePath = "firmware_test_update.img";
	std::vector<unsigned char> boot(sizeof(STRUCT_RKBOOT_HEAD) + 4, 0);
	STRUCT_RKBOOT_HEAD bootHead = {};
	bootHead.uiTag = 0x544f4f42; bootHead.usSize = sizeof(bootHead); bootHead.dwVersion = 0x102;
	bootHead.emSupportChip = static_cast<ENUM_RKDEVICE_TYPE>(0x33333939); bootHead.stReleaseTime.usYear = 2026;
	bootHead.stReleaseTime.ucMonth = 10; bootHead.stReleaseTime.ucDay = 9;
	std::memcpy(boot.data(), &bootHead, sizeof(bootHead));
	Write32(boot.data() + boot.size() - 4, CRC_32(boot.data(), boot.size() - 4));
	CHECK(Save(loaderPath, boot));
	core::FirmwareInfo info;
	CHECK(core::FirmwareService::Inspect(loaderPath, info)); CHECK(info.valid && info.loaderVersion == "1.02" && info.chip == "RK3399");
	const uint32_t fwOffset = 512, fwSize = 0x110;
	std::vector<unsigned char> image(fwOffset + fwSize + 32, 0);
	STRUCT_RKIMAGE_HEAD imageHead = {};
	imageHead.uiTag = 0x57464b52; imageHead.usSize = sizeof(imageHead); imageHead.dwVersion = 0x01020300;
	imageHead.emSupportChip = bootHead.emSupportChip; imageHead.stReleaseTime = bootHead.stReleaseTime;
	imageHead.dwBootOffset = sizeof(imageHead); imageHead.dwBootSize = boot.size();
	imageHead.dwFWOffset = fwOffset; imageHead.dwFWSize = fwSize;
	std::memcpy(image.data(), &imageHead, sizeof(imageHead)); std::memcpy(image.data() + imageHead.dwBootOffset, boot.data(), boot.size());
	Write32(image.data() + fwOffset + 0x88, 1);
	unsigned char *entry = image.data() + fwOffset + 0x8c;
	std::memcpy(entry, "boot", 5); std::memcpy(entry + 0x20, "boot.img", 9);
	Write32(entry + 0x60, 0x100); Write32(entry + 0x64, 0x2000); Write32(entry + 0x6c, 16);
	Write32(image.data() + fwOffset + 0x100, SPARSE_HEADER_MAGIC);
	CHECK(Save(imagePath, image)); CHECK(core::FirmwareService::Inspect(imagePath, info));
	CHECK(info.valid && info.version == "1.2.03" && info.loaderVersion == "1.02");
	CHECK(info.entries.size() == 1);
	if (!info.entries.empty()) { CHECK(info.entries[0].partName == "boot" && info.entries[0].type == "sparse image"); CHECK(info.entries[0].offset == fwOffset + 0x100 && info.entries[0].lba == 0x2000 && info.entries[0].size == 16); }
	core::UpgradeEngine engine; engine.BeginTask(); core::OperationRequest request;
	request.operation = core::Operation::FirmwareInfo; request.file.path = imagePath;
	auto result = engine.Execute(request); CHECK(result.ok() && result.firmware.entries.size() == 1);
	// An entry outside the physical file must fail rather than produce partial metadata.
	Write32(entry + 0x6c, 0xffff); CHECK(Save(imagePath, image)); CHECK(!core::FirmwareService::Inspect(imagePath, info)); CHECK(!info.valid);
	// Reject truncated entry tables, including excessive counts.
	Write32(image.data() + fwOffset + 0x88, 513); CHECK(Save(imagePath, image)); CHECK(!core::FirmwareService::Inspect(imagePath, info));
	// Loader corruption must be reported via the business error code.
	boot[10] ^= 1; CHECK(Save(loaderPath, boot)); request.file.path = loaderPath;
	CHECK(engine.Execute(request).error == core::ErrorCode::InvalidFirmware);
	std::remove(loaderPath); std::remove(imagePath);
	std::printf("test_firmware: %s\n", failures ? "FAIL" : "PASS"); return failures ? 1 : 0;
}
