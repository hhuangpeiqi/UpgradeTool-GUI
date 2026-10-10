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
	case core::Operation::AllwinnerFlash: return Tr(QT_TRANSLATE_NOOP("Operations", "全志固件刷写"));
	case core::Operation::AllwinnerRaw: return Tr(QT_TRANSLATE_NOOP("Operations", "全志裸镜像刷写"));
	case core::Operation::AllwinnerConvert: return Tr(QT_TRANSLATE_NOOP("Operations", "程序器镜像转换"));
	case core::Operation::AllwinnerInspect: return Tr(QT_TRANSLATE_NOOP("Operations", "全志固件信息"));
	case core::Operation::AllwinnerExtract: return Tr(QT_TRANSLATE_NOOP("Operations", "全志固件解包"));
	case core::Operation::AllwinnerDeviceInfo: return Tr(QT_TRANSLATE_NOOP("Operations", "全志设备信息"));
	case core::Operation::AllwinnerReboot: return Tr(QT_TRANSLATE_NOOP("Operations", "全志重启设备"));
	case core::Operation::FirmwareUpgrade: return Tr(QT_TRANSLATE_NOOP("Operations", "升级固件"));
	case core::Operation::FirmwareExtract: return Tr(QT_TRANSLATE_NOOP("Operations", "解包固件"));
	case core::Operation::FirmwareInfo: return Tr(QT_TRANSLATE_NOOP("Operations", "查看固件信息"));
	case core::Operation::LoaderUpgrade: return Tr(QT_TRANSLATE_NOOP("Operations", "升级 Loader"));
	case core::Operation::DownloadBoot: return Tr(QT_TRANSLATE_NOOP("Operations", "下载 Boot"));
	case core::Operation::EraseFlash: return Tr(QT_TRANSLATE_NOOP("Operations", "擦除 Flash"));
	case core::Operation::PartitionList: return Tr(QT_TRANSLATE_NOOP("Operations", "读取分区表"));
	case core::Operation::DownloadImage: return Tr(QT_TRANSLATE_NOOP("Operations", "下载镜像"));
	case core::Operation::CreateGpt: return Tr(QT_TRANSLATE_NOOP("Operations", "生成 GPT"));
	case core::Operation::SwitchDevice: return Tr(QT_TRANSLATE_NOOP("Operations", "切换设备"));
	case core::Operation::ReadSn: return Tr(QT_TRANSLATE_NOOP("Operations", "读取 SN"));
	case core::Operation::WriteSn: return Tr(QT_TRANSLATE_NOOP("Operations", "写入 SN"));
	case core::Operation::ReadComLog: return Tr(QT_TRANSLATE_NOOP("Operations", "读取串口日志"));
	case core::Operation::ReadCapability: return Tr(QT_TRANSLATE_NOOP("Operations", "读取能力"));
	case core::Operation::ReadFlashId: return Tr(QT_TRANSLATE_NOOP("Operations", "读取 Flash ID"));
	case core::Operation::ReadFlashInfo: return Tr(QT_TRANSLATE_NOOP("Operations", "读取 Flash 信息"));
	case core::Operation::ReadChipInfo: return Tr(QT_TRANSLATE_NOOP("Operations", "读取芯片信息"));
	case core::Operation::ReadOtpChip: return Tr(QT_TRANSLATE_NOOP("Operations", "读取 OTP"));
	case core::Operation::ReadSecureMode: return Tr(QT_TRANSLATE_NOOP("Operations", "读取安全模式"));
	case core::Operation::TestDevice: return Tr(QT_TRANSLATE_NOOP("Operations", "测试设备"));
	case core::Operation::ResetDevice: return Tr(QT_TRANSLATE_NOOP("Operations", "重启设备"));
	case core::Operation::ResetPipe: return Tr(QT_TRANSLATE_NOOP("Operations", "重置管道"));
	case core::Operation::ReadLba: return Tr(QT_TRANSLATE_NOOP("Operations", "读取 LBA"));
	case core::Operation::WriteLba: return Tr(QT_TRANSLATE_NOOP("Operations", "写入 LBA"));
	case core::Operation::EraseLba: return Tr(QT_TRANSLATE_NOOP("Operations", "擦除 LBA"));
	case core::Operation::WriteSector: return Tr(QT_TRANSLATE_NOOP("Operations", "写入扇区"));
	case core::Operation::EraseBlock: return Tr(QT_TRANSLATE_NOOP("Operations", "擦除块"));
	case core::Operation::RunSystem: return Tr(QT_TRANSLATE_NOOP("Operations", "运行系统"));
	case core::Operation::SwitchStorage: return Tr(QT_TRANSLATE_NOOP("Operations", "切换存储"));
	default: return Tr(QT_TRANSLATE_NOOP("Operations", "未知操作"));
	}
}

}
