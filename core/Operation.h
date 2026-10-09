#ifndef CORE_OPERATION_H
#define CORE_OPERATION_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core {
enum class Operation {
	FirmwareUpgrade,
	FirmwareExtract,
	FirmwareInfo,
	LoaderUpgrade,
	DownloadBoot,
	EraseFlash,
	PartitionList,
	DownloadImage,
	CreateGpt,
	SwitchDevice,
	ReadSn,
	WriteSn,
	ReadComLog,
	ReadCapability,
	ReadFlashId,
	ReadFlashInfo,
	ReadChipInfo,
	ReadOtpChip,
	ReadSecureMode,
	TestDevice,
	ResetDevice,
	ResetPipe,
	ReadLba,
	WriteLba,
	EraseLba,
	WriteSector,
	EraseBlock,
	RunSystem,
	SwitchStorage,
	Count
};

// Named business parameters. No argument positions or command syntax cross this boundary.
struct FileRequest { std::string path; std::string output; };
struct LbaRequest { uint32_t begin = 0; uint32_t count = 0; };
struct SectorRequest { uint32_t pageSize = 0; uint32_t spareSize = 0; };
struct BlockRequest { uint32_t chipSelect = 0; bool force = false; };
struct SystemRequest {
	uint32_t ubootAddress = 0, trustAddress = 0, bootAddress = 0;
	std::string uboot, trust, boot;
};
struct OperationRequest {
	Operation operation = Operation::Count;
	FileRequest file;
	LbaRequest lba;
	SectorRequest sector;
	BlockRequest block;
	SystemRequest system;
	std::string partition;
	std::string serial;
	bool reboot = true;
	uint32_t subcode = 0;
	uint32_t pipe = 0;
	int storageIndex = -1;
	int entryIndex = -1;
};

struct DeviceIdentity {
	std::string serial;
	uint32_t location = 0;
};

inline bool IsLocalOperation(Operation op)
{
	return op == Operation::FirmwareInfo || op == Operation::FirmwareExtract || op == Operation::CreateGpt;
}

using Cancellation = std::shared_ptr<std::atomic<bool> >;
struct TaskJob {
	std::vector<OperationRequest> steps;
	std::vector<DeviceIdentity> targets;
	bool continueOnError = false;
	bool rebootAfter = false;
	Cancellation cancelled = std::make_shared<std::atomic<bool> >(false);
};
}
#endif
