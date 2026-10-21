#include "tests/support/TestFiles.h"
#include "backends/rockchip/FirmwareService.h"
#include "backends/rockchip/UpgradeEngine.h"
#include "backends/rockchip/RockchipBackend.h"
#include "RKImage.h"
#include "RKBoot.h"
#include "crc.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static void Write32(unsigned char *p, uint32_t value) { for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (i * 8)); }
static bool Save(const char *path, const std::vector<unsigned char> &bytes) {
	std::ofstream file(platform::Path(path), std::ios::binary);
	file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	file.close(); return bool(file);
}
int main() {
	TemporaryDirectory temporary;
	const auto loaderStorage = temporary.filePath("firmware_test_loader.bin");
	const auto imageStorage = temporary.filePath("firmware_test_update.img");
	const char *loaderPath = loaderStorage.c_str(), *imagePath = imageStorage.c_str();
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
	request=core::OperationRequest(core::Operation::FirmwareInfo); request.file().path = imagePath;
	auto result = engine.Execute(request); CHECK(result.ok() && result.firmware.entries.size() == 1);
	// Firmware inspections bypass Configure and reuse the same backend across selections.
	core::RockchipBackend backend;
	auto cancellation = std::make_shared<std::stop_source>();
	CHECK(backend.Execute(request, {}, cancellation, nullptr).ok());
	for (int i = 0; i < 3; ++i) {
		cancellation->request_stop();
		CHECK(backend.Execute(request, {}, cancellation, nullptr).status == core::OperationStatus::Cancelled);
		cancellation = std::make_shared<std::stop_source>();
		result = backend.Execute(request, {}, cancellation, nullptr);
		CHECK(result.ok() && result.firmware.valid && result.firmware.entries.size() == 1);
	}
	auto alreadyCancelled = std::make_shared<std::stop_source>();
	alreadyCancelled->request_stop();
	CHECK(backend.Execute(request, {}, alreadyCancelled, nullptr).status == core::OperationStatus::Cancelled);
	cancellation = std::make_shared<std::stop_source>();
	CHECK(backend.Execute(request, {}, cancellation, nullptr).ok());
	// Cancelling an obsolete request must not cancel the current task.
	auto obsolete = cancellation;
	cancellation = std::make_shared<std::stop_source>();
	CHECK(backend.Execute(request, {}, cancellation, nullptr).ok());
	obsolete->request_stop();
	CHECK(backend.Execute(request, {}, cancellation, nullptr).ok());
	// Create fixtures independently of OpenFile so encoding bugs cannot mask themselves.
	std::string directoryName = "\u6d4b\u8bd5 package ";
	for (int i = 0; i < 70; ++i) directoryName += "\u56fa";
	const auto unicodeDir = temporary.path / platform::Path(directoryName);
	CHECK(std::filesystem::create_directory(unicodeDir));
	const auto unicodeImage = platform::Utf8(unicodeDir / platform::Path("\u56fa\u4ef6 image.img"));
	const auto unicodeLoader = platform::Utf8(unicodeDir / platform::Path("\u52a0\u8f7d Loader.bin"));
	CHECK(unicodeImage.size() > 256);
	CHECK(Save(unicodeImage.c_str(), image)); CHECK(Save(unicodeLoader.c_str(), boot));
	CHECK(core::FirmwareService::Inspect(unicodeLoader, info) && info.valid && info.loaderVersion == "1.02");
	request.file().path = unicodeImage;
	CHECK(engine.Execute(request).ok());
	CHECK(core::FirmwareService::Inspect(unicodeImage, info) && info.entries.size() == 1);
	CHECK(info.version == "1.2.03" && info.loaderVersion == "1.02");
	const auto output = unicodeDir / platform::Path("\u5bfc\u51fa files");
	CHECK(std::filesystem::create_directory(output));
	request = core::OperationRequest(core::Operation::FirmwareExtract);
	request.file().path = unicodeImage; request.file().output = platform::Utf8(output);
	CHECK(engine.Execute(request).ok());
	{ platform::File file(output / "bootloader.bin"); CHECK(file.read(boot.size()) == boot); }
	{ platform::File file(output / "boot.img");
	  CHECK(file.read(16) == std::vector<unsigned char>(image.begin() + fwOffset + 0x100, image.begin() + fwOffset + 0x110)); }
	const std::string parameter = "TYPE: GPT\nCMDLINE:mtdparts=rk29xxnand:0x1000@0x2000(boot)\n";
	const auto parameterPath = platform::Utf8(unicodeDir / "parameter.txt");
	CHECK(Save(parameterPath.c_str(), {parameter.begin(), parameter.end()}));
	CHECK(Save(platform::Utf8(unicodeDir / "boot.img").c_str(), {1, 2, 3, 4}));
	info = core::FirmwareService::InspectDirectory(platform::Utf8(unicodeDir), {});
	CHECK(info.valid && info.entries.size() == 3);
	if (info.entries.size() == 3) {
		CHECK(info.entries[0].isLoader && info.entries[0].fileName == "\u52a0\u8f7d Loader.bin");
		CHECK(info.entries[2].partName == "boot" && info.entries[2].imageLike);
	}
	request = core::OperationRequest(core::Operation::FirmwareInfo);
	request.file().path = platform::Utf8(unicodeDir / platform::Path("\u4e0d\u5b58\u5728.img"));
	CHECK(engine.Execute(request).error == core::ErrorCode::FileAccess);
	CHECK(Save(unicodeImage.c_str(), {1, 2, 3, 4}));
	request.file().path = unicodeImage;
	CHECK(engine.Execute(request).error == core::ErrorCode::InvalidFirmware);
	// An entry outside the physical file must fail rather than produce partial metadata.
	Write32(entry + 0x6c, 0xffff); CHECK(Save(imagePath, image)); CHECK(!core::FirmwareService::Inspect(imagePath, info)); CHECK(!info.valid);
	// Reject truncated entry tables, including excessive counts.
	Write32(image.data() + fwOffset + 0x88, 513); CHECK(Save(imagePath, image)); CHECK(!core::FirmwareService::Inspect(imagePath, info));
	// Loader corruption must be reported via the business error code.
	boot[10] ^= 1; CHECK(Save(loaderPath, boot)); request.file().path = loaderPath;
	CHECK(engine.Execute(request).error == core::ErrorCode::InvalidFirmware);
	std::remove(loaderPath); std::remove(imagePath);
	std::printf("test_firmware: %s\n", failures ? "FAIL" : "PASS"); return failures ? 1 : 0;
}
