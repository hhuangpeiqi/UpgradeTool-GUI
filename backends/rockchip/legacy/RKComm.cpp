/*
 * (C) Copyright 2017 Fuzhou Rockchip Electronics Co., Ltd
 * Seth Liu 2017.03.01
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <chrono>
#include <thread>

#include "RKComm.h"
#include "platform/LogService.h"
#include "Endian.h"
extern unsigned short CRC_CCITT(unsigned char* p, UINT CalculateNumber);
CRKComm::CRKComm(platform::LogService *pLog)
{
	m_deviceDesc = {};
	m_log = pLog;
}
CRKComm::~CRKComm()
{
}

CRKUsbComm::CRKUsbComm(STRUCT_RKDEVICE_DESC devDesc, platform::LogService *pLog, bool &bRet):CRKComm(pLog)
{
	bRet = InitializeUsb(devDesc);
}
CRKUsbComm::~CRKUsbComm()
{
	UninitializeUsb();
}

bool CRKUsbComm::InitializeUsb(STRUCT_RKDEVICE_DESC devDesc)
{
    m_deviceDesc = std::move(devDesc);
    m_pipeBulkIn = m_pipeBulkOut = 0;
    if (!m_deviceDesc.usbDevice) return false;
    int error = 0;
    m_usb = core::rockchip::OpenUsb(*m_deviceDesc.usbDevice, m_deviceDesc.emUsbType == RKUSB_MSC, error, m_log);
    if (!m_usb) {
        if (m_log) m_log->writef(core::LogLevel::Error, "Rockchip", "InitializeUsb: %s", core::rockchip::UsbError(error).c_str());
        return false;
    }
    m_pipeBulkIn = m_usb->bulkIn; m_pipeBulkOut = m_usb->bulkOut;
    return true;
}
void CRKUsbComm::UninitializeUsb()
{
    m_usb.reset();
    m_deviceDesc = {};
    m_pipeBulkIn = m_pipeBulkOut = 0;
}
bool CRKUsbComm::Reset_Usb_Config(STRUCT_RKDEVICE_DESC devDesc)
{
    bool bRet;
    UninitializeUsb();
    bRet = InitializeUsb(devDesc);
    return bRet;
}
bool CRKUsbComm::Reset_Usb_Device()
{
    return m_usb && m_usb->Reset() == 0;
}

bool CRKUsbComm::RKU_Read(BYTE* lpBuffer, DWORD dwSize)
{
	int  iRet;
	int  nRead;
	iRet = m_usb->Bulk(m_pipeBulkIn, lpBuffer, dwSize, nRead, 5000);
	if (iRet!=0) {
	    if (m_log) {
	        m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_Read failed,err=%d", iRet);
	    }
	    return false;
	}
	if (nRead != (int)dwSize) {
       	if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_Read failed, size=%d, read=%d", dwSize, nRead);
        }
        return false;
    }
	return true;
}

bool CRKUsbComm::RKU_Write(BYTE* lpBuffer, DWORD dwSize)
{
	int  iRet;
	int nWrite;
	iRet = m_usb->Bulk(m_pipeBulkOut, lpBuffer, dwSize, nWrite, 5000);
	if (iRet != 0) {
	    if (m_log) {
	        m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_Write failed, err=%d", iRet);
	    }
	    return false;
	}
    if (nWrite != (int)dwSize) {
       	if (m_log) {
		m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_Write failed, size=%d, read=%d", dwSize, nWrite);
    	}
        return false;
    }
	return true;
}
int CRKUsbComm::RandomInteger(int low, int high)
{
	int k;
	double d;

	d = (double)rand() / ((double)RAND_MAX + 1);
	k = (int)(d * (high - low + 1));
	return (low + k);
}
DWORD CRKUsbComm::MakeCBWTag()
{
	DWORD tag = 0;
	int i = 0;

	for(i=0; i<4; i++){
		tag <<= 8;
		tag += RandomInteger(0, 0xFF);
	}
	return tag;
}
void CRKUsbComm::InitializeCBW(PCBW pCBW, USB_OPERATION_CODE code)
{
	memset(pCBW,0, sizeof(CBW));

	pCBW->dwCBWSignature = CBW_SIGN;
	pCBW->dwCBWTag = MakeCBWTag();
	pCBW->cbwcb.ucOperCode = code;

	switch(code) {
		case TEST_UNIT_READY:	/* Test Unit Ready	: 0 */
		case READ_FLASH_ID:		/* Read Flash ID	: 1 */
		case READ_FLASH_INFO:
		case READ_CHIP_INFO:
		case READ_EFUSE:
		case READ_CAPABILITY:
		case READ_STORAGE:
		case READ_OTP_CHIP:
			pCBW->ucCBWFlags= DIRECTION_IN;
			pCBW->ucCBWCBLength = 0x06;
			break;
		case DEVICE_RESET:		/* Reset Device		: 0xff */
		case ERASE_SYSTEMDISK:
		case SET_RESET_FLAG:
		case CHANGE_STORAGE:
			pCBW->ucCBWFlags = DIRECTION_OUT;
			pCBW->ucCBWCBLength = 0x06;
			break;
		case TEST_BAD_BLOCK:	/* Test Bad Block	: 3 */
		case READ_SECTOR:		/* Read Pages		: 4 */
		case READ_LBA:		/* Read Pages		: 4 */
		case READ_SDRAM:		/* Write Pages		: 15 */
		case READ_SPI_FLASH:
		case READ_NEW_EFUSE:
		case READ_COM_DATA:
			pCBW->ucCBWFlags = DIRECTION_IN;
			pCBW->ucCBWCBLength = 0x0a;
			break;
		case WRITE_SECTOR:		/* Write Pages		: 5 */
		case WRITE_LBA:		/* Write Pages		: 15 */
		case WRITE_SDRAM:		/* Write Pages		: 15 */
		case EXECUTE_SDRAM:
		case ERASE_NORMAL:		/* Erase Blocks		: 6 */
		case ERASE_FORCE:		/* Read Spare		: 11 */
		case WRITE_EFUSE:
		case WRITE_SPI_FLASH:
		case WRITE_NEW_EFUSE:
		case ERASE_LBA:
			pCBW->ucCBWFlags = DIRECTION_OUT;
			pCBW->ucCBWCBLength = 0x0a;
			break;
		default:
			break;
	}
}


bool CRKUsbComm::RKU_ClearBuffer(CBW& cbw, CSW& csw)
{
	DWORD dwReadBytes=0;
	DWORD dwTotalRead=0;
	int iTryCount;
	iTryCount = 3;
	do {
		dwReadBytes = RKU_Read_EX((BYTE*)&csw, sizeof(CSW) );

		if (UFI_CHECK_SIGN(cbw,csw))
		{
			return true;
		}
		if (dwReadBytes != sizeof(CSW))
		{
			iTryCount--;
			std::this_thread::sleep_for(std::chrono::seconds(3));
		}
		dwTotalRead += dwReadBytes;
		if (dwTotalRead >= MAX_CLEAR_LEN)
		{
			break;
		}
	}while ( iTryCount > 0 );
	return false;
}

DWORD CRKUsbComm::RKU_Read_EX(BYTE* lpBuffer, DWORD dwSize)
{
	int  iRet;
	int  nRead;
	iRet = m_usb->Bulk(m_pipeBulkIn, lpBuffer, dwSize, nRead, 5000);
	if (iRet != 0) {
	    if (m_log) {
	        m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_Read_EX failed, err=%d", iRet);
	    }
	    return 0;
	}
	return nRead;
}

int CRKUsbComm::RKU_EraseBlock(BYTE ucFlashCS, DWORD dwPos, DWORD dwCount, BYTE ucEraseType)
{
    if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
        if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_EraseBlock failed,device not support");
        }
        return ERR_DEVICE_NOT_SUPPORT;
    }
	CBW cbw;
	CSW csw;
	USHORT usCount;
	usCount = dwCount;
	if(dwCount > MAX_ERASE_BLOCKS)
		return ERR_CROSS_BORDER;

	InitializeCBW(&cbw, (USB_OPERATION_CODE)ucEraseType);
	cbw.ucCBWLUN = ucFlashCS;
	cbw.cbwcb.dwAddress = EndianU32_LtoB(dwPos);
	cbw.cbwcb.usLength = EndianU16_LtoB(usCount);

	if(!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
	{
		return ERR_DEVICE_WRITE_FAILED;
	}

	if(!RKU_Read((BYTE *)&csw, sizeof(CSW)))
	{
		return ERR_DEVICE_READ_FAILED;
	}

	if( !UFI_CHECK_SIGN(cbw, csw) )
		return ERR_CMD_NOTMATCH;

	if(csw.ucCSWStatus == 1)
		return ERR_FOUND_BAD_BLOCK;

	return ERR_SUCCESS;
}
int CRKUsbComm::RKU_ReadChipInfo(BYTE* lpBuffer)
{
    if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
        if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ReadChipInfo failed,device not support");
        }
        return ERR_DEVICE_NOT_SUPPORT;
    }

	CBW cbw;
	CSW csw;

	InitializeCBW(&cbw, READ_CHIP_INFO);
	cbw.dwCBWTransferLength = 16;

	if(!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
	{
		return ERR_DEVICE_WRITE_FAILED;
	}

	if(!RKU_Read(lpBuffer, 16))
	{
		return ERR_DEVICE_READ_FAILED;
	}

	if(!RKU_Read( (BYTE *)&csw, sizeof(CSW)))
	{
		return ERR_DEVICE_READ_FAILED;
	}

	if( !UFI_CHECK_SIGN(cbw, csw) )
		return ERR_CMD_NOTMATCH;

	return ERR_SUCCESS;
}
int CRKUsbComm::RKU_ReadFlashID(BYTE* lpBuffer)
{
    if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
        if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ReadChipInfo failed,device not support");
        }
        return ERR_DEVICE_NOT_SUPPORT;
    }

	CBW cbw;
	CSW csw;

	InitializeCBW(&cbw, READ_FLASH_ID);
	cbw.dwCBWTransferLength = 5;

	if(!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
	{
		return ERR_DEVICE_WRITE_FAILED;
	}

	if(!RKU_Read(lpBuffer, 5))
	{
		return ERR_DEVICE_READ_FAILED;
	}

	if(!RKU_Read( (BYTE *)&csw, sizeof(CSW)))
	{
		return ERR_DEVICE_READ_FAILED;
	}

	if( !UFI_CHECK_SIGN(cbw, csw) )
		return ERR_CMD_NOTMATCH;

	return ERR_SUCCESS;
}
int CRKUsbComm::RKU_ReadFlashInfo(BYTE* lpBuffer, UINT *puiRead)
{
    if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
        if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ReadFlashInfo failed,device not support");
        }
        return ERR_DEVICE_NOT_SUPPORT;
    }
	CBW cbw;
	CSW csw;

	InitializeCBW(&cbw, READ_FLASH_INFO);
	cbw.dwCBWTransferLength = 11;

	if(!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
	{
		return ERR_DEVICE_WRITE_FAILED;
	}

	DWORD dwRead;
	dwRead = RKU_Read_EX(lpBuffer, 512);
	if ((dwRead < 11) || (dwRead > 512))
	{
		return ERR_DEVICE_READ_FAILED;
	}
	if (puiRead)
	{
		*puiRead = dwRead;
	}
/*
	if(!RKU_Read(hDev, lpBuffer, 11))
	{
		return ERR_DEVICE_READ_FAILED;
	}
*/
	if(!RKU_Read((BYTE *)&csw, sizeof(CSW)))
	{
		return ERR_DEVICE_READ_FAILED;
	}

	if( !UFI_CHECK_SIGN(cbw, csw) )
		return ERR_CMD_NOTMATCH;

	return ERR_SUCCESS;
}
int CRKUsbComm::RKU_ReadCapability(BYTE* lpBuffer)
{
    if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
        if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ReadCapability failed,device not support");
        }
        return ERR_DEVICE_NOT_SUPPORT;
    }
    
	CBW cbw;
	CSW csw;
	DWORD dwRead;
	
	InitializeCBW(&cbw, READ_CAPABILITY);
	cbw.dwCBWTransferLength = 8;
	
	if(!RKU_Write((BYTE*)&cbw, sizeof(CBW)))
	{
		return ERR_DEVICE_WRITE_FAILED;	
	}

	dwRead = RKU_Read_EX((BYTE*)&csw, sizeof(CSW));
		
	if(dwRead != 8)
	{	
		return ERR_DEVICE_READ_FAILED;		
	}
	memcpy(lpBuffer, (BYTE*)&csw, 8);
	
	if(!RKU_Read((BYTE*)&csw, sizeof(CSW)))
	{	
		return ERR_DEVICE_READ_FAILED;		
	}

	if( !UFI_CHECK_SIGN(cbw, csw) )
		return ERR_CMD_NOTMATCH;			
	
	return ERR_SUCCESS;			
}

int CRKUsbComm::RKU_ReadLBA(DWORD dwPos, DWORD dwCount, BYTE* lpBuffer, BYTE bySubCode)
{
    if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
        if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ReadLBA failed,device not support");
        }
        return ERR_DEVICE_NOT_SUPPORT;
    }
	CBW cbw;
	CSW csw;
	USHORT wSectorSize;
	USHORT usSectorLen;
	wSectorSize = 512;
	usSectorLen=dwCount;

	InitializeCBW(&cbw, READ_LBA);
	cbw.dwCBWTransferLength = dwCount * wSectorSize;
	cbw.cbwcb.dwAddress = EndianU32_LtoB(dwPos);
	cbw.cbwcb.usLength = EndianU16_LtoB(usSectorLen);
	cbw.cbwcb.ucReserved = bySubCode;

	if(!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
	{
		return ERR_DEVICE_WRITE_FAILED;
	}
	DWORD dwTotal;
	dwTotal = usSectorLen * wSectorSize;

	if(!RKU_Read(lpBuffer, dwTotal))
	{
		return ERR_DEVICE_READ_FAILED;
	}

	if(!RKU_Read((BYTE*)&csw, sizeof(CSW)))
	{
		return ERR_DEVICE_READ_FAILED;
	}

	if( !UFI_CHECK_SIGN(cbw, csw) )
		return ERR_CMD_NOTMATCH;

	if(csw.ucCSWStatus == 1)
		return ERR_FAILED;

	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_ResetDevice(BYTE bySubCode)
{
    if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
        if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ResetDevice failed,device not support");
        }
        return ERR_DEVICE_NOT_SUPPORT;
    }

	CBW cbw;
	CSW csw;

	InitializeCBW(&cbw, DEVICE_RESET);
	cbw.cbwcb.ucReserved = bySubCode;

	if(!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
	{
		return ERR_DEVICE_WRITE_FAILED;
	}

	if(!RKU_Read((BYTE *)&csw, sizeof(CSW)))
	{
		return ERR_DEVICE_READ_FAILED;
	}

	if( !UFI_CHECK_SIGN(cbw, csw) ) {
		bool bRet;
		bRet = RKU_ClearBuffer(cbw, csw);
		if (!bRet) {
			return ERR_CMD_NOTMATCH;
		}
	}

	if(csw.ucCSWStatus == 1)
		return ERR_FAILED;

	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_ChangeStorage(BYTE storage)
{
    if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
        if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ChangeStorage failed,device not support");
        }
        return ERR_DEVICE_NOT_SUPPORT;
    }

	CBW cbw;
	CSW csw;

	InitializeCBW(&cbw, CHANGE_STORAGE);
	cbw.cbwcb.ucReserved = storage;

	if(!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
	{
		printf("AMO: ERR_DEVICE_WRITE_FAILED\n");
		return ERR_DEVICE_WRITE_FAILED;
	}

	if(!RKU_Read((BYTE *)&csw, sizeof(CSW)))
	{
		return ERR_DEVICE_READ_FAILED;
	}

	if( !UFI_CHECK_SIGN(cbw, csw) ) {
		bool bRet;
		bRet = RKU_ClearBuffer(cbw, csw);
		if (!bRet) {
			return ERR_CMD_NOTMATCH;
		}
	}

	if(csw.ucCSWStatus == 1)
		return ERR_FAILED;

	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_ReadStorage(BYTE* storage)
{
    if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
        if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ReadCapability failed,device not support");
        }
        return ERR_DEVICE_NOT_SUPPORT;
    }

	CBW cbw;
	CSW csw;
	DWORD dwRead;

	InitializeCBW(&cbw, READ_STORAGE);
	cbw.dwCBWTransferLength = 4;

	if(!RKU_Write((BYTE*)&cbw, sizeof(CBW)))
	{
		return ERR_DEVICE_WRITE_FAILED;
	}

	DWORD storage_bits;
	dwRead = RKU_Read_EX((BYTE*)&storage_bits, sizeof(storage_bits));

	if(dwRead != 4)
	{
		return ERR_DEVICE_READ_FAILED;
	}

	if(!RKU_Read((BYTE*)&csw, sizeof(CSW)))
	{
		return ERR_DEVICE_READ_FAILED;
	}

	if( !UFI_CHECK_SIGN(cbw, csw) )
		return ERR_CMD_NOTMATCH;

	/* search the bit index */
	*storage = 255;
	for (unsigned i=0; i < 32; i++) {
		if (storage_bits & (1<<i)) {
			*storage = i;
			break;
		}
	}
	return ERR_SUCCESS;
}


int CRKUsbComm::RKU_TestDeviceReady(DWORD *dwTotal, DWORD *dwCurrent, BYTE bySubCode)
{
    if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
        if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_TestDeviceReady failed,device not support");
        }
        return ERR_DEVICE_NOT_SUPPORT;
    }
	CBW cbw;
	CSW csw;

	InitializeCBW(&cbw, TEST_UNIT_READY);

	cbw.cbwcb.ucReserved = bySubCode;
	if(!RKU_Write( (BYTE *)&cbw, sizeof(CBW))) {
		return ERR_DEVICE_WRITE_FAILED;
	}

	if(!RKU_Read( (BYTE *)&csw, sizeof(CSW))) {
		return ERR_DEVICE_READ_FAILED;
	}

	if( !UFI_CHECK_SIGN(cbw, csw) ) {
		bool bRet;
		bRet = RKU_ClearBuffer(cbw ,csw);
		if (!bRet) {
			return ERR_CMD_NOTMATCH;
		}
	}

	if ((dwTotal!=NULL)&&(dwCurrent!=NULL)) {
		*dwCurrent = (csw.dwCBWDataResidue >>16);
		*dwTotal = (csw.dwCBWDataResidue & 0x0000FFFF);

		*dwTotal = EndianU16_BtoL(*dwTotal);
		*dwCurrent = EndianU16_BtoL(*dwCurrent);
	}
	if(csw.ucCSWStatus == 1) {
		return ERR_DEVICE_UNREADY;
	}

	return ERR_DEVICE_READY;
}
int CRKUsbComm::RKU_WriteLBA(DWORD dwPos, DWORD dwCount, BYTE* lpBuffer, BYTE bySubCode)
{
    if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
        if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_WriteLBA failed,device not support");
        }
        return ERR_DEVICE_NOT_SUPPORT;
    }
	CBW cbw;
	CSW csw;
	USHORT wSectorSize;
	USHORT usCount;
	wSectorSize = 512;
	usCount = dwCount;
	DWORD dwTotal = usCount * wSectorSize;

	InitializeCBW(&cbw, WRITE_LBA);
	cbw.dwCBWTransferLength = dwCount * wSectorSize;
	cbw.cbwcb.dwAddress = EndianU32_LtoB(dwPos);
	cbw.cbwcb.usLength = EndianU16_LtoB(usCount);
	cbw.cbwcb.ucReserved = bySubCode;
	if(!RKU_Write( (BYTE *)&cbw, sizeof(CBW))) {
		return ERR_DEVICE_WRITE_FAILED;
	}

	if(!RKU_Write( lpBuffer, dwTotal)) {
		return ERR_DEVICE_WRITE_FAILED;
	}

	if(!RKU_Read( (BYTE *)&csw, sizeof(CSW))) {
		return ERR_DEVICE_READ_FAILED;
	}

	if( !UFI_CHECK_SIGN(cbw, csw) )
		return ERR_CMD_NOTMATCH;

	if(csw.ucCSWStatus == 1)
		return ERR_FAILED;

	return ERR_SUCCESS;
}
int CRKUsbComm::RKU_EraseLBA(DWORD dwPos, DWORD dwCount)
{
    if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
        if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_WriteLBA failed,device not support");
        }
        return ERR_DEVICE_NOT_SUPPORT;
    }
	CBW cbw;
	CSW csw;
	USHORT usCount;
	usCount = dwCount;


	InitializeCBW(&cbw, ERASE_LBA);
	cbw.cbwcb.dwAddress = EndianU32_LtoB(dwPos);
	cbw.cbwcb.usLength = EndianU16_LtoB(usCount);

	if(!RKU_Write( (BYTE *)&cbw, sizeof(CBW))) {
		return ERR_DEVICE_WRITE_FAILED;
	}

	if(!RKU_Read( (BYTE *)&csw, sizeof(CSW))) {
		return ERR_DEVICE_READ_FAILED;
	}

	if( !UFI_CHECK_SIGN(cbw, csw) )
		return ERR_CMD_NOTMATCH;

	if(csw.ucCSWStatus == 1)
		return ERR_FAILED;

	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_WriteSector(DWORD dwPos, DWORD dwCount, BYTE *lpBuffer)
{
    if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
        if (m_log) {
            m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_WriteSector failed,device not support");
        }
        return ERR_DEVICE_NOT_SUPPORT;
    }
	CBW cbw;
	CSW csw;
	USHORT wSectorSize;
	USHORT usCount;
	usCount=dwCount;
	if(usCount > 32)
		return ERR_CROSS_BORDER;

	wSectorSize = 528;
	InitializeCBW(&cbw, WRITE_SECTOR);
	cbw.dwCBWTransferLength = dwCount * wSectorSize;
	cbw.cbwcb.dwAddress = EndianU32_LtoB(dwPos);
	cbw.cbwcb.usLength = EndianU16_LtoB(usCount);

	if(!RKU_Write( (BYTE *)&cbw, sizeof(CBW))) {
		return ERR_DEVICE_WRITE_FAILED;
	}

	if(!RKU_Write( lpBuffer, usCount * wSectorSize)) {
		return ERR_DEVICE_WRITE_FAILED;
	}

	if(!RKU_Read( (BYTE *)&csw, sizeof(CSW))) {
		return ERR_DEVICE_READ_FAILED;
	}

	if( !UFI_CHECK_SIGN(cbw, csw) )
		return ERR_CMD_NOTMATCH;

	if(csw.ucCSWStatus == 1)
		return ERR_FAILED;

	return ERR_SUCCESS;
}


int CRKUsbComm::RKU_DeviceRequest(DWORD dwRequest, BYTE *lpBuffer, DWORD dwDataSize)
{
	if (m_deviceDesc.emUsbType != RKUSB_MASKROM) {
	    if (m_log) {
	        m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_DeviceRequest failed,device not support");
	    }
	    return ERR_DEVICE_NOT_SUPPORT;
	}
	if ((dwRequest != 0x0471) && (dwRequest != 0x0472)) {
		if (m_log) {
	        m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_DeviceRequest failed,request not support");
	    }
	    return ERR_REQUEST_NOT_SUPPORT;
	}

	bool bSendPendPacket = false;
	const DWORD payloadSize = dwDataSize;
	USHORT crcValue = 0xffff;
	BYTE *pData = NULL;
	pData = new BYTE[dwDataSize + 5];
	memset(pData, 0, dwDataSize + 5);
	memcpy(pData, lpBuffer, dwDataSize);

	switch(dwDataSize % 4096) {
		case 4095:
			++dwDataSize;
			break;
		case 4094:
			bSendPendPacket = true;
			break;
		case 0:
		default:
			break;
	}

	crcValue = CRC_CCITT(pData, dwDataSize);
	pData[dwDataSize] = (crcValue & 0xff00) >> 8;
	pData[dwDataSize+1] = crcValue & 0x00ff;
	dwDataSize += 2;

	UINT nSendBytes = 0;
	DWORD dwTotalSended = 0;
	int iRet;
	if (m_log) m_log->writef(core::LogLevel::Debug, "Rockchip", "Maskrom payload: index=0x%04x payload_bytes=%u wire_bytes=%u crc=0x%04x pending=%d",
		unsigned(dwRequest), unsigned(payloadSize), unsigned(dwDataSize), unsigned(crcValue), int(bSendPendPacket));

	while(dwTotalSended < dwDataSize) {
		nSendBytes = ( (dwDataSize - dwTotalSended) > 4096) ? 4096 : (dwDataSize - dwTotalSended);
		iRet = m_usb->Control(0x40, 0xC, 0, dwRequest, pData + dwTotalSended, nSendBytes, 5000);
		if (iRet != (int)nSendBytes) {
			if (m_log) {
				m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_DeviceRequest-->DeviceRequest vendor=0x%x offset=%u requested=%u pending=0 failed, err=%d (%s)",
					dwRequest, unsigned(dwTotalSended), nSendBytes, iRet, iRet < 0 ? core::rockchip::UsbError(iRet).c_str() : "short-transfer");
			}
			delete []pData;
			return ERR_REQUEST_FAIL;
		}
		dwTotalSended += nSendBytes;
	}

	if(bSendPendPacket) {
		BYTE ucFillByte = 0;
		iRet = m_usb->Control(0x40, 0xC, 0, dwRequest, &ucFillByte, 1, 5000);
		if (iRet != 0) {
			if (m_log) {
				m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_DeviceRequest-->DeviceRequest vendor=0x%x offset=%u requested=1 pending=1 failed, err=%d (%s)",
					dwRequest, unsigned(dwTotalSended), iRet, iRet < 0 ? core::rockchip::UsbError(iRet).c_str() : "unexpected-length");
			}
			delete []pData;
			return ERR_REQUEST_FAIL;
		}
	}

	delete []pData;

    return ERR_SUCCESS;
}

/* -------------------------------------------------------------------------
 * Additional device operations. Op codes, transfer lengths and CBW field
 * usage.
 * ------------------------------------------------------------------------- */

int CRKUsbComm::RKU_SwitchStorage(BYTE storage)
{
	if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
		if (m_log)
			m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_SwitchStorage failed,device not support");
		return ERR_DEVICE_NOT_SUPPORT;
	}
	CBW cbw;
	CSW csw;
	InitializeCBW(&cbw, CHANGE_STORAGE);
	cbw.cbwcb.ucReserved = storage;

	if (!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
		return ERR_DEVICE_WRITE_FAILED;
	if (!RKU_Read((BYTE *)&csw, sizeof(CSW)))
		return ERR_DEVICE_READ_FAILED;
	if (!UFI_CHECK_SIGN(cbw, csw)) {
		if (!RKU_ClearBuffer(cbw, csw))
			return ERR_CMD_NOTMATCH;
	}
	if (csw.ucCSWStatus == 1)
		return ERR_FAILED;
	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_ReadStorageList(BYTE *lpBuffer)
{
	if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
		if (m_log)
			m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ReadStorageList failed,device not support");
		return ERR_DEVICE_NOT_SUPPORT;
	}
	CBW cbw;
	CSW csw;
	InitializeCBW(&cbw, READ_STORAGE);
	cbw.dwCBWTransferLength = 4;

	if (!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
		return ERR_DEVICE_WRITE_FAILED;
	if (!RKU_Read(lpBuffer, 4))
		return ERR_DEVICE_READ_FAILED;
	if (!RKU_Read((BYTE *)&csw, sizeof(CSW)))
		return ERR_DEVICE_READ_FAILED;
	if (!UFI_CHECK_SIGN(cbw, csw))
		return ERR_CMD_NOTMATCH;
	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_ReadOtpChip(BYTE *lpBuffer)
{
	if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
		if (m_log)
			m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ReadOtpChip failed,device not support");
		return ERR_DEVICE_NOT_SUPPORT;
	}
	CBW cbw;
	CSW csw;
	InitializeCBW(&cbw, READ_OTP_CHIP);
	cbw.dwCBWTransferLength = 32;

	if (!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
		return ERR_DEVICE_WRITE_FAILED;
	if (!RKU_Read(lpBuffer, 32))
		return ERR_DEVICE_READ_FAILED;
	if (!RKU_Read((BYTE *)&csw, sizeof(CSW)))
		return ERR_DEVICE_READ_FAILED;
	if (!UFI_CHECK_SIGN(cbw, csw))
		return ERR_CMD_NOTMATCH;
	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_ReadComData(BYTE *lpBuffer, DWORD dwSize)
{
	if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
		if (m_log)
			m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ReadComData failed,device not support");
		return ERR_DEVICE_NOT_SUPPORT;
	}
	CBW cbw;
	CSW csw;
	InitializeCBW(&cbw, READ_COM_DATA);
	// dwSize is a sector count: the device returns dwSize * 512 bytes.
	cbw.cbwcb.usLength = EndianU16_LtoB((USHORT)dwSize);

	if (!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
		return ERR_DEVICE_WRITE_FAILED;
	if (!RKU_Read(lpBuffer, dwSize << 9))
		return ERR_DEVICE_READ_FAILED;
	if (!RKU_Read((BYTE *)&csw, sizeof(CSW)))
		return ERR_DEVICE_READ_FAILED;
	if (!UFI_CHECK_SIGN(cbw, csw))
		return ERR_CMD_NOTMATCH;
	if (csw.ucCSWStatus == 1)
		return ERR_FAILED;
	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_ReadSector(DWORD dwPos, DWORD dwCount, BYTE *lpBuffer)
{
	if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
		if (m_log)
			m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ReadSector failed,device not support");
		return ERR_DEVICE_NOT_SUPPORT;
	}
	if (dwCount > 32)
		return ERR_CROSS_BORDER;

	CBW cbw;
	CSW csw;
	// Sector size defaults to 528; the high 16 bits of dwCount override it.
	UINT uiSectorSize = (dwCount >> 16) == 0 ? 528 : (dwCount >> 16);
	UINT uiTotal = (dwCount & 0xFFFF) * uiSectorSize;

	InitializeCBW(&cbw, READ_SECTOR);
	cbw.dwCBWTransferLength = uiTotal;
	cbw.cbwcb.dwAddress = EndianU32_LtoB(dwPos);
	cbw.cbwcb.usLength = EndianU16_LtoB((USHORT)dwCount);

	if (!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
		return ERR_DEVICE_WRITE_FAILED;
	if (!RKU_Read(lpBuffer, uiTotal))
		return ERR_DEVICE_READ_FAILED;
	if (!RKU_Read((BYTE *)&csw, sizeof(CSW)))
		return ERR_DEVICE_READ_FAILED;
	if (!UFI_CHECK_SIGN(cbw, csw))
		return ERR_CMD_NOTMATCH;
	if (csw.ucCSWStatus == 1)
		return ERR_FAILED;
	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_TestBadBlock(BYTE ucFlashCS, DWORD dwPos, DWORD dwCount, BYTE *lpBuffer)
{
	if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
		if (m_log)
			m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_TestBadBlock failed,device not support");
		return ERR_DEVICE_NOT_SUPPORT;
	}
	if (dwCount > 0xFFFF)
		return ERR_CROSS_BORDER;

	CBW cbw;
	CSW csw;
	InitializeCBW(&cbw, TEST_BAD_BLOCK);
	cbw.ucCBWLUN = ucFlashCS;
	cbw.dwCBWTransferLength = 64;
	cbw.cbwcb.dwAddress = EndianU32_LtoB(dwPos);
	cbw.cbwcb.usLength = EndianU16_LtoB((USHORT)dwCount);

	if (!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
		return ERR_DEVICE_WRITE_FAILED;
	if (!RKU_Read(lpBuffer, 64))
		return ERR_DEVICE_READ_FAILED;
	if (!RKU_Read((BYTE *)&csw, sizeof(CSW)))
		return ERR_DEVICE_READ_FAILED;
	if (!UFI_CHECK_SIGN(cbw, csw))
		return ERR_CMD_NOTMATCH;
	if (csw.ucCSWStatus == 1)
		return ERR_FAILED;
	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_ReadNewEfuse(USHORT usAddr, USHORT usLen, BYTE *lpBuffer, BYTE ucFlag)
{
	if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
		if (m_log)
			m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ReadNewEfuse failed,device not support");
		return ERR_DEVICE_NOT_SUPPORT;
	}
	CBW cbw;
	CSW csw;
	InitializeCBW(&cbw, READ_NEW_EFUSE);
	cbw.cbwcb.ucReserved = ucFlag;
	if (ucFlag == 0) {
		cbw.dwCBWTransferLength = usLen;
		cbw.cbwcb.dwAddress = 0;
		cbw.cbwcb.usLength = EndianU16_LtoB(usLen);
	} else if (ucFlag == 2) {
		cbw.dwCBWTransferLength = 0;
		cbw.cbwcb.dwAddress = 0;
		cbw.cbwcb.usLength = 0;
	} else {
		cbw.dwCBWTransferLength = usLen;
		cbw.cbwcb.dwAddress = ((DWORD)usAddr << 24) | (((DWORD)usAddr << 8) & 0x00FF0000);
		cbw.cbwcb.usLength = EndianU16_LtoB(usLen);
	}

	if (!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
		return ERR_DEVICE_WRITE_FAILED;
	if (ucFlag != 2) {
		if (!RKU_Read(lpBuffer, usLen))
			return ERR_DEVICE_READ_FAILED;
	}
	if (!RKU_Read((BYTE *)&csw, sizeof(CSW)))
		return ERR_DEVICE_READ_FAILED;
	if (!UFI_CHECK_SIGN(cbw, csw))
		return ERR_CMD_NOTMATCH;
	if (ucFlag == 2) {
		if (csw.ucCSWStatus == 1) {
			*lpBuffer = 1;
			return ERR_SUCCESS;
		}
		if (csw.ucCSWStatus == 0) {
			*lpBuffer = 0;
			return ERR_SUCCESS;
		}
		return ERR_FAILED;
	}
	if (csw.ucCSWStatus == 1)
		return ERR_FAILED;
	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_WriteNewEfuse(USHORT usAddr, USHORT usLen, BYTE *lpBuffer, BYTE ucFlag)
{
	if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
		if (m_log)
			m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_WriteNewEfuse failed,device not support");
		return ERR_DEVICE_NOT_SUPPORT;
	}
	CBW cbw;
	CSW csw;
	InitializeCBW(&cbw, WRITE_NEW_EFUSE);
	cbw.dwCBWTransferLength = usLen;
	cbw.cbwcb.dwAddress = EndianU32_LtoB(usAddr);
	cbw.cbwcb.usLength = EndianU16_LtoB(usLen);
	cbw.cbwcb.ucReserved = ucFlag;

	if (!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
		return ERR_DEVICE_WRITE_FAILED;
	if (!RKU_Write(lpBuffer, usLen))
		return ERR_DEVICE_WRITE_FAILED;
	if (!RKU_Read((BYTE *)&csw, sizeof(CSW)))
		return ERR_DEVICE_READ_FAILED;
	if (!UFI_CHECK_SIGN(cbw, csw))
		return ERR_CMD_NOTMATCH;
	if (csw.ucCSWStatus == 1)
		return ERR_FAILED;
	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_WriteSDRam(DWORD dwSize, BYTE *lpBuffer)
{
	if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
		if (m_log)
			m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_WriteSDRam failed,device not support");
		return ERR_DEVICE_NOT_SUPPORT;
	}
	CBW cbw;
	CSW csw;
	InitializeCBW(&cbw, WRITE_SDRAM);
	cbw.dwCBWTransferLength = dwSize;
	cbw.cbwcb.ucReserved = 0xAA;
	cbw.cbwcb.usLength = EndianU16_LtoB((USHORT)dwSize);

	if (!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
		return ERR_DEVICE_WRITE_FAILED;
	if (!RKU_Write(lpBuffer, dwSize))
		return ERR_DEVICE_WRITE_FAILED;
	if (!RKU_Read((BYTE *)&csw, sizeof(CSW)))
		return ERR_DEVICE_READ_FAILED;
	if (!UFI_CHECK_SIGN(cbw, csw))
		return ERR_CMD_NOTMATCH;
	if (csw.ucCSWStatus == 1)
		return ERR_FAILED;
	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_RunSDRam()
{
	if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
		if (m_log)
			m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_RunSDRam failed,device not support");
		return ERR_DEVICE_NOT_SUPPORT;
	}
	CBW cbw;
	CSW csw;
	InitializeCBW(&cbw, EXECUTE_SDRAM);
	cbw.cbwcb.ucReserved = 0xAA;

	if (!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
		return ERR_DEVICE_WRITE_FAILED;
	if (!RKU_Read((BYTE *)&csw, sizeof(CSW)))
		return ERR_DEVICE_READ_FAILED;
	if (!UFI_CHECK_SIGN(cbw, csw))
		return ERR_CMD_NOTMATCH;
	if (csw.ucCSWStatus == 1)
		return ERR_FAILED;
	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_ResetPipe(BYTE ucPipe)
{
	if ((m_deviceDesc.emUsbType != RKUSB_LOADER) && (m_deviceDesc.emUsbType != RKUSB_MASKROM)) {
		if (m_log)
			m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_ResetPipe failed,device not support");
		return ERR_DEVICE_NOT_SUPPORT;
	}
	int iRet;
	// pipe 1 = bulk-out, pipe 2 = bulk-in (both), other = bulk-in.
	if (ucPipe == 2) {
		iRet = m_usb->Control(0x02, 0x01, 0, m_pipeBulkIn, NULL, 0, 5000);
		if (iRet < 0)
			return ERR_REQUEST_FAIL;
	}
	if (ucPipe == 1 || ucPipe == 2) {
		iRet = m_usb->Control(0x02, 0x01, 0, m_pipeBulkOut, NULL, 0, 5000);
		if (iRet < 0)
			return ERR_REQUEST_FAIL;
		return ERR_SUCCESS;
	}
	iRet = m_usb->Control(0x02, 0x01, 0, m_pipeBulkIn, NULL, 0, 5000);
	if (iRet < 0)
		return ERR_REQUEST_FAIL;
	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_MscToRockusb()
{
	if (m_deviceDesc.emUsbType != RKUSB_MSC) {
		if (m_log)
			m_log->writef(core::LogLevel::Error, "Rockchip", "RKU_MscToRockusb failed,device not support");
		return ERR_DEVICE_NOT_SUPPORT;
	}
	CBW cbw;
	CSW csw;
	InitializeCBW(&cbw, DEVICE_RESET);
	cbw.cbwcb.ucReserved = 0xE0;
	cbw.cbwcb.dwAddress = EndianU32_LtoB(0xFFFFFFFE);

	if (!RKU_Write((BYTE *)&cbw, sizeof(CBW)))
		return ERR_DEVICE_WRITE_FAILED;
	if (!RKU_Read((BYTE *)&csw, sizeof(CSW)))
		return ERR_DEVICE_READ_FAILED;
	if (!UFI_CHECK_SIGN(cbw, csw)) {
		if (!RKU_ClearBuffer(cbw, csw))
			return ERR_CMD_NOTMATCH;
	}
	if (csw.ucCSWStatus == 1)
		return ERR_FAILED;
	return ERR_SUCCESS;
}

int CRKUsbComm::RKU_UvcToRockusb()
{
	if (!m_usb)
		return ERR_DEVICE_NOT_SUPPORT;
	DWORD dwBuf = 0xFFFFFFFF;
	int iRet = m_usb->Control(0x21, 0x01,
	                                   0x100, 0x600, (BYTE *)&dwBuf, 4, 5000);
	if (iRet < 0)
		return ERR_REQUEST_FAIL;
	return ERR_SUCCESS;
}



