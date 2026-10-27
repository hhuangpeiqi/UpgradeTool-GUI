#ifndef CORE_OPERATION_H
#define CORE_OPERATION_H

#include <stop_token>
#include "domain/Device.h"
#include "domain/Config.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <variant>
#include <type_traits>
#include <stdexcept>

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
	EnterMaskrom,
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
struct RockchipParameters {
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
struct AllwinnerParameters { AllwinnerOptions options; FileRequest file; };
struct QualcommParameters { QualcommOptions options; };
template<Operation O, Vendor V, class Parameters> struct OperationPayload : Parameters {
    static constexpr Operation operation=O;
    static constexpr Vendor vendor=V;
};
using EnterMaskromRequest = OperationPayload<Operation::EnterMaskrom,Vendor::Rockchip,RockchipParameters>;
using FirmwareUpgradeRequest = OperationPayload<Operation::FirmwareUpgrade,Vendor::Rockchip,RockchipParameters>;
using FirmwareExtractRequest = OperationPayload<Operation::FirmwareExtract,Vendor::Rockchip,RockchipParameters>;
using FirmwareInfoRequest = OperationPayload<Operation::FirmwareInfo,Vendor::Rockchip,RockchipParameters>;
using LoaderUpgradeRequest = OperationPayload<Operation::LoaderUpgrade,Vendor::Rockchip,RockchipParameters>;
using DownloadBootRequest = OperationPayload<Operation::DownloadBoot,Vendor::Rockchip,RockchipParameters>;
using EraseFlashRequest = OperationPayload<Operation::EraseFlash,Vendor::Rockchip,RockchipParameters>;
using PartitionListRequest = OperationPayload<Operation::PartitionList,Vendor::Rockchip,RockchipParameters>;
using DownloadImageRequest = OperationPayload<Operation::DownloadImage,Vendor::Rockchip,RockchipParameters>;
using CreateGptRequest = OperationPayload<Operation::CreateGpt,Vendor::Rockchip,RockchipParameters>;
using SwitchDeviceRequest = OperationPayload<Operation::SwitchDevice,Vendor::Rockchip,RockchipParameters>;
using ReadSnRequest = OperationPayload<Operation::ReadSn,Vendor::Rockchip,RockchipParameters>;
using WriteSnRequest = OperationPayload<Operation::WriteSn,Vendor::Rockchip,RockchipParameters>;
using ReadComLogRequest = OperationPayload<Operation::ReadComLog,Vendor::Rockchip,RockchipParameters>;
using ReadCapabilityRequest = OperationPayload<Operation::ReadCapability,Vendor::Rockchip,RockchipParameters>;
using ReadFlashIdRequest = OperationPayload<Operation::ReadFlashId,Vendor::Rockchip,RockchipParameters>;
using ReadFlashInfoRequest = OperationPayload<Operation::ReadFlashInfo,Vendor::Rockchip,RockchipParameters>;
using ReadChipInfoRequest = OperationPayload<Operation::ReadChipInfo,Vendor::Rockchip,RockchipParameters>;
using ReadOtpChipRequest = OperationPayload<Operation::ReadOtpChip,Vendor::Rockchip,RockchipParameters>;
using ReadSecureModeRequest = OperationPayload<Operation::ReadSecureMode,Vendor::Rockchip,RockchipParameters>;
using TestDeviceRequest = OperationPayload<Operation::TestDevice,Vendor::Rockchip,RockchipParameters>;
using ResetDeviceRequest = OperationPayload<Operation::ResetDevice,Vendor::Rockchip,RockchipParameters>;
using ResetPipeRequest = OperationPayload<Operation::ResetPipe,Vendor::Rockchip,RockchipParameters>;
using ReadLbaRequest = OperationPayload<Operation::ReadLba,Vendor::Rockchip,RockchipParameters>;
using WriteLbaRequest = OperationPayload<Operation::WriteLba,Vendor::Rockchip,RockchipParameters>;
using EraseLbaRequest = OperationPayload<Operation::EraseLba,Vendor::Rockchip,RockchipParameters>;
using WriteSectorRequest = OperationPayload<Operation::WriteSector,Vendor::Rockchip,RockchipParameters>;
using EraseBlockRequest = OperationPayload<Operation::EraseBlock,Vendor::Rockchip,RockchipParameters>;
using RunSystemRequest = OperationPayload<Operation::RunSystem,Vendor::Rockchip,RockchipParameters>;
using SwitchStorageRequest = OperationPayload<Operation::SwitchStorage,Vendor::Rockchip,RockchipParameters>;
using AllwinnerFlashRequest = OperationPayload<Operation::AllwinnerFlash,Vendor::Allwinner,AllwinnerParameters>;
using AllwinnerRawRequest = OperationPayload<Operation::AllwinnerRaw,Vendor::Allwinner,AllwinnerParameters>;
using AllwinnerConvertRequest = OperationPayload<Operation::AllwinnerConvert,Vendor::Allwinner,AllwinnerParameters>;
using AllwinnerInspectRequest = OperationPayload<Operation::AllwinnerInspect,Vendor::Allwinner,AllwinnerParameters>;
using AllwinnerExtractRequest = OperationPayload<Operation::AllwinnerExtract,Vendor::Allwinner,AllwinnerParameters>;
using AllwinnerDeviceInfoRequest = OperationPayload<Operation::AllwinnerDeviceInfo,Vendor::Allwinner,AllwinnerParameters>;
using AllwinnerRebootRequest = OperationPayload<Operation::AllwinnerReboot,Vendor::Allwinner,AllwinnerParameters>;
using QualcommFlashRequest = OperationPayload<Operation::QualcommFlash,Vendor::Qualcomm,QualcommParameters>;
using QualcommRawRequest = OperationPayload<Operation::QualcommRaw,Vendor::Qualcomm,QualcommParameters>;
using QualcommChipInfoRequest = OperationPayload<Operation::QualcommChipInfo,Vendor::Qualcomm,QualcommParameters>;
using QualcommResetRequest = OperationPayload<Operation::QualcommReset,Vendor::Qualcomm,QualcommParameters>;
using QualcommRamdumpRequest = OperationPayload<Operation::QualcommRamdump,Vendor::Qualcomm,QualcommParameters>;
using QualcommKickstartRequest = OperationPayload<Operation::QualcommKickstart,Vendor::Qualcomm,QualcommParameters>;
using QualcommCreateZipRequest = OperationPayload<Operation::QualcommCreateZip,Vendor::Qualcomm,QualcommParameters>;
using QualcommCreateArchiveRequest = OperationPayload<Operation::QualcommCreateArchive,Vendor::Qualcomm,QualcommParameters>;
using QualcommCreateDigestsRequest = OperationPayload<Operation::QualcommCreateDigests,Vendor::Qualcomm,QualcommParameters>;
using QualcommProvisionRequest = OperationPayload<Operation::QualcommProvision,Vendor::Qualcomm,QualcommParameters>;
class OperationRequest {
public:
    using Payload = std::variant<std::monostate,
        EnterMaskromRequest,
        FirmwareUpgradeRequest,
        FirmwareExtractRequest,
        FirmwareInfoRequest,
        LoaderUpgradeRequest,
        DownloadBootRequest,
        EraseFlashRequest,
        PartitionListRequest,
        DownloadImageRequest,
        CreateGptRequest,
        SwitchDeviceRequest,
        ReadSnRequest,
        WriteSnRequest,
        ReadComLogRequest,
        ReadCapabilityRequest,
        ReadFlashIdRequest,
        ReadFlashInfoRequest,
        ReadChipInfoRequest,
        ReadOtpChipRequest,
        ReadSecureModeRequest,
        TestDeviceRequest,
        ResetDeviceRequest,
        ResetPipeRequest,
        ReadLbaRequest,
        WriteLbaRequest,
        EraseLbaRequest,
        WriteSectorRequest,
        EraseBlockRequest,
        RunSystemRequest,
        SwitchStorageRequest,
        AllwinnerFlashRequest,
        AllwinnerRawRequest,
        AllwinnerConvertRequest,
        AllwinnerInspectRequest,
        AllwinnerExtractRequest,
        AllwinnerDeviceInfoRequest,
        AllwinnerRebootRequest,
        QualcommFlashRequest,
        QualcommRawRequest,
        QualcommChipInfoRequest,
        QualcommResetRequest,
        QualcommRamdumpRequest,
        QualcommKickstartRequest,
        QualcommCreateZipRequest,
        QualcommCreateArchiveRequest,
        QualcommCreateDigestsRequest,
        QualcommProvisionRequest>;
    explicit OperationRequest(Operation op=Operation::Count) {
        switch(op) {
        case Operation::EnterMaskrom: payload.emplace<EnterMaskromRequest>(); break;
        case Operation::FirmwareUpgrade: payload.emplace<FirmwareUpgradeRequest>(); break;
        case Operation::FirmwareExtract: payload.emplace<FirmwareExtractRequest>(); break;
        case Operation::FirmwareInfo: payload.emplace<FirmwareInfoRequest>(); break;
        case Operation::LoaderUpgrade: payload.emplace<LoaderUpgradeRequest>(); break;
        case Operation::DownloadBoot: payload.emplace<DownloadBootRequest>(); break;
        case Operation::EraseFlash: payload.emplace<EraseFlashRequest>(); break;
        case Operation::PartitionList: payload.emplace<PartitionListRequest>(); break;
        case Operation::DownloadImage: payload.emplace<DownloadImageRequest>(); break;
        case Operation::CreateGpt: payload.emplace<CreateGptRequest>(); break;
        case Operation::SwitchDevice: payload.emplace<SwitchDeviceRequest>(); break;
        case Operation::ReadSn: payload.emplace<ReadSnRequest>(); break;
        case Operation::WriteSn: payload.emplace<WriteSnRequest>(); break;
        case Operation::ReadComLog: payload.emplace<ReadComLogRequest>(); break;
        case Operation::ReadCapability: payload.emplace<ReadCapabilityRequest>(); break;
        case Operation::ReadFlashId: payload.emplace<ReadFlashIdRequest>(); break;
        case Operation::ReadFlashInfo: payload.emplace<ReadFlashInfoRequest>(); break;
        case Operation::ReadChipInfo: payload.emplace<ReadChipInfoRequest>(); break;
        case Operation::ReadOtpChip: payload.emplace<ReadOtpChipRequest>(); break;
        case Operation::ReadSecureMode: payload.emplace<ReadSecureModeRequest>(); break;
        case Operation::TestDevice: payload.emplace<TestDeviceRequest>(); break;
        case Operation::ResetDevice: payload.emplace<ResetDeviceRequest>(); break;
        case Operation::ResetPipe: payload.emplace<ResetPipeRequest>(); break;
        case Operation::ReadLba: payload.emplace<ReadLbaRequest>(); break;
        case Operation::WriteLba: payload.emplace<WriteLbaRequest>(); break;
        case Operation::EraseLba: payload.emplace<EraseLbaRequest>(); break;
        case Operation::WriteSector: payload.emplace<WriteSectorRequest>(); break;
        case Operation::EraseBlock: payload.emplace<EraseBlockRequest>(); break;
        case Operation::RunSystem: payload.emplace<RunSystemRequest>(); break;
        case Operation::SwitchStorage: payload.emplace<SwitchStorageRequest>(); break;
        case Operation::AllwinnerFlash: payload.emplace<AllwinnerFlashRequest>(); break;
        case Operation::AllwinnerRaw: payload.emplace<AllwinnerRawRequest>(); break;
        case Operation::AllwinnerConvert: payload.emplace<AllwinnerConvertRequest>(); break;
        case Operation::AllwinnerInspect: payload.emplace<AllwinnerInspectRequest>(); break;
        case Operation::AllwinnerExtract: payload.emplace<AllwinnerExtractRequest>(); break;
        case Operation::AllwinnerDeviceInfo: payload.emplace<AllwinnerDeviceInfoRequest>(); break;
        case Operation::AllwinnerReboot: payload.emplace<AllwinnerRebootRequest>(); break;
        case Operation::QualcommFlash: payload.emplace<QualcommFlashRequest>(); break;
        case Operation::QualcommRaw: payload.emplace<QualcommRawRequest>(); break;
        case Operation::QualcommChipInfo: payload.emplace<QualcommChipInfoRequest>(); break;
        case Operation::QualcommReset: payload.emplace<QualcommResetRequest>(); break;
        case Operation::QualcommRamdump: payload.emplace<QualcommRamdumpRequest>(); break;
        case Operation::QualcommKickstart: payload.emplace<QualcommKickstartRequest>(); break;
        case Operation::QualcommCreateZip: payload.emplace<QualcommCreateZipRequest>(); break;
        case Operation::QualcommCreateArchive: payload.emplace<QualcommCreateArchiveRequest>(); break;
        case Operation::QualcommCreateDigests: payload.emplace<QualcommCreateDigestsRequest>(); break;
        case Operation::QualcommProvision: payload.emplace<QualcommProvisionRequest>(); break;
        default: break;
        }
    }
    template<class R> requires requires { R::operation; R::vendor; }
    OperationRequest(R request) : payload(std::move(request)) {}
    Operation operation() const {
        return std::visit([](const auto &r) { using R=std::decay_t<decltype(r)>;
            if constexpr(std::is_same_v<R,std::monostate>) return Operation::Count;
            else return R::operation; },payload);
    }
    Vendor vendor() const {
        return std::visit([](const auto &r) { using R=std::decay_t<decltype(r)>;
            if constexpr(std::is_same_v<R,std::monostate>) return Vendor::Rockchip;
            else return R::vendor; },payload);
    }
    RockchipParameters &rockchip() { return Parameters<RockchipParameters>(); }
    const RockchipParameters &rockchip() const { return Parameters<RockchipParameters>(); }
    AllwinnerOptions &allwinner() { return Parameters<AllwinnerParameters>().options; }
    const AllwinnerOptions &allwinner() const { return Parameters<AllwinnerParameters>().options; }
    QualcommOptions &qualcomm() { return Parameters<QualcommParameters>().options; }
    const QualcommOptions &qualcomm() const { return Parameters<QualcommParameters>().options; }
    FileRequest &file() { return File(*this); }
    const FileRequest &file() const { return File(*this); }
    template<class R> const R &get() const { return std::get<R>(payload); }
private:
    Payload payload;
    template<class P, class Self> static auto &Params(Self &self) {
        using Pointer=std::conditional_t<std::is_const_v<Self>,const P *,P *>;
        auto pointer=std::visit([](auto &r)->Pointer {
            using R=std::decay_t<decltype(r)>;
            if constexpr(std::is_base_of_v<P,R>) return &r;
            else return nullptr;
        },self.payload);
        if(!pointer) throw std::logic_error("Request parameter type mismatch");
        return *pointer;
    }
    template<class P> P &Parameters() { return Params<P>(*this); }
    template<class P> const P &Parameters() const { return Params<P>(*this); }
    template<class Self> static std::conditional_t<std::is_const_v<Self>,const FileRequest &,FileRequest &> File(Self &self) {
        using Pointer=std::conditional_t<std::is_const_v<Self>,const FileRequest *,FileRequest *>;
        auto pointer=std::visit([](auto &r)->Pointer {
            if constexpr(requires { r.file; }) return &r.file;
            else return nullptr;
        },self.payload);
        if(!pointer) throw std::logic_error("Request has no file parameters");
        return *pointer;
    }
};

inline bool IsLocalOperation(Operation op)
{
	return op == Operation::FirmwareInfo || op == Operation::FirmwareExtract || op == Operation::CreateGpt ||
        op == Operation::AllwinnerInspect || op == Operation::AllwinnerExtract || op == Operation::AllwinnerConvert ||
        op == Operation::QualcommCreateZip || op == Operation::QualcommCreateArchive ||
        op == Operation::QualcommCreateDigests;
}

inline bool IsSimulation(const OperationRequest &request)
{
    return request.vendor() == Vendor::Qualcomm &&
        (request.qualcomm().dryRun || request.operation() == Operation::QualcommCreateDigests);
}

inline bool RequiresEnumeratedDevice(const OperationRequest &r)
{
    return !IsLocalOperation(r.operation()) && !(r.vendor() == Vendor::Qualcomm &&
        (r.qualcomm().dryRun || (r.operation() == Operation::QualcommKickstart && !r.qualcomm().node.empty())));
}

using Cancellation = std::shared_ptr<std::stop_source>;
struct TaskJob {
    uint64_t logId = 0;
    ConfigSnapshot config;
	std::vector<OperationRequest> steps;
	std::vector<DeviceRef> targets;
	bool continueOnError = false;
	bool rebootAfter = false;
	Cancellation cancelled = std::make_shared<std::stop_source>();
};
}
#endif
