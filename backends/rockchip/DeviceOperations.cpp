#include "platform/File.h"
#include "backends/rockchip/UpgradeEngine.h"
#include "backends/rockchip/FirmwareService.h"
#include "domain/DeviceSelection.h"
#include "backends/rockchip/MaskromTransition.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <exception>
#include <unistd.h>

#include "Config.h"
#include "Parameter.h"
#include "RKComm.h"
#include "RKDevice.h"
#include "RKImage.h"
#include "RKBoot.h"
#include "Upgrade.h"
#include "DeviceFactory.h"
#include "AndroidDevice.h"
#include "crc.h"

extern const char *szManufName[];

namespace core {
bool UpgradeEngine::TestDevice(STRUCT_RKDEVICE_DESC &dev)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Test Device quit,Creating comm object failed!\r\n");
		return false;
	}
	int iRet = pComm->RKU_TestDeviceReady();
	bool bSuccess = (iRet == ERR_SUCCESS);
	if (!bSuccess)
		Emit(LogLevel::Error, "Test Device Fail!\r\n");
	else
		Emit(LogLevel::Info, "Test Device OK.\r\n");
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::EnterMaskrom(STRUCT_RKDEVICE_DESC &dev)
{
    const rockchip::MaskromHooks hooks{
        [this, &dev](BYTE subcode) { return ResetDevice(dev, subcode); },
        [this] {
            std::vector<rockchip::ModeDevice> devices;
            m_pScan->Search(RKUSB_MASKROM | RKUSB_LOADER);
            for (int i = 0; i < m_pScan->GetDEVICE_COUNTS(); ++i) {
                STRUCT_RKDEVICE_DESC current{};
                if (m_pScan->GetDevice(current, i))
                    devices.push_back({current.uiLocationID, static_cast<int>(current.emUsbType)});
            }
            return devices;
        },
        [this] { return IsAborted(); },
        [this] { EmitUpgradePrompt(PROMPT_WAIT_MASKROM_START, 0); }
    };
    const auto result = rockchip::EnterMaskrom(dev.emUsbType, dev.uiLocationID,
        std::chrono::seconds(m_pScan->GetRKUSB_TIMEOUT()), hooks);
    if (result == rockchip::MaskromResult::Succeeded) {
        EmitUpgradePrompt(PROMPT_WAIT_MASKROM_SUCCESS, 0);
        return true;
    }
    if (result == rockchip::MaskromResult::Cancelled) return false;
    if (result == rockchip::MaskromResult::UnsupportedMode) {
        Emit(LogLevel::Error, "Entering Maskrom requires Loader mode");
        return Fail(ErrorCode::UnsupportedDevice);
    }
    EmitUpgradePrompt(PROMPT_WAIT_MASKROM_FAIL, 0);
    return Fail(result == rockchip::MaskromResult::TimedOut ? ErrorCode::Timeout :
                result == rockchip::MaskromResult::Ambiguous ? ErrorCode::DeviceAmbiguous : ErrorCode::OperationFailed);
}

bool UpgradeEngine::ResetDevice(STRUCT_RKDEVICE_DESC &dev, BYTE subCode)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Reset Device quit,Creating comm object failed!\r\n");
		return false;
	}
	int iRet = pComm->RKU_ResetDevice(subCode);
	bool bSuccess = (iRet == ERR_SUCCESS);
	Emit(bSuccess ? LogLevel::Info : LogLevel::Error, bSuccess ? "Reset Device OK.\r\n" : "Reset Device failed!\r\n");
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::ReadFlashId(STRUCT_RKDEVICE_DESC &dev)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Read flash ID quit,Creating comm object failed!\r\n");
		return false;
	}
	BYTE flashID[5];
	int iRet = pComm->RKU_ReadFlashID(flashID);
	bool bSuccess = (iRet == ERR_SUCCESS);
	if (bSuccess) {
		Emit(LogLevel::Info, "Flash ID:%02X %02X %02X %02X %02X \r\n", flashID[0], flashID[1], flashID[2], flashID[3], flashID[4]);
		char szId[32];
		snprintf(szId, sizeof(szId), "%02X %02X %02X %02X %02X",
		         flashID[0], flashID[1], flashID[2], flashID[3], flashID[4]);
		m_lastResult.kind = ResultKind::FlashId;
		m_lastResult.bytes.assign(flashID, flashID + 5);
		m_lastResult.items.clear();
		m_lastResult.items.push_back(ResultItem{"Flash ID", szId});
	} else {
		Emit(LogLevel::Error, "Read flash ID Fail!\r\n");
	}
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::ReadFlashInfo(STRUCT_RKDEVICE_DESC &dev)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Read flash Info quit,Creating comm object failed!\r\n");
		return false;
	}
	STRUCT_FLASHINFO_CMD info;
	UINT uiRead;
	int iRet = pComm->RKU_ReadFlashInfo((BYTE *)&info, &uiRead);
	bool bSuccess = (iRet == ERR_SUCCESS);
	if (bSuccess) {
		Emit(LogLevel::Info, "Flash Info:\r\n");
		Emit(LogLevel::Info, "\tManufacturer: %s,value=%02X\r\n",
		     info.bManufCode <= 7 ? szManufName[info.bManufCode] : "Unknown", info.bManufCode);
		Emit(LogLevel::Info, "\tFlash Size: %dMB,value=0x%x\r\n", info.uiFlashSize / 2 / 1024, info.uiFlashSize);
		Emit(LogLevel::Info, "\tBlock Size: %dKB,value=%d\r\n", info.usBlockSize / 2, info.usBlockSize);
		Emit(LogLevel::Info, "\tPage Size: %dKB,value=%d\r\n", info.bPageSize / 2, info.bPageSize);
		Emit(LogLevel::Info, "\tECC Bits: %d\r\n", info.bECCBits);
		Emit(LogLevel::Info, "\tAccess Time: %d\r\n", info.bAccessTime);
        std::string chipSelects = "    Flash CS: ";
        for (unsigned i = 0; i < 8; ++i)
            if (info.bFlashCS & (1 << i)) chipSelects += "<" + std::to_string(i) + "> ";
        Emit(LogLevel::Info, chipSelects);


		char szVal[64];
		m_lastResult.kind = ResultKind::FlashInfo;
		m_lastResult.flash.sizeSectors = info.uiFlashSize; m_lastResult.flash.blockSectors = info.usBlockSize;
		m_lastResult.flash.pageSectors = info.bPageSize; m_lastResult.flash.manufacturer = info.bManufCode;
		m_lastResult.flash.eccBits = info.bECCBits; m_lastResult.flash.accessTime = info.bAccessTime;
		m_lastResult.flash.chipSelects = info.bFlashCS;
		m_lastResult.items.clear();
		snprintf(szVal, sizeof(szVal), "%s (%02X)",
		         info.bManufCode <= 7 ? szManufName[info.bManufCode] : "Unknown", info.bManufCode);
		m_lastResult.items.push_back(ResultItem{"Manufacturer", szVal});
		snprintf(szVal, sizeof(szVal), "%dMB (0x%x)", info.uiFlashSize / 2 / 1024, info.uiFlashSize);
		m_lastResult.items.push_back(ResultItem{"Flash Size", szVal});
		snprintf(szVal, sizeof(szVal), "%dKB (%d)", info.usBlockSize / 2, info.usBlockSize);
		m_lastResult.items.push_back(ResultItem{"Block Size", szVal});
		snprintf(szVal, sizeof(szVal), "%dKB (%d)", info.bPageSize / 2, info.bPageSize);
		m_lastResult.items.push_back(ResultItem{"Page Size", szVal});
		snprintf(szVal, sizeof(szVal), "%d", info.bECCBits);
		m_lastResult.items.push_back(ResultItem{"ECC Bits", szVal});
		snprintf(szVal, sizeof(szVal), "%d", info.bAccessTime);
		m_lastResult.items.push_back(ResultItem{"Access Time", szVal});
		std::string strCs;
		for (int i = 0; i < 8; i++) {
			if (info.bFlashCS & (1 << i)) {
				snprintf(szVal, sizeof(szVal), "<%d> ", i);
				strCs += szVal;
			}
		}
		m_lastResult.items.push_back(ResultItem{"Flash CS", strCs});
	} else {
		Emit(LogLevel::Error, "Read flash Info Fail!\r\n");
	}
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::ReadChipInfo(STRUCT_RKDEVICE_DESC &dev)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Read Chip Info quit,Creating comm object failed!\r\n");
		return false;
	}
	BYTE chipInfo[16];
	int iRet = pComm->RKU_ReadChipInfo(chipInfo);
	bool bSuccess = (iRet == ERR_SUCCESS);
	if (bSuccess) {
		std::string strHex = HexList(chipInfo, 16);
		Emit(LogLevel::Info, "Chip Info: %s\r\n", strHex.c_str());
		m_lastResult.kind = ResultKind::ChipInfo;
		m_lastResult.bytes.assign(chipInfo, chipInfo + 16);
		m_lastResult.items.clear();
		m_lastResult.items.push_back(ResultItem{"Chip Info", strHex});
	} else {
		Emit(LogLevel::Error, "Read Chip Info Fail!\r\n");
	}
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::ReadCapability(STRUCT_RKDEVICE_DESC &dev, bool bPrint)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	m_bReadComLog = false; m_bSwitchStorage = false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		if (m_pLog) m_pLog->writef(core::LogLevel::Error, "Rockchip", "%s->Fail to create comm object!", "read_capability");
		if (bPrint)
			Emit(LogLevel::Error, "Read capability quit,Creating comm object failed!\r\n");
		return false;
	}
	BYTE capability[8];
	int iRet = pComm->RKU_ReadCapability(capability);
	bool bSuccess = (iRet == ERR_SUCCESS);
	if (bSuccess) {
		m_bReadComLog = (capability[0] & 0x14) != 0;
		m_bSwitchStorage = (capability[1] & 0x02) != 0;
		if (bPrint) {
			char szCap[32];
			snprintf(szCap, sizeof(szCap), "%02X %02X %02X %02X %02X %02X %02X %02X",
			         capability[0], capability[1], capability[2], capability[3],
			         capability[4], capability[5], capability[6], capability[7]);
			Emit(LogLevel::Info, "Capability:%02X %02X %02X %02X %02X %02X %02X %02X \r\n",
			     capability[0], capability[1], capability[2], capability[3],
			     capability[4], capability[5], capability[6], capability[7]);
			m_lastResult.kind = ResultKind::Capability;
			m_lastResult.bytes.assign(capability, capability + 8);
			m_lastResult.items.clear();
			m_lastResult.items.push_back(ResultItem{"Capability", szCap});
			if (capability[0] & 0x01) { Emit(LogLevel::Info, "Direct LBA:\tenabled\r\n"); m_lastResult.items.push_back(ResultItem{"Direct LBA", "enabled"}); }
			if (capability[0] & 0x02) { Emit(LogLevel::Info, "Vendor Storage:\tenabled\r\n"); m_lastResult.items.push_back(ResultItem{"Vendor Storage", "enabled"}); }
			if (capability[0] & 0x04) { Emit(LogLevel::Info, "First 4m Access:\tenabled\r\n"); m_lastResult.items.push_back(ResultItem{"First 4m Access", "enabled"}); }
			if (capability[0] & 0x08) { Emit(LogLevel::Info, "Read LBA:\tenabled\r\n"); m_lastResult.items.push_back(ResultItem{"Read LBA", "enabled"}); }
			if (capability[0] & 0x14) { Emit(LogLevel::Info, "Read Com Log:\tenabled\r\n"); m_lastResult.items.push_back(ResultItem{"Read Com Log", "enabled"}); }
			if (capability[0] & 0x28) { Emit(LogLevel::Info, "Read IDB Config:\tenabled\r\n"); m_lastResult.items.push_back(ResultItem{"Read IDB Config", "enabled"}); }
			if (capability[0] & 0x50) { Emit(LogLevel::Info, "Read Secure Mode:\tenabled\r\n"); m_lastResult.items.push_back(ResultItem{"Read Secure Mode", "enabled"}); }
			if (capability[1] & 0x01) { Emit(LogLevel::Info, "New IDB:\tenabled\r\n"); m_lastResult.items.push_back(ResultItem{"New IDB", "enabled"}); }
			if (capability[1] & 0x02) { Emit(LogLevel::Info, "Switch Storage:\tenabled\r\n"); m_lastResult.items.push_back(ResultItem{"Switch Storage", "enabled"}); }
			if (capability[1] & 0x04) { Emit(LogLevel::Info, "LBA Parity:\tenabled\r\n"); m_lastResult.items.push_back(ResultItem{"LBA Parity", "enabled"}); }
			if (capability[1] & 0x08) { Emit(LogLevel::Info, "Read OTP Chip:\tenabled\r\n"); m_lastResult.items.push_back(ResultItem{"Read OTP Chip", "enabled"}); }
		}
	} else {
		if (m_pLog) m_pLog->writef(core::LogLevel::Error, "Rockchip", "%s->Fail to read capability,err=%d", "read_capability", iRet);
		if (bPrint)
			Emit(LogLevel::Error, "Read capability Fail!\r\n");
	}
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::ReadOtpChip(STRUCT_RKDEVICE_DESC &dev)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Read OTP Chip quit,Creating comm object failed!\r\n");
		return false;
	}
	BYTE otp[32];
	int iRet = pComm->RKU_ReadOtpChip(otp);
	bool bSuccess = (iRet == ERR_SUCCESS);
	if (bSuccess) {
		if (otp[0] != 'R' || otp[1] != 'K') {
			Emit(LogLevel::Error, "OTP chip tag Invalid!\r\n");
			delete pComm;
			return false;
		}
		Emit(LogLevel::Info, "OTP Info: %s\r\n", HexList(otp, 32).c_str());
		m_lastResult.bytes.assign(otp, otp + 32);
		m_lastResult.kind = ResultKind::Generic;
		m_lastResult.items.push_back(ResultItem{"OTP", HexList(otp, 32)});
	} else {
		Emit(LogLevel::Error, "Read OTP chip Fail!\r\n");
	}
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::ReadComLog(STRUCT_RKDEVICE_DESC &dev, char *szFile)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	if (!ReadCapability(dev, false)) return false;

	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		if (m_pLog) m_pLog->writef(core::LogLevel::Error, "Rockchip", "%s->Fail to create comm object", "read_com_log");
		Emit(LogLevel::Error, "Read Com Log quit,Creating comm object failed!");
		return false;
	}

	FILE *file = NULL;
	if (szFile) {
		file = platform::OpenFile(szFile, "wb+");
		if (!file) {
			if (m_pLog) m_pLog->writef(core::LogLevel::Error, "Rockchip", "%s->Fail to create %s,err=%d",
			               "read_com_log", szFile, errno);
			Emit(LogLevel::Error, "Read Com Log failed,err=%d,can't open file: %s", errno, szFile);
			delete pComm;
			return false;
		}
	}

	if (!m_bReadComLog) {
		Fail(ErrorCode::UnsupportedDevice);
		Emit(LogLevel::Error, "Device does not support reading communication logs");
		delete pComm;
		if (file)
			fclose(file);
		return false;
	}

	BYTE pBuf[0x20000];
	int iRet = pComm->RKU_ReadComData(pBuf, 0x80);
	if (iRet != ERR_SUCCESS) {
		if (m_pLog) m_pLog->writef(core::LogLevel::Error, "Rockchip", "%s->Fail to read com log,err=%d", "read_com_log", iRet);
		Emit(LogLevel::Error, "Error:Read Com Log failed!");
		delete pComm;
		if (file)
			fclose(file);
		return false;
	}

	if (*(DWORD *)pBuf == 0x55424544) {
		DWORD dwSize = *(DWORD *)(pBuf + 4);
		if (file)
			fwrite(pBuf + 8, 1, dwSize - 8, file);
		Emit(LogLevel::Info, "Reading com Log ok,save at %s\r\n", szFile);
		delete pComm;
		if (file)
			fclose(file);
		return true;
	}

	if (m_pLog) m_pLog->writef(core::LogLevel::Error, "Rockchip", "%s->Log format is invalid", "read_com_log");
	Emit(LogLevel::Error, "Error:Format of log is wrong!");
	delete pComm;
	if (file)
		fclose(file);
	return false;
}

bool UpgradeEngine::ReadVendorSn(STRUCT_RKDEVICE_DESC &dev)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Read Sn quit,Creating comm object failed!\r\n");
		return false;
	}
	BYTE sn[512];
	memset(sn, 0, sizeof(sn));
	*(USHORT *)(sn) = 1;
	*(USHORT *)(sn + 4) = sizeof(sn);
	int iRet = pComm->RKU_ReadLBA(0xfff00001, 1, sn, RWMETHOD_LBA);
	bool bSuccess = (iRet == ERR_SUCCESS);
	if (bSuccess) {
		Emit(LogLevel::Info, "Vendor SN:%s\r\n", (char *)(sn + 6));
		m_lastResult.kind = ResultKind::Sn;
		m_lastResult.serial.assign(reinterpret_cast<char *>(sn + 6), strnlen(reinterpret_cast<char *>(sn + 6), sizeof(sn) - 6));
		m_lastResult.items.clear();
		m_lastResult.items.push_back(ResultItem{"Vendor SN", m_lastResult.serial});
	} else {
		Emit(LogLevel::Error, "failed to read sn,err=%d!\r\n", iRet);
	}
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::WriteVendorSn(STRUCT_RKDEVICE_DESC &dev, char *szSn)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Write Sn quit,Creating comm object failed!\r\n");
		return false;
	}
	BYTE sn[512];
	memset(sn, 0, sizeof(sn));
	*(USHORT *)(sn) = 1;
	*(USHORT *)(sn + 4) = sizeof(sn);
	strncpy((char *)(sn + 6), szSn, sizeof(sn) - 7);
	int iRet = pComm->RKU_WriteLBA(0xfff00001, 1, sn, RWMETHOD_LBA);
	bool bSuccess = (iRet == ERR_SUCCESS);
	if (bSuccess)
		Emit(LogLevel::Info, "Write Sn succeeded.\r\n");
	else
		Emit(LogLevel::Error, "failed to write sn,err=%d!\r\n", iRet);
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::ReadSecureMode(STRUCT_RKDEVICE_DESC &dev)
{
	if (!CheckDeviceType(dev, RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Read Secure Mode quit,Creating comm object failed!\r\n");
		if (m_pLog) m_pLog->writef(core::LogLevel::Error, "Rockchip", "%s->Fail to create comm object!", "read_secure_mode");
		return false;
	}
	BYTE capability[8];
	int iRet = pComm->RKU_ReadCapability(capability);
	if (iRet != ERR_SUCCESS) {
		if (m_pLog) m_pLog->writef(core::LogLevel::Error, "Rockchip", "%s->Fail to read capability,err=%d", "read_secure_mode", iRet);
		Emit(LogLevel::Error, "Read Capability Fail!\r\n");
		delete pComm;
		return false;
	}
	if (!(capability[0] & 0x80)) {
		Emit(LogLevel::Error, "Device did not support to read secure mode!\r\n");
		if (m_pLog) m_pLog->writef(core::LogLevel::Error, "Rockchip", "%s->can't support to read secure mode!", "read_secure_mode");
		delete pComm;
		return false;
	}
	BYTE bMode = 0;
	iRet = pComm->RKU_ReadNewEfuse(0, 0, &bMode, 2);
	if (iRet != ERR_SUCCESS) {
		if (m_pLog) m_pLog->writef(core::LogLevel::Error, "Rockchip", "%s->Fail to read new efuse,err=%d", "read_secure_mode", iRet);
		Emit(LogLevel::Error, "Read Secure Mode Fail!\r\n");
		delete pComm;
		return false;
	}
	if (bMode)
		Emit(LogLevel::Info, "Device is in the Secure Mode.\r\n");
	else
		Emit(LogLevel::Info, "Device is in the Normal Mode.\r\n");
	m_lastResult.kind = ResultKind::SecureMode;
	m_lastResult.secureMode = bMode;
	m_lastResult.items.clear();
	m_lastResult.items.push_back(ResultItem{"Secure Mode",
	                                       bMode ? "Secure Mode" : "Normal Mode"});
	delete pComm;
	return true;
}

bool UpgradeEngine::SwitchDevice(STRUCT_RKDEVICE_DESC &dev)
{
	if (!CheckDeviceType(dev, RKUSB_MSC))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Switch Device quit,Creating comm object failed!\r\n");
		return false;
	}
	int iRet;
	if (dev.usPid == 0x16)
		iRet = pComm->RKU_UvcToRockusb();
	else
		iRet = pComm->RKU_MscToRockusb();
	bool bSuccess = (iRet == ERR_SUCCESS);
	if (bSuccess)
		Emit(LogLevel::Info, "Switch Device OK.\r\n");
	else {
		if (m_pLog) m_pLog->writef(core::LogLevel::Error, "Rockchip", "%s->Fail to switch,err=%d!", "switch_device", iRet);
		Emit(LogLevel::Error, "Switch Device Fail!\r\n");
	}
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::ResetPipe(STRUCT_RKDEVICE_DESC &dev, BYTE ucPipe)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Reset Pipe quit, creating comm object failed!");
		return false;
	}
	int iRet = pComm->RKU_ResetPipe(ucPipe);
	bool bSuccess = (iRet == ERR_SUCCESS);
	Emit(bSuccess ? LogLevel::Info : LogLevel::Error, bSuccess ? "Reset Pipe OK.\r\n" : "Reset Pipe failed!\r\n");
	delete pComm;
	return bSuccess;
}
}
