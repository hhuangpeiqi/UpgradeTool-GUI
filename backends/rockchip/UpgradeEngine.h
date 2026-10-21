#include "backends/DeviceIdentity.h"
#ifndef CORE_UPGRADE_ENGINE_H
#define CORE_UPGRADE_ENGINE_H
#include <string>
#include <functional>
#include <vector>
#include "DefineHeader.h"
#include "platform/FileLog.h"
#include "RKScan.h"
#include "RKComm.h"
#include "gpt.h"
#include "domain/UpgradeObserver.h"

class CRKComm;
class CRKDevice;
class CRKImage;
class CRKBoot;
class CRKUpgrade;
class CRKAndroidDevice;

#ifndef DEFAULT_RW_LBA
#define DEFAULT_RW_LBA 128
#endif

namespace core {

class UpgradeEngine
{
public:
	enum StorageCode {
		STORAGE_FLASH   = 0x001,
		STORAGE_EMMC    = 0x002,
		STORAGE_SD      = 0x004,
		STORAGE_SD1     = 0x008,
		STORAGE_RAM     = 0x040,
		STORAGE_USB     = 0x080,
		STORAGE_SPINAND = 0x100,
		STORAGE_SPINOR  = 0x200,
		STORAGE_SATA    = 0x400,
		STORAGE_PCIE    = 0x800
	};

	struct DeviceInfo {
		int iIndex;
		unsigned int uiVid;
		unsigned int uiPid;
		unsigned int uiLocationID;
		int iUsbType;
        int accessError = 0;
		std::string strMode;
		std::string strSerial;
	};

	UpgradeEngine();
	~UpgradeEngine();

	std::vector<DeviceInfo> ScanDevices();

	void SetObserver(IUpgradeObserver *pObserver);
	IUpgradeObserver *GetObserver() const;

	void RequestAbort();
	bool IsAborted() const;

	void SetLogObject(platform::FileLog *pLog);
	platform::FileLog *GetLogObject() const;

	void SetScan(CRKScan *pScan);
	CRKScan *GetScan() const;

	void ResetProgress(long long llTotal);
	void StepProgress(long long llBytes);
	void ResetDownloadProgress();
	void SetDownloadProgress(long long llTotal, long long llCurrent);

	void Emit(LogLevel emLevel, const char *lpFmt, ...);
	void Emit(LogLevel emLevel, const std::string &strText);
	void EmitProgress(ENUM_PROGRESS_PROMPT emPrompt, long long llTotal,
	                  long long llCurrent, ENUM_CALL_STEP emCall);
	void EmitTransferProgress(TransferStage stage, long long total, long long current);
	void EmitUpgradePrompt(ENUM_UPGRADE_PROMPT emPrompt, unsigned int uiParam);
	int  SelectStorage(const std::vector<std::string> &vecOptions, int iCurrent);

	OperationResult Execute(const OperationRequest &request);
	void SetCancellation(const Cancellation &cancelled);
	void BeginTask();
    void SetConfig(const ConfigSnapshot &config) { m_config=config; }
	void SetTarget(const DeviceIdentity &target) { m_target = target; }

private:
	bool Dispatch(const OperationRequest &request);
	bool ResolveDevice(STRUCT_RKDEVICE_DESC &device);
	bool DownloadPartition(STRUCT_RKDEVICE_DESC &device, const std::string &partition, char *path);
	bool Fail(ErrorCode code);

	bool CheckDeviceType(STRUCT_RKDEVICE_DESC &dev, UINT uiSupportType);
	DWORD GetFileSize(char *szFile);
	std::string HexList(PBYTE pData, int nSize);
	void PrintData(PBYTE pData, int nSize);
	std::string GetErrString(UINT uiErr);

	bool TestDevice(STRUCT_RKDEVICE_DESC &dev);
	bool EnterMaskrom(STRUCT_RKDEVICE_DESC &dev);
	bool ResetDevice(STRUCT_RKDEVICE_DESC &dev, BYTE subCode = RST_NONE_SUBCODE);
	bool ReadFlashId(STRUCT_RKDEVICE_DESC &dev);
	bool ReadFlashInfo(STRUCT_RKDEVICE_DESC &dev);
	bool ReadChipInfo(STRUCT_RKDEVICE_DESC &dev);
	bool ReadCapability(STRUCT_RKDEVICE_DESC &dev, bool bPrint);
	bool ReadLba(STRUCT_RKDEVICE_DESC &dev, UINT uiBegin, UINT uiLen, char *szFile);
	bool WriteLba(STRUCT_RKDEVICE_DESC &dev, UINT uiBegin, char *szFile, CRKComm *pExisting = NULL,
	              UINT uiChunkSectors = DEFAULT_RW_LBA);
	bool WriteSparseLba(STRUCT_RKDEVICE_DESC &dev, UINT uiBegin, UINT uiSize, char *szFile, CRKComm *pExisting = NULL,
	                    UINT uiChunkSectors = DEFAULT_RW_LBA);
	bool EraseLba(STRUCT_RKDEVICE_DESC &dev, UINT uiBegin, UINT uiCount);
	bool EraseBlock(STRUCT_RKDEVICE_DESC &dev, BYTE ucFlashCS, UINT uiBegin, UINT uiCount, bool bForce);
	bool ResetPipe(STRUCT_RKDEVICE_DESC &dev, BYTE ucPipe);
	bool ReadOtpChip(STRUCT_RKDEVICE_DESC &dev);
	bool ReadComLog(STRUCT_RKDEVICE_DESC &dev, char *szFile);
	bool SwitchStorage(STRUCT_RKDEVICE_DESC &dev, int iIndex);
	bool SwitchDevice(STRUCT_RKDEVICE_DESC &dev);
	bool ReadSecureMode(STRUCT_RKDEVICE_DESC &dev);
	bool WriteSector(STRUCT_RKDEVICE_DESC &dev, UINT uiBegin, UINT uiPageSize, UINT uiSpareSize, char *szFile);
	bool ReadVendorSn(STRUCT_RKDEVICE_DESC &dev);
	bool WriteVendorSn(STRUCT_RKDEVICE_DESC &dev, char *szSn);
	bool RunSystem(STRUCT_RKDEVICE_DESC &dev, UINT uiUbootAddr, UINT uiTrustAddr,
	               UINT uiBootAddr, char *szUboot, char *szTrust, char *szBoot);

	bool PrintGpt(STRUCT_RKDEVICE_DESC &dev);	bool PrintParameter(STRUCT_RKDEVICE_DESC &dev);
	bool DownloadBoot(STRUCT_RKDEVICE_DESC &dev, char *szLoader);
	bool UpgradeLoader(STRUCT_RKDEVICE_DESC &dev, char *szLoader, bool reboot);
	bool EraseFlash(STRUCT_RKDEVICE_DESC &dev, char *szLoader);
	bool UpgradeFirmware(STRUCT_RKDEVICE_DESC &dev, char *szFirmware, bool bNoReset);
	bool CreateGptByParameter(char *pParameter, char *pOutput);
	bool ShowFirmwareInfo(char *szFirmware, int entryNo);
	bool ExtractFirmware(char *szFirmware, char *szOutDir);
	bool DownloadImage(STRUCT_RKDEVICE_DESC &dev, char *szPartName, UINT uiOffset, UINT uiSize, char *szImage);
	bool WriteParameter(STRUCT_RKDEVICE_DESC &dev, char *szParameter, CRKComm *pExisting = NULL);
	bool WriteGpt(STRUCT_RKDEVICE_DESC &dev, char *szParameter, CRKComm *pExisting = NULL);
	bool EraseUbiBlock(STRUCT_RKDEVICE_DESC &dev, u32 uiOffset, u32 uiPartSize);
	bool ErasePartition(CRKComm *pComm, UINT uiOffset, UINT uiSize);

	bool ReadParam(STRUCT_RKDEVICE_DESC &dev, u8 *pParam);
	bool ReadGpt(STRUCT_RKDEVICE_DESC &dev, u8 *pGpt);
	bool GetPartitionLba(STRUCT_RKDEVICE_DESC &dev, char *szPartName, u64 *lba, u64 *lbaEnd);

	BYTE GetStorageIndex(UINT uiStorage);
	UINT GetStorageValue(std::string strStorage);
	std::string GetStorageString(UINT uiStorage);
	std::string GetCompositeStorageString(UINT uiStorage);
	bool GetDeviceStorage(CRKComm *pComm, BYTE *lpBuffer, StorageCode &uiStorage);
	bool SwitchDeviceStorage(CRKComm *pComm, StorageCode uiStorage);
	void ListStorage(std::vector<std::string> &vecStorage, StorageCode uiCurStorage);

	bool ReadHead(const char *szFile, BYTE *pBuf, size_t uiWant);
	bool IsStorageFirmware(char *pFirmware);
	bool IsUpdateFirmware(char *pFirmware);
	bool IsUpdateLoader(char *pFirmware);
	int ParameterFileGptFlag(char *szParameterFile);
	bool ExtractEntryData(FILE *pFw, u64 uiDataOff, u64 uiSize,
	                      const char *szOutDir, const char *szName);
	bool ExtractUpdateFw(CRKImage *pImage, char *szFirmware, char *szOutDir);
	bool ExtractStorageFw(char *szFirmware, char *szOutDir);
	bool ExtractBootTable(CRKBoot *pBoot, ENUM_RKBOOTENTRY type,
	                      const char *szLabel, const char *szOutDir, bool bPerSector);

	void OnDeviceProgress(ENUM_PROGRESS_PROMPT emPrompt, long long llTotal,
	                      long long llCurrent, ENUM_CALL_STEP emCall);
	static void ProgressThunk(UINT deviceLayer, ENUM_PROGRESS_PROMPT emPrompt,
	                          long long llTotal, long long llCurrent, ENUM_CALL_STEP emCall);
	static void PromptThunk(UINT deviceLayer, ENUM_UPGRADE_PROMPT emPrompt, UINT uiParam);

	IUpgradeObserver *m_pObserver;
	platform::FileLog *m_pLog;
	CRKScan *m_pScan;
	bool m_bReadComLog;
	bool m_bSwitchStorage;
	long long m_uiProgTotal;
	long long m_uiProgDone;
	int m_iProgLast;
	bool m_bProgActive;
	bool m_bProgFirst;
	bool m_bDlFirst;
	int m_iDlLast;
	ENUM_PROGRESS_PROMPT m_lastProgressPrompt = DOWNLOADIMAGE_PROGRESS;
	bool m_bCaptureResultText;
	OperationResult m_lastResult;
	ConfigSnapshot m_config;
    Cancellation m_cancelled;
    std::stop_source m_abort;
    std::unique_ptr<std::stop_callback<std::function<void()>>> m_cancelCallback;
	DeviceIdentity m_target;
};

}

#endif
