#include "core/UpgradeEngine.h"
#include "core/DeviceSelection.h"

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

static thread_local UpgradeEngine *g_pEngine = NULL;

// 把引擎原始输出写入日志文件（界面侧不再显示），供排查使用。
static void RecordRawToLog(CRKLog *pLog, const std::string &strText)
{
	if (!pLog)
		return;
	std::string str = strText;
	while (!str.empty() && (str.back() == '\n' || str.back() == '\r'))
		str.pop_back();
	if (!str.empty())
		pLog->Record("%s", str.c_str());
}

UpgradeEngine::UpgradeEngine()
	: 	  m_pObserver(NULL),
	  m_pLog(NULL),
	  m_pScan(NULL),
	  m_bReadComLog(false),
	  m_bSwitchStorage(false),
	  m_uiProgTotal(0),
	  m_uiProgDone(0),
	  m_iProgLast(-1),
	  m_bProgActive(false),
	  m_bProgFirst(true),
	  m_bDlFirst(true),
	  m_iDlLast(-1),
	  m_bCaptureResultText(false)
{
}

UpgradeEngine::~UpgradeEngine()
{
}

void UpgradeEngine::SetObserver(IUpgradeObserver *pObserver)
{
	m_pObserver = pObserver;
}

IUpgradeObserver *UpgradeEngine::GetObserver() const
{
	return m_pObserver;
}

void UpgradeEngine::RequestAbort()
{
	if (m_cancelled) m_cancelled->store(true);
	CRKDevice::RequestAbort();
}

bool UpgradeEngine::IsAborted() const
{
	return (m_cancelled && m_cancelled->load()) || CRKDevice::IsAborted();
}

void UpgradeEngine::SetLogObject(CRKLog *pLog)
{
	m_pLog = pLog;
}

CRKLog *UpgradeEngine::GetLogObject() const
{
	return m_pLog;
}

void UpgradeEngine::SetScan(CRKScan *pScan)
{
	m_pScan = pScan;
}

CRKScan *UpgradeEngine::GetScan() const
{
	return m_pScan;
}

void UpgradeEngine::ResetProgress(long long llTotal)
{
	m_uiProgTotal = llTotal;
	m_uiProgDone = 0;
	m_iProgLast = -1;
	m_bProgActive = (llTotal > 0);
	m_bProgFirst = true;
}

void UpgradeEngine::StepProgress(long long llBytes)
{
	if (!m_bProgActive || m_uiProgTotal == 0)
		return;
	m_uiProgDone += llBytes;
	int iPct = (int)(m_uiProgDone * 100 / m_uiProgTotal);
	if (iPct > 100) iPct = 100;
	if (iPct == 0 || iPct == m_iProgLast)
		return;
	m_iProgLast = iPct;
	EmitTransferProgress(TransferStage::Write, m_uiProgTotal, m_uiProgDone);
	m_bProgFirst = false;
}

void UpgradeEngine::ResetDownloadProgress()
{
	m_bDlFirst = true;
	m_iDlLast = -1;
}

void UpgradeEngine::SetDownloadProgress(long long llTotal, long long llCurrent)
{
	if (llTotal <= 0)
		return;
	OnDeviceProgress(DOWNLOADIMAGE_PROGRESS, llTotal, llCurrent,
	                 m_bDlFirst ? CALL_FIRST : CALL_MIDDLE);
}

void UpgradeEngine::OnDeviceProgress(ENUM_PROGRESS_PROMPT emPrompt, long long llTotal,
                                     long long llCurrent, ENUM_CALL_STEP emCall)
{
	if (llTotal <= 0)
		return;
	if (emPrompt != m_lastProgressPrompt || emCall == CALL_FIRST) {
		m_lastProgressPrompt = emPrompt; m_iDlLast = -1; m_bDlFirst = true;
	}
	int iPct = (int)(llCurrent * 100 / llTotal);
	if (iPct > 100) iPct = 100;
	if (iPct == 0 || iPct == m_iDlLast)
		return;
	m_iDlLast = iPct;
	EmitProgress(emPrompt, llTotal, llCurrent,
	             m_bDlFirst ? CALL_FIRST : emCall);
	m_bDlFirst = false;
	if (CRKDevice::IsAborted())
		Emit(LogLevel::Error, "Upgrade aborted by user!");
}

void UpgradeEngine::ProgressThunk(UINT deviceLayer, ENUM_PROGRESS_PROMPT emPrompt,
                                  long long llTotal, long long llCurrent, ENUM_CALL_STEP emCall)
{
	(void)deviceLayer;
	(void)emCall;
	if (!g_pEngine)
		return;
	g_pEngine->OnDeviceProgress(emPrompt, llTotal, llCurrent, emCall);
}

void UpgradeEngine::EmitTransferProgress(TransferStage stage, long long total, long long current)
{
	if (m_pObserver) m_pObserver->OnTransferProgress(stage, total, current);
}

void UpgradeEngine::PromptThunk(UINT deviceLayer, ENUM_UPGRADE_PROMPT emPrompt, UINT uiParam)
{
	(void)deviceLayer;
	if (!g_pEngine)
		return;
	g_pEngine->EmitUpgradePrompt(emPrompt, uiParam);
}

void UpgradeEngine::Emit(LogLevel emLevel, const char *lpFmt, ...)
{
	char szBuf[1024];
	va_list args;
	va_start(args, lpFmt);
	vsnprintf(szBuf, sizeof(szBuf) - 1, lpFmt, args);
	va_end(args);
	szBuf[sizeof(szBuf) - 1] = 0x00;
	Emit(emLevel, std::string(szBuf));
}

void UpgradeEngine::Emit(LogLevel emLevel, const std::string &strText)
{
	if (m_bCaptureResultText && m_lastResult.diagnostic.size() < 65536)
		m_lastResult.diagnostic += strText;
	RecordRawToLog(m_pLog, strText);
	if (m_pObserver)
		m_pObserver->OnOutput(emLevel, strText);
}

void UpgradeEngine::EmitProgress(ENUM_PROGRESS_PROMPT emPrompt, long long llTotal,
                                 long long llCurrent, ENUM_CALL_STEP emCall)
{
	if (m_pObserver)
		m_pObserver->OnProgress(emPrompt, llTotal, llCurrent, emCall);
}

void UpgradeEngine::EmitResult(const OperationResult &result)
{
	if (m_pObserver)
		m_pObserver->OnResult(result);
}

void UpgradeEngine::EmitUpgradePrompt(ENUM_UPGRADE_PROMPT emPrompt, unsigned int uiParam)
{
	if (m_pObserver)
		m_pObserver->OnUpgradePrompt(emPrompt, uiParam);
}

int UpgradeEngine::SelectStorage(const std::vector<std::string> &vecOptions, int iCurrent)
{
	if (!m_pObserver)
		return 0;
	return m_pObserver->OnSelectStorage(vecOptions, iCurrent);
}

DWORD UpgradeEngine::GetFileSize(char *szFile)
{
	FILE *file = fopen(szFile, "rb");
	if (!file) {
		if (m_pLog) m_pLog->Record("%s failed,err=%d,can't open file: %s\r\n",
			               "get_file_size", errno, szFile);
		return (DWORD)-1;
	}
	fseeko(file, 0, SEEK_END);
	long long llSize = ftello(file);
	fclose(file);
	return (DWORD)llSize;
}

std::string UpgradeEngine::HexList(PBYTE pData, int nSize)
{
	char szVal[8];
	std::string str;
	for (int i = 0; i < nSize; i++) {
		sprintf(szVal, i ? " %X" : "%X", pData[i]);
		str += szVal;
	}
	return str;
}

void UpgradeEngine::PrintData(PBYTE pData, int nSize)
{
	char szPrint[17] = "\0";
	int i;
	for (i = 0; i < nSize; i++) {
		if (i % 16 == 0) {
			if (i / 16 > 0)
				Emit(LogLevel::Info, "     %s\r\n", szPrint);
			Emit(LogLevel::Info, "%08d ", i / 16);
		}
		Emit(LogLevel::Info, "%02X ", pData[i]);
		szPrint[i % 16] = isprint(pData[i]) ? pData[i] : '.';
	}
	if (i / 16 > 0)
		Emit(LogLevel::Info, "     %s\r\n", szPrint);
}

std::string UpgradeEngine::GetErrString(UINT uiErr)
{
	switch (uiErr) {
	case 101: return "Note:Communication issues,Certainly please check usb,Completely please check device";
	case 105: return "Note:read chip info by rkdevtool and use new chip to create firmware";
	case 110: return "Note:device did not respond and was disconnected,please reset device and retry";
	case 113: return "Note:please check ddr,please reset device and retry";
	case 114: return "Note:please check usb,please reset device and retry";
	case 115: return "Note:please check flash if in the support list or welding issue";
	case 117: return "Note:please update the tool for creating firmware,and regenerate firmware";
	case 120: return "Note:Position of writing image is front of idblock";
	case 122: return "Note:Image is larger than partition size in the firmware";
	case 123: return "Note:Definition of partition in the firmware is larger than flash";
	case 201: return "Note:please check firmware if exist and occupied";
	case 203: return "Note:invalid tag of firmware,please check firmware";
	case 204: return "Note:wrong hash of firmware,please check firmware";
	case 208: return "Note:wrong hash of loader,please check loader";
	case 209: return "Note:invalid tag of loader,please check loader";
	default: return "";
	}
}

bool UpgradeEngine::CheckDeviceType(STRUCT_RKDEVICE_DESC &dev, UINT uiSupportType)
{
	if (((UINT)dev.emUsbType & uiSupportType) == (UINT)dev.emUsbType)
		return true;
	Emit(LogLevel::Error, "The  Device did not support this operation!");
	return Fail(ErrorCode::UnsupportedDevice);
}

std::vector<UpgradeEngine::DeviceInfo> UpgradeEngine::ScanDevices()
{
	std::vector<DeviceInfo> vecDevices;
	STRUCT_RKDEVICE_DESC desc;
	int cnt = m_pScan->DEVICE_COUNTS;
	for (int i = 0; i < cnt; i++) {
		m_pScan->GetDevice(desc, i);
		DeviceInfo info;
		info.iIndex = i + 1;
		info.uiVid = desc.usVid;
		info.uiPid = desc.usPid;
		info.uiLocationID = desc.uiLocationID;
		info.iUsbType = (int)desc.emUsbType;
		if (desc.emUsbType == RKUSB_MASKROM)
			info.strMode = "Maskrom";
		else if (desc.emUsbType == RKUSB_LOADER)
			info.strMode = "Loader";
		else
			info.strMode = "Unknown";
		libusb_device *pUsbDev = (libusb_device *)desc.pUsbHandle;
		libusb_device_handle *pUsbHandle = NULL;
		if (pUsbDev) {
			if (libusb_open(pUsbDev, &pUsbHandle) == 0) {
				struct libusb_device_descriptor usbDesc;
				if (libusb_get_device_descriptor(pUsbDev, &usbDesc) == 0 && usbDesc.iSerialNumber) {
					unsigned char szSerial[128] = {0};
					if (libusb_get_string_descriptor_ascii(pUsbHandle, usbDesc.iSerialNumber,
					                                       szSerial, sizeof(szSerial) - 1) > 0)
						info.strSerial = (char *)szSerial;
				}
				libusb_close(pUsbHandle);
			}
		}
		vecDevices.push_back(info);
	}
	return vecDevices;
}

bool UpgradeEngine::TestDevice(STRUCT_RKDEVICE_DESC &dev)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Info, "Test Device quit,Creating comm object failed!\r\n");
		return false;
	}
	int iRet = pComm->RKU_TestDeviceReady();
	bool bSuccess = (iRet == ERR_SUCCESS);
	if (!bSuccess)
		Emit(LogLevel::Info, "Test Device Fail!\r\n");
	else
		Emit(LogLevel::Info, "Test Device OK.\r\n");
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::ResetDevice(STRUCT_RKDEVICE_DESC &dev, BYTE subCode)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Info, "Reset Device quit,Creating comm object failed!\r\n");
		return false;
	}
	int iRet = pComm->RKU_ResetDevice(subCode);
	bool bSuccess = (iRet == ERR_SUCCESS);
	Emit(LogLevel::Info, bSuccess ? "Reset Device OK.\r\n" : "Reset Device failed!\r\n");
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
		Emit(LogLevel::Info, "Read flash ID quit,Creating comm object failed!\r\n");
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
		Emit(LogLevel::Info, "Read flash ID Fail!\r\n");
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
		Emit(LogLevel::Info, "Read flash Info quit,Creating comm object failed!\r\n");
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
		Emit(LogLevel::Info, "\tFlash CS: ");
		for (int i = 0; i < 8; i++)
			if (info.bFlashCS & (1 << i))
				Emit(LogLevel::Info, "<%d> ", i);
		Emit(LogLevel::Info, "\r\n");

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
		Emit(LogLevel::Info, "Read flash Info Fail!\r\n");
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
		Emit(LogLevel::Info, "Read Chip Info quit,Creating comm object failed!\r\n");
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
		Emit(LogLevel::Info, "Read Chip Info Fail!\r\n");
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
		if (m_pLog) m_pLog->Record("Error:%s->Fail to create comm object!", "read_capability");
		if (bPrint)
			Emit(LogLevel::Info, "Read capability quit,Creating comm object failed!\r\n");
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
		if (m_pLog) m_pLog->Record("Error:%s->Fail to read capability,err=%d", "read_capability", iRet);
		if (bPrint)
			Emit(LogLevel::Info, "Read capability Fail!\r\n");
	}
	delete pComm;
	return bSuccess;
}

BYTE UpgradeEngine::GetStorageIndex(UINT uiStorage)
{
	if (uiStorage & STORAGE_FLASH)   return 0;
	if (uiStorage & STORAGE_EMMC)    return 1;
	if (uiStorage & STORAGE_SD)      return 2;
	if (uiStorage & STORAGE_SD1)     return 3;
	if (uiStorage & STORAGE_SPINOR)  return 9;
	if (uiStorage & STORAGE_SPINAND) return 8;
	return 0xFF;
}

UINT UpgradeEngine::GetStorageValue(std::string strStorage)
{
	if (strStorage == "FLASH")   return STORAGE_FLASH;
	if (strStorage == "EMMC")    return STORAGE_EMMC;
	if (strStorage == "SD")      return STORAGE_SD;
	if (strStorage == "SD1")     return STORAGE_SD1;
	if (strStorage == "SPINOR")  return STORAGE_SPINOR;
	if (strStorage == "SPINAND") return STORAGE_SPINAND;
	if (strStorage == "RAM")     return STORAGE_RAM;
	if (strStorage == "USB")     return STORAGE_USB;
	if (strStorage == "SATA")    return STORAGE_SATA;
	if (strStorage == "PCIE")    return STORAGE_PCIE;
	return 0;
}

std::string UpgradeEngine::GetStorageString(UINT uiStorage)
{
	if (uiStorage & STORAGE_FLASH)   return "FLASH";
	if (uiStorage & STORAGE_EMMC)    return "EMMC";
	if (uiStorage & STORAGE_SD)      return "SD";
	if (uiStorage & STORAGE_SD1)     return "SD1";
	if (uiStorage & STORAGE_SPINOR)  return "SPINOR";
	if (uiStorage & STORAGE_SPINAND) return "SPINAND";
	if (uiStorage & STORAGE_RAM)     return "RAM";
	if (uiStorage & STORAGE_USB)     return "USB";
	if (uiStorage & STORAGE_SATA)    return "SATA";
	if (uiStorage & STORAGE_PCIE)    return "PCIE";
	return "";
}

std::string UpgradeEngine::GetCompositeStorageString(UINT uiStorage)
{
	static const UINT bits[] = {
		STORAGE_FLASH, STORAGE_EMMC, STORAGE_SD,
		STORAGE_SPINAND, STORAGE_SPINOR, STORAGE_SATA, STORAGE_PCIE
	};
	std::string result;
	for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); i++) {
		if (!(uiStorage & bits[i]))
			continue;
		std::string name = GetStorageString(bits[i]);
		if (name.empty())
			continue;
		if (!result.empty())
			result += "|";
		result += name;
	}
	return result;
}

bool UpgradeEngine::GetDeviceStorage(CRKComm *pComm, BYTE *lpBuffer, StorageCode &uiStorage)
{
	BYTE capability[8];
	int iRet = pComm->RKU_ReadCapability(capability);
	if (iRet != ERR_SUCCESS) {
		if (m_pLog) m_pLog->Record("Error:%s-->RKU_ReadCapability failed,err=%d",
			               "get_device_storage", iRet);
		return false;
	}
	if (!(capability[1] & 0x02))
		return false;
	for (int i = 0; i < 3; i++) {
		iRet = pComm->RKU_ReadStorageList(lpBuffer);
		if (iRet == ERR_SUCCESS) {
			uiStorage = (StorageCode)(*(UINT *)lpBuffer);
			return true;
		}
		if (iRet != ERR_DEVICE_UNREADY) {
			if (m_pLog)
				if (m_pLog) m_pLog->Record("Error:%s-->RKU_ReadStorageList failed,err=%d",
				               "get_device_storage", iRet);
			return false;
		}
		usleep(1000 * 1000);
	}
	return false;
}

bool UpgradeEngine::SwitchDeviceStorage(CRKComm *pComm, StorageCode uiStorage)
{
	BYTE ucIndex = GetStorageIndex(uiStorage);
	if (ucIndex == 0xFF) {
		if (m_pLog) m_pLog->Record("Error:%s-->switch_device_storage invalid storage,storage=0x%x",
		               "switch_device_storage", uiStorage);
		return false;
	}
	int iRet = pComm->RKU_SwitchStorage(ucIndex);
	if (iRet == ERR_SUCCESS)
		return true;
	if (m_pLog) m_pLog->Record("Error:%s-->RKU_SwitchStorage failed,err=%d", "switch_device_storage", iRet);
	return false;
}

void UpgradeEngine::ListStorage(std::vector<std::string> &vecStorage, StorageCode uiCurStorage)
{
	for (size_t i = 0; i < vecStorage.size(); i++) {
		if (GetStorageValue(vecStorage[i]) == (UINT)uiCurStorage)
			Emit(LogLevel::Info, "No=%d\t%s(*)\r\n", (int)(i + 1), vecStorage[i].c_str());
		else
			Emit(LogLevel::Info, "No=%d\t%s\r\n", (int)(i + 1), vecStorage[i].c_str());
	}
}

bool UpgradeEngine::ReadParam(STRUCT_RKDEVICE_DESC &dev, u8 *pParam)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false, bSuccess = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (bRet) {
		int iRet = pComm->RKU_ReadLBA(0x2000, 512, pParam);
		if (iRet == ERR_SUCCESS && *(u32 *)pParam == 0x4D524150)
			bSuccess = true;
	}
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::ReadGpt(STRUCT_RKDEVICE_DESC &dev, u8 *pGpt)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	gpt_header *gptHead = (gpt_header *)(pGpt + SECTOR_SIZE);
	bool bRet = false, bSuccess = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (bRet) {
		int iRet = pComm->RKU_ReadLBA(0, 34, pGpt);
		if (iRet == ERR_SUCCESS && gptHead->signature == le64_to_cpu(GPT_HEADER_SIGNATURE))
			bSuccess = true;
	}
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::ReadLba(STRUCT_RKDEVICE_DESC &dev, UINT uiBegin, UINT uiLen, char *szFile)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false, bSuccess = false;
	UINT iTotalRead = 0, iRead = 0;
	const int nSectorSize = 512;
	BYTE pBuf[nSectorSize * DEFAULT_RW_LBA];
	FILE *file = NULL;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Info, "Read LBA quit,Creating comm object failed!\r\n");
		return false;
	}
	if (szFile) {
		file = fopen(szFile, "wb+");
		if (!file) {
			Emit(LogLevel::Info, "Read LBA failed,err=%d,can't open file: %s\r\n", errno, szFile);
			delete pComm;
			return false;
		}
	}
	while (uiLen > 0) {
		if (IsAborted()) goto exit_read;
		memset(pBuf, 0, nSectorSize * DEFAULT_RW_LBA);
		iRead = (uiLen >= DEFAULT_RW_LBA) ? DEFAULT_RW_LBA : uiLen;
		int iRet = pComm->RKU_ReadLBA(uiBegin + iTotalRead, iRead, pBuf);
		if (iRet != ERR_SUCCESS) {
			Emit(LogLevel::Info, "Read LBA failed!\r\n");
			goto exit_read;
		}
		uiLen -= iRead;
		iTotalRead += iRead;
		if (file) {
			fwrite(pBuf, 1, iRead * nSectorSize, file);
			EmitTransferProgress(TransferStage::Read, (long long)(uiLen + iTotalRead) * nSectorSize, (long long)iTotalRead * nSectorSize);
		} else {
			PrintData(pBuf, nSectorSize * iRead);
		}
	}
	bSuccess = true;
exit_read:
	if (pComm) delete pComm;
	if (file) fclose(file);
	return bSuccess;
}

static UINT DiDownloadChunkSectors(CRKComm *pComm)
{
	u64 ullChunk;
	UINT uiForceBand = 0;
	if (GetForceDataBand(uiForceBand) && uiForceBand > 0) {
		if (uiForceBand > 16)
			uiForceBand = 16;
		ullChunk = (u64)0x20 << uiForceBand;
	} else {
		DWORD dwTotal = 0, dwCurrent = 0;
		if (pComm->RKU_TestDeviceReady(&dwTotal, &dwCurrent) != ERR_SUCCESS)
			return 0;
		if (dwCurrent > 16)
			dwCurrent = 16;
		ullChunk = (u64)0x20 << dwCurrent;
	}
	if (ullChunk < 1)
		ullChunk = 1;
	if (ullChunk > 0xFFFF)
		ullChunk = 0xFFFF;
	return (UINT)ullChunk;
}

bool UpgradeEngine::WriteLba(STRUCT_RKDEVICE_DESC &dev, UINT uiBegin, char *szFile, CRKComm *pExisting,
                             UINT uiChunkSectors)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false, bSuccess = false;
	long long iTotalWrite = 0, iFileSize = 0;
	UINT iWrite = 0, uiLen;
	const int nSectorSize = 512;
	BYTE *pBuf = NULL;
	size_t chunkBytes = 0;
	FILE *file = NULL;
	CRKComm *pComm = pExisting;
	bool bOwn = false;
	if (!pComm) {
		pComm = new CRKUsbComm(dev, m_pLog, bRet);
		bOwn = true;
		if (!bRet) {
			Emit(LogLevel::Info, "Write LBA quit, creating comm object failed!\r\n");
			return false;
		}
	}

	if (uiChunkSectors == 0) {
		uiChunkSectors = DiDownloadChunkSectors(pComm);
		if (uiChunkSectors == 0) {
			Emit(LogLevel::Info, "Write LBA failed, RKU_TestDeviceReady error\r\n");
			goto exit_write;
		}
	}
	if (uiChunkSectors > 0xFFFF)
		uiChunkSectors = 0xFFFF;
	chunkBytes = (size_t)uiChunkSectors * nSectorSize;
	pBuf = new BYTE[chunkBytes];
	if (!pBuf)
		goto exit_write;
	Emit(LogLevel::Info, "Start to write %s at 0x%x...\r\n", szFile, uiBegin);
	file = fopen(szFile, "rb");
	if (!file) {
		Emit(LogLevel::Info, "Write LBA failed, err=%d, can't open file: %s\r\n", errno, szFile);
		goto exit_write;
	}
	fseeko(file, 0, SEEK_END);
	iFileSize = ftello(file);
	fseeko(file, 0, SEEK_SET);

	ResetProgress(iFileSize);
	while (iTotalWrite < iFileSize) {
		if (IsAborted()) goto exit_write;
		memset(pBuf, 0, chunkBytes);
		iWrite = fread(pBuf, 1, chunkBytes, file);
		uiLen = ((iWrite % 512) == 0) ? (iWrite / 512) : (iWrite / 512 + 1);
		int iRet = pComm->RKU_WriteLBA(uiBegin, uiLen, pBuf);
		if (iRet != ERR_SUCCESS) {
			Emit(LogLevel::Info, "Write LBA failed!\r\n");
			goto exit_write;
		}
		uiBegin += uiLen;
		iTotalWrite += iWrite;
		StepProgress(iWrite);

	}
	bSuccess = true;
exit_write:
	if (pBuf) delete []pBuf;
	if (bOwn) delete pComm;
	if (file) fclose(file);
	return bSuccess;
}

bool UpgradeEngine::EraseLba(STRUCT_RKDEVICE_DESC &dev, UINT uiBegin, UINT uiCount)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Erase LBA quit, creating comm object failed!");
		return false;
	}
	int iRet = pComm->RKU_EraseLBA(uiBegin, uiCount);
	bool bSuccess = (iRet == ERR_SUCCESS);
	Emit(LogLevel::Info, bSuccess ? "Erase LBA OK.\r\n" : "Erase LBA failed!\r\n");
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::EraseBlock(STRUCT_RKDEVICE_DESC &dev, BYTE ucFlashCS, UINT uiBegin,
                               UINT uiCount, bool bForce)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Erase Block quit, creating comm object failed!");
		return false;
	}
	if (uiCount > MAX_ERASE_BLOCKS) {
		Emit(LogLevel::Info, "Erase block count is larger than %d!\r\n", MAX_ERASE_BLOCKS);
		delete pComm;
		return false;
	}
	int iRet = pComm->RKU_EraseBlock(ucFlashCS, uiBegin, uiCount, bForce ? ERASE_FORCE : ERASE_NORMAL);
	bool bSuccess = (iRet == ERR_SUCCESS || iRet == ERR_FOUND_BAD_BLOCK);
	Emit(LogLevel::Info, bSuccess ? "Erase Block OK.\r\n" : "Erase Block failed!\r\n");
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
	Emit(LogLevel::Info, bSuccess ? "Reset Pipe OK.\r\n" : "Reset Pipe failed!\r\n");
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
		Emit(LogLevel::Info, "Read OTP Chip quit,Creating comm object failed!\r\n");
		return false;
	}
	BYTE otp[32];
	int iRet = pComm->RKU_ReadOtpChip(otp);
	bool bSuccess = (iRet == ERR_SUCCESS);
	if (bSuccess) {
		if (otp[0] != 'R' || otp[1] != 'K') {
			Emit(LogLevel::Info, "OTP chip tag Invalid!\r\n");
			delete pComm;
			return false;
		}
		Emit(LogLevel::Info, "OTP Info: %s\r\n", HexList(otp, 32).c_str());
		m_lastResult.bytes.assign(otp, otp + 32);
		m_lastResult.kind = ResultKind::Generic;
		m_lastResult.items.push_back(ResultItem{"OTP", HexList(otp, 32)});
	} else {
		Emit(LogLevel::Info, "Read OTP chip Fail!\r\n");
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
		if (m_pLog) m_pLog->Record("Error:%s->Fail to create comm object", "read_com_log");
		Emit(LogLevel::Error, "Read Com Log quit,Creating comm object failed!");
		return false;
	}

	FILE *file = NULL;
	if (szFile) {
		file = fopen(szFile, "wb+");
		if (!file) {
			if (m_pLog) m_pLog->Record("Error:%s->Fail to create %s,err=%d",
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
		if (m_pLog) m_pLog->Record("Error:%s->Fail to read com log,err=%d", "read_com_log", iRet);
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

	if (m_pLog) m_pLog->Record("Error:%s->Log format is invalid", "read_com_log");
	Emit(LogLevel::Error, "Error:Format of log is wrong!");
	delete pComm;
	if (file)
		fclose(file);
	return false;
}

bool UpgradeEngine::SwitchStorage(STRUCT_RKDEVICE_DESC &dev, int iIndex)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	if (!ReadCapability(dev, false)) return false;

	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		if (m_pLog) m_pLog->Record("Error:%s->Fail to create comm object!", "switch_storage");
		Emit(LogLevel::Error, "SwitchStorage quit,Creating comm object failed!");
		return false;
	}
	if (!m_bSwitchStorage) {
		Fail(ErrorCode::UnsupportedDevice);
		Emit(LogLevel::Error, "Device does not support storage switching");
		delete pComm;
		return false;
	}

	std::vector<std::string> vecStorage;
	vecStorage.push_back("FLASH");
	vecStorage.push_back("EMMC");
	vecStorage.push_back("SD");
	vecStorage.push_back("SD1");
	vecStorage.push_back("SPINOR");
	vecStorage.push_back("SPINAND");
	vecStorage.push_back("RAM");
	vecStorage.push_back("USB");
	vecStorage.push_back("SATA");
	vecStorage.push_back("PCIE");

	BYTE lpBuffer[4];
	StorageCode uiCurStorage = (StorageCode)0;

	if (iIndex < 0) {
		Emit(LogLevel::Info, "List of supported storage\r\n");
		if (!GetDeviceStorage(pComm, lpBuffer, uiCurStorage)) {
			if (m_pLog) m_pLog->Record("Error:%s->Fail to get device storage", "switch_storage");
			Emit(LogLevel::Error, "Error:Read Storage list failed!");
			delete pComm;
			return false;
		}
		ListStorage(vecStorage, uiCurStorage);

		int iSel = SelectStorage(vecStorage, (int)uiCurStorage);
		if (iSel <= 0 || iSel > (int)vecStorage.size()) {
			RequestAbort();
			delete pComm;
			return false;
		}
		iIndex = iSel;
	}

	if (iIndex < 1 || iIndex > (int)vecStorage.size()) {
		delete pComm;
		return false;
	}
	{
		UINT uiValue = GetStorageValue(vecStorage[iIndex - 1]);
		int iRet = pComm->RKU_SwitchStorage(GetStorageIndex(uiValue));
		if (iRet != ERR_SUCCESS) {
			if (m_pLog) m_pLog->Record("Error:%s->Fail to switch storage,err=%d", "switch_storage", iRet);
			Emit(LogLevel::Error, "Error:Switch Storage failed!");
			delete pComm;
			return false;
		}
		if (!GetDeviceStorage(pComm, lpBuffer, uiCurStorage)) {
			if (m_pLog) m_pLog->Record("Error:%s->Fail to get device storage After switching", "switch_storage");
			Emit(LogLevel::Error, "Error:Switch Storage failed!");
			delete pComm;
			return false;
		}
		if (uiCurStorage == uiValue) {
			Emit(LogLevel::Info, "Switch %s ok.\r\n", vecStorage[iIndex - 1].c_str());
			delete pComm;
			return true;
		}
		if (m_pLog) m_pLog->Record("Error:%s->Fail to check Storage,storage=%s,switch=%s",
		               "switch_storage", GetStorageString(uiCurStorage).c_str(),
		               vecStorage[iIndex - 1].c_str());
		Emit(LogLevel::Error, "Error:Switch Storage failed!");
		delete pComm;
		return false;
	}
}

bool UpgradeEngine::SwitchDevice(STRUCT_RKDEVICE_DESC &dev)
{
	if (!CheckDeviceType(dev, RKUSB_MSC))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Info, "Switch Device quit,Creating comm object failed!\r\n");
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
		if (m_pLog) m_pLog->Record("Error:%s->Fail to switch,err=%d!", "switch_device", iRet);
		Emit(LogLevel::Info, "Switch Device Fail!\r\n");
	}
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
		Emit(LogLevel::Info, "Read Secure Mode quit,Creating comm object failed!\r\n");
		if (m_pLog) m_pLog->Record("Error:%s->Fail to create comm object!", "read_secure_mode");
		return false;
	}
	BYTE capability[8];
	int iRet = pComm->RKU_ReadCapability(capability);
	if (iRet != ERR_SUCCESS) {
		if (m_pLog) m_pLog->Record("Error:%s->Fail to read capability,err=%d", "read_secure_mode", iRet);
		Emit(LogLevel::Info, "Read Capability Fail!\r\n");
		delete pComm;
		return false;
	}
	if (!(capability[0] & 0x80)) {
		Emit(LogLevel::Info, "Device did not support to read secure mode!\r\n");
		if (m_pLog) m_pLog->Record("Error:%s->can't support to read secure mode!", "read_secure_mode");
		delete pComm;
		return false;
	}
	BYTE bMode = 0;
	iRet = pComm->RKU_ReadNewEfuse(0, 0, &bMode, 2);
	if (iRet != ERR_SUCCESS) {
		if (m_pLog) m_pLog->Record("Error:%s->Fail to read new efuse,err=%d", "read_secure_mode", iRet);
		Emit(LogLevel::Info, "Read Secure Mode Fail!\r\n");
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

bool UpgradeEngine::WriteSector(STRUCT_RKDEVICE_DESC &dev, UINT uiBegin, UINT uiPageSize,
                                UINT uiSpareSize, char *szFile)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	(void)uiPageSize; (void)uiSpareSize;
	FILE *file = fopen(szFile, "rb");
	if (!file) {
		Emit(LogLevel::Info, "Read file failed, err=%d, can't open file: %s\r\n", errno, szFile);
		return false;
	}
	fseek(file, 0, SEEK_END);
	long iFileSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	UINT uiSector = (iFileSize % SECTOR_SIZE) ? (iFileSize / SECTOR_SIZE + 1) : (iFileSize / SECTOR_SIZE);
	BYTE *pBuf = new BYTE[uiSector * SECTOR_SIZE];
	memset(pBuf, 0, uiSector * SECTOR_SIZE);
	if (fread(pBuf, 1, iFileSize, file) != (size_t)iFileSize) {
		fclose(file);
		delete []pBuf;
		return false;
	}
	fclose(file);
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		delete []pBuf;
		Emit(LogLevel::Error, "Write Sector quit, creating comm object failed!");
		return false;
	}
	int iRet = pComm->RKU_WriteSector(uiBegin, uiSector, pBuf);
	bool bSuccess = (iRet == ERR_SUCCESS);
	Emit(LogLevel::Info, bSuccess ? "Write Sector OK.\r\n" : "Write Sector failed!\r\n");
	delete pComm;
	delete []pBuf;
	return bSuccess;
}

bool UpgradeEngine::PrintGpt(STRUCT_RKDEVICE_DESC &dev)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false, bSuccess = false;
	u8 master_gpt[34 * SECTOR_SIZE];
	gpt_header *gptHead = (gpt_header *)(master_gpt + SECTOR_SIZE);
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet)
		return false;
	int iRet = pComm->RKU_ReadLBA(0, 34, master_gpt);
	if (iRet != ERR_SUCCESS || gptHead->signature != le64_to_cpu(GPT_HEADER_SIGNATURE))
		goto exit_print;
	Emit(LogLevel::Info, "Partition Info(gpt):\r\n");
	Emit(LogLevel::Info, "NO  LBA        Size       Name\r\n");
	m_lastResult.kind = ResultKind::PartitionList;
	m_lastResult.items.clear();
	m_lastResult.items.push_back(ResultItem{"Source", "gpt"});
	for (u32 i = 0; i < std::min<u32>(le32_to_cpu(gptHead->num_partition_entries), 128); i++) {
		gpt_entry *gptEntry = (gpt_entry *)(master_gpt + 2 * SECTOR_SIZE + i * GPT_ENTRY_SIZE);
		u8 zerobuf[GPT_ENTRY_SIZE];
		memset(zerobuf, 0, GPT_ENTRY_SIZE);
		if (memcmp(zerobuf, (u8 *)gptEntry, GPT_ENTRY_SIZE) == 0)
			break;
		char partName[37] = {0};
		for (u32 j = 0; j < 36 && gptEntry->partition_name[j]; j++)
			partName[j] = (char)gptEntry->partition_name[j];
		u64 uiStart = le64_to_cpu(gptEntry->starting_lba);
		u64 uiEnd = le64_to_cpu(gptEntry->ending_lba);
		Emit(LogLevel::Info, "%02d  0x%08x 0x%08x %s\r\n", i + 1, (u32)uiStart, (u32)(uiEnd - uiStart + 1), partName);
		char szLine[128];
		snprintf(szLine, sizeof(szLine), "0x%08x 0x%08x %s",
		         (u32)uiStart, (u32)(uiEnd - uiStart + 1), partName);
		char szNo[16];
		snprintf(szNo, sizeof(szNo), "%02d", i + 1);
		m_lastResult.items.push_back(ResultItem{szNo, szLine});
		m_lastResult.partitions.push_back(PartitionInfo{partName, uiStart, uiEnd - uiStart + 1});
	}
	bSuccess = true;
exit_print:
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::PrintParameter(STRUCT_RKDEVICE_DESC &dev)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false, bSuccess = false;
	std::vector<u8> paramStorage(512 * SECTOR_SIZE);
	u8 *param_buf = paramStorage.data();
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet)
		return false;
	int iRet = pComm->RKU_ReadLBA(0x2000, 512, param_buf);
	if (iRet != ERR_SUCCESS || *(u32 *)param_buf != 0x4D524150)
		goto exit_print;
	{
		u32 nParamSize = *(u32 *)(param_buf + 4);
		PARAM_ITEM_VECTOR vecParamItem;
		CONFIG_ITEM_VECTOR vecUuidItem;
		if (nParamSize > 512 * SECTOR_SIZE - 8) goto exit_print;
		memset(param_buf + 8 + nParamSize, 0, 512 * SECTOR_SIZE - nParamSize - 8);
		if (!parse_parameter((char *)(param_buf + 8), vecParamItem, vecUuidItem))
			goto exit_print;
		Emit(LogLevel::Info, "**********Partition Info(parameter)**********\r\n");
		Emit(LogLevel::Info, "NO  LBA       Name                \r\n");
		m_lastResult.kind = ResultKind::PartitionList;
		m_lastResult.items.clear();
		m_lastResult.items.push_back(ResultItem{"Source", "parameter"});
		for (u32 i = 0; i < vecParamItem.size(); i++) {
			Emit(LogLevel::Info, "%02d  %08X  %s\r\n", i, vecParamItem[i].uiItemOffset, vecParamItem[i].szItemName);
			char szLine[128];
			snprintf(szLine, sizeof(szLine), "0x%08X %s",
			         vecParamItem[i].uiItemOffset, vecParamItem[i].szItemName);
			char szNo[16];
			snprintf(szNo, sizeof(szNo), "%02d", i);
			m_lastResult.items.push_back(ResultItem{szNo, szLine});
			m_lastResult.partitions.push_back(PartitionInfo{vecParamItem[i].szItemName, vecParamItem[i].uiItemOffset, vecParamItem[i].uiItemSize});
		}
		bSuccess = true;
	}
exit_print:
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::DownloadBoot(STRUCT_RKDEVICE_DESC &dev, char *szLoader)
{
	if (!CheckDeviceType(dev, RKUSB_MASKROM))
		return false;
	bool bRet = false, bSuccess = false;
	CRKImage *pImage = new CRKImage();
	int iRet = pImage->ObjectInit(szLoader);
	if (iRet != 0) {
		Emit(LogLevel::Error, "Open loader failed(err=%d),exit download boot!", iRet);
		std::string strNote = GetErrString(pImage->GetError());
		if (!strNote.empty())
			Emit(LogLevel::Info, "%s\r\n", strNote.c_str());
		delete pImage;
		return false;
	}
	CRKBoot *pBoot = (CRKBoot *)pImage->m_bootObject;
	dev.emDeviceType = pBoot->SupportDevice;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Creating comm object failed!");
		delete pImage;
		return false;
	}
	CRKDevice *pDevice = CDeviceFactory::CreateDevice(dev);
	pDevice->SetObject(pImage, pComm, m_pLog);
	Emit(LogLevel::Info, "Download boot...\r\n");
	int iDlRet = pDevice->DownloadBoot();

	if (iDlRet == 0) {
		bSuccess = true;
		Emit(LogLevel::Info, "Download boot ok.\r\n");
	} else {
		Emit(LogLevel::Info, "Download boot failed!\r\n");
		std::string strNote = GetErrString(pDevice->GetError());
		if (!strNote.empty())
			Emit(LogLevel::Info, "%s\r\n", strNote.c_str());
	}
	CDeviceFactory::FreeDevice(pDevice);
	delete pImage;
	return bSuccess;
}

bool UpgradeEngine::UpgradeLoader(STRUCT_RKDEVICE_DESC &dev, char *szLoader)
{
	if (!CheckDeviceType(dev, RKUSB_MASKROM | RKUSB_LOADER))
		return false;
	bool bRet = false, bSuccess = false;
	CRKImage *pImage = NULL;
	CRKComm *pComm = NULL;
	CRKAndroidDevice *pDevice = NULL;
	CRKUpgrade *pUpgrade = NULL;

	pImage = new CRKImage();
	Emit(LogLevel::Info, "Loading loader...\r\n");
	int iRet = pImage->ObjectInit(szLoader);
	if (iRet != 0) {
		Emit(LogLevel::Error, "Loading loader failed,err=%d!", iRet);
		std::string strNote = GetErrString(pImage->GetError());
		if (!strNote.empty())
			Emit(LogLevel::Info, "%s\r\n", strNote.c_str());
		delete pImage;
		return false;
	}
	CRKBoot *pBoot = (CRKBoot *)pImage->m_bootObject;
	STRUCT_RKTIME tLoader = pBoot->ReleaseTime;
	UINT uiType = (UINT)pBoot->SupportDevice;
	char szType[5];
	szType[0] = (char)((uiType >> 24) & 0xFF);
	szType[1] = (char)((uiType >> 16) & 0xFF);
	szType[2] = (char)((uiType >> 8) & 0xFF);
	szType[3] = (char)(uiType & 0xFF);
	szType[4] = 0;
	bool bTagPrintable = true;
	for (int i = 0; i < 4 && szType[i]; i++)
		if (!isprint((unsigned char)szType[i]))
			bTagPrintable = false;
	char szSupport[16];
	if (bTagPrintable && szType[0])
		snprintf(szSupport, sizeof(szSupport), "RK%s", szType);
	else
		snprintf(szSupport, sizeof(szSupport), "RK%X", uiType);
	Emit(LogLevel::Info, "Support Type:%s\tLoader ver:%x.%02x\tLoader Time:%d-%02d-%02d %02d:%02d:%02d\r\n",
	     szSupport, (pBoot->Version >> 8) & 0xFF, pBoot->Version & 0xFF,
	     tLoader.usYear, tLoader.ucMonth, tLoader.ucDay, tLoader.ucHour, tLoader.ucMinute, tLoader.ucSecond);
	dev.emDeviceType = pBoot->SupportDevice;
	pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Creating comm object failed!");
		goto exit_ul;
	}
	Emit(LogLevel::Info, "Start to upgrade loader...\r\n");
	pDevice = (CRKAndroidDevice *)CDeviceFactory::CreateDevice(dev);
	pDevice->SetObject(pImage, pComm, m_pLog);

	{
		std::vector<STRUCT_UPGRADE_STEP> vecStep;
		CRKUpgrade::CreateDownloadIDBStep(dev.emDeviceType, vecStep);
		pUpgrade = new CRKUpgrade(vecStep, WORKFLOW_UPGRADE);
		pUpgrade->RemoveStep(UPGRADE_STEP_DOWNLOAD_BOOT);
		pUpgrade->RemoveStep(UPGRADE_STEP_WAIT_MASKROM);
		pUpgrade->SetObject(pDevice, m_pScan);
		pUpgrade->SetCallBackPointer(PromptThunk);
		bSuccess = pUpgrade->Run();
		if (bSuccess)
			Emit(LogLevel::Info, "Upgrade loader ok.\r\n");
		else
			Emit(LogLevel::Info, "Upgrade loader fail.\r\n");
	}
exit_ul:
	if (pUpgrade) delete pUpgrade;
	if (pImage) delete pImage;
	if (pDevice) { CDeviceFactory::FreeDevice(pDevice); pComm = NULL; }
	if (pComm) delete pComm;
	return bSuccess;
}

bool UpgradeEngine::EraseFlash(STRUCT_RKDEVICE_DESC &dev, char *szLoader)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false, bSuccess = false;
	CRKImage *pImage = new CRKImage();
	Emit(LogLevel::Info, "Loading loader...\r\n");
	int iRet = pImage->ObjectInit(szLoader);
	if (iRet != 0) {
		Emit(LogLevel::Error, "Loading loader failed,err=%d!", iRet);
		delete pImage;
		return false;
	}
	CRKBoot *pBoot = (CRKBoot *)pImage->m_bootObject;
	if (pBoot)
		dev.emDeviceType = pBoot->SupportDevice;

	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Creating Comm Object failed!");
		delete pImage;
		return false;
	}
	CRKDevice *pDevice = CDeviceFactory::CreateDevice(dev);
	pDevice->SetObject(pImage, pComm, m_pLog);

	std::vector<STRUCT_UPGRADE_STEP> vecStep;
	CRKUpgrade::CreateEraseAllBlocksStep(dev.emDeviceType, vecStep);
	CRKUpgrade *pUpgrade = new CRKUpgrade(vecStep, WORKFLOW_DOWNLOAD_IDB);
	pUpgrade->RemoveStep(UPGRADE_STEP_DOWNLOAD_BOOT);
	pUpgrade->RemoveStep(UPGRADE_STEP_WAIT_MASKROM);
	pUpgrade->SetSkipPhaseSteps(false);
	pUpgrade->SetObject(pDevice, m_pScan);
	pUpgrade->SetCallBackPointer(PromptThunk);
	Emit(LogLevel::Info, "Start to erase flash...\r\n");
	bSuccess = pUpgrade->Run();
	delete pUpgrade;

	CDeviceFactory::FreeDevice(pDevice);
	delete pImage;
	return bSuccess;
}

bool UpgradeEngine::UpgradeFirmware(STRUCT_RKDEVICE_DESC &dev, char *szFirmware, bool bNoReset)
{
	if (!CheckDeviceType(dev, RKUSB_MASKROM | RKUSB_LOADER))
		return false;
	bool bRet = false, bSuccess = false;
	CRKAndroidDevice *pDevice = NULL;
	CRKComm *pComm = NULL;
	CRKUpgrade *pUpgrade = NULL;
	CRKImage *pImage = new CRKImage();
	Emit(LogLevel::Info, "Loading firmware...\r\n");
	int iRet = pImage->ObjectInit(szFirmware);
	if (iRet != 0) {
		Emit(LogLevel::Error, "Loading firmware failed!");
		std::string strNote = GetErrString(pImage->GetError());
		if (!strNote.empty())
			Emit(LogLevel::Info, "%s\r\n", strNote.c_str());
		delete pImage;
		return false;
	}
	FILE *file = fopen(szFirmware, "rb");
	BYTE hdr[0x40];
	if (!file || fread(hdr, 1, sizeof(hdr), file) != sizeof(hdr)) {
		if (file) fclose(file);
		delete pImage;
		return false;
	}
	UINT ver = *(UINT *)(hdr + 6);
	STRUCT_RKTIME t;
	t.usYear = *(USHORT *)(hdr + 14);
	t.ucMonth = hdr[16]; t.ucDay = hdr[17]; t.ucHour = hdr[18];
	t.ucMinute = hdr[19]; t.ucSecond = hdr[20];
	UINT chip = *(UINT *)(hdr + 21);
	UINT fwOff = *(UINT *)(hdr + 0x21);
	std::string szChip = FirmwareService::ChipName(chip);

	Emit(LogLevel::Info, "Support Type:%s\tFW Ver:%x.%x.%02x\tFW Time:%d-%02d-%02d %02d:%02d:%02d\r\n",
	     szChip.c_str(), (ver >> 24) & 0xFF, (ver >> 16) & 0xFF, (ver >> 8) & 0xFF,
	     t.usYear, t.ucMonth, t.ucDay, t.ucHour, t.ucMinute, t.ucSecond);
	CRKBoot *pBoot = pImage->m_bootObject;
	if (pBoot) {
		STRUCT_RKTIME lt = pBoot->ReleaseTime;
		Emit(LogLevel::Info, "Loader ver:%u.%02u\tLoader Time:%d-%02d-%02d %02d:%02d:%02d\r\n",
		     (pBoot->Version >> 8) & 0xFF, pBoot->Version & 0xFF,
		     lt.usYear, lt.ucMonth, lt.ucDay, lt.ucHour, lt.ucMinute, lt.ucSecond);
	}
	(void)fwOff;
	Emit(LogLevel::Info, "Start to upgrade firmware...\r\n");

	pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Info, "Create comm failed!\r\n");
		fclose(file); delete pImage;
		return false;
	}
	pDevice = (CRKAndroidDevice *)CDeviceFactory::CreateDevice(dev);
	pDevice->SetObject(pImage, pComm, m_pLog);

	ResetDownloadProgress();
	pDevice->CallBackPointer = ProgressThunk;
	pDevice->LoadFirmwarePackage(szFirmware);

	{
		std::vector<STRUCT_UPGRADE_STEP> vecStep;
		CRKUpgrade::CreateUpgradeStep(dev.emDeviceType, vecStep, 0);
		pUpgrade = new CRKUpgrade(vecStep, WORKFLOW_UPGRADE);
		pUpgrade->RemoveStep(UPGRADE_STEP_DOWNLOAD_BOOT);
		pUpgrade->RemoveStep(UPGRADE_STEP_WAIT_MASKROM);
		pUpgrade->SetObject(pDevice, m_pScan);
		pUpgrade->SetCallBackPointer(PromptThunk);
		bSuccess = pUpgrade->Run();
		if (bSuccess)
			Emit(LogLevel::Info, "Upgrade firmware ok.\r\n");
	}
	if (pUpgrade) delete pUpgrade;
	fclose(file);
	delete pImage;
	if (pDevice) { CDeviceFactory::FreeDevice(pDevice); pComm = NULL; }
	if (pComm) delete pComm;
	if (bSuccess && !bNoReset) {
		bool bRet2 = false;
		CRKUsbComm *pReset = new CRKUsbComm(dev, m_pLog, bRet2);
		if (bRet2) {
			pReset->RKU_ResetDevice();
			delete pReset;
		}
	}
	return bSuccess;
}

bool UpgradeEngine::ReadVendorSn(STRUCT_RKDEVICE_DESC &dev)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Info, "Read Sn quit,Creating comm object failed!\r\n");
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
		Emit(LogLevel::Info, "ERROR:failed to read sn,err=%d!\r\n", iRet);
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
		Emit(LogLevel::Info, "Write Sn quit,Creating comm object failed!\r\n");
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
		Emit(LogLevel::Info, "ERROR:failed to write sn,err=%d!\r\n", iRet);
	delete pComm;
	return bSuccess;
}

bool UpgradeEngine::RunSystem(STRUCT_RKDEVICE_DESC &dev, UINT uiUbootAddr, UINT uiTrustAddr,
                              UINT uiBootAddr, char *szUboot, char *szTrust, char *szBoot)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;

	struct {
		DWORD dwTag;
		DWORD dwVersion;
		DWORD dwUbootType;
		DWORD dwUbootAddr;
		DWORD dwUbootSize;
		DWORD dwUbootReserved;
		DWORD dwTrustType;
		DWORD dwTrustAddr;
		DWORD dwTrustSize;
		DWORD dwTrustReserved;
		DWORD dwBootType;
		DWORD dwBootAddr;
		DWORD dwBootSize;
		DWORD dwBootReserved;
	} stParam;
	memset(&stParam, 0, sizeof(stParam));
	stParam.dwTag = 0x4D505352;
	stParam.dwVersion = 3;

	stParam.dwUbootType = 2;
	stParam.dwUbootAddr = uiUbootAddr << 9;
	stParam.dwUbootSize = GetFileSize(szUboot);
	if (stParam.dwUbootSize == (DWORD)-1) {
		Emit(LogLevel::Info, "Failed to get size of uboot!\r\n");
		return false;
	}
	stParam.dwTrustType = 1;
	stParam.dwTrustAddr = uiTrustAddr << 9;
	stParam.dwTrustSize = GetFileSize(szTrust);
	if (stParam.dwTrustSize == (DWORD)-1) {
		Emit(LogLevel::Info, "Failed to get size of trust!\r\n");
		return false;
	}
	stParam.dwBootType = 4;
	stParam.dwBootAddr = uiBootAddr << 9;
	stParam.dwBootSize = GetFileSize(szBoot);
	if (stParam.dwBootSize == (DWORD)-1) {
		Emit(LogLevel::Info, "Failed to get size of boot!\r\n");
		return false;
	}

	bool bRet = false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		if (m_pLog) m_pLog->Record("Error:%s->Fail to create comm object", "run_system");
		Emit(LogLevel::Error, "Run system quit,Creating comm object failed!");
		return false;
	}

	Emit(LogLevel::Info, "start to run system...\r\n");
	int iRet = pComm->RKU_WriteSDRam((DWORD)sizeof(stParam), (BYTE *)&stParam);
	if (iRet != ERR_SUCCESS) {
		if (m_pLog) m_pLog->Record("Error:%s->Fail to write sdram, err=%d", "run_system", iRet);
		Emit(LogLevel::Error, "Failed to send param!");
		delete pComm;
		return false;
	}
	iRet = pComm->RKU_RunSDRam();
	if (iRet != ERR_SUCCESS) {
		if (m_pLog) m_pLog->Record("Error:%s->Fail to run sdram, err=%d", "run_system", iRet);
		Emit(LogLevel::Error, "Failed to run system!");
		delete pComm;
		return false;
	}
	Emit(LogLevel::Info, "run system ok.\r\n");
	delete pComm;
	return true;
}

bool UpgradeEngine::ErasePartition(CRKComm *pComm, UINT uiOffset, UINT uiSize)
{
	UINT uiErase = 1024 * 32;
	int iRet = ERR_SUCCESS;
	while (uiSize) {
		if (IsAborted()) return false;
		if (uiSize >= uiErase) {
			iRet = pComm->RKU_EraseLBA(uiOffset, uiErase);
			uiSize -= uiErase;
			uiOffset += uiErase;
		} else {
			iRet = pComm->RKU_EraseLBA(uiOffset, uiSize);
			uiOffset += uiSize;
			uiSize = 0;
		}
		if (iRet != ERR_SUCCESS)
			return false;
	}
	return true;
}

bool UpgradeEngine::WriteSparseLba(STRUCT_RKDEVICE_DESC &dev, UINT uiBegin, UINT uiSize,
                                   char *szFile, CRKComm *pExisting, UINT uiChunkSectors)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false, bSuccess = false;
	u64 iTotalWrite = 0, iFileSize = 0, dwChunkDataSize;
	UINT uiTransferSec, curChunk, i;
	UINT dwMaxReadWriteBytes, dwTransferBytes, dwFillByte, dwCrc;
	BYTE *pBuf = NULL;
	sparse_header header;
	chunk_header chunk;
	FILE *file = NULL;
	CRKComm *pComm = pExisting;
	bool bOwn = false;
	if (!pComm) {
		pComm = new CRKUsbComm(dev, m_pLog, bRet);
		bOwn = true;
		if (!bRet) {
			Emit(LogLevel::Info, "Write sparse LBA quit, creating comm object failed!\r\n");
			return false;
		}
	}

	if (uiChunkSectors == 0) {
		uiChunkSectors = DiDownloadChunkSectors(pComm);
		if (uiChunkSectors == 0) {
			Emit(LogLevel::Info, "Write sparse LBA failed, RKU_TestDeviceReady error\r\n");
			goto exit_sparse;
		}
	}
	if (uiChunkSectors > 0xFFFF)
		uiChunkSectors = 0xFFFF;
	dwMaxReadWriteBytes = uiChunkSectors * SECTOR_SIZE;
	pBuf = new BYTE[dwMaxReadWriteBytes];
	if (!pBuf)
		goto exit_sparse;
	file = fopen(szFile, "rb");
	if (!file) {
		Emit(LogLevel::Info, "%s failed, err=%d, can't open file: %s\r\n", "write_sparse_lba", errno, szFile);
		goto exit_sparse;
	}
	fseeko(file, 0, SEEK_SET);
	if (fread(&header, 1, sizeof(header), file) != sizeof(sparse_header))
		goto exit_sparse;
	iFileSize = header.blk_sz * (u64)header.total_blks;

	ResetProgress((long long)iFileSize);
	curChunk = 0;
	if (uiSize == (u32)-1)
		uiSize = ALIGN(iFileSize, SECTOR_SIZE);
	if (!ErasePartition(pComm, uiBegin, uiSize)) {
		Emit(LogLevel::Info, "%s failed, erase partition error\r\n", "write_sparse_lba");
		goto exit_sparse;
	}
	while (curChunk < header.total_chunks) {
		if (IsAborted()) goto exit_sparse;
		if (fread(&chunk, 1, sizeof(chunk_header), file) != sizeof(chunk_header))
			goto exit_sparse;
		curChunk++;
		switch (chunk.chunk_type) {
		case CHUNK_TYPE_RAW:
			dwChunkDataSize = chunk.total_sz - sizeof(chunk_header);
			while (dwChunkDataSize) {
				if (IsAborted()) goto exit_sparse;
				memset(pBuf, 0, dwMaxReadWriteBytes);
				if (dwChunkDataSize >= dwMaxReadWriteBytes) {
					dwTransferBytes = dwMaxReadWriteBytes;
					uiTransferSec = dwMaxReadWriteBytes / SECTOR_SIZE;
				} else {
					dwTransferBytes = dwChunkDataSize;
					uiTransferSec = (dwTransferBytes % SECTOR_SIZE == 0) ?
					                (dwTransferBytes / SECTOR_SIZE) : (dwTransferBytes / SECTOR_SIZE + 1);
				}
				if (fread(pBuf, 1, dwTransferBytes, file) != dwTransferBytes)
					goto exit_sparse;
				if (pComm->RKU_WriteLBA(uiBegin, uiTransferSec, pBuf) != ERR_SUCCESS)
					goto exit_sparse;
				dwChunkDataSize -= dwTransferBytes;
				iTotalWrite += dwTransferBytes;
				uiBegin += uiTransferSec;
				StepProgress(dwTransferBytes);

			}
			break;
		case CHUNK_TYPE_FILL:
			dwChunkDataSize = (u64)chunk.chunk_sz * header.blk_sz;
			if (fread(&dwFillByte, 1, 4, file) != 4)
				goto exit_sparse;
			while (dwChunkDataSize) {
				if (IsAborted()) goto exit_sparse;
				memset(pBuf, 0, dwMaxReadWriteBytes);
				if (dwChunkDataSize >= dwMaxReadWriteBytes) {
					dwTransferBytes = dwMaxReadWriteBytes;
					uiTransferSec = dwMaxReadWriteBytes / SECTOR_SIZE;
				} else {
					dwTransferBytes = dwChunkDataSize;
					uiTransferSec = (dwTransferBytes % SECTOR_SIZE == 0) ?
					                (dwTransferBytes / SECTOR_SIZE) : (dwTransferBytes / SECTOR_SIZE + 1);
				}
				for (i = 0; i < dwTransferBytes / 4; i++)
					*(DWORD *)(pBuf + i * 4) = dwFillByte;
				if (pComm->RKU_WriteLBA(uiBegin, uiTransferSec, pBuf) != ERR_SUCCESS)
					goto exit_sparse;
				dwChunkDataSize -= dwTransferBytes;
				iTotalWrite += dwTransferBytes;
				uiBegin += uiTransferSec;
				StepProgress(dwTransferBytes);

			}
			break;
		case CHUNK_TYPE_DONT_CARE:
			dwChunkDataSize = (u64)chunk.chunk_sz * header.blk_sz;
			iTotalWrite += dwChunkDataSize;
			StepProgress(dwChunkDataSize);
			uiTransferSec = (dwChunkDataSize % SECTOR_SIZE == 0) ?
			                (dwChunkDataSize / SECTOR_SIZE) : (dwChunkDataSize / SECTOR_SIZE + 1);
			uiBegin += uiTransferSec;
			break;
		case CHUNK_TYPE_CRC32:
			if (fread(&dwCrc, 1, 4, file) != 4)
				goto exit_sparse;
			break;
		}
	}
	bSuccess = true;
exit_sparse:
	if (pBuf) delete []pBuf;
	if (bOwn) delete pComm;
	if (file) fclose(file);
	return bSuccess;
}

bool UpgradeEngine::EraseUbiBlock(STRUCT_RKDEVICE_DESC &dev, u32 uiOffset, u32 uiPartSize)
{
	STRUCT_FLASHINFO_CMD info;
	BYTE flashID[5];
	bool bRet = false, bSuccess = false;
	UINT uiReadCount, uiStartBlock, uiEraseBlock, uiBlockCount, uiErasePos;
	Emit(LogLevel::Info, "Erase ubi in, offset=0x%08x,size=0x%08x!\r\n", uiOffset, uiPartSize);
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Info, "Erase ubi quit, creating comm object failed!\r\n");
		delete pComm;
		return false;
	}
	if (pComm->RKU_ReadFlashID(flashID) != ERR_SUCCESS)
		goto exit_ubi;
	if (*(DWORD *)flashID == 0x434d4d45) {
		bSuccess = true;
		goto exit_ubi;
	}
	if (pComm->RKU_ReadFlashInfo((BYTE *)&info, &uiReadCount) != ERR_SUCCESS)
		goto exit_ubi;
	if (uiPartSize == 0xFFFFFFFF)
		uiPartSize = info.uiFlashSize - uiOffset;
	uiStartBlock = uiOffset / info.usBlockSize;
	uiEraseBlock = (uiPartSize + info.usBlockSize - 1) / info.usBlockSize;
	Emit(LogLevel::Info, "Erase block start, offset=0x%08x,count=0x%08x!\r\n", uiStartBlock, uiEraseBlock);
	uiErasePos = uiStartBlock;
	while (uiEraseBlock > 0) {
		uiBlockCount = (uiEraseBlock < MAX_ERASE_BLOCKS) ? uiEraseBlock : MAX_ERASE_BLOCKS;
		int iRet = pComm->RKU_EraseBlock(0, uiErasePos, uiBlockCount, ERASE_FORCE);
		if ((iRet != ERR_SUCCESS) && (iRet != ERR_FOUND_BAD_BLOCK))
			goto exit_ubi;
		uiErasePos += uiBlockCount;
		uiEraseBlock -= uiBlockCount;
	}
	bSuccess = true;
exit_ubi:
	delete pComm;
	return bSuccess;
}

int UpgradeEngine::ParameterFileGptFlag(char *szParameterFile)
{
	FILE *file = fopen(szParameterFile, "rb");
	if (!file)
		return 0;
	fseek(file, 0, SEEK_END);
	long lSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (lSize <= 0) {
		fclose(file);
		return 0;
	}
	char *pBuf = new char[lSize + 1];
	size_t uiRead = fread(pBuf, 1, lSize, file);
	fclose(file);
	pBuf[uiRead] = 0;
	int iRet = get_parameter_gptflag(pBuf);
	delete []pBuf;
	return iRet;
}

bool UpgradeEngine::WriteParameter(STRUCT_RKDEVICE_DESC &dev, char *szParameter, CRKComm *pExisting)
{
	if (!CheckDeviceType(dev, RKUSB_MASKROM | RKUSB_LOADER))
		return false;
	char *pParamBuf = NULL;
	bool bRet = false;
	CRKComm *pComm = pExisting;
	bool bOwn = false;
	if (!pComm) {
		pComm = new CRKUsbComm(dev, m_pLog, bRet);
		bOwn = true;
		if (!bRet) {
			Emit(LogLevel::Info, "Write parameter quit, creating comm object failed!\r\n");
			return false;
		}
	}
	if (!MakeParamBuffer(szParameter, pParamBuf)) {
		Emit(LogLevel::Info, "Generating parameter failed!\r\n");
		if (bOwn) delete pComm;
		return false;
	}
	Emit(LogLevel::Info, "Writing parameter...\r\n");
	int nParamSize = *(UINT *)(pParamBuf + 4) + 12;
	int nParamSec = BYTE2SECTOR(nParamSize);
	if (nParamSec > 1024) {
		Emit(LogLevel::Info, "parameter is too large!\r\n");
		delete []pParamBuf;
		if (bOwn) delete pComm;
		return false;
	}
	std::vector<BYTE> writeBuf(nParamSec * SECTOR_SIZE, 0);
	memcpy(writeBuf.data(), pParamBuf, nParamSize);
	int iRet = pComm->RKU_WriteLBA(0x2000, nParamSec, writeBuf.data());
	bool bSuccess = (iRet == ERR_SUCCESS);

	Emit(LogLevel::Info, bSuccess ? "Writing parameter succeeded.\r\n" : "Writing parameter failed!\r\n");
	delete []pParamBuf;
	if (bOwn) delete pComm;
	return bSuccess;
}

bool UpgradeEngine::WriteGpt(STRUCT_RKDEVICE_DESC &dev, char *szParameter, CRKComm *pExisting)
{
	u8 flash_info[SECTOR_SIZE], master_gpt[34 * SECTOR_SIZE], backup_gpt[33 * SECTOR_SIZE];
	u32 total_size_sector;
	bool bRet = false, bSuccess = false;
	if (!CheckDeviceType(dev, RKUSB_MASKROM | RKUSB_LOADER))
		return false;
	CRKComm *pComm = pExisting;
	bool bOwn = false;
	if (!pComm) {
		pComm = new CRKUsbComm(dev, m_pLog, bRet);
		bOwn = true;
		if (!bRet) {
			Emit(LogLevel::Info, "Write gpt quit, creating comm object failed!\r\n");
			return false;
		}
	}
	if (pComm->RKU_ReadFlashInfo(flash_info) != ERR_SUCCESS) {
		Emit(LogLevel::Info, "Reading Flash Info failed!\r\n");
		if (bOwn) delete pComm;
		return false;
	}
	total_size_sector = *(u32 *)flash_info;
	if (strstr(szParameter, ".img")) {
		if (!load_gpt_buffer(szParameter, master_gpt, backup_gpt)) {
			Emit(LogLevel::Info, "Loading partition image failed!\r\n");
			if (bOwn) delete pComm;
			return false;
		}
		update_gpt_disksize(master_gpt, backup_gpt, total_size_sector);
	} else {
		PARAM_ITEM_VECTOR vecItems;
		CONFIG_ITEM_VECTOR vecUuid;
		if (!parse_parameter_file(szParameter, vecItems, vecUuid)) {
			Emit(LogLevel::Info, "Parsing parameter failed!\r\n");
			if (bOwn) delete pComm;
			return false;
		}
		create_gpt_buffer(master_gpt, vecItems, vecUuid, total_size_sector, NULL);
		memcpy(backup_gpt, master_gpt + 2 * SECTOR_SIZE, 32 * SECTOR_SIZE);
		memcpy(backup_gpt + 32 * SECTOR_SIZE, master_gpt + SECTOR_SIZE, SECTOR_SIZE);
		prepare_gpt_backup(master_gpt, backup_gpt);
	}
	if (pComm->RKU_WriteLBA(0, 34, master_gpt) != ERR_SUCCESS) {
		Emit(LogLevel::Info, "Writing master gpt failed!\r\n");
		if (bOwn) delete pComm;
		return false;
	}
	if (pComm->RKU_WriteLBA(total_size_sector - 33, 33, backup_gpt) != ERR_SUCCESS) {
		Emit(LogLevel::Info, "Writing backup gpt failed!\r\n");
		if (bOwn) delete pComm;
		return false;
	}
	bSuccess = true;
	if (bOwn) delete pComm;
	return bSuccess;
}

bool UpgradeEngine::GetPartitionLba(STRUCT_RKDEVICE_DESC &dev, char *szPartName, u64 *lba, u64 *lbaEnd)
{
	u8 master_gpt[34 * SECTOR_SIZE];
	std::vector<u8> paramStorage(512 * SECTOR_SIZE);
	u8 *param_buffer = paramStorage.data();
	u32 part_offset, part_size;
	if (ReadGpt(dev, master_gpt) && get_lba_from_gpt(master_gpt, szPartName, lba, lbaEnd))
		return true;
	if (ReadParam(dev, param_buffer) &&
	    get_lba_from_param(param_buffer + 8, szPartName, &part_offset, &part_size)) {
		*lba = part_offset;
		*lbaEnd = (part_size == 0xFFFFFFFF) ? 0xFFFFFFFF : (part_offset + part_size);
		return true;
	}
	return false;
}

bool UpgradeEngine::DownloadImage(STRUCT_RKDEVICE_DESC &dev, char *szPartName, UINT uiOffset,
                                  UINT uiSize, char *szImage)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bSuccess;
	if (is_sparse_image(szImage)) {

		bSuccess = WriteSparseLba(dev, uiOffset, uiSize, szImage, NULL, 0);
	} else {
		bSuccess = true;
		if (is_ubifs_image(szImage))
			bSuccess = EraseUbiBlock(dev, uiOffset, uiSize);
		if (bSuccess)

			bSuccess = WriteLba(dev, uiOffset, szImage, NULL, 0);
		else
			Emit(LogLevel::Info, "Failure of Erase for writing ubi image!\r\n");
	}
	(void)szPartName;
	return bSuccess;
}


bool UpgradeEngine::Fail(ErrorCode code) { m_lastResult.error = code; return false; }

void UpgradeEngine::BeginTask()
{
	CRKDevice::ClearAbort();
	if (m_cancelled && m_cancelled->load()) CRKDevice::RequestAbort();
}

bool UpgradeEngine::ResolveDevice(STRUCT_RKDEVICE_DESC &device)
{
	if (!m_pScan) return Fail(ErrorCode::DeviceMissing);
	m_pScan->Search(RKUSB_MASKROM | RKUSB_LOADER);
	std::vector<DeviceInfo> devices = ScanDevices();
	std::vector<DeviceIdentity> identities;
	for (const auto &info : devices) {
		DeviceIdentity identity; identity.location = info.uiLocationID; identity.serial = info.strSerial;
		identities.push_back(identity);
	}
	int match = MatchDevice(identities, m_target);
	if (match == -2) return Fail(ErrorCode::DeviceAmbiguous);
	if (match < 0 || !m_pScan->GetDevice(device, match)) return Fail(ErrorCode::DeviceMissing);
	return true;
}

bool UpgradeEngine::DownloadPartition(STRUCT_RKDEVICE_DESC &dev, const std::string &partition, char *path)
{
	if (partition == "parameter") {
		return ParameterFileGptFlag(path) ? WriteGpt(dev, path) : WriteParameter(dev, path);
	}
	u64 begin = 0, end = 0;
	if (!GetPartitionLba(dev, const_cast<char *>(partition.c_str()), &begin, &end)) return false;
	if (begin > UINT32_MAX || (end != UINT32_MAX && (end < begin || end - begin > UINT32_MAX))) return Fail(ErrorCode::InvalidInput);
	UINT size = end == UINT32_MAX ? UINT32_MAX : static_cast<UINT>(end - begin);
	return DownloadImage(dev, const_cast<char *>(partition.c_str()), static_cast<UINT>(begin), size, path);
}

bool UpgradeEngine::Dispatch(const OperationRequest &r)
{
	if (static_cast<int>(r.operation) < 0 || r.operation >= Operation::Count) return Fail(ErrorCode::InvalidInput);
	bool needsFile = r.operation == Operation::FirmwareUpgrade || r.operation == Operation::FirmwareExtract ||
		r.operation == Operation::FirmwareInfo || r.operation == Operation::LoaderUpgrade ||
		r.operation == Operation::DownloadBoot || r.operation == Operation::EraseFlash ||
		r.operation == Operation::DownloadImage || r.operation == Operation::CreateGpt ||
		r.operation == Operation::WriteLba || r.operation == Operation::WriteSector;
	if (needsFile) {
		if (r.file.path.empty()) return Fail(ErrorCode::InvalidInput);
		FILE *file = fopen(r.file.path.c_str(), "rb");
		if (!file) return Fail(ErrorCode::FileAccess);
		fclose(file);
	}
	if ((r.operation == Operation::FirmwareExtract || r.operation == Operation::CreateGpt ||
	     r.operation == Operation::ReadComLog) && r.file.output.empty()) return Fail(ErrorCode::InvalidInput);
	if (r.operation == Operation::DownloadImage && r.partition.empty()) return Fail(ErrorCode::InvalidInput);
	if ((r.operation == Operation::ReadLba || r.operation == Operation::EraseLba || r.operation == Operation::EraseBlock) &&
	    (!r.lba.count || uint64_t(r.lba.begin) + r.lba.count > uint64_t(UINT32_MAX) + 1)) return Fail(ErrorCode::InvalidInput);
	if (r.operation == Operation::ResetDevice && r.subcode > 5) return Fail(ErrorCode::InvalidInput);
	if (r.operation == Operation::ResetPipe && r.pipe > 255) return Fail(ErrorCode::InvalidInput);
	if (r.operation == Operation::EraseBlock && r.block.chipSelect > 255) return Fail(ErrorCode::InvalidInput);
	if (r.operation == Operation::WriteSector && (!r.sector.pageSize || r.sector.pageSize > UINT32_MAX / 1024 ||
	    r.sector.spareSize > UINT32_MAX - r.sector.pageSize * 1024)) return Fail(ErrorCode::InvalidInput);
	if (r.operation == Operation::WriteSn && (r.serial.empty() || r.serial.size() > 505)) return Fail(ErrorCode::InvalidInput);
	char *path = const_cast<char *>(r.file.path.c_str());
	char *output = const_cast<char *>(r.file.output.c_str());
	if (r.operation == Operation::CreateGpt) return CreateGptByParameter(path, output);
	if (r.operation == Operation::FirmwareInfo) return ShowFirmwareInfo(path, r.entryIndex);
	if (r.operation == Operation::FirmwareExtract) return ExtractFirmware(path, output);
	STRUCT_RKDEVICE_DESC dev;
	if (!ResolveDevice(dev) || IsAborted()) return false;
	switch (r.operation) {
	case Operation::FirmwareUpgrade: return UpgradeFirmware(dev, path, !r.reboot);
	case Operation::LoaderUpgrade: return UpgradeLoader(dev, path);
	case Operation::DownloadBoot: return DownloadBoot(dev, path);
	case Operation::EraseFlash: return EraseFlash(dev, path);
	case Operation::PartitionList: return PrintGpt(dev) || PrintParameter(dev);
	case Operation::DownloadImage: return DownloadPartition(dev, r.partition, path);
	case Operation::SwitchDevice: return SwitchDevice(dev);
	case Operation::ReadSn: return ReadVendorSn(dev);
	case Operation::WriteSn: return WriteVendorSn(dev, const_cast<char *>(r.serial.c_str()));
	case Operation::ReadComLog: return ReadComLog(dev, output);
	case Operation::ReadCapability: return ReadCapability(dev, true);
	case Operation::ReadFlashId: return ReadFlashId(dev);
	case Operation::ReadFlashInfo: return ReadFlashInfo(dev);
	case Operation::ReadChipInfo: return ReadChipInfo(dev);
	case Operation::ReadOtpChip: return ReadOtpChip(dev);
	case Operation::ReadSecureMode: return ReadSecureMode(dev);
	case Operation::TestDevice: return TestDevice(dev);
	case Operation::ResetDevice: return ResetDevice(dev, static_cast<BYTE>(r.subcode));
	case Operation::ResetPipe: return ResetPipe(dev, static_cast<BYTE>(r.pipe));
	case Operation::ReadLba: return ReadLba(dev, r.lba.begin, r.lba.count, r.file.output.empty() ? NULL : output);
	case Operation::WriteLba: return WriteLba(dev, r.lba.begin, path);
	case Operation::EraseLba: return EraseLba(dev, r.lba.begin, r.lba.count);
	case Operation::WriteSector: return WriteSector(dev, r.lba.begin, r.sector.pageSize, r.sector.spareSize, path);
	case Operation::EraseBlock: return EraseBlock(dev, static_cast<BYTE>(r.block.chipSelect), r.lba.begin, r.lba.count, r.block.force);
	case Operation::RunSystem: return RunSystem(dev, r.system.ubootAddress, r.system.trustAddress, r.system.bootAddress,
		const_cast<char *>(r.system.uboot.c_str()), const_cast<char *>(r.system.trust.c_str()), const_cast<char *>(r.system.boot.c_str()));
	case Operation::SwitchStorage: return SwitchStorage(dev, r.storageIndex);
	default: return Fail(ErrorCode::InvalidInput);
	}
}

OperationResult UpgradeEngine::Execute(const OperationRequest &request)
{
	ResetProgress(0); ResetDownloadProgress();
	m_lastResult = OperationResult();
	m_lastResult.operation = request.operation;
	UpgradeEngine *previous = g_pEngine;
	g_pEngine = this;
	m_bCaptureResultText = true;
	bool ok = false;
	try { ok = !IsAborted() && Dispatch(request); }
	catch (const std::exception &error) { Fail(ErrorCode::OperationFailed); Emit(LogLevel::Error, error.what()); }
	m_bCaptureResultText = false;
	g_pEngine = previous;
	m_lastResult.status = IsAborted() ? OperationStatus::Cancelled :
		(ok && m_lastResult.error == ErrorCode::None ? OperationStatus::Succeeded : OperationStatus::Failed);
	if (!m_lastResult.ok() && m_lastResult.status != OperationStatus::Cancelled && m_lastResult.error == ErrorCode::None)
		m_lastResult.error = ErrorCode::OperationFailed;
	EmitResult(m_lastResult);
	return m_lastResult;
}

}
