#ifndef RKDEVICE_HEADER
#define RKDEVICE_HEADER
#include <stop_token>
#include "RKImage.h"
#include "RKComm.h"
#include "platform/LogService.h"
#include "DefineHeader.h"
#include "crc.h"

#define SECTOR_SIZE 512
#define PAGE_SIZE 2048
#define SPARE_SIZE 16
#define CHIPINFO_LEN 16
#define IDBLOCK_TOP 50
#define MAX_IDB_COUNT 4

#define CALC_UNIT(a, b)		((a > 0) ? ((a - 1) / b + 1) : (a))
#define BYTE2SECTOR(x)		(CALC_UNIT(x, SECTOR_SIZE))
#define PAGEALIGN(x)		(CALC_UNIT(x, 4))

#pragma pack(1)
typedef struct _STRUCT_FLASH_INFO {
	char	szManufacturerName[16];
	UINT	uiFlashSize;
	USHORT	usBlockSize;
	UINT	uiPageSize;
	UINT	uiSectorPerBlock;
	BYTE	blockState[IDBLOCK_TOP];
	UINT	uiBlockNum;
	BYTE	bECCBits;
	BYTE	bAccessTime;
	BYTE	bFlashCS;
	USHORT  usValidSecPerBlock;
	USHORT  usPhyBlokcPerIDB;
	UINT    uiSecNumPerIDB;
} STRUCT_FLASH_INFO, *PSTRUCT_FLASH_INFO;
typedef struct _STRUCT_FLASHINFO_CMD {
	UINT	uiFlashSize;
	USHORT	usBlockSize;
	BYTE	bPageSize;
	BYTE	bECCBits;
	BYTE	bAccessTime;
	BYTE	bManufCode;
	BYTE	bFlashCS;
	BYTE	reserved[501];
} STRUCT_FLASHINFO_CMD, *PSTRUCT_FLASHINFO_CMD;
#pragma pack()

class CRKDevice
{
    std::stop_token m_cancellation;
    bool m_hasForceDataBand=false;
public:
	USHORT GetVendorID();
	void SetVendorID(USHORT value);
	property<CRKDevice, USHORT, READ_WRITE> VendorID;

	USHORT GetProductID();
	void SetProductID(USHORT value);
	property<CRKDevice, USHORT, READ_WRITE> ProductID;

	ENUM_RKDEVICE_TYPE GetDeviceType();
	void SetDeviceType(ENUM_RKDEVICE_TYPE value);
	property<CRKDevice, ENUM_RKDEVICE_TYPE, READ_WRITE> DeviceType;

	ENUM_RKUSB_TYPE GetUsbType();
	void SetUsbType(ENUM_RKUSB_TYPE value);
	property<CRKDevice, ENUM_RKUSB_TYPE, READ_WRITE> UsbType;

	string GetLayerName();
	void SetLayerName(string value);
	property<CRKDevice, string, READ_WRITE> LayerName;

	string GetLinkName();
	void SetLinkName(string value);

	string GetSerialName();
	void SetSerialName(string value);

	DWORD GetLocationID();
	void SetLocationID(DWORD value);
	property<CRKDevice, DWORD, READ_WRITE> LocationID;

	USHORT GetBcdUsb();
	void SetBcdUsb(USHORT value);
	property<CRKDevice, USHORT, READ_WRITE> BcdUsb;

	ENUM_OS_TYPE GetOsType();
	void SetOsType(ENUM_OS_TYPE value);
	property<CRKDevice, ENUM_OS_TYPE, READ_WRITE> OsType;

	platform::LogService *GetLogObjectPointer();
	property<CRKDevice, platform::LogService *, READ_ONLY> LogObjectPointer;

	CRKComm *GetCommObjectPointer();
	property<CRKDevice, CRKComm *, READ_ONLY> CommObjectPointer;

	void SetCallBackPointer(ProgressPromptCB value);
	property<CRKDevice, ProgressPromptCB, WRITE_ONLY> CallBackPointer;

    void SetCancellation(std::stop_token token) { m_cancellation = token; }
    bool IsAborted() const { return m_cancellation.stop_requested(); }

	UINT GetWorkFlow();
	void SetWorkFlow(UINT value);

	PSTRUCT_BLOCK_STATE GetBlockStatePointer();
	void SetBlockStatePointer(PSTRUCT_BLOCK_STATE value);

	void SetRemallocDisk(bool value);
	bool GetRemallocDisk();

	void SetPrepareEraseFlag(bool value);
	void SetMiscModifyFlag(ENUM_MISC_MODIFY_FLAG value);
	void SetGetNewDiskSizeFlag(bool value);

	PBYTE GetCustomData();
	USHORT GetCustomDataSize();
	USHORT GetCustomDataOffset();
	void SetCustomDataSize(USHORT value);
	void SetCustomDataOffset(USHORT value);

	PBYTE GetSn();
	unsigned char GetSnSize();
	void SetSnSize(unsigned char value);

	PBYTE GetMac();
	unsigned char GetMacSize();
	PBYTE GetImei();
	unsigned char GetImeiSize();
	PBYTE GetUid();
	unsigned char GetUidSize();
	PBYTE GetBTAddr();
	unsigned char GetBTSize();
	PBYTE GetUidWriteOKPtr();
	unsigned char GetUidWriteOK();

	bool GetDirectLBA();
	void SetDirectLBA(bool value);
	bool GetReadIDBConfigFlag();
	void SetReadIDBConfigFlag(bool value);
	bool GetSecureModeFlag();
	void SetSecureModeFlag(bool value);
	bool GetNewIDBFlag();
	void SetNewIDBFlag(bool value);
	bool GetFirst4Access();
	void SetFirst4Access(bool value);
	bool GetGptFlag();
	void SetGptFlag(bool value);
	bool GetNoCheckMode();
	void SetNoCheckMode(bool value);
	bool GetNorSingleIDB();
	void SetNorSingleIDB(bool value);
	UINT GetForceDataBand();
	void SetForceDataBand(UINT value);
    bool HasForceDataBand() const { return m_hasForceDataBand; }
	UINT GetIDBend();
	bool GetEmmcStor();
	bool GetNorStor();
	int GetError();

	void SetSn(PBYTE value);
	void SetMac(PBYTE value);
	void SetImei(PBYTE value);
	void SetUid(PBYTE value);
	void SetBTAddr(PBYTE value);
	void SetCustomData(PBYTE value);

	int  BuildBlockStateMap(UCHAR ucFlashCS);
	int  ReadMutilSector(UINT uiPos, UINT uiCount, PBYTE lpBuffer);
	UCHAR RandomByte(UCHAR ucMin, UCHAR ucMax);
	bool CheckCrc16(PBYTE lpData, USHORT usCrc, USHORT usSize);
	bool CheckUid(UCHAR ucUidSize, PBYTE lpUid);
	int  GetIDBData(UINT uiCount, PBYTE lpBuffer, UINT uiSectorNum);
	int  GetWriteBackData(UINT uiCount, PBYTE lpBuffer);
	char FindValidBlocks(char cStart, char cCount);
	bool FormatDisk(char cDiskIndex, char *pName, platform::LogService *pLog);
	bool CopyData(char cSrc, char *pName, platform::LogService *pLog);

	virtual int DownloadBoot();
	bool TestDevice();
	bool ResetDevice(UCHAR ucSubCode = RST_NONE_SUBCODE);
	bool PowerOffDevice();
	bool CheckChip();
	bool GetFlashInfo();
	bool ReadCapability();
	bool LowerFormatDevice(bool &bRet);
	bool SetResetFlag();
	bool SendExtCommand(char cCommand, UINT uiCommand, platform::LogService *pLog, PBYTE pData, UINT uiSize);
	bool EraseMutilBlock(UCHAR ucFlashCS, UINT uiPos, UINT uiCount, bool bForce);
	bool EraseEmmcIDB();
	int EraseAllBlocks(bool force_block_erase=false);
	bool SetObject(CRKImage *pImage, CRKComm *pComm, platform::LogService *pLog);
	// Rebuilds the comm object for a re-enumerated device (after a reset the
	// USB address changes, so the previous handle is dead).
	bool RefreshComm(STRUCT_RKDEVICE_DESC &deviceDesc);
	string GetLayerString(UINT dwLocationID);
	CRKDevice(STRUCT_RKDEVICE_DESC &device);
	virtual ~CRKDevice();
protected:
	STRUCT_FLASH_INFO m_flashInfo;
	PBYTE	m_pFlashInfoData;
	USHORT	m_usFlashInfoDataOffset;
	USHORT  m_usFlashInfoDataLen;
	PBYTE  m_chipData;
	CRKImage *m_pImage;
	CRKComm  *m_pComm;
	platform::LogService   *m_pLog;
	ProgressPromptCB m_callBackProc;
	bool m_bEmmc;
	bool m_bDirectLba;
	bool m_bFirst4mAccess;
	UINT m_uiWorkFlow;

	USHORT m_usFlashDataSec;	// +0x74c flash data size in sectors
	UCHAR  m_ucPageSize;		// +0x74e page size in 512-byte units
	UINT   m_uiEraseSectorPerUnit;	// +0x752 sectors per erase unit
	PSTRUCT_BLOCK_STATE m_pBlockState;	// +0x828
	UINT   m_uiIDBBlock[MAX_IDB_COUNT];	// +0x7a4 reserved IDB block numbers
	PBYTE  m_pSn;			// +0x7e8 sn buffer
	UCHAR  m_ucSnSize;		// +0x7f0
	PBYTE  m_pMac;			// +0x7f8
	PBYTE  m_pBTAddr;		// +0x800
	PBYTE  m_pUid;			// +0x808
	PBYTE  m_pImei;			// +0x810
	PBYTE  m_pCustomData;		// +0x7d8
	USHORT m_usCustomDataSize;	// +0x7e0
	USHORT m_usCustomDataOffset;	// +0x7e2
	bool   m_bGetNewDiskSizeFlag;	// +0x81c
	bool   m_bRemallocDisk;		// +0xa5b
	bool   m_bEmmcStor;		// +0xa5c
	bool   m_bNorStor;		// +0xa5d
	bool   m_bUidWriteOK;		// +0xa5a
	bool   m_bPrepareEraseFlag;	// +0xa58
	bool   m_bDirectLbaFlag;	// +0xa5e
	bool   m_bFirst4AccessFlag;	// +0xa5f
	bool   m_bGptFlag;		// +0xa60
	bool   m_bNoCheckMode;		// +0xa61
	bool   m_bNorSingleIDB;		// +0xa62
	bool   m_bReadIDBConfigFlag;	// +0xa63
	bool   m_bSecureModeFlag;	// +0xa64
	bool   m_bNewIDBFlag;		// +0xa65
	UINT   m_uiForceDataBand;	// +0xa68
	UINT   m_uiMiscModifyFlag;	// +0xa6c
	UINT   m_uiIDBend;		// +0xa50
	int    m_iError;		// +0xa54

	int EraseEmmcBlock(UCHAR ucFlashCS, DWORD dwPos, DWORD dwCount);
	int EraseEmmcByWriteLBA(DWORD dwSectorPos, DWORD dwCount);
	bool EraseEmmc();
	bool Boot_VendorRequest(DWORD requestCode, PBYTE pBuffer, DWORD dwDataSize);
private:
	USHORT m_vid;
	USHORT m_pid;
	ENUM_RKDEVICE_TYPE m_device;
	ENUM_OS_TYPE m_os;
	ENUM_RKUSB_TYPE m_usb;
	UINT m_locationID;
	USHORT m_bcdUsb;
protected:
	string m_layerName;
	string m_linkName;
	string m_serialName;
};

#endif
