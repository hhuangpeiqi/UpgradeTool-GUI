/*
 * (C) Copyright 2017 Fuzhou Rockchip Electronics Co., Ltd
 * Seth Liu 2017.03.01
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include "RKImage.h"
#include "MD5Checksum.h"

UINT CRKImage::GetVersion()
{
	return m_version;
}
UINT CRKImage::GetMergeVersion()
{
	return m_mergeVersion;
}
STRUCT_RKTIME CRKImage::GetReleaseTime()
{
	return m_releaseTime;
}
ENUM_RKDEVICE_TYPE CRKImage::GetSupportDevice()
{
	return m_supportDevice;
}
ENUM_OS_TYPE CRKImage::GetOsType()
{
	UINT *pOsType;
	pOsType = (UINT *)&m_reserved[4];
	return (ENUM_OS_TYPE)*pOsType;
}

USHORT CRKImage::GetBackupSize()
{
	USHORT *pBackupSize;
	pBackupSize = (USHORT *)&m_reserved[12];
	return *pBackupSize;
}
UINT CRKImage::GetBootOffset()
{
	return m_bootOffset;
}
UINT CRKImage::GetBootSize()
{
	return m_bootSize;
}
UINT CRKImage::GetFWOffset()
{
	return m_fwOffset;
}
long long CRKImage::GetFWSize()
{
	return m_fwSize;
}
bool CRKImage::SaveBootFile(string filename)
{
	FILE *file = NULL;
	int iRead;
	file = fopen(filename.c_str(), "wb+");
	if (!file) {
		return false;
	}
	BYTE buffer[1024];
	DWORD dwBufferSize = 1024;
	DWORD dwBootSize = m_bootSize;
	DWORD dwReadSize;
	fseek(m_pFile, m_bootOffset, SEEK_SET);
	do {
		dwReadSize = (dwBootSize >= 1024) ? dwBufferSize : dwBootSize;
		iRead = fread(buffer, 1, dwReadSize, m_pFile);
		if (iRead != (int)dwReadSize) {
			fclose(file);
			return false;
		}
		fwrite(buffer, 1, dwReadSize, file);
		dwBootSize -= dwReadSize;
	} while(dwBootSize > 0);
	fclose(file);
	return true;
}
bool CRKImage::SaveFWFile(string filename)
{
	FILE *file = NULL;
	int iRead;
	file = fopen(filename.c_str(), "wb+");
	if (!file) {
		return false;
	}
	BYTE buffer[1024];
	DWORD dwBufferSize = 1024;
	long long dwFWSize = m_fwSize;
	DWORD dwReadSize;
	fseeko(m_pFile, m_fwOffset, SEEK_SET);
	do {
		dwReadSize = (dwFWSize >= 1024) ? dwBufferSize : dwFWSize;
		iRead = fread(buffer, 1, dwReadSize, m_pFile);
		if (iRead != (int)dwReadSize) {
			fclose(file);
			return false;
		}
		fwrite(buffer, 1, dwReadSize, file);
		dwFWSize -= dwReadSize;
	} while (dwFWSize > 0);
	fclose(file);
	return true;
}
bool CRKImage::GetData(long long dwOffset, DWORD dwSize, PBYTE lpBuffer)
{
	if ( (dwOffset < 0) || (dwSize == 0) ) {
		return false;
	}
	if ( dwOffset+dwSize > m_fileSize) {
		return false;
	}
	fseeko(m_pFile, dwOffset, SEEK_SET);
	UINT uiActualRead;
	uiActualRead = fread(lpBuffer,1, dwSize, m_pFile);
	if (dwSize != uiActualRead){
		return false;
	}
	return true;
}
void CRKImage::GetReservedData(PBYTE &lpData, USHORT &usSize)
{
	lpData = m_reserved;
	usSize = IMAGE_RESERVED_SIZE;
}

CRKImage::CRKImage()
{
	Version.setContainer(this);
	Version.getter(&CRKImage::GetVersion);
	MergeVersion.setContainer(this);
	MergeVersion.getter(&CRKImage::GetMergeVersion);
	ReleaseTime.setContainer(this);
	ReleaseTime.getter(&CRKImage::GetReleaseTime);
	SupportDevice.setContainer(this);
	SupportDevice.getter(&CRKImage::GetSupportDevice);
	OsType.setContainer(this);
	OsType.getter(&CRKImage::GetOsType);
	BackupSize.setContainer(this);
	BackupSize.getter(&CRKImage::GetBackupSize);
	BootOffset.setContainer(this);
	BootOffset.getter(&CRKImage::GetBootOffset);
	BootSize.setContainer(this);
	BootSize.getter(&CRKImage::GetBootSize);
	FWOffset.setContainer(this);
	FWOffset.getter(&CRKImage::GetFWOffset);
	FWSize.setContainer(this);
	FWSize.getter(&CRKImage::GetFWSize);
	SignFlag.setContainer(this);
	SignFlag.getter(&CRKImage::GetSignFlag);
	m_bootObject = NULL;
	m_pFile = NULL;
	m_bSignFlag = false;
	m_iError = 0;
	m_bSingleBootFlag = false;
	m_uiStorage = 0;
	m_startOffset = 0;
	memset(&m_storageHead, 0, sizeof(m_storageHead));

	m_signMd5Size = 0;
	memset(m_md5, 0, 32);
	memset(m_signMd5, 0, 256);
}

// Loads a firmware / loader image. Returns 0 on success, or a negative error
// code (the value recorded as "err=%d"); the detailed reason is kept in
// m_iError and turned into a "Note:" line by GetErrString().
//
// `ullStartOffset`/`ullTotalSize` bound an embedded image inside a container
// file (the sub-images of a multi-storage firmware); the default `-1` reads the
// whole file. All header offsets are relative to the image start.
int CRKImage::ObjectInit(string filename, unsigned long long ullStartOffset,
                         unsigned long long ullTotalSize)
{
	m_iError = 0;
	bool bHasWindow = (ullStartOffset != (unsigned long long)-1);
	m_startOffset = bHasWindow ? (long long)ullStartOffset : 0;

	struct stat statBuf;
	char szName[256];
	strcpy(szName, filename.c_str());
	if(stat(szName, &statBuf) < 0) {
		return -1;
	}
	if (S_ISDIR(statBuf.st_mode)) {
		return -1;
	}
	m_fileSize = statBuf.st_size;

	bool bOnlyBootFile=false;
	transform(filename.begin(), filename.end(), filename.begin(), (int(*)(int))tolower);
	if (filename.find(".bin") != string::npos) {
		bOnlyBootFile = true;
	}

	m_pFile = fopen(szName, "rb");
	if (!m_pFile) {
		m_iError = 201;
		return -3;
	}

	long long llImageSize = (ullTotalSize != (unsigned long long)-1)
	                        ? (long long)ullTotalSize
	                        : (m_fileSize - m_startOffset);
	if (llImageSize <= 0)
		llImageSize = m_fileSize;
	long long llImageEnd = m_startOffset + llImageSize;

	int nMd5DataSize, iRead;
	long long ulFwSize;
	STRUCT_RKIMAGE_HEAD imageHead;
	if (!bOnlyBootFile) {
		fseeko(m_pFile, m_startOffset, SEEK_SET);
		iRead = fread((PBYTE)(&imageHead), 1, sizeof(STRUCT_RKIMAGE_HEAD), m_pFile);
		if (iRead != sizeof(STRUCT_RKIMAGE_HEAD)) {
			return -1;
		}
		if ( imageHead.uiTag != 0x57464B52 ) {
			m_iError = 203;
			return -4;
		}
		m_bootOffset = m_startOffset + imageHead.dwBootOffset;
		m_fwOffset = m_startOffset + imageHead.dwFWOffset;
		m_bootSize = imageHead.dwBootSize;
		if ((imageHead.reserved[14] == 'H') && (imageHead.reserved[15] == 'I')) {
			ulFwSize = *((DWORD *)(&imageHead.reserved[16]));
			ulFwSize <<= 32;
			ulFwSize += m_fwOffset;
			ulFwSize += imageHead.dwFWSize;
		} else
			ulFwSize = (long long)m_fwOffset + imageHead.dwFWSize;
		nMd5DataSize = (int)(llImageEnd - ulFwSize);
		if (nMd5DataSize >= 160) {
			m_bSignFlag = true;
			m_signMd5Size = nMd5DataSize - 32;
			fseeko(m_pFile, ulFwSize, SEEK_SET);
			iRead = fread(m_md5, 1, 32, m_pFile);
			if (iRead != 32) {
				return -1;
			}
			iRead = fread(m_signMd5, 1, nMd5DataSize - 32, m_pFile);
			if (iRead != (nMd5DataSize - 32)) {
				return -1;
			}
		} else {
			fseeko(m_pFile, llImageEnd - 32, SEEK_SET);
			iRead = fread(m_md5, 1, 32, m_pFile);
			if (iRead != 32) {
				return -1;
			}
		}

		m_version = imageHead.dwVersion;
		m_mergeVersion = imageHead.dwMergeVersion;
		m_releaseTime.usYear = imageHead.stReleaseTime.usYear;
		m_releaseTime.ucMonth = imageHead.stReleaseTime.ucMonth;
		m_releaseTime.ucDay = imageHead.stReleaseTime.ucDay;
		m_releaseTime.ucHour = imageHead.stReleaseTime.ucHour;
		m_releaseTime.ucMinute = imageHead.stReleaseTime.ucMinute;
		m_releaseTime.ucSecond = imageHead.stReleaseTime.ucSecond;
		m_supportDevice = imageHead.emSupportChip;
		m_fwSize = ulFwSize - m_fwOffset;
		memcpy(m_reserved, imageHead.reserved, IMAGE_RESERVED_SIZE);
	} else {
		m_bootOffset = m_startOffset;
		m_bootSize = llImageSize;
	}

	PBYTE lpBoot;
	lpBoot = new BYTE[m_bootSize];
	fseeko(m_pFile, m_bootOffset, SEEK_SET);
	iRead = fread(lpBoot, 1, m_bootSize, m_pFile);
	if (iRead != (int)m_bootSize) {
		delete []lpBoot;
		return -6;
	}
	m_bootObject = new CRKBoot(lpBoot, m_bootSize);
	int iBootRet = m_bootObject->ObjectInit();
	if (iBootRet != 0) {
		switch (iBootRet) {
		case -2: m_iError = 208; break;
		case -3: m_iError = 209; break;
		case -4: m_iError = 210; break;
		default: break;
		}
		return -6;
	}
	if (bOnlyBootFile) {
		m_supportDevice = m_bootObject->SupportDevice;
		UINT *pOsType;
		pOsType = (UINT *)&m_reserved[4];
		*pOsType = (UINT)RK_OS;
		fclose(m_pFile);
		m_pFile = NULL;
	}
	return 0;
}

// Reads the SSFW header and every storage entry. Returns true on success.
bool CRKImage::OpenStorageFirmware(string filename)
{
	m_iError = 0;
	m_storageEntries.clear();
	FILE *file = fopen(filename.c_str(), "rb");
	if (!file) {
		m_iError = 201;
		return false;
	}
	BYTE head[0x40];
	if (fread(head, 1, sizeof(head), file) != sizeof(head)) {
		fclose(file);
		return false;
	}
	memcpy(&m_storageHead, head, sizeof(STRUCT_STORAGE_HEAD));
	if (m_storageHead.uiTag != 0x57465353 /* SSFW */) {
		fclose(file);
		return false;
	}
	UINT uiRecSize = m_storageHead.usEntrySize;
	UINT uiCount = m_storageHead.uiEntryCount;
	if (uiRecSize < 0x14 || uiRecSize > sizeof(BYTE[0x1000])) {
		fclose(file);
		return false;
	}
	for (UINT i = 0; i < uiCount; i++) {
		BYTE rec[0x1000];
		if (fread(rec, 1, uiRecSize, file) != uiRecSize)
			break;
		STRUCT_STORAGE_ENTRY entry;
		memset(&entry, 0, sizeof(entry));
		entry.uiOffset = *(unsigned long long *)(rec + 0x00);
		entry.uiSize = *(unsigned long long *)(rec + 0x08);
		entry.uiStorage = *(UINT *)(rec + 0x10);
		if (uiRecSize >= 0x20 + sizeof(entry.szName))
			memcpy(entry.szName, rec + 0x20, sizeof(entry.szName));
		entry.szName[sizeof(entry.szName) - 1] = 0;
		m_storageEntries.push_back(entry);
	}
	fclose(file);
	return true;
}

UINT CRKImage::GetStorageEntryCount()
{
	return (UINT)m_storageEntries.size();
}

bool CRKImage::GetStorageEntry(UINT uiIndex, STRUCT_STORAGE_ENTRY &entry)
{
	if (uiIndex >= m_storageEntries.size())
		return false;
	entry = m_storageEntries[uiIndex];
	return true;
}
CRKImage::~CRKImage()
{
	if (m_pFile) {
		fclose(m_pFile);
		m_pFile = NULL;
	}
	if (m_bootObject) {
		delete m_bootObject;
		m_bootObject = NULL;
	}
}

long long CRKImage::GetImageSize()
{
	return m_fileSize;
}
int CRKImage::GetMd5Data(PBYTE &lpMd5, PBYTE &lpSignMd5)
{
	lpMd5 = m_md5;
	lpSignMd5 = m_signMd5;
	return m_signMd5Size;
}
bool CRKImage::GetSignFlag()
{
	return m_bSignFlag;
}

int CRKImage::GetError()
{
	return m_iError;
}

bool CRKImage::GetSingleBootFlag()
{
	return m_bSingleBootFlag;
}

UINT CRKImage::GetStorage()
{
	return m_uiStorage;
}

void CRKImage::SetStorage(UINT value)
{
	m_uiStorage = value;
}

int CRKImage::Md5Check(long long dwOffset)
{
	if (!m_pFile)
		return -1;
	if (dwOffset <= 0)
		dwOffset = m_fileSize;
	if (fseeko(m_pFile, 0, SEEK_SET) != 0)
		return -1;
	CMD5Checksum checksum;
	checksum.GetMD5(m_pFile, 0, dwOffset);
	char *pDigest = checksum.FinalChar();
	return (memcmp(pDigest, m_md5, 16) == 0) ? 0 : -1;
}
