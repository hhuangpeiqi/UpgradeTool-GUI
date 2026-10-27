#ifndef RKCOMM_HEADER
#define RKCOMM_HEADER
#include "DefineHeader.h"
typedef enum {
		USB_BULK_READ = 0,
		USB_BULK_WRITE,
		USB_CONTROL,
} USB_ACCESS_TYPE;
typedef enum {
	TU_NONE_SUBCODE = 0,
	TU_ERASESYSTEM_SUBCODE = 0xFE,
	TU_LOWERFORMAT_SUBCODE = 0xFD,
	TU_ERASEUSERDATA_SUBCODE = 0xFB,
	TU_GETUSERSECTOR_SUBCODE = 0xF9
} TESTUNIT_SUBCODE;
typedef enum{
	RST_NONE_SUBCODE = 0,
	RST_RESETMSC_SUBCODE,
	RST_POWEROFF_SUBCODE,
	RST_RESETMASKROM_SUBCODE,
	RST_DISCONNECTRESET_SUBCODE
} RESET_SUBCODE;
typedef enum{
	RWMETHOD_IMAGE = 0,
	RWMETHOD_LBA
} RW_SUBCODE;

typedef enum {
		TEST_UNIT_READY = 0,
		READ_FLASH_ID = 0x01,
		TEST_BAD_BLOCK = 0x03,
		READ_SECTOR = 0x04,
		WRITE_SECTOR = 0x05,
		ERASE_NORMAL = 0x06,
		ERASE_FORCE = 0x0B,
		READ_LBA = 0x14,
		WRITE_LBA = 0x15,
		ERASE_SYSTEMDISK = 0x16,
		READ_SDRAM = 0x17,
		WRITE_SDRAM = 0x18,
		EXECUTE_SDRAM = 0x19,
		READ_FLASH_INFO = 0x1A,
		READ_CHIP_INFO = 0x1B,
		SET_RESET_FLAG = 0x1E,
		WRITE_EFUSE = 0x1F,
		READ_EFUSE = 0x20,
		READ_SPI_FLASH = 0x21,
		WRITE_SPI_FLASH = 0x22,
		WRITE_NEW_EFUSE = 0x23,
		READ_NEW_EFUSE = 0x24,
	ERASE_LBA=0x25,
	READ_SDRAM_EXT=0x26,
	READ_COM_DATA=0x28,
	CHANGE_STORAGE = 0x2A,
	READ_STORAGE = 0x2B,
	READ_OTP_CHIP = 0x2C,
	READ_CAPABILITY=0xAA,
	DEVICE_RESET = 0xFF
} USB_OPERATION_CODE;

#pragma pack(1)

typedef struct {
	BYTE	ucOperCode;
	BYTE	ucReserved;
	DWORD	dwAddress;
	BYTE	ucReserved2;
	USHORT	usLength;
	BYTE	ucReserved3[7];
} CBWCB, *PCBWCB;

typedef struct {
	DWORD	dwCBWSignature;
	DWORD	dwCBWTag;
	DWORD	dwCBWTransferLength;
	BYTE	ucCBWFlags;
	BYTE	ucCBWLUN;
	BYTE	ucCBWCBLength;
	CBWCB	cbwcb;
} CBW, *PCBW;

typedef struct {
	DWORD	dwCSWSignature;
	DWORD	dwCSWTag;
	DWORD	dwCBWDataResidue;
	BYTE	ucCSWStatus;
} CSW, *PCSW;

#pragma pack()
#define CMD_TIMEOUT 0
#define CBW_SIGN			0x43425355	/* "USBC" */
#define CSW_SIGN			0x53425355	/* "USBS" */

#define DIRECTION_OUT		0x00
#define DIRECTION_IN		0x80
#define MAX_TEST_BLOCKS		512
#define MAX_ERASE_BLOCKS	16
#define  MAX_CLEAR_LEN	16*1024

#ifndef ERR_SUCCESS
#define ERR_SUCCESS		0
#endif
#define ERR_DEVICE_READY		0
#define ERR_DEVICE_OPEN_FAILED	-1
#define ERR_CSW_OPEN_FAILED		-2
#define ERR_DEVICE_WRITE_FAILED	-3
#define ERR_DEVICE_READ_FAILED	-4
#define ERR_CMD_NOTMATCH		-5
#define ERR_DEVICE_UNREADY		-6
#define ERR_FOUND_BAD_BLOCK		-7
#define ERR_FAILED				-8
#define ERR_CROSS_BORDER		-9
#define ERR_DEVICE_NOT_SUPPORT  -10
#define ERR_REQUEST_NOT_SUPPORT  -11
#define ERR_REQUEST_FAIL		-12
#define ERR_BUFFER_NOT_ENOUGH   -13
#define UFI_CHECK_SIGN(cbw, csw) ((CSW_SIGN == (csw).dwCSWSignature) && ((csw).dwCSWTag == (cbw).dwCBWTag))

namespace platform { class LogService; }
class CRKComm
{
public:
	virtual int RKU_EraseBlock(BYTE ucFlashCS, DWORD dwPos, DWORD dwCount, BYTE ucEraseType) = 0;
	virtual int RKU_ReadChipInfo(BYTE *lpBuffer) = 0;
	virtual int RKU_ReadFlashID(BYTE *lpBuffer) = 0;
	virtual int RKU_ReadCapability(BYTE *lpBuffer)=0;
	virtual int RKU_ReadFlashInfo(BYTE *lpBuffer, UINT *puiRead = NULL) = 0;
	virtual int RKU_ReadLBA(DWORD dwPos, DWORD dwCount, BYTE *lpBuffer, BYTE bySubCode = RWMETHOD_IMAGE) = 0;	
	virtual int RKU_ResetDevice(BYTE bySubCode = RST_NONE_SUBCODE) = 0;
	virtual int RKU_TestDeviceReady(DWORD *dwTotal = NULL, DWORD *dwCurrent = NULL, BYTE bySubCode = TU_NONE_SUBCODE) = 0;
	virtual int RKU_WriteLBA(DWORD dwPos, DWORD dwCount, BYTE *lpBuffer, BYTE bySubCode = RWMETHOD_IMAGE) = 0;
	virtual int RKU_WriteSector(DWORD dwPos, DWORD dwCount, BYTE *lpBuffer) = 0;
    virtual int RKU_DeviceRequest(DWORD dwRequest, BYTE *lpBuffer, DWORD dwDataSize) = 0;
	virtual bool Reset_Usb_Config(STRUCT_RKDEVICE_DESC devDesc) = 0;
	virtual bool Reset_Usb_Device() = 0;
	virtual int RKU_EraseLBA(DWORD dwPos,DWORD dwCount)=0;
	// Additional device operations. Op codes / lengths in RKComm.cpp.
	virtual int RKU_SwitchStorage(BYTE storage) = 0;
	virtual int RKU_ReadStorageList(BYTE *lpBuffer) = 0;
	virtual int RKU_ReadOtpChip(BYTE *lpBuffer) = 0;
	virtual int RKU_ReadComData(BYTE *lpBuffer, DWORD dwSize) = 0;
	virtual int RKU_ReadSector(DWORD dwPos, DWORD dwCount, BYTE *lpBuffer) = 0;
	virtual int RKU_TestBadBlock(BYTE ucFlashCS, DWORD dwPos, DWORD dwCount, BYTE *lpBuffer) = 0;
	virtual int RKU_ReadNewEfuse(USHORT usAddr, USHORT usLen, BYTE *lpBuffer, BYTE ucFlag) = 0;
	virtual int RKU_WriteNewEfuse(USHORT usAddr, USHORT usLen, BYTE *lpBuffer, BYTE ucFlag) = 0;
	virtual int RKU_WriteSDRam(DWORD dwAddress, BYTE *lpBuffer) = 0;
	virtual int RKU_RunSDRam() = 0;
	virtual int RKU_ResetPipe(BYTE ucPipe) = 0;
	virtual int RKU_MscToRockusb() = 0;
	virtual int RKU_UvcToRockusb() = 0;
	CRKComm(platform::LogService *pLog);
	virtual ~CRKComm();
protected:
	STRUCT_RKDEVICE_DESC m_deviceDesc;
	platform::LogService *m_log;
private:
	virtual bool RKU_Write(BYTE *lpBuffer, DWORD dwSize) = 0;
	virtual bool RKU_Read(BYTE *lpBuffer, DWORD dwSize) = 0;
};
class CRKUsbComm:public CRKComm
{
public:
	virtual	int RKU_EraseBlock(BYTE ucFlashCS, DWORD dwPos, DWORD dwCount, BYTE ucEraseType);
	virtual int RKU_ChangeStorage(BYTE storage);
	virtual int RKU_ReadStorage(BYTE *storage);
	virtual int RKU_ReadChipInfo(BYTE *lpBuffer);
	virtual int RKU_ReadFlashID(BYTE *lpBuffer);
	virtual int RKU_ReadCapability(BYTE *lpBuffer);
	virtual int RKU_ReadFlashInfo(BYTE *lpBuffer, UINT *puiRead = NULL);
	virtual int RKU_ReadLBA(DWORD dwPos, DWORD dwCount, BYTE *lpBuffer, BYTE bySubCode = RWMETHOD_IMAGE);
	virtual int RKU_ResetDevice(BYTE bySubCode = RST_NONE_SUBCODE);
	virtual int RKU_TestDeviceReady(DWORD *dwTotal = NULL, DWORD *dwCurrent = NULL, BYTE bySubCode = TU_NONE_SUBCODE);
	virtual int RKU_WriteLBA(DWORD dwPos, DWORD dwCount, BYTE *lpBuffer, BYTE bySubCode = RWMETHOD_IMAGE);
	virtual int RKU_WriteSector(DWORD dwPos, DWORD dwCount, BYTE *lpBuffer);
    virtual int RKU_DeviceRequest(DWORD dwRequest, BYTE *lpBuffer, DWORD dwDataSize);
	CRKUsbComm(STRUCT_RKDEVICE_DESC devDesc, platform::LogService *pLog, bool &bRet);
	virtual ~CRKUsbComm();
    virtual bool Reset_Usb_Config(STRUCT_RKDEVICE_DESC devDesc);
	virtual bool Reset_Usb_Device();
    virtual int RKU_EraseLBA(DWORD dwPos,DWORD dwCount);
    virtual int RKU_SwitchStorage(BYTE storage);
    virtual int RKU_ReadStorageList(BYTE *lpBuffer);
    virtual int RKU_ReadOtpChip(BYTE *lpBuffer);
    virtual int RKU_ReadComData(BYTE *lpBuffer, DWORD dwSize);
    virtual int RKU_ReadSector(DWORD dwPos, DWORD dwCount, BYTE *lpBuffer);
    virtual int RKU_TestBadBlock(BYTE ucFlashCS, DWORD dwPos, DWORD dwCount, BYTE *lpBuffer);
    virtual int RKU_ReadNewEfuse(USHORT usAddr, USHORT usLen, BYTE *lpBuffer, BYTE ucFlag);
    virtual int RKU_WriteNewEfuse(USHORT usAddr, USHORT usLen, BYTE *lpBuffer, BYTE ucFlag);
    virtual int RKU_WriteSDRam(DWORD dwAddress, BYTE *lpBuffer);
    virtual int RKU_RunSDRam();
    virtual int RKU_ResetPipe(BYTE ucPipe);
    virtual int RKU_MscToRockusb();
    virtual int RKU_UvcToRockusb();
private:
	std::unique_ptr<core::rockchip::UsbTransport> m_usb;
	unsigned char m_pipeBulkIn;
	unsigned char m_pipeBulkOut;
	virtual bool RKU_Write(BYTE *lpBuffer, DWORD dwSize);
	virtual bool RKU_Read(BYTE *lpBuffer, DWORD dwSize);
	bool InitializeUsb(STRUCT_RKDEVICE_DESC devDesc);
	void UninitializeUsb();
	bool RKU_ClearBuffer(CBW &cbw, CSW &csw);
	DWORD RKU_Read_EX(BYTE *lpBuffer, DWORD dwSize);
	void InitializeCBW(PCBW pCBW, USB_OPERATION_CODE code);
	int RandomInteger(int low, int high);
	DWORD MakeCBWTag();
};

#endif