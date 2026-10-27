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
			Emit(LogLevel::Error, "Write parameter quit, creating comm object failed!\r\n");
			return false;
		}
	}
	if (!MakeParamBuffer(szParameter, pParamBuf)) {
		Emit(LogLevel::Error, "Generating parameter failed!\r\n");
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

	Emit(bSuccess ? LogLevel::Info : LogLevel::Error, bSuccess ? "Writing parameter succeeded.\r\n" : "Writing parameter failed!\r\n");
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
			Emit(LogLevel::Error, "Write gpt quit, creating comm object failed!\r\n");
			return false;
		}
	}
	if (pComm->RKU_ReadFlashInfo(flash_info) != ERR_SUCCESS) {
		Emit(LogLevel::Error, "Reading Flash Info failed!\r\n");
		if (bOwn) delete pComm;
		return false;
	}
	total_size_sector = *(u32 *)flash_info;
	if (strstr(szParameter, ".img")) {
		if (!load_gpt_buffer(szParameter, master_gpt, backup_gpt)) {
			Emit(LogLevel::Error, "Loading partition image failed!\r\n");
			if (bOwn) delete pComm;
			return false;
		}
		update_gpt_disksize(master_gpt, backup_gpt, total_size_sector);
	} else {
		PARAM_ITEM_VECTOR vecItems;
		CONFIG_ITEM_VECTOR vecUuid;
		if (!parse_parameter_file(szParameter, vecItems, vecUuid)) {
			Emit(LogLevel::Error, "Parsing parameter failed!\r\n");
			if (bOwn) delete pComm;
			return false;
		}
		create_gpt_buffer(master_gpt, vecItems, vecUuid, total_size_sector, NULL);
		memcpy(backup_gpt, master_gpt + 2 * SECTOR_SIZE, 32 * SECTOR_SIZE);
		memcpy(backup_gpt + 32 * SECTOR_SIZE, master_gpt + SECTOR_SIZE, SECTOR_SIZE);
		prepare_gpt_backup(master_gpt, backup_gpt);
	}
	if (pComm->RKU_WriteLBA(0, 34, master_gpt) != ERR_SUCCESS) {
		Emit(LogLevel::Error, "Writing master gpt failed!\r\n");
		if (bOwn) delete pComm;
		return false;
	}
	if (pComm->RKU_WriteLBA(total_size_sector - 33, 33, backup_gpt) != ERR_SUCCESS) {
		Emit(LogLevel::Error, "Writing backup gpt failed!\r\n");
		if (bOwn) delete pComm;
		return false;
	}
	bSuccess = true;
	if (bOwn) delete pComm;
	return bSuccess;
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
}
