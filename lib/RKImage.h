#ifndef RKIMAGE_HEADER
#define RKIMAGE_HEADER
#include "DefineHeader.h"
#include "RKBoot.h"
#define  IMAGE_RESERVED_SIZE 61
#pragma pack(1)
typedef struct {
	UINT uiTag;
	USHORT usSize;
	DWORD  dwVersion;
	DWORD  dwMergeVersion;
	STRUCT_RKTIME stReleaseTime;
	ENUM_RKDEVICE_TYPE emSupportChip;
	DWORD  dwBootOffset;
	DWORD  dwBootSize;
	DWORD  dwFWOffset;
	DWORD  dwFWSize;
	BYTE   reserved[IMAGE_RESERVED_SIZE];
} STRUCT_RKIMAGE_HEAD, *PSTRUCT_RKIMAGE_HEAD;

// Multi-storage ("SSFW") firmware: a 0x40-byte header followed by
// `uiEntryCount` fixed-size records of `usEntrySize` bytes (0x40 in every
// observed sample). Each record describes one embedded image:
//   +0x00 u64 offset   absolute file offset of the payload
//   +0x08 u64 size     payload size
//   +0x10 u32 storage  STORAGE_CODE bit of the target storage
//   +0x20 char name[32]
// A record whose low 32 bits of `offset` are 0xffffffff is the "div" (divide)
// marker; it carries no payload and is reported as name=div offset=0 size=0.
typedef struct {
	unsigned long long uiOffset;
	unsigned long long uiSize;
	UINT               uiStorage;
	char               szName[32];
} STRUCT_STORAGE_ENTRY, *PSTRUCT_STORAGE_ENTRY;

typedef struct {
	UINT uiTag;                    // "SSFW" (0x57465353)
	UINT uiReserved;               // +0x04
	UINT usEntrySize;              // +0x08 record size
	UINT uiReserved2;              // +0x0c
	UINT uiEntryCount;             // +0x10
	BYTE reserved[0x40 - 0x14];
} STRUCT_STORAGE_HEAD, *PSTRUCT_STORAGE_HEAD;
#pragma pack()

class CRKImage
{
public:
	UINT GetVersion();
 	property<CRKImage, UINT, READ_ONLY> Version;
	UINT GetMergeVersion();
 	property<CRKImage, UINT, READ_ONLY> MergeVersion;
	STRUCT_RKTIME GetReleaseTime();
 	property<CRKImage, STRUCT_RKTIME, READ_ONLY> ReleaseTime;
	ENUM_RKDEVICE_TYPE GetSupportDevice();
 	property<CRKImage, ENUM_RKDEVICE_TYPE, READ_ONLY> SupportDevice;
	ENUM_OS_TYPE GetOsType();
 	property<CRKImage, ENUM_OS_TYPE, READ_ONLY> OsType;

	unsigned short GetBackupSize();
 	property<CRKImage, unsigned short, READ_ONLY> BackupSize;
	UINT GetBootOffset();
 	property<CRKImage, UINT, READ_ONLY> BootOffset;
	UINT GetBootSize();
 	property<CRKImage, UINT, READ_ONLY> BootSize;
	UINT GetFWOffset();
 	property<CRKImage, UINT, READ_ONLY> FWOffset;
	long long GetFWSize();
 	property<CRKImage, long long, READ_ONLY> FWSize;
	bool GetSignFlag();
	property<CRKImage, bool, READ_ONLY> SignFlag;

	CRKBoot *m_bootObject;
	bool SaveBootFile(string filename);
	bool SaveFWFile(string filename);
	bool GetData(long long dwOffset, DWORD dwSize, PBYTE lpBuffer);
	void GetReservedData(PBYTE &lpData, USHORT &usSize);
	int GetMd5Data(PBYTE &lpMd5, PBYTE &lpSignMd5);
	long long GetImageSize();
	int GetError();
	bool GetSingleBootFlag();
	UINT GetStorage();
	void SetStorage(UINT value);
	// Multi-storage ("SSFW") container support.
	bool OpenStorageFirmware(string filename);
	UINT GetStorageEntryCount();
	bool GetStorageEntry(UINT uiIndex, STRUCT_STORAGE_ENTRY &entry);
	int Md5Check(long long dwOffset);
	// `ullStartOffset`/`ullTotalSize` bound the image inside a container file
	// (used for the embedded images of an SSFW firmware). The one-argument
	// overload reads the whole file.
	int ObjectInit(string filename,
	               unsigned long long ullStartOffset = (unsigned long long)-1,
	               unsigned long long ullTotalSize = (unsigned long long)-1);
	CRKImage();
	~CRKImage();
protected:

private:
	DWORD m_version;
	DWORD m_mergeVersion;
	STRUCT_RKTIME m_releaseTime;
	ENUM_RKDEVICE_TYPE m_supportDevice;
	DWORD m_bootOffset;
	DWORD m_bootSize;
	DWORD m_fwOffset;
	long long m_fwSize;

	BYTE  m_md5[32];
	BYTE  m_signMd5[256];
	BYTE  m_reserved[IMAGE_RESERVED_SIZE];
	bool  m_bSignFlag;
	int   m_signMd5Size;
	long long m_startOffset;
	STRUCT_STORAGE_HEAD m_storageHead;
	vector<STRUCT_STORAGE_ENTRY> m_storageEntries;
	FILE *m_pFile;
	long long m_fileSize;
	int   m_iError;
	bool  m_bSingleBootFlag;
	UINT  m_uiStorage;
};
#endif