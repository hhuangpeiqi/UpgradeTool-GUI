/*
 * (C) Copyright 2017 Fuzhou Rockchip Electronics Co., Ltd
 * Seth Liu 2017.03.01
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include "platform/File.h"

#include "RKBoot.h"
extern UINT CRC_32(PBYTE pData, UINT ulSize);
bool CRKBoot::GetRc4DisableFlag()
{
	return m_bRc4Disable;
}
bool CRKBoot::GetSignFlag()
{
	return m_bSignFlag;
}
UINT CRKBoot::GetVersion()
{
	return m_version;
}
UINT CRKBoot::GetMergeVersion()
{
	return m_mergeVersion;
}
STRUCT_RKTIME CRKBoot::GetReleaseTime()
{
	return m_releaseTime;
}
ENUM_RKDEVICE_TYPE CRKBoot::GetSupportDevice()
{
	return m_supportDevice;
}
UCHAR CRKBoot::GetEntry471Count()
{
	return m_471Count;
}
UCHAR CRKBoot::GetEntry472Count()
{
	return m_472Count;
}
UCHAR CRKBoot::GetEntryLoaderCount()
{
	return m_loaderCount;
}
bool CRKBoot::CrcCheck()
{
	UINT*pOldCrc,ulNewCrc;
	pOldCrc = (UINT*)(m_BootData+(m_BootSize-4));
	ulNewCrc = CRC_32(m_BootData,m_BootSize-4);
	return (*pOldCrc==ulNewCrc)?true:false;
}
bool CRKBoot::SaveEntryFile(ENUM_RKBOOTENTRY type,UCHAR ucIndex,string fileName)
{
	DWORD dwOffset;
	UCHAR ucCount,ucSize;
	switch ( type )
	{
	case ENTRY471:
		dwOffset = m_471Offset;
		ucCount = m_471Count;
		ucSize = m_471Size;
		break;
	case ENTRY472:
		dwOffset = m_472Offset;
		ucCount = m_472Count;
		ucSize = m_472Size;
		break;
	case ENTRYLOADER:
		dwOffset = m_loaderOffset;
		ucCount = m_loaderCount;
		ucSize = m_loaderSize;
		break;
	default:
		return false;
	}
	if (ucIndex >= ucCount)
	{
		return false;
	}
	PSTRUCT_RKBOOT_ENTRY pEntry;
	pEntry = (PSTRUCT_RKBOOT_ENTRY)(m_BootData+dwOffset+(ucSize*ucIndex));
	FILE *file=NULL;
	file = platform::OpenFile(fileName.c_str(),"wb+");
	if ( !file )
	{
		return false;
	}
	fwrite(m_BootData+pEntry->dwDataOffset,1,pEntry->dwDataSize,file);
	fclose(file);
	return true;
}
bool CRKBoot::GetEntryProperty(ENUM_RKBOOTENTRY type,UCHAR ucIndex,DWORD &dwSize,DWORD &dwDelay,char *pName)
{
	DWORD dwOffset;
	UCHAR ucCount,ucSize;
	switch ( type )
	{
	case ENTRY471:
		dwOffset = m_471Offset;
		ucCount = m_471Count;
		ucSize = m_471Size;
		break;
	case ENTRY472:
		dwOffset = m_472Offset;
		ucCount = m_472Count;
		ucSize = m_472Size;
		break;
	case ENTRYLOADER:
		dwOffset = m_loaderOffset;
		ucCount = m_loaderCount;
		ucSize = m_loaderSize;//Loader��������ʱ�Ѿ�512����
		break;
	default:
		return false;
	}
	if (ucIndex >= ucCount)
	{
		return false;
	}
	PSTRUCT_RKBOOT_ENTRY pEntry;
	pEntry = (PSTRUCT_RKBOOT_ENTRY)(m_BootData+dwOffset+(ucSize*ucIndex));
	dwDelay = pEntry->dwDataDelay;
	dwSize = pEntry->dwDataSize;
	if (pName)
	{
		WCHAR_To_char(pEntry->szName,pName,20);
	}
	return true;
}
bool CRKBoot::GetEntryData(ENUM_RKBOOTENTRY type,UCHAR ucIndex,PBYTE lpData)
{
	DWORD dwOffset;
	UCHAR ucCount,ucSize;
	switch ( type )
	{
	case ENTRY471:
		dwOffset = m_471Offset;
		ucCount = m_471Count;
		ucSize = m_471Size;
		break;
	case ENTRY472:
		dwOffset = m_472Offset;
		ucCount = m_472Count;
		ucSize = m_472Size;
		break;
	case ENTRYLOADER:
		dwOffset = m_loaderOffset;
		ucCount = m_loaderCount;
		ucSize = m_loaderSize;
		break;
	default:
		return false;
	}
	if (ucIndex >= ucCount)
	{
		return false;
	}
	PSTRUCT_RKBOOT_ENTRY pEntry;
	pEntry = (PSTRUCT_RKBOOT_ENTRY)(m_BootData+dwOffset+(ucSize*ucIndex));
	memcpy(lpData,m_BootData+pEntry->dwDataOffset,pEntry->dwDataSize);
	return true;
}
char CRKBoot::GetIndexByName(ENUM_RKBOOTENTRY type,char *pName)
{
	DWORD dwOffset;
	UCHAR ucCount,ucSize;
	switch ( type )
	{
	case ENTRY471:
		dwOffset = m_471Offset;
		ucCount = m_471Count;
		ucSize = m_471Size;
		break;
	case ENTRY472:
		dwOffset = m_472Offset;
		ucCount = m_472Count;
		ucSize = m_472Size;
		break;
	case ENTRYLOADER:
		dwOffset = m_loaderOffset;
		ucCount = m_loaderCount;
		ucSize = m_loaderSize;
		break;
	default:
		return -1;
	}

	for (UCHAR i=0;i<ucCount;i++)
	{
		PSTRUCT_RKBOOT_ENTRY pEntry;
		pEntry = (PSTRUCT_RKBOOT_ENTRY)(m_BootData+dwOffset+(ucSize*i));

		char szName[20];
		WCHAR_To_char(pEntry->szName,szName,20);

		if (strcasecmp(pName,szName)==0)
		{
			return i;
		}
	}
	return -1;
}
CRKBoot::~CRKBoot()
{
	if (m_BootData!=NULL)
	{
		delete []m_BootData;
	}
}

UINT CRKBoot::GetBootRc4Off()
{
	return (UINT)offsetof(STRUCT_RKBOOT_HEAD, ucRc4Flag);
}

int CRKBoot::ObjectInit()
{
	if (m_BootData == NULL)
		return -1;
	if (!CrcCheck())
		return -2;
	PSTRUCT_RKBOOT_HEAD pBootHead;
	pBootHead = (PSTRUCT_RKBOOT_HEAD)(m_BootData);
	if ((pBootHead->uiTag != 0x544F4F42) && (pBootHead->uiTag != 0x2052444C))
		return -3;
	if (pBootHead->dwMergeVersion > 0x1000000)
		return -4;
	m_bRc4Disable = pBootHead->ucRc4Flag ? true : false;
	m_bSignFlag = (pBootHead->ucSignFlag == 'S') ? true : false;
	m_version = pBootHead->dwVersion;
	m_mergeVersion = pBootHead->dwMergeVersion;
	m_BootHeadSize = pBootHead->usSize;
	m_releaseTime.usYear = pBootHead->stReleaseTime.usYear;
	m_releaseTime.ucMonth = pBootHead->stReleaseTime.ucMonth;
	m_releaseTime.ucDay = pBootHead->stReleaseTime.ucDay;
	m_releaseTime.ucHour = pBootHead->stReleaseTime.ucHour;
	m_releaseTime.ucMinute = pBootHead->stReleaseTime.ucMinute;
	m_releaseTime.ucSecond = pBootHead->stReleaseTime.ucSecond;
	m_supportDevice = pBootHead->emSupportChip;
	m_471Offset = pBootHead->dw471EntryOffset;
	m_471Count = pBootHead->uc471EntryCount;
	m_471Size = pBootHead->uc471EntrySize;
	m_472Offset = pBootHead->dw472EntryOffset;
	m_472Count = pBootHead->uc472EntryCount;
	m_472Size = pBootHead->uc472EntrySize;
	m_loaderOffset = pBootHead->dwLoaderEntryOffset;
	m_loaderCount = pBootHead->ucLoaderEntryCount;
	m_loaderSize = pBootHead->ucLoaderEntrySize;
	memcpy(m_crc, m_BootData + (m_BootSize - 4), 4);
	return 0;
}

char CRKBoot::GetEntryName(ENUM_RKBOOTENTRY type, UCHAR ucIndex, char *pName)
{
	DWORD dwOffset;
	UCHAR ucCount, ucSize;
	switch (type) {
	case ENTRY471: dwOffset = m_471Offset; ucCount = m_471Count; ucSize = m_471Size; break;
	case ENTRY472: dwOffset = m_472Offset; ucCount = m_472Count; ucSize = m_472Size; break;
	case ENTRYLOADER: dwOffset = m_loaderOffset; ucCount = m_loaderCount; ucSize = m_loaderSize; break;
	default: return -1;
	}
	if (ucIndex >= ucCount)
		return -1;
	PSTRUCT_RKBOOT_ENTRY pEntry;
	pEntry = (PSTRUCT_RKBOOT_ENTRY)(m_BootData + dwOffset + (ucSize * ucIndex));
	WCHAR_To_char(pEntry->szName, pName, 20);
	// The 20-byte name buffer is terminated at index 19, so a
	// name that fills the field is truncated to 19 characters.
	if (pName[19] != 0)
		pName[19] = 0;
	return 0;
}

void CRKBoot::wchar_To_char(WCHAR *src, char *dst, int len)
{
	WCHAR_To_char(src, dst, len);
}

CRKBoot::CRKBoot(PBYTE lpBootData,DWORD dwBootSize)
{
	Rc4DisableFlag.setContainer(this);
	Rc4DisableFlag.getter(&CRKBoot::GetRc4DisableFlag);
	SignFlag.setContainer(this);
	SignFlag.getter(&CRKBoot::GetSignFlag);
	Version.setContainer(this);
	Version.getter(&CRKBoot::GetVersion);
	MergeVersion.setContainer(this);
	MergeVersion.getter(&CRKBoot::GetMergeVersion);
	ReleaseTime.setContainer(this);
	ReleaseTime.getter(&CRKBoot::GetReleaseTime);
	SupportDevice.setContainer(this);
	SupportDevice.getter(&CRKBoot::GetSupportDevice);
	Entry471Count.setContainer(this);
	Entry471Count.getter(&CRKBoot::GetEntry471Count);
	Entry472Count.setContainer(this);
	Entry472Count.getter(&CRKBoot::GetEntry472Count);
	EntryLoaderCount.setContainer(this);
	EntryLoaderCount.getter(&CRKBoot::GetEntryLoaderCount);
	m_bRc4Disable = false;
	m_bSignFlag = false;
	m_BootData = lpBootData;
	m_BootSize = dwBootSize;
}
void CRKBoot::WCHAR_To_wchar(WCHAR *src,wchar_t *dst,int len)
{
	int i;
	memset(dst,0,len*sizeof(wchar_t));
	for (i=0;i<len;i++)
	{
		memcpy(dst,src,2);
		src++;
		dst++;
	}
}
void CRKBoot::WCHAR_To_char(WCHAR *src,char *dst,int len)
{
	int i;
	memset(dst,0,len*sizeof(char));
	for (i=0;i<len;i++)
	{
		memcpy(dst,src,1);
		src++;
		dst++;
	}
}

