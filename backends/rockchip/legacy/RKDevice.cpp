/*
 * (C) Copyright 2017 Fuzhou Rockchip Electronics Co., Ltd
 * Seth Liu 2017.03.01
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <chrono>
#include <thread>

#include "RKDevice.h"

const char* szManufName[] =
{
	"SAMSUNG",
	"TOSHIBA",
	"HYNIX",
	"INFINEON",
	"MICRON",
	"RENESAS",
	"ST",
	"INTEL"
};

static unsigned char ByteToBCD(unsigned char ucByte)
{
	return (unsigned char)(((ucByte / 10) << 4) | (ucByte % 10));
}

static unsigned short UshortToBCD(unsigned short usValue)
{
	unsigned int uiHigh = (usValue / 100) % 100;
	unsigned int uiLow = usValue % 100;
	return (unsigned short)(((ByteToBCD((unsigned char)uiHigh)) << 8) | ByteToBCD((unsigned char)uiLow));
}

static bool ReadFileToBuffer(char *szFile, char *&pBuffer)
{
	FILE *file = fopen(szFile, "rb");
	if (!file)
		return false;
	fseeko(file, 0, SEEK_END);
	long long iSize = ftello(file);
	fseeko(file, 0, SEEK_SET);
	pBuffer = new char[iSize + 1];
	memset(pBuffer, 0, iSize + 1);
	if (fread(pBuffer, 1, iSize, file) != (size_t)iSize) {
		delete []pBuffer;
		pBuffer = NULL;
		fclose(file);
		return false;
	}
	fclose(file);
	return true;
}

void CRKDevice::SetVendorID(USHORT value)
{
	m_vid = value;
}
void CRKDevice::SetProductID(USHORT value)
{
	m_pid = value;
}
void CRKDevice::SetDeviceType(ENUM_RKDEVICE_TYPE value)
{
	m_device = value;
}
void CRKDevice::SetOsType(ENUM_OS_TYPE value)
{
	m_os = value;
}

void CRKDevice::SetUsbType(ENUM_RKUSB_TYPE value)
{
	m_usb = value;
}
void CRKDevice::SetBcdUsb(USHORT value)
{
	m_bcdUsb = value;
}

// LayerName/LinkName/SerialName are std::string members and the setters assign
// (rather than strcpy into a fixed buffer).
string CRKDevice::GetLayerName()
{
	return m_layerName;
}
void CRKDevice::SetLayerName(string value)
{
	m_layerName = value;
}
string CRKDevice::GetLinkName()
{
	return m_linkName;
}
void CRKDevice::SetLinkName(string value)
{
	m_linkName = value;
}
string CRKDevice::GetSerialName()
{
	return m_serialName;
}
void CRKDevice::SetSerialName(string value)
{
	m_serialName = value;
}

void CRKDevice::SetLocationID(DWORD value)
{
	m_locationID = value;
	m_layerName = GetLayerString(value);
}

void CRKDevice::SetCallBackPointer(ProgressPromptCB value)
{
	if (value)
	{
		m_callBackProc = value;
	}
}
platform::FileLog* CRKDevice::GetLogObjectPointer()
{
	return m_pLog;
}

CRKComm* CRKDevice::GetCommObjectPointer()
{
	return m_pComm;
}

USHORT CRKDevice::GetVendorID()
{
	return m_vid;
}
USHORT CRKDevice::GetProductID()
{
	return m_pid;
}
ENUM_RKDEVICE_TYPE CRKDevice::GetDeviceType()
{
	return m_device;
}
ENUM_OS_TYPE CRKDevice::GetOsType()
{
	return m_os;
}

ENUM_RKUSB_TYPE CRKDevice::GetUsbType()
{
	return m_usb;
}

USHORT CRKDevice::GetBcdUsb()
{
	return m_bcdUsb;
}
DWORD CRKDevice::GetLocationID()
{
	return m_locationID;
}

// ---------------------------------------------------------------------------
// Buffer accessors.
// ---------------------------------------------------------------------------
void CRKDevice::SetBlockStatePointer(PSTRUCT_BLOCK_STATE value)
{
	if (value)
		m_pBlockState = value;
}
PSTRUCT_BLOCK_STATE CRKDevice::GetBlockStatePointer()
{
	return m_pBlockState;
}

void CRKDevice::SetRemallocDisk(bool value)
{
	m_bRemallocDisk = value;
}
bool CRKDevice::GetRemallocDisk()
{
	return m_bRemallocDisk;
}

void CRKDevice::SetPrepareEraseFlag(bool value)
{
	m_bPrepareEraseFlag = value;
}
void CRKDevice::SetMiscModifyFlag(ENUM_MISC_MODIFY_FLAG value)
{
	m_uiMiscModifyFlag = value;
}
void CRKDevice::SetGetNewDiskSizeFlag(bool value)
{
	m_bGetNewDiskSizeFlag = value;
}

PBYTE CRKDevice::GetCustomData()
{
	return m_pCustomData;
}
USHORT CRKDevice::GetCustomDataSize()
{
	return m_usCustomDataSize;
}
USHORT CRKDevice::GetCustomDataOffset()
{
	return m_usCustomDataOffset;
}
// Clamps so size + offset never exceeds the 0x200-byte custom area.
void CRKDevice::SetCustomDataSize(USHORT value)
{
	USHORT usNew = (USHORT)(m_usCustomDataOffset + value);
	if (usNew > 0x1ff)
		value = (USHORT)(0x200 - m_usCustomDataOffset);
	m_usCustomDataSize = value;
}
// An offset beyond the custom area collapses to zero.
void CRKDevice::SetCustomDataOffset(USHORT value)
{
	if (value >= 0x200)
		value = 0;
	m_usCustomDataOffset = value;
}

PBYTE CRKDevice::GetSn()
{
	return m_pSn;
}
unsigned char CRKDevice::GetSnSize()
{
	return m_ucSnSize;
}
// Sn size is capped at 30 bytes.
void CRKDevice::SetSnSize(unsigned char value)
{
	if (value > 0x1e)
		value = 0x1e;
	m_ucSnSize = value;
}

PBYTE CRKDevice::GetMac()
{
	return m_pMac;
}
unsigned char CRKDevice::GetMacSize()
{
	return RKDEVICE_MAC_LEN;
}
PBYTE CRKDevice::GetImei()
{
	return m_pImei;
}
unsigned char CRKDevice::GetImeiSize()
{
	return RKDEVICE_IMEI_LEN;
}
PBYTE CRKDevice::GetUid()
{
	return m_pUid;
}
unsigned char CRKDevice::GetUidSize()
{
	return RKDEVICE_UID_LEN;
}
PBYTE CRKDevice::GetBTAddr()
{
	return m_pBTAddr;
}
unsigned char CRKDevice::GetBTSize()
{
	return RKDEVICE_BT_LEN;
}
PBYTE CRKDevice::GetUidWriteOKPtr()
{
	return &m_pCustomData[0]; // placeholder, never referenced
}
unsigned char CRKDevice::GetUidWriteOK()
{
	return m_bUidWriteOK;
}

bool CRKDevice::GetDirectLBA()
{
	return m_bDirectLbaFlag;
}
void CRKDevice::SetDirectLBA(bool value)
{
	m_bDirectLbaFlag = value;
}
bool CRKDevice::GetReadIDBConfigFlag()
{
	return m_bReadIDBConfigFlag;
}
void CRKDevice::SetReadIDBConfigFlag(bool value)
{
	m_bReadIDBConfigFlag = value;
}
bool CRKDevice::GetSecureModeFlag()
{
	return m_bSecureModeFlag;
}
void CRKDevice::SetSecureModeFlag(bool value)
{
	m_bSecureModeFlag = value;
}
bool CRKDevice::GetNewIDBFlag()
{
	return m_bNewIDBFlag;
}
void CRKDevice::SetNewIDBFlag(bool value)
{
	m_bNewIDBFlag = value;
}
bool CRKDevice::GetFirst4Access()
{
	return m_bFirst4AccessFlag;
}
void CRKDevice::SetFirst4Access(bool value)
{
	m_bFirst4AccessFlag = value;
}
bool CRKDevice::GetGptFlag()
{
	return m_bGptFlag;
}
void CRKDevice::SetGptFlag(bool value)
{
	m_bGptFlag = value;
}
bool CRKDevice::GetNoCheckMode()
{
	return m_bNoCheckMode;
}
void CRKDevice::SetNoCheckMode(bool value)
{
	m_bNoCheckMode = value;
}
bool CRKDevice::GetNorSingleIDB()
{
	return m_bNorSingleIDB;
}
void CRKDevice::SetNorSingleIDB(bool value)
{
	m_bNorSingleIDB = value;
}
UINT CRKDevice::GetForceDataBand()
{
	return m_uiForceDataBand;
}
void CRKDevice::SetForceDataBand(UINT value)
{
	m_uiForceDataBand = value;
    m_hasForceDataBand=true;
}
UINT CRKDevice::GetIDBend()
{
	return m_uiIDBend;
}
bool CRKDevice::GetEmmcStor()
{
	return m_bEmmcStor;
}
bool CRKDevice::GetNorStor()
{
	return m_bNorStor;
}
int CRKDevice::GetError()
{
	return m_iError;
}

// Set* buffer accessors: allocate on demand and copy the caller's bytes. A
// NULL argument frees the buffer.
void CRKDevice::SetSn(PBYTE value)
{
	if (!value) {
		if (m_pSn) {
			delete []m_pSn;
			m_pSn = NULL;
		}
		return;
	}
	if (!m_pSn)
		m_pSn = new BYTE[0x1e];
	memcpy(m_pSn, value, m_ucSnSize);
}
void CRKDevice::SetMac(PBYTE value)
{
	if (!value) {
		if (m_pMac) {
			delete []m_pMac;
			m_pMac = NULL;
		}
		return;
	}
	if (!m_pMac)
		m_pMac = new BYTE[6];
	memcpy(m_pMac, value, 6);
}
void CRKDevice::SetBTAddr(PBYTE value)
{
	if (!value) {
		if (m_pBTAddr) {
			delete []m_pBTAddr;
			m_pBTAddr = NULL;
		}
		return;
	}
	if (!m_pBTAddr)
		m_pBTAddr = new BYTE[6];
	memcpy(m_pBTAddr, value, 6);
}
void CRKDevice::SetImei(PBYTE value)
{
	if (!value) {
		if (m_pImei) {
			delete []m_pImei;
			m_pImei = NULL;
		}
		return;
	}
	if (!m_pImei)
		m_pImei = new BYTE[0xf];
	memcpy(m_pImei, value, 0xf);
}
void CRKDevice::SetUid(PBYTE value)
{
	if (!value) {
		if (m_pUid) {
			delete []m_pUid;
			m_pUid = NULL;
		}
		return;
	}
	if (!m_pUid)
		m_pUid = new BYTE[0x1e];
	memcpy(m_pUid, value, 0x1e);
}
void CRKDevice::SetCustomData(PBYTE value)
{
	if (!value) {
		if (m_pCustomData) {
			delete []m_pCustomData;
			m_pCustomData = NULL;
		}
		return;
	}
	if (!m_pCustomData)
		m_pCustomData = new BYTE[0x200];
	memcpy(m_pCustomData, value, 0x200);
}

string CRKDevice::GetLayerString(UINT dwLocationID)
{
	string strResult;
	char szHex[4];
	for (int i = 0; i < 8; i++) {
		unsigned int uiNibble = (dwLocationID >> ((7 - i) * 4)) & 0xf;
		if (uiNibble == 0 && strResult.empty())
			continue;
		snprintf(szHex, sizeof(szHex), "%x", uiNibble);
		strResult += szHex;
		if (i != 7)
			strResult += "-";
	}
	if (strResult.empty())
		strResult = "0";
	return strResult;
}

CRKDevice::CRKDevice(STRUCT_RKDEVICE_DESC &device)
{
	VendorID.setContainer(this);
    	VendorID.getter(&CRKDevice::GetVendorID);
 	VendorID.setter(&CRKDevice::SetVendorID);

	ProductID.setContainer(this);
    	ProductID.getter(&CRKDevice::GetProductID);
 	ProductID.setter(&CRKDevice::SetProductID);

	DeviceType.setContainer(this);
    	DeviceType.getter(&CRKDevice::GetDeviceType);
 	DeviceType.setter(&CRKDevice::SetDeviceType);

	UsbType.setContainer(this);
    	UsbType.getter(&CRKDevice::GetUsbType);
 	UsbType.setter(&CRKDevice::SetUsbType);

	LayerName.setContainer(this);
    	LayerName.getter(&CRKDevice::GetLayerName);
 	LayerName.setter(&CRKDevice::SetLayerName);

	BcdUsb.setContainer(this);
    	BcdUsb.getter(&CRKDevice::GetBcdUsb);
 	BcdUsb.setter(&CRKDevice::SetBcdUsb);

	LocationID.setContainer(this);
    	LocationID.getter(&CRKDevice::GetLocationID);
	LocationID.setter(&CRKDevice::SetLocationID);

	OsType.setContainer(this);
    	OsType.getter(&CRKDevice::GetOsType);
 	OsType.setter(&CRKDevice::SetOsType);

	LogObjectPointer.setContainer(this);
    	LogObjectPointer.getter(&CRKDevice::GetLogObjectPointer);

	CommObjectPointer.setContainer(this);
    	CommObjectPointer.getter(&CRKDevice::GetCommObjectPointer);

	CallBackPointer.setContainer(this);
 	CallBackPointer.setter(&CRKDevice::SetCallBackPointer);

	m_vid = device.usVid;
	m_pid = device.usPid;
	m_usb = device.emUsbType;
	m_device = device.emDeviceType;
	m_bcdUsb = device.usbcdUsb;
	m_locationID = device.uiLocationID;
	m_layerName = GetLayerString(m_locationID);

	memset(m_flashInfo.blockState, 0, IDBLOCK_TOP);
	m_flashInfo.usPhyBlokcPerIDB = 1;
	m_flashInfo.uiSecNumPerIDB = 0;
	m_callBackProc = NULL;
	m_chipData = NULL;
	m_pImage = NULL;
	m_pLog = NULL;
	m_pComm = NULL;
	m_pFlashInfoData = NULL;
	m_usFlashInfoDataLen = 0;
	m_usFlashInfoDataOffset = 0;
	m_bEmmc = false;
	m_bDirectLba = false;
	m_bFirst4mAccess = false;
	m_uiWorkFlow = 0;

	m_usFlashDataSec = 0;
	m_ucPageSize = 0;
	m_uiEraseSectorPerUnit = 0;
	m_pBlockState = NULL;
	memset(m_uiIDBBlock, 0, sizeof(m_uiIDBBlock));
	m_pSn = NULL;
	m_ucSnSize = 0;
	m_pMac = NULL;
	m_pBTAddr = NULL;
	m_pUid = NULL;
	m_pImei = NULL;
	m_pCustomData = NULL;
	m_usCustomDataSize = 0;
	m_usCustomDataOffset = 0;
	m_bGetNewDiskSizeFlag = false;
	m_bRemallocDisk = false;
	m_bEmmcStor = false;
	m_bNorStor = false;
	m_bUidWriteOK = false;
	m_bPrepareEraseFlag = false;
	m_bDirectLbaFlag = false;
	m_bFirst4AccessFlag = false;
	m_bGptFlag = false;
	m_bNoCheckMode = false;
	m_bNorSingleIDB = false;
	m_bReadIDBConfigFlag = false;
	m_bSecureModeFlag = false;
	m_bNewIDBFlag = false;
	m_uiForceDataBand = 0;
	m_uiMiscModifyFlag = 0;
	m_uiIDBend = 0;
	m_iError = 0;
}
CRKDevice::~CRKDevice()
{
	if (m_pComm) {
		delete m_pComm;
		m_pComm = NULL;
	}
	if (m_chipData) {
		delete []m_chipData;
		m_chipData = NULL;
	}

	if (m_pFlashInfoData) {
		delete []m_pFlashInfoData;
		m_pFlashInfoData = NULL;
	}
	if (m_pSn) { delete []m_pSn; m_pSn = NULL; }
	if (m_pMac) { delete []m_pMac; m_pMac = NULL; }
	if (m_pBTAddr) { delete []m_pBTAddr; m_pBTAddr = NULL; }
	if (m_pUid) { delete []m_pUid; m_pUid = NULL; }
	if (m_pImei) { delete []m_pImei; m_pImei = NULL; }
	if (m_pCustomData) { delete []m_pCustomData; m_pCustomData = NULL; }
}
bool CRKDevice::SetObject(CRKImage *pImage, CRKComm *pComm, platform::FileLog *pLog)
{
	if (!pComm) {
		return false;
	}
	m_pImage = pImage;
	m_pComm = pComm;
	m_pLog = pLog;
	if (m_pImage) {
		m_os = m_pImage->OsType;
	} else
		m_os = RK_OS;
	return true;
}

int CRKDevice::BuildBlockStateMap(UCHAR ucFlashCS)
{
	UCHAR ucState[0x200];
	memset(ucState, 0, sizeof(ucState));
	if (!m_pBlockState)
		return 0;
	int iRet = m_pComm->RKU_TestBadBlock(ucFlashCS, 0, 0x200, ucState);
	if (iRet != ERR_SUCCESS) {
		if (iRet == ERR_FOUND_BAD_BLOCK)
			return 1;
		m_iError = 0x65;
		if (m_pLog)
			m_pLog->Record("ERROR:BuildBlockStateMap-->RKU_TestBadBlock failed,RetCode(%d)", iRet);
		return 0;
	}
	for (unsigned int i = 0; i < 0x200; i++) {
		for (unsigned int b = 0; b < 8; b++) {
			if (ucState[i] & (1 << b)) {
				UINT idx = i * 8 + b;
				if (idx <= 0x30)
					m_pBlockState->blockState[idx] = 1;
			}
		}
	}
	return 1;
}

int CRKDevice::ReadMutilSector(UINT uiPos, UINT uiCount, PBYTE lpBuffer)
{
	UINT uiBlock, uiRemain, uiPerBlock, uiChunk, uiSectorBase;
	UINT uiStartSector = 0;
	USHORT usSecPerBlock;
	if (!m_pComm)
		return -1;
	uiBlock = uiPos / m_uiEraseSectorPerUnit;
	uiPerBlock = uiPos % m_uiEraseSectorPerUnit;
	usSecPerBlock = m_usFlashDataSec;
	uiRemain = uiCount;
	uiStartSector = uiBlock * m_uiEraseSectorPerUnit + uiPerBlock;
	int iRet = m_pComm->RKU_ReadSector(uiStartSector, uiRemain, lpBuffer);
	(void)usSecPerBlock;
	if (iRet != ERR_SUCCESS) {
		m_iError = 0x65;
		if (m_pLog)
			m_pLog->Record("ERROR:ReadMutilSector-->RKU_ReadSector failed,RetCode(%d)", iRet);
		return iRet;
	}
	return ERR_SUCCESS;
}

UCHAR CRKDevice::RandomByte(UCHAR ucMin, UCHAR ucMax)
{
	double dRange = (double)((int)ucMax - (int)ucMin + 1);
	return (UCHAR)((int)(dRange * ((double)rand() / (double)RAND_MAX)) + ucMin);
}

bool CRKDevice::CheckCrc16(PBYTE lpData, USHORT usCrc, USHORT usSize)
{
	return CRC_16(lpData, usSize) == usCrc;
}

bool CRKDevice::CheckUid(UCHAR ucUidSize, PBYTE lpUid)
{
	if (ucUidSize != 0x1e)
		return false;
	USHORT usCrc = *(USHORT *)(lpUid + 0x1c);
	return CRC_CCITT(lpUid, 0x1c) == usCrc;
}

char CRKDevice::FindValidBlocks(char cStart, char cCount)
{
	unsigned char *pState = m_pBlockState->blockState;
	int iBlock = (int)cStart;
	int iRun = 0;
	int iCandidate = (int)cStart;
	const unsigned char *p = pState + (int)cStart;
	while (iBlock <= 0x31) {
		iBlock++;
		iRun++;
		if (*p != 0) {
			iCandidate = iBlock;
			iRun = 0;
		}
		p++;
		if (iRun >= (int)cCount) {
			if ((char)iBlock == 0x32)
				return -1;
			return (char)iCandidate;
		}
	}
	return -1;
}

int CRKDevice::GetIDBData(UINT uiCount, PBYTE lpBuffer, UINT uiSectorNum)
{
	UINT uiSectorBytes = uiSectorNum * 0x210;
	PBYTE pTemp = new BYTE[uiSectorBytes];
	memset(pTemp, 0, uiSectorBytes);
	if (uiCount == 0) {
		delete []pTemp;
		return 0;
	}
	int iLast = -1;
	for (UINT i = 0; i < uiCount; i++) {
		UINT uiOffset = m_uiIDBBlock[i] * m_uiEraseSectorPerUnit;
		if (iLast == -1) {
			if (m_pComm->RKU_ReadSector(uiOffset, uiSectorNum, pTemp) != ERR_SUCCESS) {
				if (m_pLog)
					m_pLog->Record("ERROR:GetIDBData-->RKU_ReadSector failed");
				continue;
			}
			if (*(UINT *)pTemp != 0x0FF0AA55)
				continue;
			memcpy(lpBuffer, pTemp, uiSectorBytes);
			iLast = (int)i;
		} else {
			PBYTE pTmp2 = new BYTE[uiSectorBytes];
			memset(pTmp2, 0, uiSectorBytes);
			if (m_pComm->RKU_ReadSector(uiOffset, uiSectorNum, pTmp2) == ERR_SUCCESS) {
				if (*(UINT *)pTmp2 == 0x0FF0AA55 && memcmp(pTmp2, lpBuffer, 0x200) != 0) {
					memcpy(lpBuffer, pTmp2, uiSectorBytes);
					iLast = (int)i;
				}
			}
			delete []pTmp2;
		}
	}
	delete []pTemp;
	return (iLast >= 0) ? 1 : 1;
}

int CRKDevice::GetWriteBackData(UINT uiCount, PBYTE lpBuffer)
{
	return GetIDBData(uiCount, lpBuffer, 4);
}

bool CRKDevice::FormatDisk(char cDiskIndex, char *pName, platform::FileLog *pLog)
{
	(void)cDiskIndex; (void)pName; (void)pLog;
	return false;
}
bool CRKDevice::CopyData(char cSrc, char *pName, platform::FileLog *pLog)
{
	(void)cSrc; (void)pName; (void)pLog;
	return false;
}

int CRKDevice::EraseEmmcBlock(UCHAR ucFlashCS, DWORD dwPos, DWORD dwCount)
{
	int sectorOffset,nWrittenBlcok,iRet;
	BYTE emptyData[4 * (SECTOR_SIZE+SPARE_SIZE)];
	memset(emptyData, 0xff, 4 * (SECTOR_SIZE + SPARE_SIZE));
	nWrittenBlcok = 0;
	while (dwCount > 0) {
 		sectorOffset = (ucFlashCS * m_flashInfo.uiBlockNum + dwPos + nWrittenBlcok) * m_flashInfo.uiSectorPerBlock;
		iRet = m_pComm->RKU_WriteSector(sectorOffset, 4, emptyData);
		if ((iRet != ERR_SUCCESS) && (iRet != ERR_FOUND_BAD_BLOCK)) {
			if (m_pLog) {
				m_pLog->Record("<LAYER %s> ERROR:EraseEmmcBlock-->RKU_WriteSector failed, RetCode(%d)", m_layerName.c_str(), iRet);
			}
			return iRet;
		}
		dwCount--;
		nWrittenBlcok++;
	}
	return ERR_SUCCESS;
}
int CRKDevice::EraseEmmcByWriteLBA(DWORD dwSectorPos, DWORD dwCount)
{
	int nWritten,iRet;
	BYTE emptyData[32 * SECTOR_SIZE];
	memset(emptyData, 0xff, 32 * SECTOR_SIZE);

	while (dwCount > 0) {
		if (IsAborted())
			return ERR_FAILED;
		nWritten = (dwCount < 32) ? dwCount : 32;
		iRet = m_pComm->RKU_WriteLBA(dwSectorPos, nWritten, emptyData);
		if (iRet != ERR_SUCCESS) {
			if (m_pLog) {
				m_pLog->Record("<LAYER %s> ERROR:EraseEmmcByWriteLBA-->RKU_WriteLBA failed, RetCode(%d)", m_layerName.c_str(), iRet);
			}
			return iRet;
		}
		dwCount -= nWritten;
		dwSectorPos += nWritten;
	}
	return ERR_SUCCESS;
}
bool CRKDevice::EraseEmmc()
{
	UINT uiCount,uiEraseCount,uiSectorOffset,uiTotalCount;
	UINT uiErase=1024*32;
	int iRet=ERR_SUCCESS,iLoopTimes=0;
	uiTotalCount = uiCount = m_flashInfo.uiFlashSize*2*1024;
	uiSectorOffset = 0;
	DWORD dwLayerID;
	dwLayerID = m_locationID;
	ENUM_CALL_STEP emCallStep = CALL_FIRST;

	while (uiCount)
	{
		if (IsAborted())
			return false;
		if (uiCount >= uiErase)
		{
			uiEraseCount = uiErase;
		}
		else
			uiEraseCount = uiCount;
		iRet = m_pComm->RKU_EraseLBA(uiSectorOffset, uiEraseCount);
			
		if (iRet != ERR_SUCCESS) {
			if (m_pLog) {
				m_pLog->Record("ERROR:EraseEmmc-->RKU_EraseLBA failed,RetCode(%d),offset=0x%x,count=0x%x",iRet, uiSectorOffset, uiEraseCount);
			}
			return false;
		}
		uiCount -= uiEraseCount;
		uiSectorOffset += uiEraseCount;
		iLoopTimes++;
		if (iLoopTimes % 8 == 0) {
			if (m_callBackProc) {
				m_callBackProc(dwLayerID, ERASEFLASH_PROGRESS, uiTotalCount, uiSectorOffset, emCallStep);
				emCallStep = CALL_MIDDLE;
			}
		}
	}
	if (m_callBackProc) {
		emCallStep = CALL_LAST;
		m_callBackProc(dwLayerID, ERASEFLASH_PROGRESS, uiTotalCount, uiTotalCount, emCallStep);
	}
	return true;
}
bool CRKDevice::GetFlashInfo()
{
	STRUCT_FLASHINFO_CMD info;
	BYTE flashID[5];
	int iRet;
	UINT uiRead;
	iRet = m_pComm->RKU_ReadFlashInfo((PBYTE)&info, &uiRead);
	if( ERR_SUCCESS == iRet ) {
		if ((info.usBlockSize == 0) || (info.bPageSize == 0)) {
			if (m_pLog) {
				m_pLog->Record("<LAYER %s> ERROR:GetFlashInfo-->RKU_ReadFlashInfo failed,pagesize or blocksize is zero", m_layerName.c_str());
			}
			return false;
		}
		if (info.bManufCode <= 7) {
			strcpy(m_flashInfo.szManufacturerName, szManufName[info.bManufCode]);
		} else {
			strcpy(m_flashInfo.szManufacturerName, "UNKNOWN");
		}
		m_flashInfo.uiFlashSize = info.uiFlashSize / 2 / 1024;
		m_flashInfo.uiPageSize = info.bPageSize / 2;
		m_flashInfo.usBlockSize = info.usBlockSize / 2;
		m_flashInfo.bECCBits = info.bECCBits;
		m_flashInfo.bAccessTime = info.bAccessTime;
		m_flashInfo.uiBlockNum = m_flashInfo.uiFlashSize * 1024 / m_flashInfo.usBlockSize;
		m_flashInfo.uiSectorPerBlock = info.usBlockSize;
		m_flashInfo.bFlashCS = info.bFlashCS;
		m_flashInfo.usValidSecPerBlock = (info.usBlockSize / info.bPageSize) * 4;
		m_uiEraseSectorPerUnit = info.usBlockSize;
		m_usFlashDataSec = info.usBlockSize / 2;
		m_ucPageSize = info.bPageSize / 2;
		if (m_pFlashInfoData) {
			delete []m_pFlashInfoData;
			m_pFlashInfoData = NULL;
		}
		m_usFlashInfoDataLen = BYTE2SECTOR(uiRead);
		m_pFlashInfoData = new BYTE[SECTOR_SIZE * m_usFlashInfoDataLen];
		memset(m_pFlashInfoData, 0, SECTOR_SIZE * m_usFlashInfoDataLen);
		memcpy(m_pFlashInfoData, (PBYTE)&info, uiRead);
		if (m_pLog) {
			string strFlashInfo;
			m_pLog->PrintBuffer(strFlashInfo, m_pFlashInfoData, 11);
			m_pLog->Record("<LAYER %s> INFO:FlashInfo:%s", m_layerName.c_str(), strFlashInfo.c_str());
		}
	} else {
		if (m_pLog) {
			m_pLog->Record("<LAYER %s> ERROR:GetFlashInfo-->RKU_ReadFlashInfo failed, RetCode(%d)", m_layerName.c_str(), iRet);
		}
		return false;
	}
	iRet = m_pComm->RKU_ReadFlashID(flashID);
	if( ERR_SUCCESS == iRet ) {
		DWORD *pID = (DWORD *)flashID;
		if (*pID==0x434d4d45)/*emmc*/ {
			m_bEmmc = true;
		} else
			m_bEmmc = false;
	} else {
		if (m_pLog) {
			m_pLog->Record("<LAYER %s> ERROR:GetFlashInfo-->RKU_ReadFlashID failed, RetCode(%d)", m_layerName.c_str(), iRet);
		}
		return false;
	}
	return true;
}
bool CRKDevice::TestDevice()
{
	int iResult, iTryCount;
	DWORD dwTotal, dwCurrent, dwLayerID;
	dwLayerID = m_locationID;
	ENUM_CALL_STEP emCallStep = CALL_FIRST;
	do {
		iTryCount = 3;
		while (iTryCount > 0) {
			iResult = m_pComm->RKU_TestDeviceReady(&dwTotal, &dwCurrent);
			if ((iResult == ERR_SUCCESS) || (iResult == ERR_DEVICE_UNREADY)) {
				break;
			}
			if (m_pLog) {
				m_pLog->Record("<LAYER %s> ERROR:TestDevice-->RKU_TestDeviceReady failed, RetCode(%d)", m_layerName.c_str(), iResult);
			}
			iTryCount--;
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
		if (iTryCount <= 0) {
			return false;
		}

		if (iResult == ERR_SUCCESS) {
			if (emCallStep == CALL_MIDDLE) {
				if (m_callBackProc) {
					dwCurrent = dwTotal;
					emCallStep = CALL_LAST;
					m_callBackProc(dwLayerID, TESTDEVICE_PROGRESS, dwTotal, dwCurrent, emCallStep);
				}
			}
			break;
		}
		if (dwCurrent>dwTotal) {
			if (m_pLog) {
				m_pLog->Record("<LAYER %s> ERROR:TestDevice-->RKU_TestDeviceReady failed,Total=%d, Current=%d", m_layerName.c_str(), dwTotal, dwCurrent);
			}
			return false;
		}
		if (UsbType == RKUSB_LOADER) {
			if (dwTotal == 0) {
				if (m_pLog)
				{
					m_pLog->Record("<LAYER %s> ERROR:TestDevice-->RKU_TestDeviceReady failed, Total is zero", m_layerName.c_str());
				}
				return false;
			}
		}
		if (m_callBackProc)
		{
			m_callBackProc(dwLayerID, TESTDEVICE_PROGRESS, dwTotal, dwCurrent, emCallStep);
			emCallStep = CALL_MIDDLE;
		}
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}while(iResult == ERR_DEVICE_UNREADY);
	return true;
}

bool CRKDevice::ResetDevice(UCHAR ucSubCode)
{
	int iRet;
	iRet = m_pComm->RKU_ResetDevice(ucSubCode);
	if (iRet == ERR_SUCCESS) {
		return true;
	} else {
		bool bRet = false;
		if (iRet == -2 || iRet == -4)
			bRet = true;
		if (!bRet)
			m_iError = 0x65;
		if (m_pLog)
			m_pLog->Record("<LAYER %s> ERROR:ResetDevice-->RKU_ResetDevice failed, RetCode(%d)", m_layerName.c_str(), iRet);
		return bRet;
	}
}

bool CRKDevice::PowerOffDevice()
{
	int iRet;
	iRet = m_pComm->RKU_ResetDevice(RST_POWEROFF_SUBCODE);
	if (iRet == ERR_SUCCESS) {
		return true;
	} else {
		m_iError = 0x65;
		if (m_pLog) {
			m_pLog->Record("<LAYER %s> ERROR:PowerOffDevice-->RKU_ResetDevice failed, RetCode(%d)", m_layerName.c_str(), iRet);
		}
		return false;
	}
}
bool CRKDevice::CheckChip()
{
	int iRet;
	BYTE bChipInfo[CHIPINFO_LEN];
	ENUM_RKDEVICE_TYPE curDeviceType = RKNONE_DEVICE;
	memset(bChipInfo, 0, CHIPINFO_LEN);
	iRet = m_pComm->RKU_ReadChipInfo(bChipInfo);
	if (iRet == ERR_SUCCESS) {
		if (!m_chipData) {
			m_chipData = new BYTE[CHIPINFO_LEN];
		}
		memset(m_chipData, 0, CHIPINFO_LEN);
		memcpy(m_chipData, bChipInfo, CHIPINFO_LEN);
		DWORD *pValue;
		pValue = (DWORD *)(&bChipInfo[0]);

		if ((ENUM_RKDEVICE_TYPE)(*pValue) == m_device) {
			return true;
		}
		if (*pValue == 0x524B3237) {
			curDeviceType = RK27_DEVICE;
		} else if (*pValue == 0x32373341) {
			curDeviceType = RKCAYMAN_DEVICE;
		} else if (*pValue == 0x524B3238) {
			curDeviceType = RK28_DEVICE;
		} else if (*pValue == 0x32383158) {
			curDeviceType = RK281X_DEVICE;
		} else if (*pValue == 0x32383242) {
			curDeviceType = RKPANDA_DEVICE;
		} else if (*pValue == 0x32393058) {
			curDeviceType = RK29_DEVICE;
		} else if (*pValue == 0x32393258) {
			curDeviceType = RK292X_DEVICE;
		} else if (*pValue == 0x33303041) {
			curDeviceType = RK30_DEVICE;
		} else if (*pValue == 0x33313041) {
			curDeviceType = RK30B_DEVICE;
		} else if (*pValue == 0x33313042) {
			curDeviceType = RK31_DEVICE;
		} else if (*pValue == 0x33323041) {
			curDeviceType = RK32_DEVICE;
		} else if (*pValue == 0x32363243) {
			curDeviceType = RKSMART_DEVICE;
		} else if (*pValue == 0x6E616E6F) {
			curDeviceType = RKNANO_DEVICE;
		} else if (*pValue == 0x4E4F5243) {
			curDeviceType = RKCROWN_DEVICE;
		}

		if (curDeviceType == m_device){
			return true;
		} else {
			if (m_pLog) {
				m_pLog->Record("<LAYER %s> ERROR:CheckChip-->Chip is not match, firmware(0x%x), device(0x%x)", m_layerName.c_str(), m_device, *pValue);
			}
			return false;
		}
	}
	else {
		if (m_pLog) {
			m_pLog->Record("<LAYER %s> ERROR:CheckChip-->RKU_ReadChipInfo failed,RetCode(%d)", m_layerName.c_str(), iRet);
		}
		return false;
	}
}

int CRKDevice::DownloadBoot()
{
	UCHAR i;
	DWORD dwSize, dwDelay;
	PBYTE pBuffer = NULL;
	for ( i = 0; i < m_pImage->m_bootObject->Entry471Count; i++ ) {
		if ( !m_pImage->m_bootObject->GetEntryProperty(ENTRY471, i, dwSize, dwDelay) ) {
			if (m_pLog) {
				m_pLog->Record("<LAYER %s> ERROR:DownloadBoot-->GetEntry471Property failed,index(%d)", m_layerName.c_str(), i);
			}
			return -2;
		}
		if (dwSize>0) {
			pBuffer = new BYTE[dwSize];
			if ( !m_pImage->m_bootObject->GetEntryData(ENTRY471, i, pBuffer) ) {
				if (m_pLog) {
					m_pLog->Record("<LAYER %s> ERROR:DownloadBoot-->GetEntry471Data failed,index(%d)", m_layerName.c_str(), i);
				}
				delete []pBuffer;
				return -3;
			}
			if ( !Boot_VendorRequest(0x0471,pBuffer,dwSize) ) {
				if (m_pLog) {
					m_pLog->Record("<LAYER %s> ERROR:DownloadBoot-->Boot_VendorRequest471 failed,index(%d)", m_layerName.c_str(), i);
				}
				delete []pBuffer;
				return -4;
			}
			delete []pBuffer;
			pBuffer = NULL;
			if (dwDelay>0) {
				std::this_thread::sleep_for(std::chrono::microseconds(dwDelay * 1000));
			}

		}
	}

	for ( i=0; i < m_pImage->m_bootObject->Entry472Count; i++ ) {
		if ( !m_pImage->m_bootObject->GetEntryProperty(ENTRY472, i, dwSize, dwDelay) ) {
			if (m_pLog) {
				m_pLog->Record("<LAYER %s> ERROR:DownloadBoot-->GetEntry472Property failed,index(%d)", m_layerName.c_str(), i);
			}
			return -2;
		}
		if (dwSize > 0) {
			pBuffer = new BYTE[dwSize];
			if ( !m_pImage->m_bootObject->GetEntryData(ENTRY472, i, pBuffer) ) {
				if (m_pLog) {
					m_pLog->Record("<LAYER %s> ERROR:DownloadBoot-->GetEntry472Data failed,index(%d)", m_layerName.c_str(), i);
				}
				delete []pBuffer;
				return -3;
			}
			if ( !Boot_VendorRequest(0x0472, pBuffer, dwSize) ) {
				if (m_pLog) {
					m_pLog->Record("<LAYER %s> ERROR:DownloadBoot-->Boot_VendorRequest472 failed,index(%d)", m_layerName.c_str(), i);
				}
				delete []pBuffer;
				return -4;
			}
			delete []pBuffer;
			pBuffer = NULL;
			if (dwDelay > 0) {
				std::this_thread::sleep_for(std::chrono::microseconds(dwDelay * 1000));
			}
		}
	}
	std::this_thread::sleep_for(std::chrono::seconds(1));
	return 0;

}
bool CRKDevice::Boot_VendorRequest( DWORD requestCode, PBYTE pBuffer, DWORD dwDataSize)
{
	int iRet;
	iRet = m_pComm->RKU_DeviceRequest(requestCode, pBuffer, dwDataSize);
	return (iRet == ERR_SUCCESS) ? true : false;
}
int CRKDevice::EraseAllBlocks(bool force_block_erase)
{
	int i;
	UINT uiBlockCount;
	int iRet = ERR_SUCCESS, iErasePos = 0, iEraseBlockNum = 0, iEraseTimes = 0, iCSIndex = 0;
	BYTE bCSCount = 0;
	for (i = 0; i < 8; i++) {
		if ( m_flashInfo.bFlashCS & (1 << i) ) {
			bCSCount++;
		}
	}
	ReadCapability();
	DWORD dwLayerID;
	dwLayerID = LocationID;
	ENUM_CALL_STEP emCallStep = CALL_FIRST;
	if (!force_block_erase) {
		if ((m_bEmmc)||(m_bDirectLba)) {
			if (!EraseEmmc()) {
				if (m_pLog) {
					m_pLog->Record("<LAYER %s> ERROR:EraseAllBlocks-->EraseEmmc failed", m_layerName.c_str());
				}
				return -1;
			}
			return 0;
		}
	}
	for (i = 0; i < 8; i++) {
		if ( m_flashInfo.bFlashCS & (1 << i) ) {
			uiBlockCount = m_flashInfo.uiBlockNum;
			iErasePos = 0;
			iEraseTimes = 0;
			while (uiBlockCount > 0) {
				if (IsAborted())
					return -1;
				iEraseBlockNum = (uiBlockCount < MAX_ERASE_BLOCKS) ? uiBlockCount : MAX_ERASE_BLOCKS;
				iRet = m_pComm->RKU_EraseBlock(i, iErasePos, iEraseBlockNum, ERASE_FORCE);
				if ((iRet != ERR_SUCCESS) && (iRet != ERR_FOUND_BAD_BLOCK)) {
					if (m_pLog) {
						m_pLog->Record("<LAYER %s> ERROR:EraseAllBlocks-->RKU_EraseBlock failed,RetCode(%d)", m_layerName.c_str(), iRet);
					}
					return -1;
				}
				iErasePos += iEraseBlockNum;
				uiBlockCount -= iEraseBlockNum;
				iEraseTimes++;
				if (iEraseTimes % 8 == 0) {
					if (m_callBackProc) {
						m_callBackProc(dwLayerID, ERASEFLASH_PROGRESS, m_flashInfo.uiBlockNum * bCSCount, iCSIndex * m_flashInfo.uiBlockNum + iErasePos, emCallStep);
						emCallStep = CALL_MIDDLE;
					}
				}
			}
			iCSIndex++;
		}
	}

	if (m_callBackProc) {
		emCallStep = CALL_LAST;
		m_callBackProc(dwLayerID, ERASEFLASH_PROGRESS, m_flashInfo.uiBlockNum * bCSCount, iCSIndex * m_flashInfo.uiBlockNum, emCallStep);
	}
	return 0;
}
bool CRKDevice::ReadCapability()
{
	int ret;
	BYTE data[8];
	ret = m_pComm->RKU_ReadCapability(data);
	if (ret != ERR_SUCCESS)
	{
		if (m_pLog)
		{
			m_pLog->Record("ERROR:ReadCapability-->RKU_ReadCapability failed,err(%d)", ret);
		}
		return false;
	}
	if (data[0] & 0x1)
	{
		m_bDirectLba = true;
	}
	else
		m_bDirectLba = false;
	if (data[0] & 0x4)
	{
		m_bFirst4mAccess = true;
	}
	else
		m_bFirst4mAccess = false;
	return true;
}

// Workflow value selected by CRKUpgrade::SetObject; reused by the step engine
// as the capability mask each step is tested against.
UINT CRKDevice::GetWorkFlow()
{
	return m_uiWorkFlow;
}
void CRKDevice::SetWorkFlow(UINT value)
{
	m_uiWorkFlow = value;
}
bool CRKDevice::LowerFormatDevice(bool &bRet)
{
	DWORD uiTotal = 0;
	DWORD uiCurrent = 0;
	BYTE ucSubCode = TU_LOWERFORMAT_SUBCODE;
	int iRet = ERR_SUCCESS;
	int i;
	bRet = true;
	for (i = 0; i < 3; i++)
	{
		iRet = m_pComm->RKU_TestDeviceReady(&uiTotal, &uiCurrent, ucSubCode);
		if ((iRet == ERR_SUCCESS) || (iRet == ERR_DEVICE_UNREADY))
			return true;
		if (m_pLog)
			m_pLog->Record("ERROR:LowerFormatDevice-->RKU_TestDeviceReady failed,subCode(0x%x),RetCode(%d)", ucSubCode, iRet);
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	bRet = false;
	return false;
}
bool CRKDevice::SetResetFlag()
{
	int iRet = m_pComm->RKU_ResetDevice(RST_RESETMSC_SUBCODE);
	if (iRet == ERR_SUCCESS)
		return true;
	if (m_pLog)
		m_pLog->Record("ERROR:SetResetFlag-->RKU_ResetDevice failed,RetCode(%d)", iRet);
	return false;
}
bool CRKDevice::RefreshComm(STRUCT_RKDEVICE_DESC &deviceDesc)
{
	bool bRet = false;
	CRKComm *pComm = new CRKUsbComm(deviceDesc, m_pLog, bRet);
	if (!bRet) {
		delete pComm;
		return false;
	}
	if (m_pComm)
		delete m_pComm;
	m_pComm = pComm;
	m_vid = deviceDesc.usVid;
	m_pid = deviceDesc.usPid;
	m_usb = deviceDesc.emUsbType;
	m_locationID = deviceDesc.uiLocationID;
	return true;
}
bool CRKDevice::SendExtCommand(char cCommand, UINT uiCommand, platform::FileLog *pLog, PBYTE pData, UINT uiSize)
{
	return false;
}
bool CRKDevice::EraseMutilBlock(UCHAR ucFlashCS, UINT uiPos, UINT uiCount, bool bForce)
{
	BYTE ucType = bForce ? ERASE_FORCE : ERASE_NORMAL;
	while (uiCount > 0) {
		UINT uiBlock = (uiCount < MAX_ERASE_BLOCKS) ? uiCount : MAX_ERASE_BLOCKS;
		int iRet = m_pComm->RKU_EraseBlock(ucFlashCS, uiPos, uiBlock, ucType);
		if ((iRet != ERR_SUCCESS) && (iRet != ERR_FOUND_BAD_BLOCK)) {
			if (m_pLog)
				m_pLog->Record("<LAYER %s> ERROR:EraseMutilBlock-->RKU_EraseBlock failed, RetCode(%d)", m_layerName.c_str(), iRet);
			return false;
		}
		uiPos += uiBlock;
		uiCount -= uiBlock;
	}
	return true;
}
bool CRKDevice::EraseEmmcIDB()
{
	UINT uiPos = 0;
	UINT uiRemain = 0x2000;
	while (uiRemain > 0) {
		UINT uiChunk = (uiRemain < 0x400) ? uiRemain : 0x400;
		int iRet = m_pComm->RKU_EraseLBA(uiPos, uiChunk);
		if (iRet != ERR_SUCCESS) {
			if (m_pLog)
				m_pLog->Record("<LAYER %s> ERROR:EraseEmmcIDB-->RKU_EraseLBA failed, RetCode(%d)", m_layerName.c_str(), iRet);
			return false;
		}
		uiPos += uiChunk;
		uiRemain -= uiChunk;
	}
	return true;
}

