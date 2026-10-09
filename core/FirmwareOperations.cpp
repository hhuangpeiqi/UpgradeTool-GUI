#include "core/UpgradeEngine.h"
#include "core/FirmwareService.h"
#include "RKImage.h"
#include "AndroidDevice.h"
#include "RKBoot.h"
#include "Parameter.h"
#include "Config.h"
#include "crc.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <sys/stat.h>
namespace core {
bool UpgradeEngine::ReadHead(const char *szFile, BYTE *pBuf, size_t uiWant)
{
	FILE *file = fopen(szFile, "rb");
	if (!file)
		return false;
	size_t uiRead = fread(pBuf, 1, uiWant, file);
	fclose(file);
	return uiRead == uiWant;
}

bool UpgradeEngine::IsStorageFirmware(char *pFirmware)
{
	BYTE buf[0x40];
	if (!ReadHead(pFirmware, buf, sizeof(buf)))
		return false;
	return *(UINT *)buf == 0x57465353;
}

bool UpgradeEngine::IsUpdateFirmware(char *pFirmware)
{
	BYTE buf[0x66];
	if (!ReadHead(pFirmware, buf, sizeof(buf)))
		return false;
	return *(UINT *)buf == 0x57464B52;
}

bool UpgradeEngine::IsUpdateLoader(char *pFirmware)
{
	BYTE buf[0x66];
	if (!ReadHead(pFirmware, buf, sizeof(buf)))
		return false;
	UINT uiTag = *(UINT *)buf;
	return (uiTag == 0x544F4F42) || (uiTag == 0x2052444C);
}

bool UpgradeEngine::ShowFirmwareInfo(char *path, int entryIndex)
{
	if (!FirmwareService::Inspect(path, m_lastResult.firmware, entryIndex)) return Fail(ErrorCode::InvalidFirmware);
	m_lastResult.kind = ResultKind::FirmwareInfo;
	return true;
}

bool UpgradeEngine::ExtractEntryData(FILE *pFw, u64 uiDataOff, u64 uiSize,
                                     const char *szOutDir, const char *szName)
{
	char szPath[512];
	snprintf(szPath, sizeof(szPath), "%s%c%s", szOutDir, '/', szName);
	FILE *file = fopen(szPath, "wb");
	if (!file) {
		Emit(LogLevel::Info, "Failed to create %s\r\n", szName);
		return false;
	}
	if (uiSize == 0) {
		fclose(file);
		return true;
	}
	PBYTE pBuf = new BYTE[0x40000];
	u64 uiDone = 0;
	int iLastPct = 0;
	do {
		u64 uiLeft = uiSize - uiDone;
		DWORD dwChunk = (uiLeft > 0x40000) ? 0x40000 : (DWORD)uiLeft;
		fseeko(pFw, uiDataOff + uiDone, SEEK_SET);
		if (fread(pBuf, 1, dwChunk, pFw) != dwChunk) {
			Emit(LogLevel::Info, "Failed to read,err=%d\r\n", errno);
			delete []pBuf;
			fclose(file);
			return false;
		}
		if (fwrite(pBuf, 1, dwChunk, file) != dwChunk) {
			delete []pBuf;
			fclose(file);
			return false;
		}
		if (IsAborted()) { delete []pBuf; fclose(file); return false; }
		uiDone += dwChunk;
		int iPct = uiSize ? (int)(uiDone * 100 / uiSize) : 100;
		if (iPct != iLastPct) {
			EmitTransferProgress(TransferStage::Extract, uiSize, uiDone);
			iLastPct = iPct;
		}
	} while (uiDone < uiSize);
	delete []pBuf;
	fclose(file);
	return true;
}

bool UpgradeEngine::ExtractUpdateFw(CRKImage *image, char *path, char *output)
{
	FirmwareInfo info;
	if (!FirmwareService::InspectImage(*image, path, info)) return Fail(ErrorCode::InvalidFirmware);
	if (IsAborted()) return false;
	std::string boot = std::string(output) + "/bootloader.bin";
	if (!image->SaveBootFile(boot)) return Fail(ErrorCode::FileAccess);
	FILE *file = fopen(path, "rb");
	if (!file) return Fail(ErrorCode::FileAccess);
	bool ok = true;
	for (const auto &entry : info.entries) {
		if (IsAborted() || !ExtractEntryData(file, entry.offset, entry.size, output, entry.fileName.c_str())) { ok = false; break; }
	}
	fclose(file);
	return ok;
}

bool UpgradeEngine::ExtractStorageFw(char *szFirmware, char *szOutDir)
{
	CRKImage *pStorage = new CRKImage();
	if (!pStorage->OpenStorageFirmware(szFirmware)) {
		Fail(ErrorCode::InvalidFirmware);
	Emit(LogLevel::Info, "File is not valid format!\r\n");
		delete pStorage;
		return false;
	}
	FILE *file = fopen(szFirmware, "rb");
	if (!file) {
		Emit(LogLevel::Info, "Failed to Open %s,err=%d\r\n", szFirmware, errno);
		delete pStorage;
		return false;
	}
	bool bOk = true;
	UINT uiCount = pStorage->GetStorageEntryCount();
	for (UINT i = 0; i < uiCount; i++) {
		STRUCT_STORAGE_ENTRY ent;
		pStorage->GetStorageEntry(i, ent);
		if (ent.uiOffset == 0xFFFFFFFFULL)
			continue;
		if (strcmp(ent.szName, "link") == 0)
			continue;
		Emit(LogLevel::Info, "start to extract %s...\r\n", ent.szName);
		if (!ExtractEntryData(file, ent.uiOffset, ent.uiSize, szOutDir, ent.szName)) {
			bOk = false;
			break;
		}
	}
	fclose(file);
	delete pStorage;
	return bOk;
}

bool UpgradeEngine::ExtractBootTable(CRKBoot *pBoot, ENUM_RKBOOTENTRY type,
                                     const char *szLabel, const char *szOutDir, bool bPerSector)
{
	DWORD dwSize, dwDelay;
	char szName[20];
	UCHAR ucCount;
	switch (type) {
	case ENTRY471:     ucCount = pBoot->Entry471Count; break;
	case ENTRY472:     ucCount = pBoot->Entry472Count; break;
	default:           ucCount = pBoot->EntryLoaderCount; break;
	}
	Emit(LogLevel::Info, "%s Count:%d\r\n", szLabel, ucCount);
	for (UCHAR i = 0; i < ucCount; i++) {
		if (IsAborted()) return false;
		dwSize = dwDelay = 0;
		pBoot->GetEntryName(type, i, szName);
		pBoot->GetEntryProperty(type, i, dwSize, dwDelay);
		char szPath[512];
		snprintf(szPath, sizeof(szPath), "%s%c%s.bin", szOutDir, '/', szName);
		Emit(LogLevel::Info, "start to extract %s...", szName);
		PBYTE pEntry = new BYTE[dwSize];
		if (pBoot->GetEntryData(type, i, pEntry)) {
			if (bPerSector) {
				DWORD off = 0;
				for (; off + 0x200 <= dwSize; off += 0x200)
					P_RC4(pEntry + off, 0x200);
				if (off < dwSize)
					P_RC4(pEntry + off, dwSize - off);
			} else if (!pBoot->Rc4DisableFlag) {
				P_RC4(pEntry, dwSize);
			}
			FILE *pOut = fopen(szPath, "wb");
			if (!pOut) { delete []pEntry; return Fail(ErrorCode::FileAccess); }
			bool written = fwrite(pEntry, 1, dwSize, pOut) == dwSize;
			if (fclose(pOut) != 0) written = false;
			if (!written) { delete []pEntry; return Fail(ErrorCode::FileAccess); }
		} else { delete []pEntry; return Fail(ErrorCode::InvalidFirmware); }
		delete []pEntry;
		Emit(LogLevel::Info, "ok\r");
	}
	return true;
}

bool UpgradeEngine::ExtractFirmware(char *szFirmware, char *szOutDir)
{
	if (IsStorageFirmware(szFirmware)) {
		Emit(LogLevel::Info, "File:%s\r\n", szFirmware);
		Emit(LogLevel::Info, "Type:Multi-Storage Firmware\r");
		Emit(LogLevel::Info, "Extract Dir:%s\r\n", szOutDir);
		if (!ExtractStorageFw(szFirmware, szOutDir))
			return false;
		Emit(LogLevel::Info, "Extract ok.\r");
		return true;
	}

	if (IsUpdateFirmware(szFirmware)) {
		Emit(LogLevel::Info, "File:%s\r\n", szFirmware);
		Emit(LogLevel::Info, "Type:Update Firmware\r");
		CRKImage *pImage = new CRKImage();
		int iRet = pImage->ObjectInit(szFirmware);
		if (iRet != 0) {
			Fail(ErrorCode::InvalidFirmware);
			Emit(LogLevel::Info, "Failed to Create update object\r");
			delete pImage;
			return false;
		}
		bool bOk = ExtractUpdateFw(pImage, szFirmware, szOutDir);
		delete pImage;
		if (!bOk)
			return false;
		Emit(LogLevel::Info, "Extract ok.\r");
		return true;
	}

	if (IsUpdateLoader(szFirmware)) {
		Emit(LogLevel::Info, "File:%s\r\n", szFirmware);
		Emit(LogLevel::Info, "Type:Update Loader\r");
		FILE *file = fopen(szFirmware, "rb");
		if (!file) {
			Emit(LogLevel::Info, "Failed to Open %s,err=%d\r\n", szFirmware, errno);
			return false;
		}
		fseeko(file, 0, SEEK_END);
		long lSize = ftello(file);
		fseeko(file, 0, SEEK_SET);
		PBYTE pBuf = new BYTE[lSize];
		if (fread(pBuf, 1, lSize, file) != (size_t)lSize) {
			fclose(file);
			delete []pBuf;
			return false;
		}
		fclose(file);
		CRKBoot *pBoot = new CRKBoot(pBuf, (DWORD)lSize);
		int iRet = pBoot->ObjectInit();
		if (iRet != 0) {
			Fail(ErrorCode::InvalidFirmware);
			Emit(LogLevel::Info, "Failed to init Boot Object,ret=%d\r\n", iRet);
			delete pBoot;
			return false;
		}
		bool ok = ExtractBootTable(pBoot, ENTRY471, "471", szOutDir, false) &&
		          ExtractBootTable(pBoot, ENTRY472, "472", szOutDir, false) &&
		          ExtractBootTable(pBoot, ENTRYLOADER, "loader", szOutDir, true);
		delete pBoot;
		if (!ok) return false;
		Emit(LogLevel::Info, "Extract ok.\r");
		return true;
	}

	Fail(ErrorCode::InvalidFirmware);
	Emit(LogLevel::Info, "File is not valid format!\r\n");
	return false;
}


bool UpgradeEngine::CreateGptByParameter(char *pParameter, char *pOutput)
{
	u8 master[34 * SECTOR_SIZE];
	PARAM_ITEM_VECTOR vecItems;
	CONFIG_ITEM_VECTOR vecUuid;
	bool bGrowAlign = false;
	char szMsg[512];

	Emit(LogLevel::Info, "in=%s,out=%s\r\n", pParameter, pOutput);
	char *pBuffer = NULL;
	FILE *file = fopen(pParameter, "rb");
	if (file) {
		fseek(file, 0, SEEK_END);
		long lSize = ftell(file);
		fseek(file, 0, SEEK_SET);
		if (lSize > 0) {
			pBuffer = new char[lSize + 1];
			size_t uiRead = fread(pBuffer, 1, lSize, file);
			pBuffer[uiRead] = 0;
			if (uiRead == 0) {
				delete []pBuffer;
				pBuffer = NULL;
			}
		}
		fclose(file);
	}
	if (!pBuffer) {
		if (m_pLog) m_pLog->Record("%s failed,can't open or read %s\r\n", "create_gpt_by_parameter", pParameter);
		snprintf(szMsg, sizeof(szMsg), "Fail to open or read %s!", pParameter);
		Emit(LogLevel::Error, szMsg);
		return false;
	}
	if (!get_parameter_gptflag(pBuffer)) {
		if (m_pLog) m_pLog->Record("%s failed,can't get gpt flag\r\n", "create_gpt_by_parameter");
		Emit(LogLevel::Error, "No Found Gpt Line in the parameter!");
		delete []pBuffer;
		return false;
	}
	unsigned int uiDiskSize = get_parameter_gpt_size(pBuffer);
	if (!parse_parameter(pBuffer, vecItems, vecUuid)) {
		if (m_pLog) m_pLog->Record("%s failed,can't find mtdparts line\r\n", "create_gpt_by_parameter");
		Emit(LogLevel::Error, "Fail to find mtdparts in the parameter!");
		delete []pBuffer;
		return false;
	}
	if (get_grow_align_flag(pBuffer, bGrowAlign))
		create_gpt_buffer(master, vecItems, vecUuid, uiDiskSize, &bGrowAlign);
	else
		create_gpt_buffer(master, vecItems, vecUuid, uiDiskSize, NULL);
	delete []pBuffer;

	file = fopen(pOutput, "wb");
	if (!file) {
		if (m_pLog) m_pLog->Record("%s failed,can't create %s,err=%d\r\n", "create_gpt_by_parameter", pOutput, errno);
		snprintf(szMsg, sizeof(szMsg), "Create gpt image failed,err=%d!", errno);
		Emit(LogLevel::Error, szMsg);
		return false;
	}
	bool written = fwrite(master, 1, 0x4400, file) == 0x4400;
	if (fclose(file) != 0) written = false;
	if (!written) return Fail(ErrorCode::FileAccess);
	Emit(LogLevel::Info, "Create Gpt Image OK.\r\n");
	return true;
}


}
