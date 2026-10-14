#ifndef CORE_OPERATION_H
#define CORE_OPERATION_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core {
enum class Vendor { Rockchip, Allwinner, Qualcomm };
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
	AllwinnerFlash,
	AllwinnerRaw,
	AllwinnerConvert,
	AllwinnerInspect,
	AllwinnerExtract,
	AllwinnerDeviceInfo,
	AllwinnerReboot,
	QualcommFlash,
	QualcommRaw,
	QualcommChipInfo,
	QualcommReset,
	QualcommRamdump,
	QualcommKickstart,
	QualcommCreateZip,
	QualcommCreateArchive,
	QualcommCreateDigests,
	QualcommProvision,
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
enum class AllwinnerFlashMode { Partition, KeepData, PartitionErase, FullErase };
enum class PostAction { Reboot, PowerOff, Shutdown };
enum class RawMode { Command, LogicOffset };
enum class RawStorage { Sdmmc, Ufs, Nor };
enum class ConvertTarget { Spinor, Sdcard, Emmc, Sdnand, Ufs };
enum class SecureMode { Auto, Enabled, Disabled };
struct AllwinnerOptions {
    AllwinnerFlashMode mode = AllwinnerFlashMode::FullErase;
    bool verify = true;
    std::vector<std::string> partitions;
    PostAction postAction = PostAction::Reboot;
    std::string rawImage;
    RawMode rawMode = RawMode::LogicOffset;
    RawStorage rawStorage = RawStorage::Sdmmc;
    ConvertTarget convertTarget = ConvertTarget::Emmc;
    SecureMode secure = SecureMode::Auto;
    bool hasLogicOffset = false, hasUbootOffset = false;
    uint64_t logicOffset = 0, ubootOffset = 0, norMiB = 16;
    std::string storageSize = "auto";
};
enum class QualcommStorage { Emmc = 1, Nand, Ufs, Nvme, Spinor };
enum class QualcommTransport { Auto = -1, Usb = 0, Qud = 2 };
enum class QualcommRawType { Read, Write, Erase, Sha256, Reset };
struct QualcommRawOperation {
    QualcommRawType type = QualcommRawType::Read;
    std::string address, file;
};
struct QualcommImage { unsigned int id = 13; std::string file; };
struct QualcommOptions {
    std::string programmer, package, selector, includeDir, output, filter, node, vipTables, digestDir;
    std::vector<std::string> xml;
    std::vector<QualcommRawOperation> raw;
    std::vector<QualcommImage> images;
    QualcommStorage storage = QualcommStorage::Ufs;
    QualcommTransport transport = QualcommTransport::Auto;
    int slot = -1;
    long chunkSize = 0;
    bool packageBuild = false, debug = false, dryRun = false, allowMissing = false;
    bool allowFusing = false, finalize = false, skipReset = false, skipblock = false;
};
struct OperationRequest {
	Operation operation = Operation::Count;
	Vendor vendor = Vendor::Rockchip;
	AllwinnerOptions allwinner;
	QualcommOptions qualcomm;
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
    // Qualcomm interface incarnation. Zero IDs retain unspecified-target semantics.
    unsigned int vid = 0, pid = 0, usbAddress = 0;
    int interfaceNumber = -1, alternate = 0, inEndpoint = 0, outEndpoint = 0;
	std::string serial;
	uint32_t location = 0;
	Vendor vendor = Vendor::Rockchip;
	std::string devicePath, deviceInstance;
	QualcommTransport transport = QualcommTransport::Auto;
	uint8_t bus = 0;
	std::vector<uint8_t> ports;
};

inline bool IsLocalOperation(Operation op)
{
	return op == Operation::FirmwareInfo || op == Operation::FirmwareExtract || op == Operation::CreateGpt ||
        op == Operation::AllwinnerInspect || op == Operation::AllwinnerExtract || op == Operation::AllwinnerConvert ||
        op == Operation::QualcommCreateZip || op == Operation::QualcommCreateArchive ||
        op == Operation::QualcommCreateDigests;
}

inline bool RequiresEnumeratedDevice(const OperationRequest &r)
{
    return !IsLocalOperation(r.operation) && !(r.vendor == Vendor::Qualcomm &&
        (r.qualcomm.dryRun || (r.operation == Operation::QualcommKickstart && !r.qualcomm.node.empty())));
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
