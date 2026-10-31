#include "tests/support/TestFiles.h"
#include "backends/rockchip/FirmwareService.h"
#include "backends/rockchip/UpgradeEngine.h"
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
int main(int argc, char **argv) {
	if (argc == 4 && std::string(argv[1]) == "--extract") {
		core::UpgradeEngine engine;
		core::OperationRequest request(core::Operation::FirmwareExtract);
		request.file().path = argv[2]; request.file().output = argv[3];
		return engine.Execute(request).ok() ? 0 : 1;
	}
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

	image.resize(fwOffset + 0x800 + 32);
	imageHead.dwFWSize = 0x800;
	std::memcpy(image.data(), &imageHead, sizeof(imageHead));
	entry = image.data() + fwOffset + 0x8c;
	Write32(entry + 0x60, 0x400);
	const std::vector<unsigned char> payload(16, 0x6a);
	std::memcpy(image.data() + fwOffset + 0x400, payload.data(), payload.size());
	const auto output = temporary.path / platform::Path("导出 files");
	std::filesystem::create_directory(output);
	core::UpgradeEngine engine;
	core::OperationRequest request(core::Operation::FirmwareExtract);
	request.file().path = imagePath; request.file().output = platform::Utf8(output);
	auto extract = [&] { CHECK(Save(imagePath, image)); return engine.Execute(request).ok(); };
	auto name = [&](const char *value) { std::memset(entry + 0x20, 0, 32); std::memcpy(entry + 0x20, value, std::strlen(value)); };
	for (const char *value : {"boot.img", "Image/sub/boot.img", "Image\\sub\\boot.img"}) {
		name(value); CHECK(extract());
		platform::File file(output / "boot.img"); CHECK(file.read(16) == payload);
	}
	// Parameter output is the text between its header and CRC.
	std::memset(entry, 0, 32); std::memcpy(entry, "parameter", 9); name("Image/parameter.txt");
	CHECK(extract());
	CHECK(std::filesystem::file_size(output / "parameter.txt") == 4);
	{ platform::File file(output / "parameter.txt"); CHECK(file.read(4) == std::vector<unsigned char>(4, 0x6a)); }
	Write32(entry + 0x6c, 11); CHECK(!extract());
	Write32(entry + 0x6c, 16); std::memset(entry, 0, 32); std::memcpy(entry, "boot_a", 6);
	// A/B entries share exactly the same payload offset.
	Write32(image.data() + fwOffset + 0x88, 2);
	std::memcpy(entry + 0x70, entry, 0x70);
	std::memset(entry + 0x70, 0, 32); std::memcpy(entry + 0x70, "boot_b", 6);
	std::memset(entry + 0x90, 0, 32); std::memcpy(entry + 0x90, "b.img", 5);
	CHECK(extract());
	{ platform::File file(output / "b.img"); CHECK(file.read(16) == payload); }
	// Stop on the first failed output without writing the next entry.
	std::filesystem::remove(output / "b.img"); name("blocked.img");
	std::filesystem::create_directory(output / "blocked.img");
	CHECK(!extract()); CHECK(!std::filesystem::exists(output / "b.img"));
	Write32(image.data() + fwOffset + 0x88, 1); name("boot.img");
	// High size bits must participate in bounds validation, even if the low word fits.
	entry[0x57] = 'H'; Write32(entry + 0x58, 1); CHECK(!extract());
	// Sparse file validates a real >4 GiB size without allocating its payload in RAM.
	CHECK(Save(imagePath, image));
	const uint64_t largeSize = (uint64_t(1) << 32) + 16;
	{ FILE *file = platform::OpenFile(imagePath, "rb+"); CHECK(file != nullptr);
	  if (file) { CHECK(fseeko(file, fwOffset + 0x400 + largeSize - 1, SEEK_SET) == 0); CHECK(std::fputc(0, file) != EOF); std::fclose(file); } }
	CHECK(core::FirmwareService::Inspect(imagePath, info));
	CHECK(info.entries.size() == 1 && info.entries[0].size == largeSize);
	entry[0x57] = 0; entry[0x52] = 'H'; Write32(entry + 0x53, 1);
	CHECK(Save(imagePath, image));
	{ FILE *file = platform::OpenFile(imagePath, "rb+"); CHECK(file != nullptr);
	  if (file) { CHECK(fseeko(file, fwOffset + (uint64_t(1) << 32) + 0x400, SEEK_SET) == 0); CHECK(std::fwrite(payload.data(), 1, payload.size(), file) == payload.size()); std::fclose(file); } }
	CHECK(core::FirmwareService::Inspect(imagePath, info));
	CHECK(info.entries.size() == 1 && info.entries[0].offset == fwOffset + (uint64_t(1) << 32) + 0x400);
	CHECK(engine.Execute(request).ok());
	{ platform::File file(output / "boot.img"); CHECK(file.read(16) == payload); }
	std::printf("test_extract: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
