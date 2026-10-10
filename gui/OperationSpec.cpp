#include "gui/OperationSpec.h"
namespace gui {
QString OperationName(core::Operation operation) {
	switch (operation) {
	case core::Operation::AllwinnerFlash: return QString::fromUtf8("全志固件刷写");
	case core::Operation::AllwinnerRaw: return QString::fromUtf8("全志裸镜像刷写");
	case core::Operation::AllwinnerConvert: return QString::fromUtf8("程序器镜像转换");
	case core::Operation::AllwinnerInspect: return QString::fromUtf8("全志固件信息");
	case core::Operation::AllwinnerExtract: return QString::fromUtf8("全志固件解包");
	case core::Operation::AllwinnerDeviceInfo: return QString::fromUtf8("全志设备信息");
	case core::Operation::AllwinnerReboot: return QString::fromUtf8("全志重启设备");
	case core::Operation::FirmwareUpgrade: return QString::fromUtf8("升级固件");
	case core::Operation::FirmwareExtract: return QString::fromUtf8("解包固件");
	case core::Operation::FirmwareInfo: return QString::fromUtf8("查看固件信息");
	case core::Operation::LoaderUpgrade: return QString::fromUtf8("升级 Loader");
	case core::Operation::DownloadBoot: return QString::fromUtf8("下载 Boot");
	case core::Operation::EraseFlash: return QString::fromUtf8("擦除 Flash");
	case core::Operation::PartitionList: return QString::fromUtf8("读取分区表");
	case core::Operation::DownloadImage: return QString::fromUtf8("下载镜像");
	case core::Operation::CreateGpt: return QString::fromUtf8("生成 GPT");
	case core::Operation::SwitchDevice: return QString::fromUtf8("切换设备");
	case core::Operation::ReadSn: return QString::fromUtf8("读取 SN");
	case core::Operation::WriteSn: return QString::fromUtf8("写入 SN");
	case core::Operation::ReadComLog: return QString::fromUtf8("读取串口日志");
	case core::Operation::ReadCapability: return QString::fromUtf8("读取能力");
	case core::Operation::ReadFlashId: return QString::fromUtf8("读取 Flash ID");
	case core::Operation::ReadFlashInfo: return QString::fromUtf8("读取 Flash 信息");
	case core::Operation::ReadChipInfo: return QString::fromUtf8("读取芯片信息");
	case core::Operation::ReadOtpChip: return QString::fromUtf8("读取 OTP");
	case core::Operation::ReadSecureMode: return QString::fromUtf8("读取安全模式");
	case core::Operation::TestDevice: return QString::fromUtf8("测试设备");
	case core::Operation::ResetDevice: return QString::fromUtf8("重启设备");
	case core::Operation::ResetPipe: return QString::fromUtf8("重置管道");
	case core::Operation::ReadLba: return QString::fromUtf8("读取 LBA");
	case core::Operation::WriteLba: return QString::fromUtf8("写入 LBA");
	case core::Operation::EraseLba: return QString::fromUtf8("擦除 LBA");
	case core::Operation::WriteSector: return QString::fromUtf8("写入扇区");
	case core::Operation::EraseBlock: return QString::fromUtf8("擦除块");
	case core::Operation::RunSystem: return QString::fromUtf8("运行系统");
	case core::Operation::SwitchStorage: return QString::fromUtf8("切换存储");
	default: return QString::fromUtf8("未知操作");
	}
}
}
