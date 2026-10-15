#include "backends/rockchip/UpgradeEngine.h"
#include "backends/rockchip/FirmwareService.h"
#include "domain/DeviceSelection.h"

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

static void RecordRawToLog(platform::FileLog *pLog, const std::string &strText)
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
	if (m_cancelled) m_cancelled->request_stop();
	m_abort.request_stop();
}

bool UpgradeEngine::IsAborted() const
{
	return (m_cancelled && m_cancelled->stop_requested()) || m_abort.stop_requested();
}

void UpgradeEngine::SetLogObject(platform::FileLog *pLog)
{
	m_pLog = pLog;
}

platform::FileLog *UpgradeEngine::GetLogObject() const
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
	if (m_abort.stop_requested())
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
		m_pObserver->OnProgress(static_cast<ProgressPrompt>(emPrompt), llTotal, llCurrent, static_cast<CallStep>(emCall));
}

void UpgradeEngine::EmitUpgradePrompt(ENUM_UPGRADE_PROMPT emPrompt, unsigned int uiParam)
{
	if (m_pObserver)
		m_pObserver->OnUpgradePrompt(static_cast<UpgradePrompt>(emPrompt), uiParam);
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










bool UpgradeEngine::Fail(ErrorCode code) { m_lastResult.error = code; return false; }

void UpgradeEngine::SetCancellation(const Cancellation &cancelled)
{
    m_cancelCallback.reset();
    m_cancelled=cancelled;
    if(cancelled) m_cancelCallback=std::make_unique<std::stop_callback<std::function<void()>>>(
        cancelled->get_token(), [this] { m_abort.request_stop(); });
}

void UpgradeEngine::BeginTask()
{
	m_abort=std::stop_source{};
	if (m_cancelled && m_cancelled->stop_requested()) m_abort.request_stop();
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



bool UpgradeEngine::Dispatch(const OperationRequest &r)
{
	if (static_cast<int>(r.operation()) < 0 || r.operation() >= Operation::Count) return Fail(ErrorCode::InvalidInput);
	bool needsFile = r.operation()== Operation::FirmwareUpgrade || r.operation() == Operation::FirmwareExtract ||
		r.operation() == Operation::FirmwareInfo || r.operation() == Operation::LoaderUpgrade ||
		r.operation() == Operation::DownloadBoot || r.operation() == Operation::EraseFlash ||
		r.operation() == Operation::DownloadImage || r.operation() == Operation::CreateGpt ||
		r.operation() == Operation::WriteLba || r.operation() == Operation::WriteSector;
	if (needsFile) {
		if (r.file().path.empty()) return Fail(ErrorCode::InvalidInput);
		FILE *file = fopen(r.file().path.c_str(), "rb");
		if (!file) return Fail(ErrorCode::FileAccess);
		fclose(file);
	}
	if ((r.operation()== Operation::FirmwareExtract || r.operation() == Operation::CreateGpt ||
	     r.operation() == Operation::ReadComLog) && r.file().output.empty()) return Fail(ErrorCode::InvalidInput);
	if (r.operation()== Operation::DownloadImage && r.rockchip().partition.empty()) return Fail(ErrorCode::InvalidInput);
	if ((r.operation()== Operation::ReadLba || r.operation() == Operation::EraseLba || r.operation() == Operation::EraseBlock) &&
	    (!r.rockchip().lba.count || uint64_t(r.rockchip().lba.begin) + r.rockchip().lba.count > uint64_t(UINT32_MAX) + 1)) return Fail(ErrorCode::InvalidInput);
	if (r.operation()== Operation::ResetDevice && r.rockchip().subcode > 5) return Fail(ErrorCode::InvalidInput);
	if (r.operation()== Operation::ResetPipe && r.rockchip().pipe > 255) return Fail(ErrorCode::InvalidInput);
	if (r.operation()== Operation::EraseBlock && r.rockchip().block.chipSelect > 255) return Fail(ErrorCode::InvalidInput);
	if (r.operation()== Operation::WriteSector && (!r.rockchip().sector.pageSize || r.rockchip().sector.pageSize > UINT32_MAX / 1024 ||
	    r.rockchip().sector.spareSize > UINT32_MAX - r.rockchip().sector.pageSize * 1024)) return Fail(ErrorCode::InvalidInput);
	if (r.operation()== Operation::WriteSn && (r.rockchip().serial.empty() || r.rockchip().serial.size() > 505)) return Fail(ErrorCode::InvalidInput);
	char *path = const_cast<char *>(r.file().path.c_str());
	char *output = const_cast<char *>(r.file().output.c_str());
	if (r.operation()== Operation::CreateGpt) return CreateGptByParameter(path, output);
	if (r.operation()== Operation::FirmwareInfo) return ShowFirmwareInfo(path, r.rockchip().entryIndex);
	if (r.operation()== Operation::FirmwareExtract) return ExtractFirmware(path, output);
	STRUCT_RKDEVICE_DESC dev;
	if (!ResolveDevice(dev) || IsAborted()) return false;
	switch (r.operation()) {
	case Operation::FirmwareUpgrade: return UpgradeFirmware(dev, path, !r.rockchip().reboot);
	case Operation::LoaderUpgrade: return UpgradeLoader(dev, path);
	case Operation::DownloadBoot: return DownloadBoot(dev, path);
	case Operation::EraseFlash: return EraseFlash(dev, path);
	case Operation::PartitionList: return PrintGpt(dev) || PrintParameter(dev);
	case Operation::DownloadImage: return DownloadPartition(dev, r.rockchip().partition, path);
	case Operation::SwitchDevice: return SwitchDevice(dev);
	case Operation::ReadSn: return ReadVendorSn(dev);
	case Operation::WriteSn: return WriteVendorSn(dev, const_cast<char *>(r.rockchip().serial.c_str()));
	case Operation::ReadComLog: return ReadComLog(dev, output);
	case Operation::ReadCapability: return ReadCapability(dev, true);
	case Operation::ReadFlashId: return ReadFlashId(dev);
	case Operation::ReadFlashInfo: return ReadFlashInfo(dev);
	case Operation::ReadChipInfo: return ReadChipInfo(dev);
	case Operation::ReadOtpChip: return ReadOtpChip(dev);
	case Operation::ReadSecureMode: return ReadSecureMode(dev);
	case Operation::TestDevice: return TestDevice(dev);
	case Operation::ResetDevice: return ResetDevice(dev, static_cast<BYTE>(r.rockchip().subcode));
	case Operation::ResetPipe: return ResetPipe(dev, static_cast<BYTE>(r.rockchip().pipe));
	case Operation::ReadLba: return ReadLba(dev, r.rockchip().lba.begin, r.rockchip().lba.count, r.file().output.empty() ? NULL : output);
	case Operation::WriteLba: return WriteLba(dev, r.rockchip().lba.begin, path);
	case Operation::EraseLba: return EraseLba(dev, r.rockchip().lba.begin, r.rockchip().lba.count);
	case Operation::WriteSector: return WriteSector(dev, r.rockchip().lba.begin, r.rockchip().sector.pageSize, r.rockchip().sector.spareSize, path);
	case Operation::EraseBlock: return EraseBlock(dev, static_cast<BYTE>(r.rockchip().block.chipSelect), r.rockchip().lba.begin, r.rockchip().lba.count, r.rockchip().block.force);
	case Operation::RunSystem: return RunSystem(dev, r.rockchip().system.ubootAddress, r.rockchip().system.trustAddress, r.rockchip().system.bootAddress,
		const_cast<char *>(r.rockchip().system.uboot.c_str()), const_cast<char *>(r.rockchip().system.trust.c_str()), const_cast<char *>(r.rockchip().system.boot.c_str()));
	case Operation::SwitchStorage: return SwitchStorage(dev, r.rockchip().storageIndex);
	default: return Fail(ErrorCode::InvalidInput);
	}
}

OperationResult UpgradeEngine::Execute(const OperationRequest &request)
{
	ResetProgress(0); ResetDownloadProgress();
	m_lastResult = OperationResult();
	m_lastResult.operation = request.operation();
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
	return m_lastResult;
}

}
