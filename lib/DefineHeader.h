#ifndef DEFINE_HEADER
#define DEFINE_HEADER
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>
#include <errno.h>
#ifndef _WIN32
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#else
#include <direct.h>
#include <io.h>
#endif
#include <libusb.h>

#include "Property.hpp"
#include <list>
#include <vector>
#include <set>
#include <string>
#include <sstream>
#include <algorithm>
using namespace std;
#ifndef __MINGW32__
typedef unsigned char BYTE, *PBYTE;
typedef unsigned char UCHAR;
typedef unsigned short WCHAR;
typedef unsigned short USHORT;
typedef unsigned int	UINT;
typedef unsigned int	DWORD;
#endif
#define ALIGN(x, a)		__ALIGN_MASK((x), (a) - 1)
#define __ALIGN_MASK(x, mask)	(((x) + (mask)) & ~(mask))
#define RK28_SEC2_RESERVED_LEN 473
#define CHIPINFO_LEN 16
#define RK28_SEC3_RESERVED_LEN 382
#define RKDEVICE_SN_LEN 60
#define RKDEVICE_UID_LEN 30
#define RKDEVICE_MAC_LEN 6
#define RKDEVICE_WIFI_LEN 6
#define RKDEVICE_BT_LEN 6
#define RKDEVICE_IMEI_LEN 15
typedef enum{
	RKNONE_DEVICE = 0,
	RK27_DEVICE = 0x10,
	RKCAYMAN_DEVICE,
	RK28_DEVICE = 0x20,
	RK281X_DEVICE,
	RKPANDA_DEVICE,
	RKNANO_DEVICE = 0x30,
	RKSMART_DEVICE,
	RKCROWN_DEVICE = 0x40,
	RK29_DEVICE = 0x50,
	RK292X_DEVICE,
	RK30_DEVICE = 0x60,
	RK30B_DEVICE,
	RK31_DEVICE = 0x70,
	RK32_DEVICE = 0x80
} ENUM_RKDEVICE_TYPE;
typedef enum{
	RK_OS = 0,
	ANDROID_OS = 0x1
} ENUM_OS_TYPE;

typedef enum{
	RKUSB_NONE = 0x0,
	RKUSB_MASKROM = 0x01,
	RKUSB_LOADER = 0x02,
	RKUSB_MSC = 0x04
} ENUM_RKUSB_TYPE;
typedef enum{
	ENTRY471 = 1,
	ENTRY472 = 2,
	ENTRYLOADER = 4
} ENUM_RKBOOTENTRY;

#pragma pack(1)
typedef struct sparse_header_t { 
	UINT	magic;		/* 0xed26ff3a */
	USHORT	major_version;	/* (0x1) - reject images with higher major versions */
	USHORT	minor_version;	/* (0x0) - allow images with higer minor versions */
	USHORT	file_hdr_sz;	/* 28 bytes for first revision of the file format */
	USHORT	chunk_hdr_sz;	/* 12 bytes for first revision of the file format */  
	UINT	blk_sz;		/* block size in bytes, must be a multiple of 4 (4096) */
	UINT	total_blks;	/* total blocks in the non-sparse output image */
	UINT	total_chunks;	/* total chunks in the sparse input image */ 
	UINT	image_checksum; /* CRC32 checksum of the original data, counting "don't care" */
							/* as 0. Standard 802.3 polynomial, use a Public Domain */
							/* table implementation */
} sparse_header;
#define SPARSE_HEADER_MAGIC	0xed26ff3a
#define UBI_HEADER_MAGIC	0x23494255
#define CHUNK_TYPE_RAW		0xCAC1
#define CHUNK_TYPE_FILL		0xCAC2
#define CHUNK_TYPE_DONT_CARE	0xCAC3
#define CHUNK_TYPE_CRC32    0xCAC4
typedef struct chunk_header_t {  
	USHORT	chunk_type;	/* 0xCAC1 -> raw; 0xCAC2 -> fill; 0xCAC3 -> don't care */
	USHORT	reserved1;
	UINT	chunk_sz;	/* in blocks in output image */
	UINT	total_sz;	/* in bytes of chunk input file including chunk header and data */
} chunk_header;

typedef struct{
	USHORT	usYear;
	BYTE	ucMonth;
	BYTE  	ucDay;
	BYTE  	ucHour;
	BYTE  	ucMinute;
	BYTE  	ucSecond;
} STRUCT_RKTIME, *PSTRUCT_RKTIME;

// Block-state map built by CRKDevice::BuildBlockStateMap / CRKAndroidDevice::
// GetBlocksState. Packed layout:
//   +0x00 BYTE           bFlashCS        flash chip-select bitmask
//   +0x01 UINT           uiBlockNum      total blocks
//   +0x05 USHORT         usBlockStateLen bytes in the trailing state array
//   +0x07 BYTE[]         blockState[]    1 = used/reserved, 0 = free
#pragma pack(1)
typedef struct {
	BYTE   bFlashCS;
	UINT   uiBlockNum;
	USHORT usBlockStateLen;
	BYTE   blockState[1];
} STRUCT_BLOCK_STATE, *PSTRUCT_BLOCK_STATE;

// Device-information modify flags (CRKDevice::SetMiscModifyFlag).
typedef enum {
	MISC_MODIFY_NONE = 0,
	MISC_MODIFY_SN,
	MISC_MODIFY_MAC,
	MISC_MODIFY_IMEI,
	MISC_MODIFY_UID,
	MISC_MODIFY_BT,
	MISC_MODIFY_CUSTOM
} ENUM_MISC_MODIFY_FLAG;

// One entry of an update firmware (RKAF) package. 0x70 bytes on disk:
//   +0x00 char szName[32]      partition name
//   +0x20 char szFile[32]      file name
//   +0x40 UINT uiReserved
//   +0x44 UINT uiPartOffset    partition offset (sectors)
//   +0x48 UINT uiPartSize      partition size (sectors)
//   +0x4c UINT reserved2[4]
//   +0x5c UINT uiSize          data size in bits (rounded to 512 bytes)
//   +0x60 UINT uiDataOffset    absolute data offset inside the package
//   +0x6c UINT uiDataSize      raw data size in bytes
#pragma pack(1)
typedef struct {
	char szName[32];
	char szFile[32];
	UINT uiReserved;
	UINT uiPartOffset;
	UINT uiPartSize;
	UINT reserved2[4];
	UINT uiSize;
	UINT uiDataOffset;
	UINT uiDataSize;
} STRUCT_RKIMAGE_ITEM, *PSTRUCT_RKIMAGE_ITEM;
#pragma pack()

typedef struct{
	char szItemName[20];
	char szItemValue[256];
} STRUCT_CONFIG_ITEM, *PSTRUCT_CONFIG_ITEM;
typedef struct
{
	char szItemName[64];
	UINT uiItemOffset;
	UINT uiItemSize;
}STRUCT_PARAM_ITEM,*PSTRUCT_PARAM_ITEM;
typedef struct _STRUCT_RKDEVICE_DESC{
	USHORT usVid;
	USHORT usPid;
	USHORT usbcdUsb;
	UINT     uiLocationID;
	ENUM_RKUSB_TYPE emUsbType;
	ENUM_RKDEVICE_TYPE emDeviceType;
	void   *pUsbHandle;
} STRUCT_RKDEVICE_DESC, *PSTRUCT_RKDEVICE_DESC;
typedef	struct {
	DWORD	dwTag;
	BYTE	reserved[4];
	UINT	uiRc4Flag;
	USHORT	usBootCode1Offset;
	USHORT	usBootCode2Offset;
	BYTE	reserved1[490];
	USHORT  usBootDataSize;
	USHORT	usBootCodeSize;
	USHORT	usCrc;
} RK28_IDB_SEC0, *PRK28_IDB_SEC0;

typedef struct {
	USHORT  usSysReservedBlock;
	USHORT  usDisk0Size;
	USHORT  usDisk1Size;
	USHORT  usDisk2Size;
	USHORT  usDisk3Size;
	UINT	uiChipTag;
	UINT	uiMachineId;
	USHORT	usLoaderYear;
	USHORT	usLoaderDate;
	USHORT	usLoaderVer;
	USHORT  usLastLoaderVer;
	USHORT  usReadWriteTimes;
	DWORD	dwFwVer;
	USHORT  usMachineInfoLen;
	UCHAR	ucMachineInfo[30];
	USHORT	usManufactoryInfoLen;
	UCHAR	ucManufactoryInfo[30];
	USHORT	usFlashInfoOffset;
	USHORT	usFlashInfoLen;
	UCHAR	reserved[384];
	UINT	uiFlashSize;
	BYTE    reserved1;
	BYTE    bAccessTime;
	USHORT  usBlockSize;
	BYTE    bPageSize;
	BYTE    bECCBits;
	BYTE    reserved2[8];
	USHORT  usIdBlock0;
	USHORT  usIdBlock1;
	USHORT  usIdBlock2;
	USHORT  usIdBlock3;
	USHORT  usIdBlock4;
} RK28_IDB_SEC1, *PRK28_IDB_SEC1;

typedef struct {
	USHORT  usInfoSize;
	BYTE    bChipInfo[CHIPINFO_LEN];
	BYTE    reserved[RK28_SEC2_RESERVED_LEN];
	char    szVcTag[3];
	USHORT  usSec0Crc;
	USHORT  usSec1Crc;
	UINT	uiBootCodeCrc;
	USHORT  usSec3CustomDataOffset;
	USHORT  usSec3CustomDataSize;
	char    szCrcTag[4];
	USHORT  usSec3Crc;
} RK28_IDB_SEC2, *PRK28_IDB_SEC2;

// Android IDB sector2 view. Same layout as RK28_IDB_SEC2 (szCrcTag at +0x1fa,
// usSec3Crc at +0x1fe) but a distinct type.
typedef struct {
	USHORT  usInfoSize;
	BYTE    bChipInfo[CHIPINFO_LEN];
	BYTE    reserved[RK28_SEC2_RESERVED_LEN];
	char    szVcTag[3];
	USHORT  usSec0Crc;
	USHORT  usSec1Crc;
	UINT	uiBootCodeCrc;
	USHORT  usSec3CustomDataOffset;
	USHORT  usSec3CustomDataSize;
	char    szCrcTag[4];
	USHORT  usSec3Crc;
} RKANDROID_IDB_SEC2, *PRKANDROID_IDB_SEC2;

typedef struct {
	USHORT  usSNSize;
	BYTE    sn[RKDEVICE_SN_LEN];
	BYTE    reserved[RK28_SEC3_RESERVED_LEN];
	BYTE	wifiSize;
	BYTE	wifiAddr[RKDEVICE_WIFI_LEN];
	BYTE	imeiSize;
	BYTE	imei[RKDEVICE_IMEI_LEN];
	BYTE	uidSize;
	BYTE	uid[RKDEVICE_UID_LEN];
	BYTE    blueToothSize;
	BYTE	blueToothAddr[RKDEVICE_BT_LEN];
	BYTE	macSize;
	BYTE	macAddr[RKDEVICE_MAC_LEN];
} RK28_IDB_SEC3, *PRK28_IDB_SEC3;
#pragma pack()
typedef list<STRUCT_RKDEVICE_DESC> RKDEVICE_DESC_SET;
typedef RKDEVICE_DESC_SET::iterator device_list_iter;
typedef vector<string> STRING_VECTOR;
typedef vector<UINT> UINT_VECTOR;
typedef vector<STRUCT_CONFIG_ITEM> CONFIG_ITEM_VECTOR;
typedef vector<STRUCT_PARAM_ITEM> PARAM_ITEM_VECTOR;
typedef enum{
	TESTDEVICE_PROGRESS,
	DOWNLOADIMAGE_PROGRESS,
	CHECKIMAGE_PROGRESS,
	TAGBADBLOCK_PROGRESS,
	TESTBLOCK_PROGRESS,
	ERASEFLASH_PROGRESS,
	ERASESYSTEM_PROGRESS,
	LOWERFORMAT_PROGRESS,
	ERASEUSERDATA_PROGRESS
} ENUM_PROGRESS_PROMPT;

typedef enum{
	CALL_FIRST,
	CALL_MIDDLE,
	CALL_LAST
} ENUM_CALL_STEP;

typedef void (*ProgressPromptCB)(UINT deviceLayer, ENUM_PROGRESS_PROMPT promptID, long long totalValue, long long currentValue, ENUM_CALL_STEP emCall);

// ---------------------------------------------------------------------------
// Workflow step engine (CRKUpgrade):
// - ENUM_UPGRADE_PROMPT identifies the prompt text printed for each step.
// - ENUM_UPGRADE_STEP identifies each step in the upgrade flow.
// - STRUCT_UPGRADE_STEP is 0x20 bytes.
// ---------------------------------------------------------------------------
typedef enum {
	UPGRADE_PROMPT_NONE = 0,

	PROMPT_DOWNLOAD_BOOT_START = 0x01,
	PROMPT_DOWNLOAD_BOOT_FAIL,
	PROMPT_DOWNLOAD_BOOT_SUCCESS,

	PROMPT_DOWNLOAD_IDB_START = 0x04,
	PROMPT_DOWNLOAD_IDB_FAIL,
	PROMPT_DOWNLOAD_IDB_SUCCESS,

	PROMPT_DOWNLOAD_FIRMWARE_START = 0x07,
	PROMPT_DOWNLOAD_FIRMWARE_FAIL,
	PROMPT_DOWNLOAD_FIRMWARE_SUCCESS,

	PROMPT_TEST_DEVICE_START = 0x0a,
	PROMPT_TEST_DEVICE_FAIL,
	PROMPT_TEST_DEVICE_SUCCESS,

	PROMPT_RESET_DEVICE_START = 0x0d,
	PROMPT_RESET_DEVICE_FAIL,
	PROMPT_RESET_DEVICE_SUCCESS,

	PROMPT_WAIT_MSC_START = 0x16,
	PROMPT_WAIT_MSC_FAIL,
	PROMPT_WAIT_MSC_SUCCESS,

	PROMPT_WAIT_LOADER_START = 0x19,
	PROMPT_WAIT_LOADER_FAIL,
	PROMPT_WAIT_LOADER_SUCCESS,

	PROMPT_WAIT_MASKROM_START = 0x1c,
	PROMPT_WAIT_MASKROM_FAIL,
	PROMPT_WAIT_MASKROM_SUCCESS,

	PROMPT_ERASE_IDB_START = 0x1f,
	PROMPT_ERASE_IDB_FAIL,
	PROMPT_ERASE_IDB_SUCCESS,

	PROMPT_SWITCH_MSC_START = 0x22,
	PROMPT_SWITCH_MSC_FAIL,
	PROMPT_SWITCH_MSC_SUCCESS,

	PROMPT_CHECK_CHIP_START = 0x25,
	PROMPT_CHECK_CHIP_FAIL,
	PROMPT_CHECK_CHIP_SUCCESS,

	PROMPT_PREPARE_IDB_START = 0x28,
	PROMPT_PREPARE_IDB_FAIL,
	PROMPT_PREPARE_IDB_SUCCESS,

	PROMPT_ERASE_FLASH_START = 0x37,
	PROMPT_ERASE_FLASH_FAIL,
	PROMPT_ERASE_FLASH_SUCCESS,

	PROMPT_GET_BLOCK_STATE_START = 0x3a,
	PROMPT_GET_BLOCK_STATE_FAIL,
	PROMPT_GET_BLOCK_STATE_SUCCESS,

	PROMPT_GET_FLASH_INFO_START = 0x3d,
	PROMPT_GET_FLASH_INFO_FAIL,
	PROMPT_GET_FLASH_INFO_SUCCESS,

	PROMPT_LOWER_FORMAT_START = 0x55,
	PROMPT_LOWER_FORMAT_FAIL,
	PROMPT_LOWER_FORMAT_SUCCESS
} ENUM_UPGRADE_PROMPT;

typedef enum {
	UPGRADE_STEP_DOWNLOAD_BOOT = 0x01,
	UPGRADE_STEP_DOWNLOAD_IDB = 0x04,
	UPGRADE_STEP_DOWNLOAD_IMAGE = 0x07,
	UPGRADE_STEP_TEST_DEVICE = 0x0a,
	UPGRADE_STEP_RESET_DEVICE = 0x0d,
	UPGRADE_STEP_WAIT_LOADER = 0x19,
	UPGRADE_STEP_WAIT_MASKROM = 0x1c,
	UPGRADE_STEP_ERASE_IDB = 0x1f,
	UPGRADE_STEP_SWITCH_MSC = 0x22,
	UPGRADE_STEP_CHECK_CHIP = 0x25,
	UPGRADE_STEP_PREPARE_IDB = 0x28,
	UPGRADE_STEP_ERASE_FLASH = 0x37,
	UPGRADE_STEP_GET_FLASH_INFO = 0x3d,
	UPGRADE_STEP_GET_BLOCK_STATE = 0x40,
	UPGRADE_STEP_LOWER_FORMAT = 0x55,
	UPGRADE_STEP_SET_RESET_FLAG = 0x73,
	UPGRADE_STEP_RESET_DEVICE_EXT = 0x7f,
	UPGRADE_STEP_GET_NEW_DISK_SIZE = 0x88
} ENUM_UPGRADE_STEP;

// Workflow / capability mask. A step runs only when its mask contains every
// bit the device reports (Run tests `(step.uiMask & capability) == capability`).
typedef enum {
	WORKFLOW_DOWNLOAD_IDB = 0x01,
	WORKFLOW_DOWNLOAD_IMAGE = 0x02,
	WORKFLOW_UPGRADE = 0x03,
	WORKFLOW_RESTORE = 0x04
} ENUM_WORKFLOW;

#pragma pack(1)
typedef struct {
	UINT   uiStep;			// +0x00  ENUM_UPGRADE_STEP
	UINT   uiMask;			// +0x04  workflow mask required for this step
	UINT   reserved[2];		// +0x08
	USHORT usParam;			// +0x10  (0x0100 on some steps)
	BYTE   bConditional;	// +0x12  if set, step's bool& result gates the next steps
	BYTE   bSkipCount;		// +0x13  steps to skip when the condition holds
	BYTE   bReset;			// +0x14  reset device after the step
	BYTE   reserved2[11];	// +0x15  pad to 0x20
} STRUCT_UPGRADE_STEP, *PSTRUCT_UPGRADE_STEP;
#pragma pack()

typedef void (*UpgradePromptCB)(UINT deviceLayer, ENUM_UPGRADE_PROMPT promptID, UINT param);

//	bool WideStringToString(wchar_t *pszSrc, char *&pszDest);
//	bool StringToWideString(char *pszSrc, wchar_t *&pszDest);
#endif
