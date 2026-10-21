#include "platform/File.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <thread>

#include "AndroidDevice.h"
#include "Parameter.h"
#include "Config.h"
#include "crc.h"
#include "Idb.h"

#include <vector>

namespace {
class TemporaryParameterFiles {
    std::filesystem::path directory;
public:
    TemporaryParameterFiles() {
        static std::atomic<unsigned> sequence{0};
        const auto root = std::filesystem::temp_directory_path();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            auto candidate = root / ("upgrade-parameter-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(sequence++));
            if (std::filesystem::create_directory(candidate)) {
                directory = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("Cannot create parameter temporary directory");
    }
    ~TemporaryParameterFiles() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
    std::string filePath(const char *name) const { return platform::Utf8(directory / name); }
};
}

// File-local helper: 1 when the parameter declares a GPT table.
static int fw_get_parameter_gptflag(const char *szParameter)
{
	FILE *file = platform::OpenFile(szParameter, "rb");
	if (!file)
		return 0;
	char buf[4096];
	size_t n = fread(buf, 1, sizeof(buf) - 1, file);
	fclose(file);
	buf[n] = 0;
	return strstr(buf, "TYPE: GPT") ? 1 : 0;
}

static unsigned char ByteToBCD(unsigned char ucByte)
{
	return (unsigned char)(((ucByte / 10) << 4) | (ucByte % 10));
}

static unsigned short UshortToBCD(unsigned short usValue)
{
	unsigned int uiHigh = usValue / 1000000;
	unsigned int uiMid = (usValue / 10000) % 100;
	unsigned int uiLow = usValue % 100;
	return (unsigned short)(((ByteToBCD((unsigned char)(uiHigh * 100 + uiMid))) << 8) | ByteToBCD((unsigned char)uiLow));
}

// MakeSectorSpareData: BCH-encode 0x200 data bytes into a 0x10-byte spare area.
void MakeSectorSpareData(PBYTE pData, UINT uiSectorIndex, PBYTE pSpare)
{
	unsigned char spareData[0x40];
	memset(spareData, 0, sizeof(spareData));
	memcpy(spareData, pData, 0x200 < 0x40 ? 0x200 : 0x40);
	spareData[0x0e] = (unsigned char)(uiSectorIndex >= 1 ? 0x6a : 0x69);
	spareData[0x0f] = 0xff;
	bch_encode(spareData, spareData);
	memcpy(pSpare, spareData, 0x10);
}

CRKAndroidDevice::CRKAndroidDevice(STRUCT_RKDEVICE_DESC &device) : CRKDevice(device)
{
	m_pIDBData = NULL;
	m_uiIDBSectorNum = 0;
	m_uiLoaderDataSec = 0;
	m_uiLoaderSizeSec = 0;
	m_uiLoaderHeadSec = 0;
	m_uiIDBSectorCount = 0;
	m_uiIDBBlockCount = 0;
	m_ucIDBCount = 0;
	m_bNewIDB = false;
	m_pSectorData = NULL;
	m_pOldIDBData = NULL;
	m_pOldSectorData = NULL;
	m_uiNewDiskSize = 0;
	m_uiReservedBlockSize = 0;
	m_uiParamDataSize = 0;
	m_pParamData = NULL;
	m_bIsEnvFirmware = false;
	m_usSec3Crc = 0;
	m_uiBand = 1;
	m_nEntries = 0;
	m_pPackageFile = NULL;
	m_uiFwTotal = 0;
	m_uiFwDone = 0;
}

CRKAndroidDevice::~CRKAndroidDevice()
{
	if (m_pIDBData) {
		delete []m_pIDBData;
		m_pIDBData = NULL;
	}
	if (m_pSectorData) { delete []m_pSectorData; m_pSectorData = NULL; }
	if (m_pOldIDBData) { delete []m_pOldIDBData; m_pOldIDBData = NULL; }
	if (m_pOldSectorData) { delete []m_pOldSectorData; m_pOldSectorData = NULL; }
	if (m_pParamData) { delete []m_pParamData; m_pParamData = NULL; }
	if (m_pPackageFile) {
		fclose(m_pPackageFile);
		m_pPackageFile = NULL;
	}
}

bool CRKAndroidDevice::IsSparseImage(STRUCT_RKIMAGE_ITEM &item)
{
	if (!m_pImage)
		return false;
	unsigned char buf[0x1c];
	if (!m_pImage->GetData(item.uiDataOffset, 0x1c, buf))
		return false;
	return *(UINT *)buf == SPARSE_HEADER_MAGIC;
}

bool CRKAndroidDevice::IsUbiImage(STRUCT_RKIMAGE_ITEM &item)
{
	if (!m_pImage)
		return false;
	unsigned int magic = 0;
	if (!m_pImage->GetData(item.uiDataOffset, 4, (PBYTE)&magic))
		return false;
	return magic == UBI_HEADER_MAGIC;
}

bool CRKAndroidDevice::IsJffsImage(STRUCT_RKIMAGE_ITEM &item)
{
	if (!m_pImage)
		return false;
	unsigned int magic = 0;
	if (!m_pImage->GetData(item.uiDataOffset, 4, (PBYTE)&magic))
		return false;
	return magic == 0xe0011985 || magic == 0x20031985;
}

u64 CRKAndroidDevice::GetSparseImageSize(STRUCT_RKIMAGE_ITEM &item)
{
	if (!m_pImage)
		return 0;
	unsigned char buf[0x1c];
	if (!m_pImage->GetData(item.uiDataOffset, 0x1c, buf))
		return 0;
	sparse_header *pHeader = (sparse_header *)buf;
	return (u64)pHeader->blk_sz * pHeader->total_blks;
}

// ---------------------------------------------------------------------------
// Firmware package (RKFW header + RKAF entry table).
// ---------------------------------------------------------------------------
bool CRKAndroidDevice::LoadFirmwarePackage(const char *szFile)
{
	FILE *file = platform::OpenFile(szFile, "rb");
	if (!file)
		return false;
	BYTE hdr[0x40];
	if (fread(hdr, 1, sizeof(hdr), file) != sizeof(hdr)) {
		fclose(file);
		return false;
	}
	if (*(UINT *)hdr != 0x57464B52) { // "RKFW"
		fclose(file);
		return false;
	}
	UINT fwOff = *(UINT *)(hdr + 0x21);
	UINT cnt = 0;
	fseeko(file, fwOff + 0x88, SEEK_SET);
	if (fread(&cnt, 1, 4, file) != 4) {
		fclose(file);
		return false;
	}
	if (cnt > 64)
		cnt = 64;
	UINT i;
	for (i = 0; i < cnt; i++) {
		BYTE e[0x70];
		fseeko(file, fwOff + 0x8C + (u64)i * 0x70, SEEK_SET);
		if (fread(e, 1, sizeof(e), file) != sizeof(e))
			break;
		memcpy(m_entries[i].szPart, e, 32); m_entries[i].szPart[32] = 0;
		memcpy(m_entries[i].szFile, e + 0x20, 32); m_entries[i].szFile[32] = 0;
		u64 uiDataOff = *(UINT *)(e + 0x60);
		u64 uiSize = *(UINT *)(e + 0x6C);
		if (e[0x52] == 'H')
			uiDataOff |= (u64)*(UINT *)(e + 0x53) << 32;
		if (e[0x57] == 'H')
			uiSize |= (u64)*(UINT *)(e + 0x58) << 32;
		m_entries[i].uiSize = uiSize;
		m_entries[i].uiPartSize = *(UINT *)(e + 0x5C);
		m_entries[i].uiPartLba = *(UINT *)(e + 0x64);
		m_entries[i].uiDataOff = (u64)fwOff + uiDataOff;
	}
	m_nEntries = (int)i;
	m_pPackageFile = file;
	return m_nEntries > 0;
}

bool CRKAndroidDevice::ExtractEntry(FW_ENTRY &entry, const char *szOut)
{
	if (!m_pPackageFile)
		return false;
	std::vector<BYTE> buf(64 * 1024);
	FILE *out = platform::OpenFile(szOut, "wb");
	if (!out)
		return false;
	if (fseeko(m_pPackageFile, entry.uiDataOff, SEEK_SET) != 0) {
		fclose(out);
		return false;
	}
	u64 remain = entry.uiSize;
	bool bOk = true;
	while (remain) {
		UINT want = remain > buf.size() ? (UINT)buf.size() : (UINT)remain;
		if (fread(buf.data(), 1, want, m_pPackageFile) != want) { bOk = false; break; }
		if (fwrite(buf.data(), 1, want, out) != want) { bOk = false; break; }
		remain -= want;
	}
	// A failed/short fclose (e.g. ENOSPC flush) must not be reported as success.
	if (fclose(out) != 0)
		bOk = false;
	return bOk;
}

void CRKAndroidDevice::NotifyProgress(u64 uiTotal, u64 uiDone)
{
	if (m_callBackProc)
		m_callBackProc(0, DOWNLOADIMAGE_PROGRESS, (long long)uiTotal, (long long)uiDone, CALL_MIDDLE);
}

// ---------------------------------------------------------------------------
// Device-side partition primitives
// ---------------------------------------------------------------------------
bool CRKAndroidDevice::ErasePartition(UINT uiOffset, UINT uiSize)
{
	UINT uiErase = 1024 * 32;
	int iRet = ERR_SUCCESS;
	while (uiSize) {
		if (IsAborted())
			return false;
		if (uiSize >= uiErase) {
			iRet = m_pComm->RKU_EraseLBA(uiOffset, uiErase);
			uiSize -= uiErase;
			uiOffset += uiErase;
		} else {
			iRet = m_pComm->RKU_EraseLBA(uiOffset, uiSize);
			uiOffset += uiSize;
			uiSize = 0;
		}
		if (iRet != ERR_SUCCESS)
			return false;
	}
	return true;
}

// Streaming core: read `iFileSize` bytes from `file` starting at `uiDataOff`
// and push them to the device. Used both for extracted temp files (offset 0)
// and to stream a partition straight out of the firmware package, so a multi-GB
// image never has to be materialized on disk.
#define WRITE_CHUNK_SECTORS_MAX 0xFFFF	// RKU_WriteLBA carries the count in a u16

bool CRKAndroidDevice::WriteLBACore(UINT uiBegin, FILE *file, long long uiDataOff, long long iFileSize)
{
	if (!file)
		return false;
	u64 ullChunk = (u64)m_uiBand << 14;
	if (ullChunk > (u64)WRITE_CHUNK_SECTORS_MAX * SECTOR_SIZE)
		ullChunk = (u64)WRITE_CHUNK_SECTORS_MAX * SECTOR_SIZE;
	if (ullChunk < SECTOR_SIZE)
		ullChunk = SECTOR_SIZE;
	size_t chunkBytes = (size_t)ullChunk;
	BYTE *pBuf = new BYTE[chunkBytes];
	if (!pBuf)
		return false;
	if (fseeko(file, uiDataOff, SEEK_SET) != 0) {
		delete []pBuf;
		return false;
	}
	long long iTotalWrite = 0;
	bool bOk = true;
	while (iTotalWrite < iFileSize) {
		if (IsAborted()) { bOk = false; break; }
		memset(pBuf, 0, chunkBytes);
		long long remain = iFileSize - iTotalWrite;
		size_t want = remain > (long long)chunkBytes ? chunkBytes : (size_t)remain;
		UINT iWrite = fread(pBuf, 1, want, file);
		if (iWrite == 0) { bOk = false; break; }
		UINT uiLen = ((iWrite % SECTOR_SIZE) == 0) ? (iWrite / SECTOR_SIZE) : (iWrite / SECTOR_SIZE + 1);
		if (m_pComm->RKU_WriteLBA(uiBegin, uiLen, pBuf) != ERR_SUCCESS) { bOk = false; break; }
		uiBegin += uiLen;
		iTotalWrite += iWrite;
		m_uiFwDone += iWrite;
		NotifyProgress(m_uiFwTotal, m_uiFwDone);
	}
	delete []pBuf;
	return bOk;
}

bool CRKAndroidDevice::WriteLBA(UINT uiBegin, const char *szFile)
{
	FILE *file = platform::OpenFile(szFile, "rb");
	if (!file)
		return false;
	fseeko(file, 0, SEEK_END);
	long long iFileSize = ftello(file);
	bool bOk = WriteLBACore(uiBegin, file, 0, iFileSize);
	fclose(file);
	return bOk;
}

bool CRKAndroidDevice::WriteSparseLBACore(UINT uiBegin, UINT uiSize, FILE *file)
{
	bool bSuccess = false;
	u64 iTotalWrite = 0, iFileSize = 0, dwChunkDataSize;
	UINT uiTransferSec, curChunk, i;
	UINT dwMaxReadWriteBytes, dwTransferBytes, dwFillByte, dwCrc;
	BYTE *pBuf = NULL;
	sparse_header header;
	chunk_header chunk;
	u64 ullChunk = (u64)m_uiBand << 14;
	if (ullChunk > (u64)WRITE_CHUNK_SECTORS_MAX * SECTOR_SIZE)
		ullChunk = (u64)WRITE_CHUNK_SECTORS_MAX * SECTOR_SIZE;
	if (ullChunk < SECTOR_SIZE)
		ullChunk = SECTOR_SIZE;
	dwMaxReadWriteBytes = (UINT)ullChunk;
	if (!file)
		return false;
	if (fread(&header, 1, sizeof(header), file) != sizeof(sparse_header))
		goto exit_sparse;
	pBuf = new BYTE[dwMaxReadWriteBytes];
	if (!pBuf)
		return false;
	iFileSize = header.blk_sz * (u64)header.total_blks;
	curChunk = 0;
	if (uiSize == (u32)-1)
		uiSize = ALIGN(iFileSize, SECTOR_SIZE);
	if (!ErasePartition(uiBegin, uiSize))
		goto exit_sparse;
	while (curChunk < header.total_chunks) {
		if (IsAborted())
			goto exit_sparse;
		if (fread(&chunk, 1, sizeof(chunk_header), file) != sizeof(chunk_header))
			goto exit_sparse;
		curChunk++;
		switch (chunk.chunk_type) {
		case CHUNK_TYPE_RAW:
			dwChunkDataSize = chunk.total_sz - sizeof(chunk_header);
			while (dwChunkDataSize) {
				if (IsAborted())
					goto exit_sparse;
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
				if (m_pComm->RKU_WriteLBA(uiBegin, uiTransferSec, pBuf) != ERR_SUCCESS)
					goto exit_sparse;
				dwChunkDataSize -= dwTransferBytes;
				iTotalWrite += dwTransferBytes;
				uiBegin += uiTransferSec;
				m_uiFwDone += dwTransferBytes;
				NotifyProgress(m_uiFwTotal, m_uiFwDone);
			}
			break;
		case CHUNK_TYPE_FILL:
			dwChunkDataSize = (u64)chunk.chunk_sz * header.blk_sz;
			if (fread(&dwFillByte, 1, 4, file) != 4)
				goto exit_sparse;
			while (dwChunkDataSize) {
				if (IsAborted())
					goto exit_sparse;
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
				if (m_pComm->RKU_WriteLBA(uiBegin, uiTransferSec, pBuf) != ERR_SUCCESS)
					goto exit_sparse;
				dwChunkDataSize -= dwTransferBytes;
				iTotalWrite += dwTransferBytes;
				uiBegin += uiTransferSec;
				m_uiFwDone += dwTransferBytes;
				NotifyProgress(m_uiFwTotal, m_uiFwDone);
			}
			break;
		case CHUNK_TYPE_DONT_CARE:
			dwChunkDataSize = (u64)chunk.chunk_sz * header.blk_sz;
			iTotalWrite += dwChunkDataSize;
			uiTransferSec = (dwChunkDataSize % SECTOR_SIZE == 0) ?
			                (dwChunkDataSize / SECTOR_SIZE) : (dwChunkDataSize / SECTOR_SIZE + 1);
			uiBegin += uiTransferSec;
			m_uiFwDone += dwChunkDataSize;
			NotifyProgress(m_uiFwTotal, m_uiFwDone);
			break;
		case CHUNK_TYPE_CRC32:
			if (fread(&dwCrc, 1, 4, file) != 4)
				goto exit_sparse;
			break;
		}
	}
	bSuccess = true;
exit_sparse:
	if (pBuf)
		delete []pBuf;
	return bSuccess;
}

bool CRKAndroidDevice::WriteSparseLBA(UINT uiBegin, UINT uiSize, const char *szFile)
{
	FILE *file = platform::OpenFile(szFile, "rb");
	if (!file)
		return false;
	bool bOk = WriteSparseLBACore(uiBegin, uiSize, file);
	fclose(file);
	return bOk;
}

// Returns true when the entry's payload starts with the Android sparse magic.
bool CRKAndroidDevice::IsSparseEntry(FW_ENTRY &entry)
{
	if (!m_pPackageFile)
		return false;
	sparse_header header;
	if (fseeko(m_pPackageFile, entry.uiDataOff, SEEK_SET) != 0)
		return false;
	if (fread(&header, 1, sizeof(header), m_pPackageFile) != sizeof(sparse_header))
		return false;
	return header.magic == SPARSE_HEADER_MAGIC;
}

// UBI/JFFS target SPI NAND/NOR, which this project does not support yet, so
// detect them and fail loudly instead of writing them as raw data.
bool CRKAndroidDevice::IsUbiEntry(FW_ENTRY &entry)
{
	if (!m_pPackageFile)
		return false;
	UINT magic = 0;
	if (fseeko(m_pPackageFile, entry.uiDataOff, SEEK_SET) != 0)
		return false;
	if (fread(&magic, 1, 4, m_pPackageFile) != 4)
		return false;
	return magic == UBI_HEADER_MAGIC;
}

bool CRKAndroidDevice::IsJffsEntry(FW_ENTRY &entry)
{
	if (!m_pPackageFile)
		return false;
	UINT magic = 0;
	if (fseeko(m_pPackageFile, entry.uiDataOff, SEEK_SET) != 0)
		return false;
	if (fread(&magic, 1, 4, m_pPackageFile) != 4)
		return false;
	return magic == 0xe0011985 || magic == 0x20031985;
}

// Writes a package entry by streaming it straight from the package, so the
// partition image is never materialized on disk (the rootfs can be several GB).
bool CRKAndroidDevice::WriteEntry(UINT uiBegin, FW_ENTRY &entry)
{
	if (!m_pPackageFile)
		return false;
	if (IsUbiEntry(entry) || IsJffsEntry(entry)) {
		if (m_pLog)
			m_pLog->Record("ERROR:WriteEntry--> %s is a UBI/JFFS image (SPI NAND/NOR), not supported", entry.szFile);
		return false;
	}
	if (IsSparseEntry(entry)) {
		// IsSparseEntry consumed the header; rewind so the sparse writer parses
		// the entry from its first byte.
		if (fseeko(m_pPackageFile, entry.uiDataOff, SEEK_SET) != 0)
			return false;
		return WriteSparseLBACore(uiBegin, entry.uiPartSize, m_pPackageFile);
	}
	return WriteLBACore(uiBegin, m_pPackageFile, (long long)entry.uiDataOff, (long long)entry.uiSize);
}

bool CRKAndroidDevice::EraseUbiBlock(u32 uiOffset, u32 uiPartSize)
{
	STRUCT_FLASHINFO_CMD info;
	BYTE flashID[5];
	UINT uiReadCount, uiStartBlock, uiEraseBlock, uiBlockCount, uiErasePos;
	if (m_pComm->RKU_ReadFlashID(flashID) != ERR_SUCCESS)
		return false;
	if (*(DWORD *)flashID == 0x434d4d45) // "EMMC"
		return true;
	if (m_pComm->RKU_ReadFlashInfo((BYTE *)&info, &uiReadCount) != ERR_SUCCESS)
		return false;
	if (uiPartSize == 0xFFFFFFFF)
		uiPartSize = info.uiFlashSize - uiOffset;
	uiStartBlock = uiOffset / info.usBlockSize;
	uiEraseBlock = (uiPartSize + info.usBlockSize - 1) / info.usBlockSize;
	uiErasePos = uiStartBlock;
	while (uiEraseBlock > 0) {
		uiBlockCount = (uiEraseBlock < MAX_ERASE_BLOCKS) ? uiEraseBlock : MAX_ERASE_BLOCKS;
		int iRet = m_pComm->RKU_EraseBlock(0, uiErasePos, uiBlockCount, ERASE_FORCE);
		if ((iRet != ERR_SUCCESS) && (iRet != ERR_FOUND_BAD_BLOCK))
			return false;
		uiErasePos += uiBlockCount;
		uiEraseBlock -= uiBlockCount;
	}
	return true;
}

bool CRKAndroidDevice::WriteParameter(char *szParameter)
{
	char *pParamBuf = NULL;
	if (!MakeParamBuffer(szParameter, pParamBuf))
		return false;
	int nParamSize = *(UINT *)(pParamBuf + 4) + 12;
	int nParamSec = BYTE2SECTOR(nParamSize);
	if (nParamSec > 1024) {
		delete []pParamBuf;
		return false;
	}
	std::vector<BYTE> writeBuf(nParamSec * SECTOR_SIZE, 0);
	memcpy(writeBuf.data(), pParamBuf, nParamSize);
	int iRet = m_pComm->RKU_WriteLBA(0x2000, nParamSec, writeBuf.data());
	delete []pParamBuf;
	return (iRet == ERR_SUCCESS);
}

bool CRKAndroidDevice::WriteGpt(char *szParameter)
{
	u8 flash_info[SECTOR_SIZE], master_gpt[34 * SECTOR_SIZE], backup_gpt[33 * SECTOR_SIZE];
	u32 total_size_sector;
	if (m_pComm->RKU_ReadFlashInfo(flash_info) != ERR_SUCCESS)
		return false;
	total_size_sector = *(u32 *)flash_info;
	if (strstr(szParameter, ".img")) {
		if (!load_gpt_buffer(szParameter, master_gpt, backup_gpt))
			return false;
		update_gpt_disksize(master_gpt, backup_gpt, total_size_sector);
	} else {
		PARAM_ITEM_VECTOR vecItems;
		CONFIG_ITEM_VECTOR vecUuid;
		if (!parse_parameter_file(szParameter, vecItems, vecUuid))
			return false;
		create_gpt_buffer(master_gpt, vecItems, vecUuid, total_size_sector, NULL);
		memcpy(backup_gpt, master_gpt + 2 * SECTOR_SIZE, 32 * SECTOR_SIZE);
		memcpy(backup_gpt + 32 * SECTOR_SIZE, master_gpt + SECTOR_SIZE, SECTOR_SIZE);
		prepare_gpt_backup(master_gpt, backup_gpt);
	}
	if (m_pComm->RKU_WriteLBA(0, 34, master_gpt) != ERR_SUCCESS)
		return false;
	if (m_pComm->RKU_WriteLBA(total_size_sector - 33, 33, backup_gpt) != ERR_SUCCESS)
		return false;
	return true;
}

bool CRKAndroidDevice::MakeSector0(PBYTE pSector)
{
	RK28_IDB_SEC0 *pSec0 = (RK28_IDB_SEC0 *)pSector;
	memset(pSector, 0, SECTOR_SIZE);
	pSec0->dwTag = 0x0FF0AA55;
	if (m_pImage && m_pImage->m_bootObject && m_pImage->m_bootObject->Rc4DisableFlag)
		pSec0->uiRc4Flag = 1;
	pSec0->usBootCode1Offset = 4;
	pSec0->usBootCode2Offset = 4;
	pSec0->usBootDataSize = (USHORT)m_uiLoaderDataSec;
	pSec0->usBootCodeSize = (USHORT)(m_uiLoaderDataSec + m_uiLoaderSizeSec);
	return true;
}

bool CRKAndroidDevice::MakeSector1(PBYTE pSector)
{
	RK28_IDB_SEC1 *pSec1 = (RK28_IDB_SEC1 *)pSector;
	memset(pSector, 0, SECTOR_SIZE);
	pSec1->usSysReservedBlock = 0xC;
	pSec1->usDisk0Size = 0xFFFF;
	pSec1->uiChipTag = 0x38324B52;
	if (m_pImage && m_pImage->m_bootObject) {
		STRUCT_RKTIME rt = (STRUCT_RKTIME)m_pImage->m_bootObject->ReleaseTime;
		if (rt.usYear != 0) {
			pSec1->usLoaderYear = UshortToBCD(rt.usYear);
			pSec1->usLoaderDate = (USHORT)((ByteToBCD(rt.ucMonth) << 8) | ByteToBCD(rt.ucDay));
			pSec1->usLoaderVer = (USHORT)((ByteToBCD(rt.ucHour) << 8) | ByteToBCD(rt.ucMinute));
			pSec1->usLastLoaderVer = (USHORT)((ByteToBCD(rt.ucSecond) << 8) | 0);
		}
	}
	if (m_usFlashDataSec)
		pSec1->usBlockSize = (USHORT)m_usFlashDataSec;
	return true;
}

bool CRKAndroidDevice::MakeSector2(PBYTE pSector)
{
	RK28_IDB_SEC2 *pSec2 = (RK28_IDB_SEC2 *)pSector;
	unsigned char chipInfo[CHIPINFO_LEN];
	memset(pSector, 0, SECTOR_SIZE);
	pSec2->usInfoSize = 0x10;
	if (!m_pComm || m_pComm->RKU_ReadChipInfo(chipInfo) != ERR_SUCCESS) {
		m_iError = 0x65;
		if (m_pLog)
			m_pLog->Record("ERROR:MakeSector2-->RKU_ReadChipInfo failed,RetCode(%d)", m_iError);
		return false;
	}
	memcpy(pSec2->bChipInfo, chipInfo, CHIPINFO_LEN);
	strcpy(pSec2->szVcTag, "VC");
	strcpy(pSec2->szCrcTag, "CRC");
	return true;
}

bool CRKAndroidDevice::MakeSector3(PBYTE pSector)
{
	RK28_IDB_SEC3 *pSec3 = (RK28_IDB_SEC3 *)pSector;
	memset(pSector, 0, SECTOR_SIZE);
	if (GetSnSize() && GetSnSize() <= 0x1e) {
		pSec3->usSNSize = GetSnSize();
		memcpy(pSec3->sn, GetSn(), GetSnSize());
	}
	if (GetBTAddr()) {
		pSec3->blueToothSize = RKDEVICE_BT_LEN;
		memcpy(pSec3->blueToothAddr, GetBTAddr(), RKDEVICE_BT_LEN);
	}
	if (GetMac()) {
		pSec3->macSize = RKDEVICE_MAC_LEN;
		memcpy(pSec3->macAddr, GetMac(), RKDEVICE_MAC_LEN);
	}
	if (GetImei()) {
		pSec3->imeiSize = RKDEVICE_IMEI_LEN;
		memcpy(pSec3->imei, GetImei(), RKDEVICE_IMEI_LEN);
	}
	if (GetUid()) {
		if (!m_bNewIDB || CheckUid(GetUidSize(), GetUid())) {
			pSec3->uidSize = GetUidSize();
			memcpy(pSec3->uid, GetUid(), GetUidSize());
		}
	}
	return true;
}

bool CRKAndroidDevice::MakeIDBlockData(PBYTE pIDB)
{
	RK28_IDB_SEC0 sector0Info;
	RK28_IDB_SEC1 sector1Info;
	RK28_IDB_SEC2 sector2Info;
	RK28_IDB_SEC3 sector3Info;
	UINT i;
	if (!MakeSector0((PBYTE)&sector0Info) ||
	    !MakeSector1((PBYTE)&sector1Info) ||
	    !MakeSector2((PBYTE)&sector2Info) ||
	    !MakeSector3((PBYTE)&sector3Info))
		return false;
	sector2Info.usSec0Crc = CRC_16((PBYTE)&sector0Info, SECTOR_SIZE);
	sector2Info.usSec1Crc = CRC_16((PBYTE)&sector1Info, SECTOR_SIZE);
	sector2Info.usSec3Crc = CRC_16((PBYTE)&sector3Info, SECTOR_SIZE);

	memcpy(pIDB, &sector0Info, SECTOR_SIZE);
	memcpy(pIDB + SECTOR_SIZE, &sector1Info, SECTOR_SIZE);
	memcpy(pIDB + SECTOR_SIZE * 3, &sector3Info, SECTOR_SIZE);
	memcpy(pIDB + SECTOR_SIZE * 2, &sector2Info, SECTOR_SIZE);
	return true;
}

bool CRKAndroidDevice::MakeNewIDBlockData(PBYTE pIDB)
{
	if (!m_pImage || !m_pImage->m_bootObject)
		return false;
	CRKBoot *pBoot = m_pImage->m_bootObject;
	DWORD dwSize, dwDelay, i;
	char szName[] = "FlashHead";
	char idx = pBoot->GetIndexByName(ENTRYLOADER, szName);
	if (idx == (char)-1)
		return false;
	if (!pBoot->GetEntryProperty(ENTRYLOADER, idx, dwSize, dwDelay))
		return false;
	PBYTE pHead = new BYTE[dwSize];
	memset(pHead, 0, dwSize);
	if (!pBoot->GetEntryData(ENTRYLOADER, idx, pHead)) {
		delete []pHead;
		return false;
	}
	m_uiLoaderHeadSec = (DWORD)((ALIGN(dwSize, 2048)) / SECTOR_SIZE);
	memcpy(pIDB, pHead, dwSize);
	if (pBoot->Rc4DisableFlag) {
		for (i = 0; i < dwSize / SECTOR_SIZE; i++)
			P_RC4(pHead + SECTOR_SIZE * i, SECTOR_SIZE);
	}
	delete []pHead;
	return true;
}

bool CRKAndroidDevice::MakeSpareData(PBYTE pData, UINT uiSectorCount, PBYTE pSpare)
{
	for (UINT i = 0; i < uiSectorCount; i++) {
		unsigned char oneSector[SECTOR_SIZE];
		memcpy(oneSector, pData + i * SECTOR_SIZE, SECTOR_SIZE);
		MakeSectorSpareData(oneSector, i + 1, pSpare + i * SPARE_SIZE);
	}
	return true;
}

bool CRKAndroidDevice::MakeParamFileBuffer(STRUCT_RKIMAGE_ITEM &item)
{
	if (!m_pImage)
		return false;
	m_uiParamDataSize = item.uiDataSize;
	PBYTE pBuf = new BYTE[m_uiParamDataSize * 2];
	memset(pBuf, 0, m_uiParamDataSize * 2);
	if (!m_pImage->GetData(item.uiDataOffset, m_uiParamDataSize, pBuf)) {
		delete []pBuf;
		return false;
	}
	if (m_pParamData) {
		delete []m_pParamData;
		m_pParamData = NULL;
	}
	m_pParamData = pBuf;
	return true;
}

// ---------------------------------------------------------------------------
// IDB layout helpers.
// ---------------------------------------------------------------------------
bool CRKAndroidDevice::FindAllIDB()
{
	m_ucIDBCount = 0;
	char cBlock = 0;
	for (int i = 0; i < MAX_IDB_COUNT; i++) {
		char cFound = (char)-1;
		if (!FindIDBlock(cBlock, cFound))
			break;
		m_uiIDBBlock[i] = (UINT)(unsigned char)cFound;
		cBlock = (char)(cFound + 1);
		m_ucIDBCount++;
	}
	return m_ucIDBCount > 0;
}

bool CRKAndroidDevice::FindIDBlock(char cStart, char &cBlock)
{
	char c = FindValidBlocks(cStart, 1);
	if ((unsigned char)c > 0x31)
		return false;
	while ((unsigned char)c <= 0x31) {
		unsigned char sector[SECTOR_SIZE];
		if (m_pComm->RKU_ReadSector(c * m_uiEraseSectorPerUnit, 4, sector) != ERR_SUCCESS) {
			m_iError = 0x65;
			if (m_pLog)
				m_pLog->Record("ERROR:FindIDBlock-->RKU_ReadSector failed,RetCode(%d)", m_iError);
			return false;
		}
		P_RC4(sector, SECTOR_SIZE);
		if (*(UINT *)sector == 0x0FF0AA55 && *(UINT *)(sector + 0x21a) == 0x38324B52) {
			cBlock = c;
			return true;
		}
		c = FindValidBlocks((char)(c + 1), 1);
		if ((unsigned char)c > 0x31)
			break;
	}
	return false;
}

bool CRKAndroidDevice::ReserveIDBlock(char cStart, char cCount)
{
	if ((unsigned char)cCount > 4)
		return true;
	unsigned int uiFirst = (unsigned int)cStart;
	char cLast = (char)(cStart + cCount - 1);
	for (unsigned int j = uiFirst; j <= (unsigned int)cCount + uiFirst - 1; j++) {
		char c = (char)j;
		char cFound = FindValidBlocks((char)c, 1);
		m_uiIDBBlock[j] = (UINT)(unsigned char)cFound;
		if ((unsigned char)cFound > 0x31)
			return false;
	}
	(void)cLast;
	return true;
}

bool CRKAndroidDevice::OffsetIDBlock(char cDiskIndex)
{
	unsigned char ucMask = (unsigned char)m_uiIDBBlock[(unsigned char)cDiskIndex];
	for (unsigned int j = ucMask; j < ucMask + (unsigned int)m_uiIDBBlockCount; j++)
		m_pBlockState->blockState[j] = 1;
	unsigned int uiPos = ucMask + m_uiIDBBlockCount;
	char cEnd = (char)((unsigned char)cDiskIndex + 1);
	for (int i = (unsigned char)cDiskIndex + 1; i <= 4; i++) {
		char cFound = FindValidBlocks((char)i, 1);
		if ((unsigned char)cFound > 0x31)
			return false;
		m_uiIDBBlock[i] = (UINT)(unsigned char)cFound;
	}
	(void)cEnd; (void)uiPos;
	return true;
}

bool CRKAndroidDevice::CalcIDBCount(bool bNewIDB)
{
	if (!bNewIDB) {
		if (!m_pOldIDBData)
			return false;
		RK28_IDB_SEC0 *pSec0 = (RK28_IDB_SEC0 *)m_pOldIDBData;
		m_uiLoaderDataSec = pSec0->usBootDataSize;
		m_uiLoaderSizeSec = pSec0->usBootCodeSize - pSec0->usBootDataSize;
		m_uiIDBBlockCount = 1;
		if (m_flashInfo.usPhyBlokcPerIDB)
			m_uiIDBBlockCount = m_flashInfo.usPhyBlokcPerIDB;
		m_uiIDBSectorCount = m_uiLoaderDataSec + m_uiLoaderSizeSec + 4;
		m_uiIDBBlockCount = (m_uiIDBSectorCount + m_usFlashDataSec - 1) / m_usFlashDataSec;
		return m_uiIDBBlockCount > 0;
	}
	// new IDB path: sizes come from the boot entry table.
	if (!GetLoaderSize() || !GetLoaderDataSize())
		return false;
	m_uiIDBSectorCount = m_uiLoaderHeadSec + m_uiLoaderDataSec + m_uiLoaderSizeSec;
	return true;
}

bool CRKAndroidDevice::CheckIDBData(PBYTE pIDB, bool bCheckCrc)
{
	RK28_IDB_SEC2 *pSec2 = (RK28_IDB_SEC2 *)(pIDB + SECTOR_SIZE * 2);
	if (pSec2->szVcTag[0] != 'V' || pSec2->szVcTag[1] != 'C' || pSec2->szVcTag[2] != 0) {
		if (m_pLog)
			m_pLog->Record("ERROR:CheckIDBData-->Check sector0 data failed !");
		return false;
	}
	if (CRC_16(pIDB, SECTOR_SIZE) != pSec2->usSec0Crc) {
		if (m_pLog)
			m_pLog->Record("ERROR:CheckIDBData-->Check sector0 crc failed !");
		return false;
	}
	if (pSec2->szCrcTag[0] != 'C') {
		if (m_pLog)
			m_pLog->Record("ERROR:CheckIDBData-->Boot Offset is wrong !");
		return false;
	}
	if (*(USHORT *)(pIDB + 0xc) != 4 || *(USHORT *)(pIDB + 0xe) != 4) {
		if (m_pLog)
			m_pLog->Record("ERROR:CheckIDBData-->Boot Offset is wrong !");
		return false;
	}
	if (bCheckCrc) {
		UINT uiCrc = CRC_32(pIDB + SECTOR_SIZE * 4, (*(USHORT *)(pIDB + 0x1fc)) * SECTOR_SIZE);
		if (uiCrc != *(UINT *)(pIDB + 0x1f2)) {
			if (m_pLog)
				m_pLog->Record("ERROR:CheckIDBData-->Check sector2 crc failed !");
			return false;
		}
	}
	return true;
}

bool CRKAndroidDevice::IDB528To512(PBYTE lpSrc, PBYTE lpDst, PBYTE lpSpare, UINT uiSectorNum)
{
	for (UINT i = 0; i < uiSectorNum; i++) {
		memcpy(lpDst, lpSrc, SECTOR_SIZE);
		if (lpSpare)
			memcpy(lpSpare, lpSrc + SECTOR_SIZE, SPARE_SIZE);
		lpSrc += SECTOR_SIZE + SPARE_SIZE;
		lpDst += SECTOR_SIZE;
		lpSpare += SPARE_SIZE;
	}
	return true;
}

bool CRKAndroidDevice::IDBRC4(PBYTE pSector)
{
	P_RC4(pSector, SECTOR_SIZE);
	P_RC4(pSector + 2 * SECTOR_SIZE, SECTOR_SIZE);
	P_RC4(pSector + 3 * SECTOR_SIZE, SECTOR_SIZE);
	return true;
}

// IsExistSector3Crc: returns 1 when pSec2 is non-NULL, and when its CRC tag
// (+0x1fa) is "CRC" it latches the read-IDB config flag and stores the sector3
// CRC (+0x1fe).
bool CRKAndroidDevice::IsExistSector3Crc(RKANDROID_IDB_SEC2 *pSec2)
{
	if (!pSec2)
		return false;
	if (strncmp((char *)((PBYTE)pSec2 + 0x1fa), "CRC", 3) == 0) {
		m_bReadIDBConfigFlag = true;
		m_usSec3Crc = *(USHORT *)((PBYTE)pSec2 + 0x1fe);
	}
	return true;
}

// FindBackupBuffer: scans candidate backup blocks (5..9) for the sector-1 tag
// (0x38324b52) or a "CRC" backup record, verifying the sector CRC when found.
bool CRKAndroidDevice::FindBackupBuffer()
{
	unsigned char buf[0x210];
	for (int i = 5; i < 10; i++) {
		memset(buf, 0, sizeof(buf));
		UINT uiSector = (UINT)i * m_uiEraseSectorPerUnit;
		if (m_pComm->RKU_ReadSector(uiSector, 2, buf) != ERR_SUCCESS) {
			if (m_pLog)
				m_pLog->Record("ERROR:FindBackupBuffer-->RKU_ReadSector failed,RetCode(%d)", m_iError);
			continue;
		}
		if (*(UINT *)buf == 0x38324b52)
			return true;
		if (*(UINT *)buf == 0x435243) { // "CRC"
			return CheckCrc16(buf + 0x210, *(USHORT *)(buf + 4), 0x200);
		}
		if (m_pLog)
			m_pLog->Record("ERROR:FindBackupBuffer-->Check IDB failed");
	}
	return false;
}

bool CRKAndroidDevice::FindBackupSector3()
{
	unsigned char buf[0x210];
	memset(buf, 0, sizeof(buf));
	if (m_pComm->RKU_ReadLBA(0x3c, 2, buf) != ERR_SUCCESS) {
		if (m_pLog)
			m_pLog->Record("ERROR:FindBackupSector3-->RKU_ReadLBA failed,RetCode(%d)", m_iError);
		return false;
	}
	if (*(UINT *)buf != 0x435243) { // "CRC"
		if (m_pLog)
			m_pLog->Record("ERROR:FindBackupSector3-->no found sector3");
		return true;
	}
	if (!CheckCrc16(buf + 0x200, *(USHORT *)(buf + 4), 0x200)) {
		if (m_pLog)
			m_pLog->Record("ERROR:FindBackupSector3-->check crc failed");
		*(USHORT *)(buf + 4) = CRC_16(buf + 0x200, 0x200);
	}
	if (!m_pOldSectorData) {
		m_pOldSectorData = new BYTE[SECTOR_SIZE];
		memset(m_pOldSectorData, 0, SECTOR_SIZE);
	}
	memcpy(m_pOldSectorData, buf + 0x200, SECTOR_SIZE);
	return true;
}

// GetOldIDBData: scans the first IDB blocks looking for the 0x0ff0aa55 sector-0
// tag, saving sector0..3 into the old-IDB buffers (m_pOldIDBData /
// m_pSectorData / m_pOldSectorData).
bool CRKAndroidDevice::GetOldIDBData()
{
	m_ucIDBCount = 0;
	for (UINT uiSector = 0x40; uiSector < 0x1440; uiSector += 0x400) {
		unsigned char buf[SECTOR_SIZE];
		if (m_pComm->RKU_ReadLBA(uiSector, 4, buf) != ERR_SUCCESS)
			continue;
		P_RC4(buf, SECTOR_SIZE);
		if (*(UINT *)buf != 0x0FF0AA55)
			continue;
		if (*(UINT *)(buf + 0x20a) != 0x38324B52)
			continue;
		m_ucIDBCount++;
		return true;
	}
	return false;
}

bool CRKAndroidDevice::GetOldSectorData()
{
	if (m_ucIDBCount <= 0)
		return false;
	UINT uiCount = (UINT)m_ucIDBCount;
	PBYTE pBuf = new BYTE[uiCount * 0x210];
	memset(pBuf, 0, uiCount * 0x210);
	if (!GetWriteBackData(uiCount, pBuf)) {
		delete []pBuf;
		return false;
	}
	if (m_pOldIDBData) { delete []m_pOldIDBData; m_pOldIDBData = NULL; }
	m_pOldIDBData = new BYTE[SECTOR_SIZE];
	memcpy(m_pOldIDBData, pBuf, SECTOR_SIZE);
	P_RC4(m_pOldIDBData, SECTOR_SIZE);
	delete []pBuf;
	return true;
}

bool CRKAndroidDevice::GetReservedBlockByte()
{
	unsigned char buf[0x210];
	if (m_pComm->RKU_ReadSector(m_uiIDBBlock[0] * m_uiEraseSectorPerUnit, 1, buf) != ERR_SUCCESS) {
		if (m_pLog)
			m_pLog->Record("ERROR:GetReservedBlockByte-->RKU_ReadSector failed,RetCode(%d)", m_iError);
		return false;
	}
	USHORT usBlocks = *(USHORT *)(buf + 0x210 + 0);
	m_uiReservedBlockSize = (UINT)usBlocks * m_usFlashDataSec * 1024;
	return true;
}

UINT CRKAndroidDevice::GetLoaderSize()
{
	if (!m_pImage || !m_pImage->m_bootObject)
		return 0;
	char szName[] = "FlashBoot";
	char idx = m_pImage->m_bootObject->GetIndexByName(ENTRYLOADER, szName);
	if (idx == (char)-1)
		return 0;
	DWORD dwSize, dwDelay;
	if (!m_pImage->m_bootObject->GetEntryProperty(ENTRYLOADER, idx, dwSize, dwDelay))
		return 0;
	m_uiLoaderSizeSec = dwSize ? (((dwSize - 1) >> 11) * 4 + 4) : 0;
	return dwSize;
}

UINT CRKAndroidDevice::GetLoaderDataSize()
{
	if (!m_pImage || !m_pImage->m_bootObject)
		return 0;
	char szName[] = "FlashData";
	char idx = m_pImage->m_bootObject->GetIndexByName(ENTRYLOADER, szName);
	if (idx == (char)-1)
		return 0;
	DWORD dwSize, dwDelay;
	if (!m_pImage->m_bootObject->GetEntryProperty(ENTRYLOADER, idx, dwSize, dwDelay))
		return 0;
	m_uiLoaderDataSec = dwSize ? (((dwSize - 1) >> 11) * 4 + 4) : 0;
	return dwSize;
}

UINT CRKAndroidDevice::GetLoaderHeadSize()
{
	if (!m_pImage || !m_pImage->m_bootObject)
		return 0;
	CRKBoot *pBoot = m_pImage->m_bootObject;
	char szName[] = "FlashHead";
	char idx = pBoot->GetIndexByName(ENTRYLOADER, szName);
	if (idx == (char)-1)
		return 0;
	UCHAR ucCount = pBoot->EntryLoaderCount;
	UINT uiTotal = 0;
	for (UCHAR i = 0; i < ucCount; i++) {
		DWORD dwSize, dwDelay;
		if (!pBoot->GetEntryProperty(ENTRYLOADER, i, dwSize, dwDelay))
			continue;
		if (dwSize)
			uiTotal += ((dwSize - 1) >> 11) * 4 + 4;
	}
	m_uiLoaderHeadSec = uiTotal;
	return uiTotal;
}

UINT CRKAndroidDevice::GetNewDiskSize()
{
	if (!m_pImage)
		return 0;
	PBYTE pData = NULL;
	USHORT usLen = 0;
	m_pImage->GetReservedData(pData, usLen);
	if (pData) {
		m_uiNewDiskSize = *(USHORT *)pData;
	}
	return m_uiNewDiskSize;
}

bool CRKAndroidDevice::GetBlocksState()
{
	return BuildBlockStateMap((UCHAR)m_flashInfo.bFlashCS) != 0;
}

// ---------------------------------------------------------------------------
// Environment / partition classification.
// ---------------------------------------------------------------------------
bool CRKAndroidDevice::IsEnvFirmware(bool &bRet)
{
	bRet = false;
	return false;
}

// EraseAllBlocks: the body is the base CRKDevice::EraseAllBlocks flow
// (m_bEmmc/m_bDirectLba -> EraseEmmc, other parts erased block-by-block with
// RKU_EraseBlock(ERASE_FORCE)); it only adds the ERASEFLASH progress callback,
// which CRKDevice::EraseAllBlocks already issues. So it is the base result
// translated to bool (0 == success).
bool CRKAndroidDevice::EraseAllBlocks()
{
	return CRKDevice::EraseAllBlocks() == 0;
}

// EraseSparseRegion: steps uiSize down in uiChunk-sized RKU_EraseLBA calls,
// tolerating both ERR_SUCCESS and ERR_FOUND_BAD_BLOCK (-7); any other status
// records error 0x65 with the ErasePartition log string and returns false. Note
// the return value is the number of bytes still pending cast to bool: a
// successful run returns true.
bool CRKAndroidDevice::EraseSparseRegion(UINT uiOffset, UINT uiSize, UINT uiChunk)
{
	UINT uiPos = uiOffset;
	UINT uiRemain = uiSize;
	while (uiRemain) {
		UINT uiErase = (uiRemain >= uiChunk) ? uiChunk : uiRemain;
		uiRemain -= uiErase;
		uiPos += uiErase;
		int iRet = m_pComm->RKU_EraseLBA(uiPos - uiErase, uiErase);
		if (iRet != ERR_SUCCESS && iRet != ERR_FOUND_BAD_BLOCK) {
			m_iError = 0x65;
			if (m_pLog)
				m_pLog->Record("ERROR:ErasePartition-->erase partition failed,err=%d", iRet);
			return false;
		}
	}
	return true;
}

// EraseUbiRegion: eMMC is a no-op (returns true); on NOR the range is converted
// from bytes to blocks using the flash-info block size (0xffffffff means "to the
// end of the device") and erased with RKU_EraseBlock(ERASE_FORCE) in
// MAX_ERASE_BLOCKS chunks, tolerating ERR_FOUND_BAD_BLOCK.
bool CRKAndroidDevice::EraseUbiRegion(UINT uiOffset, UINT uiPartSize)
{
	BYTE flashID[5];
	int iRet;
	iRet = m_pComm->RKU_ReadFlashID(flashID);
	if (iRet != ERR_SUCCESS) {
		m_iError = 0x65;
		if (m_pLog)
			m_pLog->Record("Error:EraseUbiRegion-->RKU_ReadFlashID failed,RetCode(%d)", iRet);
		return false;
	}
	if (*(DWORD *)flashID == 0x434d4d45) // "EMMC"
		return true;
	STRUCT_FLASHINFO_CMD info;
	UINT uiRead;
	iRet = m_pComm->RKU_ReadFlashInfo((BYTE *)&info, &uiRead);
	if (iRet != ERR_SUCCESS) {
		m_iError = 0x65;
		if (m_pLog)
			m_pLog->Record("Error:EraseUbiRegion-->RKU_ReadFlashInfo err=%d", iRet);
		return false;
	}
	UINT uiStartBlock = uiOffset / info.usBlockSize;
	UINT uiEraseBlock;
	if (uiPartSize == 0xffffffff)
		uiEraseBlock = (info.uiFlashSize - uiOffset) / info.usBlockSize;
	else {
		uiEraseBlock = uiPartSize / info.usBlockSize;
		if (uiPartSize % info.usBlockSize)
			uiEraseBlock++;
	}
	UINT uiErasePos = uiStartBlock;
	while (uiEraseBlock) {
		UINT uiCount = (uiEraseBlock < MAX_ERASE_BLOCKS) ? uiEraseBlock : MAX_ERASE_BLOCKS;
		iRet = m_pComm->RKU_EraseBlock(0, uiErasePos, uiCount, ERASE_FORCE);
		if (iRet != ERR_SUCCESS && iRet != ERR_FOUND_BAD_BLOCK) {
			m_iError = 0x65;
			if (m_pLog)
				m_pLog->Record("Error:EraseUbiRegion-->RKU_EraseBlock failed,RetCode(%d)", iRet);
			return false;
		}
		uiErasePos += uiCount;
		uiEraseBlock -= uiCount;
	}
	return true;
}

// Poll a Test-Unit-Ready subcode until the device reports the erase complete.
// Shared by EraseSystemPart / EraseUserDataPart (only the subcode, progress id
// and log string differ). Mirrors 0x43aee0 / 0x43b040: >= 3 consecutive
// unexpected errors abort; ERR_DEVICE_UNREADY (-6) fires the intermediate
// callback (once) and loops, resetting the retry budget; ERR_SUCCESS fires the
// final CALL_LAST callback when an intermediate one was issued, then returns.
static bool ErasePartByTestUnit(CRKComm *pComm, ProgressPromptCB cb, platform::FileLog *pLog,
                                UINT uiPartSize, BYTE ucSubCode, ENUM_PROGRESS_PROMPT emPrompt,
                                const char *szPart)
{
	DWORD uiTotal = 0, uiCurrent = 0;
	int iReported = 0;
	int iRet;
	for (int iTry = 3; iTry > 0; iTry--) {
		iRet = pComm->RKU_TestDeviceReady(&uiTotal, &uiCurrent, ucSubCode);
		if (iRet == ERR_SUCCESS || iRet == ERR_DEVICE_UNREADY) {
			if (iRet != ERR_SUCCESS) {
				if (cb) {
					cb(uiPartSize, emPrompt, uiTotal, uiCurrent, (ENUM_CALL_STEP)iReported);
					iReported = 1;
				}
				std::this_thread::sleep_for(std::chrono::seconds(1));
				continue;
			}
			if (iReported && cb)
				cb(uiPartSize, emPrompt, uiTotal, uiCurrent, CALL_LAST);
			return true;
		}
		if (pLog)
			pLog->Record("ERROR:Erase%sPart-->RKU_TestDeviceReady failed,RetCode(%d)", szPart, iRet);
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	return false;
}

bool CRKAndroidDevice::EraseSystemPart(UINT uiPartSize)
{
	return ErasePartByTestUnit(m_pComm, m_callBackProc, m_pLog, uiPartSize,
	                           TU_ERASESYSTEM_SUBCODE, ERASESYSTEM_PROGRESS, "System");
}

bool CRKAndroidDevice::EraseUserDataPart(UINT uiPartSize)
{
	return ErasePartByTestUnit(m_pComm, m_callBackProc, m_pLog, uiPartSize,
	                           TU_ERASEUSERDATA_SUBCODE, ERASEUSERDATA_PROGRESS, "UserData");
}

// EraseDirectLBABlock: for each sector in [uiOffset, uiOffset+uiSize) write a
// 0xff buffer of up to 0x20 sectors (one erase unit). The buffer is 0x4000 bytes
// (0x20 * 0x200); the device is only asked for at most `m_uiEraseSectorPerUnit`
// (CRKDevice+0x752) sectors per call, and each iteration advances by the *erase
// unit* count, not by the amount actually written. uiSize==0 returns
// immediately. On failure error 0x65 is recorded and false is returned (the
// native nonzero status is treated as failure).
bool CRKAndroidDevice::EraseDirectLBABlock(UINT uiOffset, UINT uiSize)
{
	UINT uiChunk = 0x20;
	BYTE emptyData[0x4000];
	memset(emptyData, 0xff, sizeof(emptyData));
	if (uiSize == 0)
		return true;
	UINT uiEnd = uiOffset + uiSize;
	for (UINT uiSector = uiOffset; uiSector != uiEnd; uiSector++) {
		UINT uiEraseUnit = m_uiEraseSectorPerUnit;
		UINT uiPos = uiSector * uiEraseUnit;
		while (uiEraseUnit > 0) {
			UINT uiWrite = (uiEraseUnit <= uiChunk) ? uiEraseUnit : uiChunk;
			int iRet = m_pComm->RKU_WriteLBA(uiPos, uiWrite, emptyData);
			if (iRet != ERR_SUCCESS) {
				m_iError = 0x65;
				if (m_pLog)
					m_pLog->Record("ERROR:EraseDirectLBABlock-->RKU_WriteLBA failed,RetCode(%d)", iRet);
				return false;
			}
			uiPos += uiChunk;
			uiEraseUnit -= uiWrite;
		}
	}
	return true;
}

// DownloadBoot: same entry 471/472 walk as CRKDevice, but each failure also
// records the detail code the application can use for diagnostics
// (0x6f/0x70/0x71/0x72 -> 111..114).
int CRKAndroidDevice::DownloadBoot()
{
	UCHAR i;
	DWORD dwSize, dwDelay;
	PBYTE pBuffer = NULL;
	if (!m_pImage || !m_pImage->m_bootObject || !m_pComm)
		return -2;
	CRKBoot *pBoot = m_pImage->m_bootObject;
	for (i = 0; i < pBoot->Entry471Count; i++) {
		if (!pBoot->GetEntryProperty(ENTRY471, i, dwSize, dwDelay)) {
			m_iError = 0x6f;
			if (m_pLog)
				m_pLog->Record("<LAYER %s> ERROR:DownloadBoot-->GetEntry471Property failed,index(%d)", m_layerName.c_str(), i);
			return -2;
		}
		if (dwSize > 0) {
			pBuffer = new BYTE[dwSize];
			if (!pBoot->GetEntryData(ENTRY471, i, pBuffer)) {
				m_iError = 0x6f;
				if (m_pLog)
					m_pLog->Record("<LAYER %s> ERROR:DownloadBoot-->GetEntry471Data failed,index(%d)", m_layerName.c_str(), i);
				delete []pBuffer;
				return -3;
			}
			if (!Boot_VendorRequest(0x0471, pBuffer, dwSize)) {
				m_iError = 0x71;
				if (m_pLog)
					m_pLog->Record("<LAYER %s> ERROR:DownloadBoot-->Boot_VendorRequest471 failed,index(%d)", m_layerName.c_str(), i);
				delete []pBuffer;
				return -4;
			}
			delete []pBuffer;
			pBuffer = NULL;
			if (dwDelay > 0)
				std::this_thread::sleep_for(std::chrono::microseconds(dwDelay * 1000));
		}
	}
	for (i = 0; i < pBoot->Entry472Count; i++) {
		if (!pBoot->GetEntryProperty(ENTRY472, i, dwSize, dwDelay)) {
			m_iError = 0x70;
			if (m_pLog)
				m_pLog->Record("<LAYER %s> ERROR:DownloadBoot-->GetEntry472Property failed,index(%d)", m_layerName.c_str(), i);
			return -2;
		}
		if (dwSize > 0) {
			pBuffer = new BYTE[dwSize];
			if (!pBoot->GetEntryData(ENTRY472, i, pBuffer)) {
				m_iError = 0x70;
				if (m_pLog)
					m_pLog->Record("<LAYER %s> ERROR:DownloadBoot-->GetEntry472Data failed,index(%d)", m_layerName.c_str(), i);
				delete []pBuffer;
				return -3;
			}
			if (!Boot_VendorRequest(0x0472, pBuffer, dwSize)) {
				m_iError = 0x72;
				if (m_pLog)
					m_pLog->Record("<LAYER %s> ERROR:DownloadBoot-->Boot_VendorRequest472 failed,index(%d)", m_layerName.c_str(), i);
				delete []pBuffer;
				return -4;
			}
			delete []pBuffer;
			pBuffer = NULL;
			if (dwDelay > 0)
				std::this_thread::sleep_for(std::chrono::microseconds(dwDelay * 1000));
		}
	}
	std::this_thread::sleep_for(std::chrono::seconds(1));
	return 0;
}

bool CRKAndroidDevice::Boot_VendorRequest(UINT uiRequest, PBYTE pBuffer, UINT uiSize)
{
	return (m_pComm->RKU_DeviceRequest(uiRequest, pBuffer, uiSize) == ERR_SUCCESS);
}

bool CRKAndroidDevice::ReadCapability()
{
	int ret;
	BYTE data[8];
	ret = m_pComm->RKU_ReadCapability(data);
	if (ret != ERR_SUCCESS) {
		if (m_pLog)
			m_pLog->Record("ERROR:ReadCapability-->RKU_ReadCapability failed,err(%d)", ret);
		return false;
	}
	m_bDirectLbaFlag = (data[0] & 0x1) != 0;
	m_bFirst4AccessFlag = (data[0] & 0x4) != 0;
	return true;
}

bool CRKAndroidDevice::CheckParamPartSize(PBYTE pParam, int iOffset, int iSize)
{
	(void)iOffset; (void)iSize;
	if (!GetParameterPartOffset(*(STRUCT_RKIMAGE_ITEM *)pParam))
		return false;
	return false;
}

bool CRKAndroidDevice::GetParameterPartOffset(STRUCT_RKIMAGE_ITEM &item)
{
	if (!m_pImage)
		return false;
	UINT uiSize = item.uiDataSize - 12;
	PBYTE pBuf = new BYTE[uiSize];
	memset(pBuf, 0, uiSize);
	if (!m_pImage->GetData((long long)item.uiDataOffset + 8, uiSize, pBuf)) {
		delete []pBuf;
		return false;
	}
	PARAM_ITEM_VECTOR vecItems;
	CONFIG_ITEM_VECTOR vecUuid;
	// The parameter payload is text; parse it and expose the "parameter"
	// partition offset through the item.
	pBuf[uiSize - 1] = 0;
	parse_parameter((char *)pBuf, vecItems, vecUuid);
	delete []pBuf;
	return true;
}

bool CRKAndroidDevice::GetParameterGptFlag(STRUCT_RKIMAGE_ITEM &item)
{
	if (!m_pImage)
		return false;
	UINT uiSize = item.uiDataSize - 12;
	PBYTE pBuf = new BYTE[uiSize];
	memset(pBuf, 0, uiSize);
	if (!m_pImage->GetData((long long)item.uiDataOffset + 8, uiSize, pBuf)) {
		delete []pBuf;
		return false;
	}
	pBuf[uiSize - 1] = 0;
	int iRet = fw_get_parameter_gptflag((const char *)pBuf);
	delete []pBuf;
	return iRet != 0;
}

// ---------------------------------------------------------------------------
// RKA_* worker family. The Check variants classify the entry (sparse/ubi/
// jffs/param/gpt) and advance the progress counter; the Download variants
// push the payload to the device.
// ---------------------------------------------------------------------------
bool CRKAndroidDevice::RKA_File_Check(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep)
{
	ucRet = 0;
	ullDone += item.uiDataSize;
	if (m_callBackProc)
		m_callBackProc(0, CHECKIMAGE_PROGRESS, (long long)ullTotal, (long long)ullDone, emStep);
	emStep = CALL_MIDDLE;
	return true;
}

bool CRKAndroidDevice::RKA_File_Download(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep)
{
	(void)ucRet; (void)ullDone; (void)ullTotal; (void)emStep;
	if (!m_pImage)
		return false;
	UINT uiSize = item.uiDataSize;
	PBYTE pBuf = new BYTE[uiSize];
	memset(pBuf, 0, uiSize);
	if (!m_pImage->GetData(item.uiDataOffset, uiSize, pBuf)) {
		delete []pBuf;
		return false;
	}
	FILE *f = platform::OpenFile("/tmp/ut_part.img", "wb");
	if (!f) { delete []pBuf; return false; }
	fwrite(pBuf, 1, uiSize, f);
	fclose(f);
	delete []pBuf;
	if (IsSparseImage(item))
		return RKA_SparseFile_Download(item, ucRet, ullDone, ullTotal, emStep);
	return WriteLBA(item.uiPartOffset, "/tmp/ut_part.img");
}

bool CRKAndroidDevice::RKA_SparseFile_Check(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep)
{
	return RKA_File_Check(item, ucRet, ullDone, ullTotal, emStep);
}

bool CRKAndroidDevice::RKA_SparseFile_Download(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep)
{
	(void)ucRet; (void)ullDone; (void)ullTotal; (void)emStep;
	return WriteSparseLBA(item.uiPartOffset, item.uiPartSize, "/tmp/ut_part.img");
}

bool CRKAndroidDevice::RKA_Param_Check(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep)
{
	return RKA_File_Check(item, ucRet, ullDone, ullTotal, emStep);
}

bool CRKAndroidDevice::RKA_Param_Download(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep)
{
	(void)ucRet; (void)ullDone; (void)ullTotal; (void)emStep;
	if (!MakeParamFileBuffer(item))
		return false;
	if (GetParameterGptFlag(item))
		return RKA_Gpt_Download(item, ucRet, ullDone, ullTotal, emStep);
	return WriteParameter((char *)m_pParamData);
}

bool CRKAndroidDevice::RKA_Gpt_Check(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep)
{
	return RKA_File_Check(item, ucRet, ullDone, ullTotal, emStep);
}

bool CRKAndroidDevice::RKA_Gpt_Download(STRUCT_RKIMAGE_ITEM &item, UCHAR &ucRet, unsigned long long &ullDone, unsigned long long ullTotal, ENUM_CALL_STEP &emStep)
{
	(void)item; (void)ucRet; (void)ullDone; (void)ullTotal; (void)emStep;
	return WriteGpt("/tmp/ut_parameter.txt");
}

// ---------------------------------------------------------------------------
// IDB workflow
// ---------------------------------------------------------------------------
bool CRKAndroidDevice::PrepareIDB(bool &bRet)
{
	bRet = false;
	if (!m_pImage || !m_pComm)
		return false;
	CRKBoot *pBoot = m_pImage->m_bootObject;
	if (!pBoot)
		return false;

	PBYTE loaderCodeBuffer = NULL, loaderDataBuffer = NULL, loaderHeadBuffer = NULL;
	signed char index;
	USHORT usFlashDataSec, usFlashBootSec, usFlashHeadSec;
	DWORD dwLoaderSize = 0, dwLoaderDataSize = 0, dwLoaderHeadSize = 0, dwDelay, dwSectorNum;
	char loaderCodeName[] = "FlashBoot", loaderDataName[] = "FlashData", loaderHeadName[] = "FlashHead";
	BYTE capability[8];
	bool bOk = false;

	if (m_pIDBData) {
		delete []m_pIDBData;
		m_pIDBData = NULL;
	}

	index = pBoot->GetIndexByName(ENTRYLOADER, loaderCodeName);
	if (index == -1) goto done;
	if (!pBoot->GetEntryProperty(ENTRYLOADER, index, dwLoaderSize, dwDelay)) goto done;
	loaderCodeBuffer = new BYTE[dwLoaderSize];
	memset(loaderCodeBuffer, 0, dwLoaderSize);
	if (!pBoot->GetEntryData(ENTRYLOADER, index, loaderCodeBuffer)) goto done;

	index = pBoot->GetIndexByName(ENTRYLOADER, loaderDataName);
	if (index == -1) goto done;
	if (!pBoot->GetEntryProperty(ENTRYLOADER, index, dwLoaderDataSize, dwDelay)) goto done;
	loaderDataBuffer = new BYTE[dwLoaderDataSize];
	memset(loaderDataBuffer, 0, dwLoaderDataSize);
	if (!pBoot->GetEntryData(ENTRYLOADER, index, loaderDataBuffer)) goto done;

	index = pBoot->GetIndexByName(ENTRYLOADER, loaderHeadName);
	if (index != -1) {
		if (!pBoot->GetEntryProperty(ENTRYLOADER, index, dwLoaderHeadSize, dwDelay)) goto done;
		loaderHeadBuffer = new BYTE[dwLoaderHeadSize];
		memset(loaderHeadBuffer, 0, dwLoaderHeadSize);
		if (!pBoot->GetEntryData(ENTRYLOADER, index, loaderHeadBuffer)) goto done;
		if (m_pComm->RKU_ReadCapability(capability) != ERR_SUCCESS) goto done;
		if ((capability[1] & 1) == 0) goto done;
		m_bNewIDB = true;
	} else {
		m_bNewIDB = false;
	}

	usFlashDataSec = (ALIGN(dwLoaderDataSize, 2048)) / SECTOR_SIZE;
	usFlashBootSec = (ALIGN(dwLoaderSize, 2048)) / SECTOR_SIZE;
	if (m_bNewIDB) {
		usFlashHeadSec = (ALIGN(dwLoaderHeadSize, 2048)) / SECTOR_SIZE;
		dwSectorNum = usFlashHeadSec + usFlashDataSec + usFlashBootSec;
	} else {
		dwSectorNum = 4 + usFlashDataSec + usFlashBootSec;
	}
	m_uiLoaderDataSec = usFlashDataSec;
	m_uiLoaderSizeSec = usFlashBootSec;
	m_uiLoaderHeadSec = m_bNewIDB ? usFlashHeadSec : 0;
	m_uiIDBSectorCount = 4 + usFlashDataSec + usFlashBootSec;
	m_pIDBData = new BYTE[dwSectorNum * SECTOR_SIZE];
	memset(m_pIDBData, 0, dwSectorNum * SECTOR_SIZE);
	if (m_bNewIDB) {
		if (pBoot->Rc4DisableFlag) {
			for (unsigned int i = 0; i < dwLoaderHeadSize / SECTOR_SIZE; i++) P_RC4(loaderHeadBuffer + SECTOR_SIZE * i, SECTOR_SIZE);
			for (unsigned int i = 0; i < dwLoaderDataSize / SECTOR_SIZE; i++) P_RC4(loaderDataBuffer + SECTOR_SIZE * i, SECTOR_SIZE);
			for (unsigned int i = 0; i < dwLoaderSize / SECTOR_SIZE; i++) P_RC4(loaderCodeBuffer + SECTOR_SIZE * i, SECTOR_SIZE);
		}
		memcpy(m_pIDBData, loaderHeadBuffer, dwLoaderHeadSize);
		memcpy(m_pIDBData + SECTOR_SIZE * usFlashHeadSec, loaderDataBuffer, dwLoaderDataSize);
		memcpy(m_pIDBData + SECTOR_SIZE * (usFlashHeadSec + usFlashDataSec), loaderCodeBuffer, dwLoaderSize);
	} else {
		if (MakeIDBlockData(m_pIDBData) != true)
			goto done;
		if (pBoot->Rc4DisableFlag) {
			for (unsigned int i = 0; i < dwLoaderDataSize / SECTOR_SIZE; i++) P_RC4(loaderDataBuffer + SECTOR_SIZE * i, SECTOR_SIZE);
			for (unsigned int i = 0; i < dwLoaderSize / SECTOR_SIZE; i++) P_RC4(loaderCodeBuffer + SECTOR_SIZE * i, SECTOR_SIZE);
		}
		memcpy(m_pIDBData + SECTOR_SIZE * 4, loaderDataBuffer, dwLoaderDataSize);
		memcpy(m_pIDBData + SECTOR_SIZE * (4 + usFlashDataSec), loaderCodeBuffer, dwLoaderSize);
		RK28_IDB_SEC2 *pSec2 = (RK28_IDB_SEC2 *)(m_pIDBData + SECTOR_SIZE * 2);
		pSec2->uiBootCodeCrc = CRC_32(m_pIDBData + SECTOR_SIZE * 4, (*(USHORT *)(m_pIDBData + 0x1fc)) * SECTOR_SIZE);
		IDBRC4(m_pIDBData);
	}
	m_uiIDBSectorNum = dwSectorNum;
	bOk = true;
done:
	if (loaderCodeBuffer) delete []loaderCodeBuffer;
	if (loaderDataBuffer) delete []loaderDataBuffer;
	if (loaderHeadBuffer) delete []loaderHeadBuffer;
	bRet = bOk;
	return bOk;
}

bool CRKAndroidDevice::DownloadIDBlock(bool &bRet)
{
	bRet = false;
	if (!m_pIDBData || !m_uiIDBSectorNum || !m_pComm)
		return false;
	bool bOk = WriteIDBlock(NULL, m_pIDBData, m_uiIDBSectorNum, m_bNewIDB);
	bRet = bOk;
	return bOk;
}

bool CRKAndroidDevice::WriteIDBlock(PBYTE pData, PBYTE pIDB, UINT uiSectorNum, bool bNewIDB)
{
	(void)pData; (void)bNewIDB;
	if (!m_pComm)
		return false;
	return (m_pComm->RKU_WriteLBA(64, uiSectorNum, pIDB) == ERR_SUCCESS);
}

bool CRKAndroidDevice::WriteIDBWithDirect(PBYTE pIDB)
{
	if (!pIDB)
		return false;
	UINT uiChunk = 0x400;
	UINT uiSectorCount = m_uiIDBSectorCount;
	UINT uiPos = 0x40;
	for (UINT i = 0; i < 2; i++) {
		int iRet = m_pComm->RKU_WriteLBA(uiPos, uiSectorCount, pIDB);
		if (iRet != ERR_SUCCESS) {
			if (m_pLog)
				m_pLog->Record("ERROR:WriteIDBWithDirect-->RKU_WriteLBA failed,RetCode(%d)", iRet);
			return false;
		}
		uiPos += uiChunk;
	}
	return true;
}

// EraseIDB. The sector count is the saved old-IDB count (first USHORT of
// m_pOldIDBData) when an IDB was found, else the default 0x32. eMMC parts go
// through EraseDirectLBABlock (direct-LBA) or EraseEmmcBlock; other parts erase
// the IDB via EraseEmmcIDB (when direct-LBA is advertised) followed by the block
// erase.
bool CRKAndroidDevice::EraseIDB()
{
	if (!m_pComm)
		return false;
	UINT uiCount = 0x32;
	if (m_ucIDBCount > 0)
		uiCount = *(USHORT *)m_pOldIDBData;
	if (m_bEmmcStor) {
		if (!ReadCapability())
			return false;
		if (m_bDirectLbaFlag) {
			if (!EraseDirectLBABlock(0, uiCount)) {
				if (m_pLog)
					m_pLog->Record("ERROR:EraseIDB-->EraseDirectLBABlock failed");
				return false;
			}
			if (!EraseEmmcIDB()) {
				if (m_pLog)
					m_pLog->Record("ERROR:EraseIDB-->EraseEmmcIDB failed");
				return false;
			}
			return true;
		}
		if (EraseEmmcBlock(0, 0, uiCount) != ERR_SUCCESS) {
			if (m_pLog)
				m_pLog->Record("ERROR:EraseIDB-->EraseEmmcBlock failed");
			return false;
		}
		return true;
	}
	if (!ReadCapability())
		return false;
	if (m_bDirectLbaFlag) {
		if (!EraseEmmcIDB()) {
			if (m_pLog)
				m_pLog->Record("ERROR:EraseIDB-->EraseEmmcIDB failed 2");
			return false;
		}
	}
	if (!EraseMutilBlock(0, 0, uiCount, false)) {
		if (m_pLog)
			m_pLog->Record("ERROR:EraseIDB-->EraseMutilBlock failed");
		return false;
	}
	return true;
}

// BufferWriteBack: flushes cached IDB write-back data when a valid old IDB
// exists.
bool CRKAndroidDevice::BufferWriteBack()
{
	if (!m_pComm)
		return false;
	bool bRet = ReadCapability();
	if (m_bDirectLbaFlag) {
		if (m_pIDBData) { delete []m_pIDBData; m_pIDBData = NULL; }
		GetOldIDBData();
		if (m_ucIDBCount > 0) {
			if (m_pLog)
				m_pLog->Record("ERROR:BufferWriteBack-->Found IDB");
			return true;
		}
	} else {
		GetOldSectorData();
		if (m_ucIDBCount > 0) {
			if (m_pLog)
				m_pLog->Record("ERROR:BufferWriteBack-->Found IDB");
			return true;
		}
	}
	if (m_uiIDBend == 0) {
		if (m_pLog)
			m_pLog->Record("INFO:BufferWriteBack-->Crc is zero");
		return true;
	}
	return bRet;
}

// ---------------------------------------------------------------------------
// Firmware download loop. Writes the parameter/GPT entry, then every mapped
// partition, taking the layout from the RKAF entry itself (see WriteEntry).
// ---------------------------------------------------------------------------
int CRKAndroidDevice::DownloadImage()
{
	if (!m_pPackageFile || !m_pComm)
		return -1;
	m_uiFwTotal = 0;
	m_uiFwDone = 0;
	for (int i = 0; i < m_nEntries; i++)
		m_uiFwTotal += m_entries[i].uiSize;
	if (m_uiFwTotal == 0)
		m_uiFwTotal = 1;

	// Transfer band (DownloadImage): it starts at 1; config.ini `force_data_band`
	// (capped at 6) overrides it, otherwise the loader is asked via
	// RKU_TestDeviceReady and its value is used as the exponent. RKA_File_Download
	// then issues one WRITE_LBA of `m_uiBand << 14` bytes at a time. Skipping
	// this query would silently fall back to 16 KB and make UF much slower.
	m_uiBand = 1;
	UINT uiForceBand = GetForceDataBand();
	if (HasForceDataBand()) {
		if (uiForceBand > 6)
			uiForceBand = 6;
		m_uiBand = 1u << uiForceBand;
	} else {
		DWORD dwTotal = 0, dwCurrent = 0;
		int iReady = m_pComm->RKU_TestDeviceReady(&dwTotal, &dwCurrent);
		if (iReady != ERR_SUCCESS) {
			if (m_pLog)
				m_pLog->Record("ERROR:DownloadImage-->RKU_TestDeviceReady failed,Err=%d", iReady);
			return -1;
		}
		if (dwCurrent > 16)		// clamp the shift; WriteLBACore caps the block
			dwCurrent = 16;
		if (dwCurrent)
			m_uiBand <<= dwCurrent;
	}
	if (m_pLog)
		m_pLog->Record("INFO:Start download image,total=%lld,band=%d",
		               (long long)m_uiFwTotal, m_uiBand);

	bool bOk = true;

	for (int i = 0; i < m_nEntries; i++) {
		FW_ENTRY &entry = m_entries[i];
		if (strstr(entry.szFile, "parameter") && !strstr(entry.szFile, ".img")) {
			TemporaryParameterFiles temporary;
			const auto rawPath = temporary.filePath("parameter.raw");
			auto textPath = temporary.filePath("parameter.txt");
			if (!ExtractEntry(entry, rawPath.c_str())) {
				if (m_pLog)
					m_pLog->Record("ERROR:DownloadImage-->extract %s failed", entry.szFile);
				bOk = false;
				continue;
			}
			// The package stores the parameter wrapped as PARM+size+text+crc; unwrap it.
			BYTE raw[16];
			FILE *rf = platform::OpenFile(rawPath.c_str(), "rb");
			size_t rn = rf ? fread(raw, 1, 12, rf) : 0;
			if (rf) fclose(rf);
			if (rn >= 8 && *(UINT *)raw == 0x4D524150) {
				UINT uiText = *(UINT *)(raw + 4);
				FILE *in = platform::OpenFile(rawPath.c_str(), "rb");
				FILE *out = platform::OpenFile(textPath.data(), "wb");
				if (in && out) {
					fseeko(in, 8, SEEK_SET);
					BYTE b2[4096];
					UINT remain = uiText;
					while (remain) {
						UINT want = remain > sizeof(b2) ? (UINT)sizeof(b2) : remain;
						UINT got = fread(b2, 1, want, in);
						if (!got) break;
						fwrite(b2, 1, got, out);
						remain -= got;
					}
				}
				if (in) fclose(in);
				if (out) fclose(out);
			} else {
				std::error_code error;
				std::filesystem::rename(platform::Path(rawPath), platform::Path(textPath), error);
				if (error) { bOk = false; continue; }
			}
			bool bParamOk = fw_get_parameter_gptflag(textPath.data())
			                ? WriteGpt(textPath.data())
			                : WriteParameter(textPath.data());
			if (!bParamOk) {
				if (m_pLog)
					m_pLog->Record("ERROR:DownloadImage-->write parameter/GPT failed");
				bOk = false;
			}
			m_uiFwDone += entry.uiSize;
			NotifyProgress(m_uiFwTotal, m_uiFwDone);
			continue;
		}
		if (!strstr(entry.szFile, ".img")) {
			m_uiFwDone += entry.uiSize;
			NotifyProgress(m_uiFwTotal, m_uiFwDone);
			continue;
		}
		// The partition offset/size come from the RKAF entry itself (fields
		// +0x5c/+0x64), and that is the only correct source for a "grow"
		// partition whose parameter name carries a suffix (e.g. "rootfs:grow"
		// vs. the entry's "rootfs").
		if (entry.uiPartLba == (UINT)-1) {
			if (m_pLog)
				m_pLog->Record("WARNING:DownloadImage-->entry %s is not mapped, skipped", entry.szFile);
			m_uiFwDone += entry.uiSize;
			NotifyProgress(m_uiFwTotal, m_uiFwDone);
			continue;
		}
		if (!WriteEntry(entry.uiPartLba, entry)) {
			if (m_pLog)
				m_pLog->Record("ERROR:DownloadImage-->write %s failed", entry.szFile);
			bOk = false;
		}
	}
	return bOk ? 0 : -1;
}
