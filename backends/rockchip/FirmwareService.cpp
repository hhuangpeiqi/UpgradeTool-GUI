#include "platform/File.h"
#include "platform/Text.h"
#include "Parameter.h"
#include <algorithm>
#include <map>
#include "backends/rockchip/FirmwareService.h"
#include "RKImage.h"
#include "RKBoot.h"
#include <cstdio>
#include <cstring>
#include <cctype>
namespace core {
namespace {
uint32_t Read32(const unsigned char *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
std::string FormatChip(uint32_t tag) {
	char name[5] = {char(tag >> 24), char(tag >> 16), char(tag >> 8), char(tag), 0};
	bool printable = true;
	for (int i = 0; i < 4; ++i) printable = printable && std::isprint(static_cast<unsigned char>(name[i]));
	char text[32];
	if (printable) std::snprintf(text, sizeof(text), "RK%s", name);
	else std::snprintf(text, sizeof(text), "RK%X", tag);
	return text;
}
}
std::string FirmwareService::ChipName(uint32_t tag) { return FormatChip(tag); }
bool FirmwareService::Inspect(const std::string &path, FirmwareInfo &info, int entryIndex) {
	info = FirmwareInfo();
	FILE *file = std::fopen(path.c_str(), "rb");
	if (!file) return false;
	unsigned char magic[4] = {};
	bool read = std::fread(magic, 1, 4, file) == 4;
	std::fclose(file);
	if (!read) return false;
	CRKImage image;
	if (Read32(magic) == 0x57465353) {
		if (!image.OpenStorageFirmware(path)) return false;
		if (entryIndex >= 0) {
			STRUCT_STORAGE_ENTRY entry;
			if (!image.GetStorageEntry(static_cast<UINT>(entryIndex), entry) || entry.uiOffset == 0xffffffffULL) return false;
			CRKImage embedded;
			if (embedded.ObjectInit(path, entry.uiOffset, entry.uiSize) != 0) return false;
			if (!InspectImage(embedded, path, info)) return false;
			info.storage = entry.uiStorage; return true;
		}
		for (UINT i = 0; i < image.GetStorageEntryCount(); ++i) {
			STRUCT_STORAGE_ENTRY source;
			if (!image.GetStorageEntry(i, source)) return false;
			FirmwareEntry entry;
			entry.fileName = source.szName; entry.partName = source.szName;
			entry.offset = source.uiOffset; entry.size = source.uiSize; entry.storage = source.uiStorage;
			entry.type = source.uiOffset == 0xffffffffULL ? "divider" : "storage";
			info.entries.push_back(entry);
		}
		info.valid = true; return true;
	}
	if (Read32(magic) == 0x544f4f42 || Read32(magic) == 0x2052444c) {
		file = std::fopen(path.c_str(), "rb");
		if (!file) return false;
		fseeko(file, 0, SEEK_END); auto size = ftello(file);
		if (size < static_cast<decltype(size)>(sizeof(STRUCT_RKBOOT_HEAD) + 4) || uint64_t(size) > UINT32_MAX) { std::fclose(file); return false; }
		fseeko(file, 0, SEEK_SET);
		BYTE *data = new BYTE[static_cast<size_t>(size)];
		if (std::fread(data, 1, size, file) != static_cast<size_t>(size)) { delete []data; std::fclose(file); return false; }
		std::fclose(file);
		CRKBoot boot(data, static_cast<DWORD>(size));
		if (boot.ObjectInit() != 0) return false;
		char version[32]; std::snprintf(version, sizeof(version), "%u.%02u", (boot.GetVersion() >> 8) & 255, boot.GetVersion() & 255);
		info.loaderVersion = version; info.version = version; info.chip = ChipName(boot.GetSupportDevice());
		STRUCT_RKTIME t = boot.GetReleaseTime(); char time[64];
		std::snprintf(time, sizeof(time), "%u-%02u-%02u %02u:%02u:%02u", t.usYear, t.ucMonth, t.ucDay, t.ucHour, t.ucMinute, t.ucSecond);
		info.buildTime = time; info.sign = boot.GetSignFlag() ? "true" : "false"; info.valid = true;
		return true;
	}
	if (image.ObjectInit(path) != 0) return false;
	return InspectImage(image, path, info);
}
bool FirmwareService::InspectImage(CRKImage &image, const std::string &path, FirmwareInfo &info) {
	info = FirmwareInfo();
	info.chip = ChipName(image.GetSupportDevice());
	uint32_t ver = image.GetVersion();
	char text[128];
	std::snprintf(text, sizeof(text), "%x.%x.%02x", (ver >> 24) & 255, (ver >> 16) & 255, (ver >> 8) & 255);
	info.version = text;
	info.loaderVersion = "-";
	if (image.m_bootObject) {
		uint32_t lv = image.m_bootObject->GetVersion();
		std::snprintf(text, sizeof(text), "%u.%02u", (lv >> 8) & 255, lv & 255);
		info.loaderVersion = text;
	}
	STRUCT_RKTIME t = image.GetReleaseTime();
	std::snprintf(text, sizeof(text), "%u-%02u-%02u %02u:%02u:%02u", t.usYear, t.ucMonth, t.ucDay, t.ucHour, t.ucMinute, t.ucSecond);
	info.buildTime = text;
	info.sign = image.GetSignFlag() ? "true" : "false";
	info.storage = image.GetStorage();
	FILE *file = std::fopen(path.c_str(), "rb");
	if (!file) return false;
	uint64_t fwOffset = image.GetFWOffset();
	if (fwOffset) {
		// Use the platform's 64-bit file offsets for firmware larger than 2 GiB.
		fseeko(file, 0, SEEK_END);
		const auto fileSize = ftello(file);
		unsigned char countBytes[4];
		if (fileSize < 0 || fwOffset > uint64_t(fileSize) ||
		    uint64_t(fileSize) - fwOffset < 0x8c || fseeko(file, fwOffset + 0x88, SEEK_SET) != 0 ||
		    std::fread(countBytes, 1, 4, file) != 4) { std::fclose(file); return false; }
		uint32_t count = Read32(countBytes);
		if (count > 512 || uint64_t(count) * 0x70 > uint64_t(fileSize) - fwOffset - 0x8c) { std::fclose(file); return false; }
		for (uint32_t i = 0; i < count; ++i) {
			unsigned char ent[0x70];
			if (fseeko(file, fwOffset + 0x8c + uint64_t(i) * 0x70, SEEK_SET) != 0 || std::fread(ent, 1, sizeof(ent), file) != sizeof(ent)) { std::fclose(file); return false; }
			char part[33] = {}, name[33] = {};
			std::memcpy(part, ent, 32); std::memcpy(name, ent + 0x20, 32);
			FirmwareEntry e;
			e.partName = part; e.fileName = name;
			e.offset = fwOffset + Read32(ent + 0x60);
			if (ent[0x52] == 'H') e.offset += uint64_t(Read32(ent + 0x53)) << 32;
			e.size = Read32(ent + 0x6c); e.lba = Read32(ent + 0x64);
			if (e.offset > uint64_t(fileSize) || e.size > uint64_t(fileSize) - e.offset) { std::fclose(file); return false; }
			e.hasPart = e.lba != 0xffffffff;
			e.imageLike = std::strstr(name, ".img") || std::strstr(name, "parameter.txt");
			e.type = "other";
			if (e.hasPart && e.imageLike) {
				if (std::strstr(name, "parameter.txt")) e.type = "parameter";
				else {
					unsigned char magic[4] = {};
					fseeko(file, e.offset, SEEK_SET);
					bool read = std::fread(magic, 1, 4, file) == 4;
					e.type = read && Read32(magic) == SPARSE_HEADER_MAGIC ? "sparse image" : "image";
				}
			}
			info.entries.push_back(e);
		}
	}
	std::fclose(file);
	info.valid = true;
	return true;
}
FirmwareInfo FirmwareService::InspectDirectory(const std::string &path,const Cancellation &cancelled) {
    FirmwareInfo info;
    const auto dir=platform::Path(path); const auto parameter=dir/"parameter.txt";
    if(!std::filesystem::is_regular_file(parameter)) return info;
    PARAM_ITEM_VECTOR parts; CONFIG_ITEM_VECTOR uuids;
    auto name=platform::Utf8(parameter);
    if(!parse_parameter_file(name.data(),parts,uuids)) return info;
    std::vector<std::filesystem::path> files;
    for(const auto &entry:std::filesystem::directory_iterator(dir)) {
        if(cancelled && cancelled->stop_requested()) return {};
        if(entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(),files.end());
    auto imageKey=[](std::string text) { text=platform::FoldCase(text); std::replace(text.begin(),text.end(),'-','_'); return text; };
    std::map<std::string,std::string> images; std::string loader;
    for(const auto &file:files) {
        const auto base=platform::Utf8(file.filename());
        const auto lower=platform::FoldCase(base);
        const auto extension=platform::FoldCase(platform::Utf8(file.extension()));
        if(extension==".img") images[imageKey(platform::Utf8(file.stem()))]=base;
        if(extension==".bin" && (lower=="miniloaderall.bin" || (loader.empty() && lower.find("loader")!=std::string::npos))) loader=base;
    }
    if(!loader.empty()) { FirmwareEntry row; row.partName="Loader"; row.fileName=loader; row.lba=UINT32_MAX; row.imageLike=row.isLoader=true; row.type="loader"; info.entries.push_back(row); }
    FirmwareEntry param; param.partName="parameter"; param.fileName="parameter.txt"; param.lba=UINT32_MAX; param.imageLike=true; param.type="parameter"; info.entries.push_back(param);
    for(const auto &part:parts) {
        FirmwareEntry row; row.partName=part.szItemName; row.partName=row.partName.substr(0,row.partName.find(':'));
        const auto resolved=images.find(imageKey(row.partName));
        row.fileName=resolved==images.end()?row.partName+".img":resolved->second;
        row.size=part.uiItemSize; row.lba=part.uiItemOffset; row.hasPart=true;
        row.imageLike=resolved!=images.end() || std::filesystem::is_regular_file(dir/platform::Path(row.fileName));
        row.type=row.imageLike?"image":"Other"; info.entries.push_back(row);
    }
    info.valid=true; return info;
}

}
