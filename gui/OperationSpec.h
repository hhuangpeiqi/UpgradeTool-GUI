#ifndef GUI_OPERATION_SPEC_H
#define GUI_OPERATION_SPEC_H
#include <QObject>
#include <QString>
#include "core/Operation.h"
namespace gui {
class Operations {
	Q_GADGET
public:
	enum Id {
		FirmwareUpgrade = static_cast<int>(core::Operation::FirmwareUpgrade),
		FirmwareExtract = static_cast<int>(core::Operation::FirmwareExtract),
		FirmwareInfo = static_cast<int>(core::Operation::FirmwareInfo),
		LoaderUpgrade = static_cast<int>(core::Operation::LoaderUpgrade),
		DownloadBoot = static_cast<int>(core::Operation::DownloadBoot),
		EraseFlash = static_cast<int>(core::Operation::EraseFlash),
		PartitionList = static_cast<int>(core::Operation::PartitionList),
		DownloadImage = static_cast<int>(core::Operation::DownloadImage),
		CreateGpt = static_cast<int>(core::Operation::CreateGpt),
		SwitchDevice = static_cast<int>(core::Operation::SwitchDevice),
		ReadSn = static_cast<int>(core::Operation::ReadSn),
		WriteSn = static_cast<int>(core::Operation::WriteSn),
		ReadComLog = static_cast<int>(core::Operation::ReadComLog),
		ReadCapability = static_cast<int>(core::Operation::ReadCapability),
		ReadFlashId = static_cast<int>(core::Operation::ReadFlashId),
		ReadFlashInfo = static_cast<int>(core::Operation::ReadFlashInfo),
		ReadChipInfo = static_cast<int>(core::Operation::ReadChipInfo),
		ReadOtpChip = static_cast<int>(core::Operation::ReadOtpChip),
		ReadSecureMode = static_cast<int>(core::Operation::ReadSecureMode),
		TestDevice = static_cast<int>(core::Operation::TestDevice),
		ResetDevice = static_cast<int>(core::Operation::ResetDevice),
		ResetPipe = static_cast<int>(core::Operation::ResetPipe),
		ReadLba = static_cast<int>(core::Operation::ReadLba),
		WriteLba = static_cast<int>(core::Operation::WriteLba),
		EraseLba = static_cast<int>(core::Operation::EraseLba),
		WriteSector = static_cast<int>(core::Operation::WriteSector),
		EraseBlock = static_cast<int>(core::Operation::EraseBlock),
		RunSystem = static_cast<int>(core::Operation::RunSystem),
		SwitchStorage = static_cast<int>(core::Operation::SwitchStorage),
		AllwinnerFlash = static_cast<int>(core::Operation::AllwinnerFlash),
		AllwinnerRaw = static_cast<int>(core::Operation::AllwinnerRaw),
		AllwinnerConvert = static_cast<int>(core::Operation::AllwinnerConvert),
		AllwinnerInspect = static_cast<int>(core::Operation::AllwinnerInspect),
		AllwinnerExtract = static_cast<int>(core::Operation::AllwinnerExtract),
		AllwinnerDeviceInfo = static_cast<int>(core::Operation::AllwinnerDeviceInfo),
		AllwinnerReboot = static_cast<int>(core::Operation::AllwinnerReboot),
		QualcommFlash = static_cast<int>(core::Operation::QualcommFlash),
		QualcommRaw = static_cast<int>(core::Operation::QualcommRaw),
		QualcommChipInfo = static_cast<int>(core::Operation::QualcommChipInfo),
		QualcommReset = static_cast<int>(core::Operation::QualcommReset),
		QualcommRamdump = static_cast<int>(core::Operation::QualcommRamdump),
		QualcommKickstart = static_cast<int>(core::Operation::QualcommKickstart),
		QualcommCreateZip = static_cast<int>(core::Operation::QualcommCreateZip),
		QualcommCreateArchive = static_cast<int>(core::Operation::QualcommCreateArchive),
		QualcommCreateDigests = static_cast<int>(core::Operation::QualcommCreateDigests),
		QualcommProvision = static_cast<int>(core::Operation::QualcommProvision),
		Count = static_cast<int>(core::Operation::Count)
	};
	Q_ENUM(Id)
};
QString OperationName(core::Operation operation);
}
#endif
