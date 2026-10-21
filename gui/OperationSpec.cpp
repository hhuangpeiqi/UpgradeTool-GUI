#include "gui/OperationSpec.h"

#include <QCoreApplication>

namespace gui {

namespace {
QString Tr(const char *szText)
{
	return QCoreApplication::translate("Operations", szText);
}
}

QString OperationName(core::Operation operation) {
	switch (operation) {
	case core::Operation::EnterMaskrom: return Tr(QT_TRANSLATE_NOOP("Operations", "Enter Maskrom"));
	case core::Operation::QualcommFlash: return Tr(QT_TRANSLATE_NOOP("Operations", "Download"));
	case core::Operation::QualcommRaw: return Tr(QT_TRANSLATE_NOOP("Operations", "Partition Operations"));
	case core::Operation::QualcommChipInfo: return Tr(QT_TRANSLATE_NOOP("Operations", "Chip Info"));
	case core::Operation::QualcommReset: return Tr(QT_TRANSLATE_NOOP("Operations", "Reset"));
	case core::Operation::QualcommRamdump: return Tr(QT_TRANSLATE_NOOP("Operations", "RAM Dump"));
	case core::Operation::QualcommKickstart: return Tr(QT_TRANSLATE_NOOP("Operations", "Sahara Kickstart"));
	case core::Operation::QualcommCreateZip: return Tr(QT_TRANSLATE_NOOP("Operations", "Create ZIP"));
	case core::Operation::QualcommCreateArchive: return Tr(QT_TRANSLATE_NOOP("Operations", "Create Sahara Archive"));
	case core::Operation::QualcommCreateDigests: return Tr(QT_TRANSLATE_NOOP("Operations", "Create VIP Digests"));
	case core::Operation::QualcommProvision: return Tr(QT_TRANSLATE_NOOP("Operations", "UFS Provisioning"));

	case core::Operation::AllwinnerFlash: return Tr(QT_TRANSLATE_NOOP("Operations", "Allwinner Firmware Flash"));
	case core::Operation::AllwinnerRaw: return Tr(QT_TRANSLATE_NOOP("Operations", "Allwinner Raw Image Flash"));
	case core::Operation::AllwinnerConvert: return Tr(QT_TRANSLATE_NOOP("Operations", "Programmer Image Convert"));
	case core::Operation::AllwinnerInspect: return Tr(QT_TRANSLATE_NOOP("Operations", "Allwinner Firmware Info"));
	case core::Operation::AllwinnerExtract: return Tr(QT_TRANSLATE_NOOP("Operations", "Allwinner Firmware Extract"));
	case core::Operation::AllwinnerDeviceInfo: return Tr(QT_TRANSLATE_NOOP("Operations", "Allwinner Device Info"));
	case core::Operation::AllwinnerReboot: return Tr(QT_TRANSLATE_NOOP("Operations", "Allwinner Reboot Device"));
	case core::Operation::FirmwareUpgrade: return Tr(QT_TRANSLATE_NOOP("Operations", "Upgrade Firmware"));
	case core::Operation::FirmwareExtract: return Tr(QT_TRANSLATE_NOOP("Operations", "Extract Firmware"));
	case core::Operation::FirmwareInfo: return Tr(QT_TRANSLATE_NOOP("Operations", "View Firmware Info"));
	case core::Operation::LoaderUpgrade: return Tr(QT_TRANSLATE_NOOP("Operations", "Upgrade Loader"));
	case core::Operation::DownloadBoot: return Tr(QT_TRANSLATE_NOOP("Operations", "Download Boot"));
	case core::Operation::EraseFlash: return Tr(QT_TRANSLATE_NOOP("Operations", "Erase Flash"));
	case core::Operation::PartitionList: return Tr(QT_TRANSLATE_NOOP("Operations", "Read Partition Table"));
	case core::Operation::DownloadImage: return Tr(QT_TRANSLATE_NOOP("Operations", "Download Image"));
	case core::Operation::CreateGpt: return Tr(QT_TRANSLATE_NOOP("Operations", "Generate GPT"));
	case core::Operation::SwitchDevice: return Tr(QT_TRANSLATE_NOOP("Operations", "Switch Device"));
	case core::Operation::ReadSn: return Tr(QT_TRANSLATE_NOOP("Operations", "Read SN"));
	case core::Operation::WriteSn: return Tr(QT_TRANSLATE_NOOP("Operations", "Write SN"));
	case core::Operation::ReadComLog: return Tr(QT_TRANSLATE_NOOP("Operations", "Read COM Log"));
	case core::Operation::ReadCapability: return Tr(QT_TRANSLATE_NOOP("Operations", "Read Capability"));
	case core::Operation::ReadFlashId: return Tr(QT_TRANSLATE_NOOP("Operations", "Read Flash ID"));
	case core::Operation::ReadFlashInfo: return Tr(QT_TRANSLATE_NOOP("Operations", "Reading Flash info"));
	case core::Operation::ReadChipInfo: return Tr(QT_TRANSLATE_NOOP("Operations", "Read Chip Info"));
	case core::Operation::ReadOtpChip: return Tr(QT_TRANSLATE_NOOP("Operations", "Read OTP"));
	case core::Operation::ReadSecureMode: return Tr(QT_TRANSLATE_NOOP("Operations", "Read Secure Mode"));
	case core::Operation::TestDevice: return Tr(QT_TRANSLATE_NOOP("Operations", "Test Device"));
	case core::Operation::ResetDevice: return Tr(QT_TRANSLATE_NOOP("Operations", "Reset Device"));
	case core::Operation::ResetPipe: return Tr(QT_TRANSLATE_NOOP("Operations", "Reset Pipe"));
	case core::Operation::ReadLba: return Tr(QT_TRANSLATE_NOOP("Operations", "Read LBA"));
	case core::Operation::WriteLba: return Tr(QT_TRANSLATE_NOOP("Operations", "Write LBA"));
	case core::Operation::EraseLba: return Tr(QT_TRANSLATE_NOOP("Operations", "Erase LBA"));
	case core::Operation::WriteSector: return Tr(QT_TRANSLATE_NOOP("Operations", "Write Sector"));
	case core::Operation::EraseBlock: return Tr(QT_TRANSLATE_NOOP("Operations", "Erase Block"));
	case core::Operation::RunSystem: return Tr(QT_TRANSLATE_NOOP("Operations", "Run System"));
	case core::Operation::SwitchStorage: return Tr(QT_TRANSLATE_NOOP("Operations", "Switch Storage"));
	default: return Tr(QT_TRANSLATE_NOOP("Operations", "Unknown Operation"));
	}
}

}
