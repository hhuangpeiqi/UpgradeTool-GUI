#include "platform/File.h"
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
static UINT DiDownloadChunkSectors(CRKComm *pComm, const ConfigSnapshot &config)
{
	u64 ullChunk;
	UINT uiForceBand = 0;
	if ((config.Number("force_data_band") ? (uiForceBand=*config.Number("force_data_band"), true) : false) && uiForceBand > 0) {
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
		file = platform::OpenFile(szFile, "wb+");
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
		uiChunkSectors = DiDownloadChunkSectors(pComm,m_config);
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
	file = platform::OpenFile(szFile, "rb");
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
		uiChunkSectors = DiDownloadChunkSectors(pComm,m_config);
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
	file = platform::OpenFile(szFile, "rb");
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

bool UpgradeEngine::WriteSector(STRUCT_RKDEVICE_DESC &dev, UINT uiBegin, UINT uiPageSize,
                                UINT uiSpareSize, char *szFile)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false;
	(void)uiPageSize; (void)uiSpareSize;
	FILE *file = platform::OpenFile(szFile, "rb");
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
}
