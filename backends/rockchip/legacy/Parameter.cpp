#include "Parameter.h"
#include "RKDevice.h"
#include "crc.h"
#include "Config.h"
#include <sstream>
#include <ctype.h>
#include <stdio.h>


bool ParsePartitionInfo(string &strPartInfo, string &strName, UINT &uiOffset, UINT &uiLen)
{
	string::size_type pos, prevPos;
	string strOffset, strLen;
	int iCount;
	prevPos = pos = 0;
	if (strPartInfo.size() <= 0)
		return false;
	pos = strPartInfo.find('@');
	if (pos == string::npos)
		return false;
	strLen = strPartInfo.substr(prevPos, pos - prevPos);
	strLen.erase(0, strLen.find_first_not_of(" "));
	strLen.erase(strLen.find_last_not_of(" ") + 1);
	if (strchr(strLen.c_str(), '-'))
		uiLen = 0xFFFFFFFF;
	else {
		iCount = sscanf(strLen.c_str(), "0x%x", &uiLen);
		if (iCount != 1)
			return false;
	}

	prevPos = pos + 1;
	pos = strPartInfo.find('(', prevPos);
	if (pos == string::npos)
		return false;
	strOffset = strPartInfo.substr(prevPos, pos - prevPos);
	strOffset.erase(0, strOffset.find_first_not_of(" "));
	strOffset.erase(strOffset.find_last_not_of(" ") + 1);
	iCount = sscanf(strOffset.c_str(), "0x%x", &uiOffset);
	if (iCount != 1)
		return false;

	prevPos = pos + 1;
	pos = strPartInfo.find(')', prevPos);
	if (pos == string::npos)
		return false;
	strName = strPartInfo.substr(prevPos, pos - prevPos);
	strName.erase(0, strName.find_first_not_of(" "));
	strName.erase(strName.find_last_not_of(" ") + 1);
	return true;
}

bool ParseUuidInfo(string &strUuidInfo, string &strName, string &strUUid)
{
	string::size_type pos(0);
	if (strUuidInfo.size() <= 0)
		return false;
	pos = strUuidInfo.find('=');
	if (pos == string::npos)
		return false;
	strName = strUuidInfo.substr(0, pos);
	strName.erase(0, strName.find_first_not_of(" "));
	strName.erase(strName.find_last_not_of(" ") + 1);
	strUUid = strUuidInfo.substr(pos + 1);
	strUUid.erase(0, strUUid.find_first_not_of(" "));
	strUUid.erase(strUUid.find_last_not_of(" ") + 1);
	while (true) {
		if ((pos = strUUid.find("-")) != string::npos)
			strUUid.replace(pos, 1, "");
		else
			break;
	}
	if (strUUid.size() != 32)
		return false;
	return true;
}

bool parse_parameter(char *pParameter, PARAM_ITEM_VECTOR &vecItem, CONFIG_ITEM_VECTOR &vecUuidItem)
{
	stringstream paramStream(pParameter);
	bool bRet, bFind = false;
	string strLine, strPartition, strPartInfo, strPartName, strUUid;
	string::size_type line_size, pos, posColon, posComma;
	UINT uiPartOffset, uiPartSize;
	STRUCT_PARAM_ITEM item;
	STRUCT_CONFIG_ITEM uuid_item;
	vecItem.clear();
	vecUuidItem.clear();
	while (!paramStream.eof()) {
		getline(paramStream, strLine);
		line_size = strLine.size();
		if (line_size == 0)
			continue;
		if (strLine[line_size - 1] == '\r')
			strLine = strLine.substr(0, line_size - 1);
		strLine.erase(0, strLine.find_first_not_of(" "));
		strLine.erase(strLine.find_last_not_of(" ") + 1);
		if (strLine.size() == 0)
			continue;
		if (strLine[0] == '#')
			continue;
		pos = strLine.find("uuid:");
		if (pos != string::npos) {
			strPartInfo = strLine.substr(pos + 5);
			bRet = ParseUuidInfo(strPartInfo, strPartName, strUUid);
			if (bRet) {
				strcpy(uuid_item.szItemName, strPartName.c_str());
				string_to_uuid(strUUid, uuid_item.szItemValue);
				vecUuidItem.push_back(uuid_item);
			}
			continue;
		}
		pos = strLine.find("mtdparts");
		if (pos == string::npos)
			continue;
		bFind = true;
		posColon = strLine.find(':', pos);
		if (posColon == string::npos)
			continue;
		strPartition = strLine.substr(posColon + 1);
		pos = 0;
		posComma = strPartition.find(',', pos);
		while (posComma != string::npos) {
			strPartInfo = strPartition.substr(pos, posComma - pos);
			bRet = ParsePartitionInfo(strPartInfo, strPartName, uiPartOffset, uiPartSize);
			if (bRet) {
				strcpy(item.szItemName, strPartName.c_str());
				item.uiItemOffset = uiPartOffset;
				item.uiItemSize = uiPartSize;
				vecItem.push_back(item);
			}
			pos = posComma + 1;
			posComma = strPartition.find(',', pos);
		}
		strPartInfo = strPartition.substr(pos);
		if (strPartInfo.size() > 0) {
			bRet = ParsePartitionInfo(strPartInfo, strPartName, uiPartOffset, uiPartSize);
			if (bRet) {
				strcpy(item.szItemName, strPartName.c_str());
				item.uiItemOffset = uiPartOffset;
				item.uiItemSize = uiPartSize;
				vecItem.push_back(item);
			}
		}
	}
	return bFind;
}

bool parse_parameter_file(char *pParamFile, PARAM_ITEM_VECTOR &vecItem, CONFIG_ITEM_VECTOR &vecUuidItem, platform::FileLog *log)
{
	FILE *file = fopen(pParamFile, "rb");
	if (!file) {
		if (log)
			log->Record("%s failed, err=%d, can't open file: %s\r\n", __func__, errno, pParamFile);
		return false;
	}
	fseek(file, 0, SEEK_END);
	int iFileSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	// +1 and a NUL terminator: parse_parameter feeds the buffer to a
	// stringstream, which would otherwise read past the end of the allocation.
	char *pParamBuf = new char[iFileSize + 1];
	if (!pParamBuf) {
		fclose(file);
		return false;
	}
	memset(pParamBuf, 0, iFileSize + 1);
	int iRead = fread(pParamBuf, 1, iFileSize, file);
	if (iRead != iFileSize) {
		if (log)
			log->Record("%s failed, err=%d, read=%d, total=%d\r\n", __func__, errno, iRead, iFileSize);
		fclose(file);
		delete []pParamBuf;
		return false;
	}
	fclose(file);
	bool bRet = parse_parameter(pParamBuf, vecItem, vecUuidItem);
	delete []pParamBuf;
	return bRet;
}

void string_to_uuid(string strUUid, char *uuid)
{
	unsigned int i;
	char value;
	memset(uuid, 0, 16);
	for (i = 0; i < strUUid.size(); i++) {
		value = 0;
		if ((strUUid[i] >= '0') && (strUUid[i] <= '9'))
			value = strUUid[i] - '0';
		if ((strUUid[i] >= 'a') && (strUUid[i] <= 'f'))
			value = strUUid[i] - 'a' + 10;
		if ((strUUid[i] >= 'A') && (strUUid[i] <= 'F'))
			value = strUUid[i] - 'A' + 10;
		if ((i % 2) == 0)
			uuid[i / 2] += (value << 4);
		else
			uuid[i / 2] += value;
	}
	unsigned int *p32;
	unsigned short *p16;
	p32 = (unsigned int *)uuid;
	*p32 = cpu_to_be32(*p32);
	p16 = (unsigned short *)(uuid + 4);
	*p16 = cpu_to_be16(*p16);
	p16 = (unsigned short *)(uuid + 6);
	*p16 = cpu_to_be16(*p16);
}

void gen_rand_uuid(unsigned char *uuid_bin)
{
	efi_guid_t id;
	unsigned int *ptr = (unsigned int *)&id;
	unsigned int i;
	for (i = 0; i < sizeof(id) / sizeof(*ptr); i++)
		*(ptr + i) = cpu_to_be32(rand());
	id.uuid.time_hi_and_version = (id.uuid.time_hi_and_version & 0x0FFF) | 0x4000;
	id.uuid.clock_seq_hi_and_reserved = id.uuid.clock_seq_hi_and_reserved | 0x80;
	memcpy(uuid_bin, id.raw, sizeof(id));
}

bool MakeParamBuffer(char *pParamFile, char *&pParamData, platform::FileLog *log)
{
	FILE *file = fopen(pParamFile, "rb");
	if (!file) {
		if (log)
			log->Record("MakeParamBuffer failed,err=%d,can't open file: %s\r\n", errno, pParamFile);
		return false;
	}
	fseek(file, 0, SEEK_END);
	int iFileSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	char *pParamBuf = new char[iFileSize + 12];
	if (!pParamBuf) {
		fclose(file);
		return false;
	}
	memset(pParamBuf, 0, iFileSize + 12);
	*(UINT *)(pParamBuf) = 0x4D524150;
	int iRead = fread(pParamBuf + 8, 1, iFileSize, file);
	if (iRead != iFileSize) {
		if (log)
			log->Record("MakeParamBuffer failed,err=%d,read=%d,total=%d\r\n", errno, iRead, iFileSize);
		fclose(file);
		delete []pParamBuf;
		return false;
	}
	fclose(file);
	*(UINT *)(pParamBuf + 4) = iFileSize;
	*(UINT *)(pParamBuf + 8 + iFileSize) = CRC_32((PBYTE)pParamBuf + 8, iFileSize);
	pParamData = pParamBuf;
	return true;
}

bool is_sparse_image(char *szImage, platform::FileLog *log)
{
	FILE *file = fopen(szImage, "rb");
	if (!file) {
		if (log)
			log->Record("%s failed, err=%d, can't open file: %s\r\n", __func__, errno, szImage);
		return false;
	}
	sparse_header head;
	u32 uiRead = fread(&head, 1, sizeof(head), file);
	if (uiRead != sizeof(head)) {
		if (log)
			log->Record("%s failed, err=%d, read=%d, total=%d\r\n", __func__, errno, uiRead, (int)sizeof(head));
		fclose(file);
		return false;
	}
	fclose(file);
	return head.magic == SPARSE_HEADER_MAGIC;
}

bool is_ubifs_image(char *szImage, platform::FileLog *log)
{
	FILE *file = fopen(szImage, "rb");
	if (!file) {
		if (log)
			log->Record("%s failed, err=%d, can't open file: %s\r\n", __func__, errno, szImage);
		return false;
	}
	u32 magic;
	u32 uiRead = fread(&magic, 1, sizeof(magic), file);
	if (uiRead != sizeof(magic)) {
		fclose(file);
		return false;
	}
	fclose(file);
	return magic == UBI_HEADER_MAGIC;
}

void prepare_gpt_backup(u8 *master, u8 *backup)
{
	gpt_header *gptMasterHead = (gpt_header *)(master + SECTOR_SIZE);
	gpt_header *gptBackupHead = (gpt_header *)(backup + 32 * SECTOR_SIZE);
	u64 val;
	val = le64_to_cpu(gptMasterHead->my_lba);
	gptBackupHead->my_lba = gptMasterHead->alternate_lba;
	gptBackupHead->alternate_lba = cpu_to_le64(val);
	gptBackupHead->partition_entry_lba = cpu_to_le64(le64_to_cpu(gptMasterHead->last_usable_lba) + 1);
	gptBackupHead->header_crc32 = 0;
	u32 calc_crc32 = crc32_le(0, (unsigned char *)gptBackupHead, le32_to_cpu(gptBackupHead->header_size));
	gptBackupHead->header_crc32 = cpu_to_le32(calc_crc32);
}

bool get_lba_from_gpt(u8 *master, char *pszName, u64 *lba, u64 *lba_end)
{
	gpt_header *gptMasterHead = (gpt_header *)(master + SECTOR_SIZE);
	gpt_entry *gptEntry = NULL;
	u32 i, j;
	u8 zerobuf[GPT_ENTRY_SIZE];
	bool bFound = false;
	memset(zerobuf, 0, GPT_ENTRY_SIZE);
	for (i = 0; i < le32_to_cpu(gptMasterHead->num_partition_entries); i++) {
		gptEntry = (gpt_entry *)(master + 2 * SECTOR_SIZE + i * GPT_ENTRY_SIZE);
		if (memcmp(zerobuf, (u8 *)gptEntry, GPT_ENTRY_SIZE) == 0)
			break;
		for (j = 0; j < strlen(pszName); j++)
			if (gptEntry->partition_name[j] != pszName[j])
				break;
		if (gptEntry->partition_name[j] != 0)
			continue;
		if (j == strlen(pszName)) {
			bFound = true;
			break;
		}
	}
	if (bFound) {
		*lba = le64_to_cpu(gptEntry->starting_lba);
		if (gptMasterHead->last_usable_lba == gptEntry->ending_lba)
			*lba_end = 0xFFFFFFFF;
		else
			*lba_end = le64_to_cpu(gptEntry->ending_lba);
		return true;
	}
	return false;
}

bool get_lba_from_param(u8 *param, char *pszName, u32 *part_offset, u32 *part_size)
{
	u32 i;
	bool bFound = false, bRet;
	PARAM_ITEM_VECTOR vecItem;
	CONFIG_ITEM_VECTOR vecUuid;
	bRet = parse_parameter((char *)param, vecItem, vecUuid);
	if (!bRet)
		return false;
	for (i = 0; i < vecItem.size(); i++) {
		if (strcasecmp(pszName, vecItem[i].szItemName) == 0) {
			bFound = true;
			break;
		}
	}
	if (bFound) {
		*part_offset = vecItem[i].uiItemOffset;
		*part_size = vecItem[i].uiItemSize;
		return true;
	}
	return false;
}

void update_gpt_disksize(u8 *master, u8 *backup, u32 total_sector)
{
	gpt_header *gptMasterHead = (gpt_header *)(master + SECTOR_SIZE);
	gpt_entry *gptLastPartEntry = NULL;
	u32 i;
	u64 old_disksize;
	u8 zerobuf[GPT_ENTRY_SIZE];
	memset(zerobuf, 0, GPT_ENTRY_SIZE);
	old_disksize = le64_to_cpu(gptMasterHead->alternate_lba) + 1;
	for (i = 0; i < le32_to_cpu(gptMasterHead->num_partition_entries); i++) {
		gptLastPartEntry = (gpt_entry *)(master + 2 * SECTOR_SIZE + i * GPT_ENTRY_SIZE);
		if (memcmp(zerobuf, (u8 *)gptLastPartEntry, GPT_ENTRY_SIZE) == 0)
			break;
	}
	i--;
	gptLastPartEntry = (gpt_entry *)(master + 2 * SECTOR_SIZE + i * sizeof(gpt_entry));
	gptMasterHead->alternate_lba = cpu_to_le64(total_sector - 1);
	gptMasterHead->last_usable_lba = cpu_to_le64(total_sector - 34);
	if (gptLastPartEntry->ending_lba == (old_disksize - 34)) {
		gptLastPartEntry->ending_lba = cpu_to_le64(total_sector - 34);
		gptMasterHead->partition_entry_array_crc32 = cpu_to_le32(crc32_le(0, master + 2 * SECTOR_SIZE, GPT_ENTRY_SIZE * GPT_ENTRY_NUMBERS));
	}
	gptMasterHead->header_crc32 = 0;
	gptMasterHead->header_crc32 = cpu_to_le32(crc32_le(0, master + SECTOR_SIZE, sizeof(gpt_header)));
	memcpy(backup, master + 2 * SECTOR_SIZE, GPT_ENTRY_SIZE * GPT_ENTRY_NUMBERS);
	memcpy(backup + GPT_ENTRY_SIZE * GPT_ENTRY_NUMBERS, master + SECTOR_SIZE, SECTOR_SIZE);
	prepare_gpt_backup(master, backup);
}

bool load_gpt_buffer(char *pParamFile, u8 *master, u8 *backup, platform::FileLog *log)
{
	FILE *file = fopen(pParamFile, "rb");
	if (!file) {
		if (log)
			log->Record("%s failed, err=%d, can't open file: %s\r\n", __func__, errno, pParamFile);
		return false;
	}
	fseek(file, 0, SEEK_END);
	int iFileSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (iFileSize != 67 * SECTOR_SIZE) {
		if (log)
			log->Record("%s failed, wrong size file: %s\r\n", __func__, pParamFile);
		fclose(file);
		return false;
	}
	int iRead = fread(master, 1, 34 * SECTOR_SIZE, file);
	if (iRead != 34 * SECTOR_SIZE) {
		fclose(file);
		return false;
	}
	iRead = fread(backup, 1, 33 * SECTOR_SIZE, file);
	if (iRead != 33 * SECTOR_SIZE) {
		fclose(file);
		return false;
	}
	fclose(file);
	return true;
}

static string lower_name(const char *s)
{
	string r(s);
	for (string::size_type i = 0; i < r.size(); i++)
		r[i] = (char)tolower((unsigned char)r[i]);
	return r;
}

// The grow partition's ending_lba is diskSectors-1-reserve. The reserve is 33
// sectors normally, or 64 when grow alignment is requested. Alignment comes
// from a GROW_ALIGN line when present (pGrowAlign != NULL), otherwise from the
// presence of a "cache" partition (the NULL-pointer path).
void create_gpt_buffer(u8 *gpt, PARAM_ITEM_VECTOR &vecParts, CONFIG_ITEM_VECTOR &vecUuid, u64 diskSectors, const bool *pGrowAlign)
{
	legacy_mbr *mbr = (legacy_mbr *)gpt;
	gpt_header *gptHead = (gpt_header *)(gpt + SECTOR_SIZE);
	gpt_entry *gptEntry = (gpt_entry *)(gpt + 2 * SECTOR_SIZE);
	u32 i, j;
	int pos;
	u64 growReserve;
	bool bGrowAlign;
	string strPartName;
	string::size_type colonPos;

	if (pGrowAlign) {
		bGrowAlign = *pGrowAlign;
	} else {
		bGrowAlign = false;
		for (i = 0; i < vecParts.size(); i++) {
			if (lower_name(vecParts[i].szItemName) == "cache") {
				bGrowAlign = true;
				break;
			}
		}
	}
	growReserve = bGrowAlign ? 0x40 : 0x21;

	memset(gpt, 0, SECTOR_SIZE);
	mbr->signature = MSDOS_MBR_SIGNATURE;
	mbr->partition_record[0].sys_ind = EFI_PMBR_OSTYPE_EFI_GPT;
	mbr->partition_record[0].start_sect = 1;
	mbr->partition_record[0].nr_sects = (u32)-1;

	memset(gpt + SECTOR_SIZE, 0, SECTOR_SIZE);
	gptHead->signature = cpu_to_le64(GPT_HEADER_SIGNATURE);
	gptHead->revision = cpu_to_le32(GPT_HEADER_REVISION_V1);
	gptHead->header_size = cpu_to_le32(sizeof(gpt_header));
	gptHead->my_lba = cpu_to_le64(1);
	gptHead->alternate_lba = cpu_to_le64(diskSectors - 1);
	gptHead->first_usable_lba = cpu_to_le64(34);
	gptHead->last_usable_lba = cpu_to_le64(diskSectors - 34);
	gptHead->partition_entry_lba = cpu_to_le64(2);
	gptHead->num_partition_entries = cpu_to_le32(GPT_ENTRY_NUMBERS);
	gptHead->sizeof_partition_entry = cpu_to_le32(GPT_ENTRY_SIZE);
	gptHead->header_crc32 = 0;
	gptHead->partition_entry_array_crc32 = 0;
	gen_rand_uuid(gptHead->disk_guid.raw);

	memset(gpt + 2 * SECTOR_SIZE, 0, 32 * SECTOR_SIZE);
	for (i = 0; i < vecParts.size(); i++) {
		gen_rand_uuid(gptEntry->partition_type_guid.raw);
		gen_rand_uuid(gptEntry->unique_partition_guid.raw);
		gptEntry->starting_lba = cpu_to_le64(vecParts[i].uiItemOffset);
		gptEntry->ending_lba = cpu_to_le64(gptEntry->starting_lba + vecParts[i].uiItemSize - 1);
		gptEntry->attributes.raw = 0;
		string strLower = lower_name(vecParts[i].szItemName);
		if (strLower.find("bootable") != string::npos ||
		    strLower.find("efi") != string::npos)
			gptEntry->attributes.raw = PART_PROPERTY_BOOTABLE;
		if (strLower.find("grow") != string::npos)
			gptEntry->ending_lba = cpu_to_le64(diskSectors - 1 - growReserve);
		strPartName = vecParts[i].szItemName;
		colonPos = strPartName.find_first_of(':');
		if (colonPos != string::npos) {
			strPartName = strPartName.substr(0, colonPos);
			vecParts[i].szItemName[strPartName.size()] = 0;
		}
		for (j = 0; j < strlen(vecParts[i].szItemName); j++)
			gptEntry->partition_name[j] = vecParts[i].szItemName[j];
		if ((pos = find_config_item(vecUuid, vecParts[i].szItemName)) != -1)
			memcpy(gptEntry->unique_partition_guid.raw, vecUuid[pos].szItemValue, 16);
		gptEntry++;
	}

	gptHead->partition_entry_array_crc32 = cpu_to_le32(crc32_le(0, gpt + 2 * SECTOR_SIZE, GPT_ENTRY_SIZE * GPT_ENTRY_NUMBERS));
	gptHead->header_crc32 = cpu_to_le32(crc32_le(0, gpt + SECTOR_SIZE, sizeof(gpt_header)));
}

// Returns 1 when a line (excluding comments) contains both "TYPE" and "GPT"
// (compared after upper-casing).
int get_parameter_gptflag(char *pParameter)
{
	stringstream ss(pParameter);
	string strLine;
	while (!ss.eof()) {
		getline(ss, strLine);
		if (strLine.size() == 0)
			continue;
		if (strLine[strLine.size() - 1] == '\r')
			strLine.erase(strLine.size() - 1);
		if (strLine.size() == 0)
			continue;
		if (strLine[0] == '#')
			continue;
		for (string::size_type i = 0; i < strLine.size(); i++)
			strLine[i] = (char)toupper((unsigned char)strLine[i]);
		if (strLine.find("TYPE") == string::npos)
			continue;
		if (strLine.find("GPT") == string::npos)
			continue;
		return 1;
	}
	return 0;
}

// Disk size in sectors for a GPT image: 0xFFFFFFFF (i.e. -1) unless the
// parameter carries a "DISKSIZE: 0x..." line.
unsigned int get_parameter_gpt_size(char *pParameter)
{
	unsigned int uiSize = 0xFFFFFFFF;
	stringstream ss(pParameter);
	string strLine;
	while (!ss.eof()) {
		getline(ss, strLine);
		if (strLine.size() == 0)
			continue;
		if (strLine[strLine.size() - 1] == '\r')
			strLine.erase(strLine.size() - 1);
		if (strLine.size() == 0)
			continue;
		if (strLine[0] == '#')
			continue;
		for (string::size_type i = 0; i < strLine.size(); i++)
			strLine[i] = (char)toupper((unsigned char)strLine[i]);
		string::size_type pos = strLine.find("DISKSIZE");
		if (pos == string::npos)
			continue;
		pos = strLine.find(':');
		if (pos == string::npos)
			continue;
		string strVal = strLine.substr(pos + 1);
		strVal.erase(0, strVal.find_first_not_of(" "));
		if (strVal.size() == 0)
			continue;
		strVal.erase(strVal.find_last_not_of(" ") + 1);
		unsigned int uiVal;
		if (sscanf(strVal.c_str(), "0X%x", &uiVal) == 1)
			uiSize = uiVal;
	}
	return uiSize;
}

// Reports whether a "GROW_ALIGN:" line exists; when it does, bGrowAlign is set
// from its value (true when it contains '1'). The return value drives the
// NULL-vs-&flag choice in create_gpt_by_parameter.
bool get_grow_align_flag(char *pParameter, bool &bGrowAlign)
{
	bool bFound = false;
	bGrowAlign = false;
	stringstream ss(pParameter);
	string strLine;
	while (!ss.eof()) {
		getline(ss, strLine);
		if (strLine.size() == 0)
			continue;
		if (strLine[strLine.size() - 1] == '\r')
			strLine.erase(strLine.size() - 1);
		if (strLine.size() == 0)
			continue;
		if (strLine[0] == '#')
			continue;
		for (string::size_type i = 0; i < strLine.size(); i++)
			strLine[i] = (char)toupper((unsigned char)strLine[i]);
		string::size_type pos = strLine.find("GROW_ALIGN:");
		if (pos == string::npos)
			continue;
		bFound = true;
		string strVal = strLine.substr(pos + 11);
		strVal.erase(0, strVal.find_first_not_of(" "));
		if (strVal.size() > 0)
			strVal.erase(strVal.find_last_not_of(" ") + 1);
		bGrowAlign = (strVal.find("1") != string::npos);
	}
	return bFound;
}
