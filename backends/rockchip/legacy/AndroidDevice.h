#ifndef ANDROIDDEVICE_HEADER
#define ANDROIDDEVICE_HEADER

#include "RKDevice.h"
#include "gpt.h"

// Device implementation used by upgrade_tool. The only concrete CRKDevice
// subclass instantiated by CDeviceFactory.
//
// Provides the full method set (IDB build/write, sparse/ubi/jffs handling,
// parameter/GPT download, backup-sector search and the RKA_* worker family).
class CRKAndroidDevice : public CRKDevice
{
public:
	struct FW_ENTRY {
		char szPart[33];
		char szFile[33];
		u64  uiSize;
		u64  uiDataOff;
		// Partition mapping carried by the RKAF entry itself (downloads use
		// these, not the parameter text):
		//   +0x5c  partition size in sectors (0xffffffff = grow)
		//   +0x64  partition start LBA  (0xffffffff = not mapped)
		UINT uiPartSize;
		UINT uiPartLba;
	};

	CRKAndroidDevice(STRUCT_RKDEVICE_DESC &device);
	virtual ~CRKAndroidDevice();

	// Image helpers (host-side, no device access).
	bool IsSparseImage(STRUCT_RKIMAGE_ITEM &item);
	bool IsUbiImage(STRUCT_RKIMAGE_ITEM &item);
	bool IsJffsImage(STRUCT_RKIMAGE_ITEM &item);
	u64  GetSparseImageSize(STRUCT_RKIMAGE_ITEM &item);

	// IDB (loader) workflow.
	bool PrepareIDB(bool &bRet);
	bool DownloadIDBlock(bool &bRet);
	bool WriteIDBlock(PBYTE pData, PBYTE pIDB, UINT uiSectorNum, bool bNewIDB);
	bool WriteIDBWithDirect(PBYTE pIDB);
	bool EraseIDB();
	bool BufferWriteBack();

	// IDB sector builders (one 512-byte sector each, no device access).
	bool MakeSector0(PBYTE pSector);
	bool MakeSector1(PBYTE pSector);
	bool MakeSector2(PBYTE pSector);
	bool MakeSector3(PBYTE pSector);
	bool MakeIDBlockData(PBYTE pIDB);
	bool MakeNewIDBlockData(PBYTE pIDB);
	bool MakeSpareData(PBYTE pData, UINT uiSectorCount, PBYTE pSpare);
	bool MakeParamFileBuffer(STRUCT_RKIMAGE_ITEM &item);

	// IDB layout helpers.
	bool FindAllIDB();
	bool FindBackupBuffer();
	bool FindBackupSector3();
	bool FindIDBlock(char cStart, char &cBlock);
	bool ReserveIDBlock(char cStart, char cCount);
	bool OffsetIDBlock(char cDiskIndex);
	bool CalcIDBCount(bool bNewIDB);
	bool CheckIDBData(PBYTE pIDB, bool bCheckCrc);
	bool IDB528To512(PBYTE lpSrc, PBYTE lpDst, PBYTE lpSpare, UINT uiSectorNum);
	bool IDBRC4(PBYTE pSector);
	bool IsExistSector3Crc(RKANDROID_IDB_SEC2 *pSec2);
	bool GetOldIDBData();
	bool GetOldSectorData();
	bool GetReservedBlockByte();
	UINT GetLoaderSize();
	UINT GetLoaderDataSize();
	UINT GetLoaderHeadSize();
	UINT GetNewDiskSize();
	bool GetBlocksState();

	// Environment / partition classification.
	bool IsEnvFirmware(bool &bRet);
	bool EraseAllBlocks();
	bool EraseSparseRegion(UINT uiOffset, UINT uiSize, UINT uiChunk);
	bool EraseUbiRegion(UINT uiOffset, UINT uiPartSize);
	bool EraseSystemPart(UINT uiPartSize);
	bool EraseUserDataPart(UINT uiPartSize);
	bool EraseDirectLBABlock(UINT uiOffset, UINT uiSize);

	int  DownloadBoot();
	bool Boot_VendorRequest(UINT uiRequest, PBYTE pBuffer, UINT uiSize);
	bool ReadCapability();
	bool CheckParamPartSize(PBYTE pParam, int iOffset, int iSize);
	bool GetParameterPartOffset(STRUCT_RKIMAGE_ITEM &item);
	bool GetParameterGptFlag(STRUCT_RKIMAGE_ITEM &item);

	// Firmware package workflow.
	bool LoadFirmwarePackage(const char *szFile);
	int  DownloadImage();
	int  GetEntryCount() const { return m_nEntries; }

	// RKA_* worker family (Check + Download).
	bool RKA_File_Check(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep);
	bool RKA_File_Download(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep);
	bool RKA_SparseFile_Check(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep);
	bool RKA_SparseFile_Download(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep);
	bool RKA_Param_Check(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep);
	bool RKA_Param_Download(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep);
	bool RKA_Gpt_Check(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep);
	bool RKA_Gpt_Download(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep);

protected:
	// Download primitives (operate on m_pComm).
	bool WriteLBA(UINT uiBegin, const char *szFile);
	bool WriteSparseLBA(UINT uiBegin, UINT uiSize, const char *szFile);
	// Streaming variants: read from an already-open file (used to write a
	// package entry without extracting it to a temp file first).
	bool WriteLBACore(UINT uiBegin, FILE *file, long long uiDataOff, long long iFileSize);
	bool WriteSparseLBACore(UINT uiBegin, UINT uiSize, FILE *file);
	bool IsSparseEntry(FW_ENTRY &entry);
	bool IsUbiEntry(FW_ENTRY &entry);
	bool IsJffsEntry(FW_ENTRY &entry);
	bool WriteEntry(UINT uiBegin, FW_ENTRY &entry);
	bool ErasePartition(UINT uiOffset, UINT uiSize);
	bool EraseUbiBlock(u32 uiOffset, u32 uiPartSize);
	bool WriteParameter(char *szParameter);
	bool WriteGpt(char *szParameter);
	bool ExtractEntry(FW_ENTRY &entry, const char *szOut);
	void NotifyProgress(u64 uiTotal, u64 uiDone);

	// Internal state (field offsets mirror the CRKAndroidDevice layout).
	PBYTE m_pIDBData;		// +0xb10
	UINT  m_uiIDBSectorNum;		// +0xb0c ... (loader data/head sectors at 0xaf8/0xafa)
	UINT  m_uiLoaderDataSec;	// +0xaf8
	UINT  m_uiLoaderSizeSec;	// +0xafa
	UINT  m_uiLoaderHeadSec;	// +0xafc
	UINT  m_uiIDBSectorCount;	// +0x793
	UINT  m_uiIDBBlockCount;	// +0x791
	UCHAR m_ucIDBCount;		// +0xaf6
	bool  m_bNewIDB;		// +0xaf4
	PBYTE m_pSectorData;		// +0xb18 saved sector1/2/3 buffers
	PBYTE m_pOldIDBData;		// +0xb20
	PBYTE m_pOldSectorData;		// +0xb28
	UINT  m_uiNewDiskSize;		// +0x818
	UINT  m_uiReservedBlockSize;	// +0xb0c
	UINT  m_uiParamDataSize;	// +0xb08
	PBYTE m_pParamData;		// +0xb00
	bool  m_bIsEnvFirmware;		// +0x81d
	USHORT m_usSec3Crc;		// +0x81e saved sector3 CRC
	UINT  m_uiBand;			// +0xb30 transfer band; WRITE_LBA block = m_uiBand << 14

	FW_ENTRY m_entries[64];
	int  m_nEntries;
	FILE *m_pPackageFile;
	u64  m_uiFwTotal;
	u64  m_uiFwDone;
};

#endif
