#include "gui/OperationSpec.h"
#include "gui/MessageDisplay.h"

#include <QCoreApplication>

namespace gui {

core::Message OperationMessage(core::Operation operation) {
	switch (operation) {
	case core::Operation::EnterMaskrom: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Enter Maskrom"), {}};
	case core::Operation::QualcommFlash: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Download"), {}};
	case core::Operation::QualcommRaw: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Partition Operations"), {}};
	case core::Operation::QualcommChipInfo: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Chip Info"), {}};
	case core::Operation::QualcommReset: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Reset"), {}};
	case core::Operation::QualcommRamdump: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "RAM Dump"), {}};
	case core::Operation::QualcommKickstart: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Sahara Kickstart"), {}};
	case core::Operation::QualcommCreateZip: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Create ZIP"), {}};
	case core::Operation::QualcommCreateArchive: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Create Sahara Archive"), {}};
	case core::Operation::QualcommCreateDigests: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Create VIP Digests"), {}};
	case core::Operation::QualcommProvision: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "UFS Provisioning"), {}};

	case core::Operation::AllwinnerFlash: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Allwinner Firmware Flash"), {}};
	case core::Operation::AllwinnerRaw: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Allwinner Raw Image Flash"), {}};
	case core::Operation::AllwinnerConvert: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Programmer Image Convert"), {}};
	case core::Operation::AllwinnerInspect: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Allwinner Firmware Info"), {}};
	case core::Operation::AllwinnerExtract: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Allwinner Firmware Extract"), {}};
	case core::Operation::AllwinnerDeviceInfo: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Allwinner Device Info"), {}};
	case core::Operation::AllwinnerReboot: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Allwinner Reboot Device"), {}};
	case core::Operation::FirmwareUpgrade: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Upgrade Firmware"), {}};
	case core::Operation::FirmwareExtract: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Extract Firmware"), {}};
	case core::Operation::FirmwareInfo: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "View Firmware Info"), {}};
	case core::Operation::LoaderUpgrade: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Upgrade Loader"), {}};
	case core::Operation::DownloadBoot: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Download Boot"), {}};
	case core::Operation::EraseFlash: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Erase Flash"), {}};
	case core::Operation::PartitionList: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Read Partition Table"), {}};
	case core::Operation::DownloadImage: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Download Image"), {}};
	case core::Operation::CreateGpt: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Generate GPT"), {}};
	case core::Operation::SwitchDevice: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Switch Device"), {}};
	case core::Operation::ReadSn: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Read SN"), {}};
	case core::Operation::WriteSn: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Write SN"), {}};
	case core::Operation::ReadComLog: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Read COM Log"), {}};
	case core::Operation::ReadCapability: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Read Capability"), {}};
	case core::Operation::ReadFlashId: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Read Flash ID"), {}};
	case core::Operation::ReadFlashInfo: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Reading Flash info"), {}};
	case core::Operation::ReadChipInfo: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Read Chip Info"), {}};
	case core::Operation::ReadOtpChip: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Read OTP"), {}};
	case core::Operation::ReadSecureMode: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Read Secure Mode"), {}};
	case core::Operation::TestDevice: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Test Device"), {}};
	case core::Operation::ResetDevice: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Reset Device"), {}};
	case core::Operation::ResetPipe: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Reset Pipe"), {}};
	case core::Operation::ReadLba: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Read LBA"), {}};
	case core::Operation::WriteLba: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Write LBA"), {}};
	case core::Operation::EraseLba: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Erase LBA"), {}};
	case core::Operation::WriteSector: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Write Sector"), {}};
	case core::Operation::EraseBlock: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Erase Block"), {}};
	case core::Operation::RunSystem: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Run System"), {}};
	case core::Operation::SwitchStorage: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Switch Storage"), {}};
	default: return core::Message{"Operations", QT_TRANSLATE_NOOP("Operations", "Unknown Operation"), {}};
	}
}

QString OperationName(core::Operation operation) { return Display(OperationMessage(operation)); }

}
