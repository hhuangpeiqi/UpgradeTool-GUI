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
bool UpgradeEngine::DownloadBoot(STRUCT_RKDEVICE_DESC &dev, char *szLoader)
{
	if (!CheckDeviceType(dev, RKUSB_MASKROM))
		return false;
	bool bRet = false, bSuccess = false;
	CRKImage *pImage = new CRKImage();
	int iRet = pImage->ObjectInit(szLoader);
	if (iRet != 0) {
		Emit(LogLevel::Error, "Open loader failed(err=%d),exit download boot!", iRet);
		std::string strNote = GetErrString(pImage->GetError());
		if (!strNote.empty())
			Emit(LogLevel::Info, "%s\r\n", strNote.c_str());
		delete pImage;
		return false;
	}
	CRKBoot *pBoot = (CRKBoot *)pImage->m_bootObject;
	dev.emDeviceType = pBoot->SupportDevice;
	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Creating comm object failed!");
		delete pImage;
		return false;
	}
	CRKDevice *pDevice = CDeviceFactory::CreateDevice(dev);
	pDevice->SetCancellation(m_abort.get_token());
    if(auto band=m_config.Number("force_data_band")) pDevice->SetForceDataBand(*band);
	pDevice->SetObject(pImage, pComm, m_pLog);
	Emit(LogLevel::Info, "Download boot...\r\n");
	int iDlRet = pDevice->DownloadBoot();

	if (iDlRet == 0) {
		bSuccess = true;
		Emit(LogLevel::Info, "Download boot ok.\r\n");
	} else {
		Emit(LogLevel::Info, "Download boot failed!\r\n");
		std::string strNote = GetErrString(pDevice->GetError());
		if (!strNote.empty())
			Emit(LogLevel::Info, "%s\r\n", strNote.c_str());
	}
	CDeviceFactory::FreeDevice(pDevice);
	delete pImage;
	return bSuccess;
}

bool UpgradeEngine::UpgradeLoader(STRUCT_RKDEVICE_DESC &dev, char *szLoader, bool reboot)
{
	if (!CheckDeviceType(dev, RKUSB_MASKROM | RKUSB_LOADER))
		return false;
	bool bRet = false, bSuccess = false;
	CRKImage *pImage = NULL;
	CRKComm *pComm = NULL;
	CRKAndroidDevice *pDevice = NULL;
	CRKUpgrade *pUpgrade = NULL;

	pImage = new CRKImage();
	Emit(LogLevel::Info, "Loading loader...\r\n");
	int iRet = pImage->ObjectInit(szLoader);
	if (iRet != 0) {
		Emit(LogLevel::Error, "Loading loader failed,err=%d!", iRet);
		std::string strNote = GetErrString(pImage->GetError());
		if (!strNote.empty())
			Emit(LogLevel::Info, "%s\r\n", strNote.c_str());
		delete pImage;
		return false;
	}
	CRKBoot *pBoot = (CRKBoot *)pImage->m_bootObject;
	STRUCT_RKTIME tLoader = pBoot->ReleaseTime;
	UINT uiType = (UINT)pBoot->SupportDevice;
	char szType[5];
	szType[0] = (char)((uiType >> 24) & 0xFF);
	szType[1] = (char)((uiType >> 16) & 0xFF);
	szType[2] = (char)((uiType >> 8) & 0xFF);
	szType[3] = (char)(uiType & 0xFF);
	szType[4] = 0;
	bool bTagPrintable = true;
	for (int i = 0; i < 4 && szType[i]; i++)
		if (!isprint((unsigned char)szType[i]))
			bTagPrintable = false;
	char szSupport[16];
	if (bTagPrintable && szType[0])
		snprintf(szSupport, sizeof(szSupport), "RK%s", szType);
	else
		snprintf(szSupport, sizeof(szSupport), "RK%X", uiType);
	Emit(LogLevel::Info, "Support Type:%s\tLoader ver:%x.%02x\tLoader Time:%d-%02d-%02d %02d:%02d:%02d\r\n",
	     szSupport, (pBoot->Version >> 8) & 0xFF, pBoot->Version & 0xFF,
	     tLoader.usYear, tLoader.ucMonth, tLoader.ucDay, tLoader.ucHour, tLoader.ucMinute, tLoader.ucSecond);
	dev.emDeviceType = pBoot->SupportDevice;
	pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Creating comm object failed!");
		goto exit_ul;
	}
	Emit(LogLevel::Info, "Start to upgrade loader...\r\n");
	pDevice = (CRKAndroidDevice *)CDeviceFactory::CreateDevice(dev);
	pDevice->SetCancellation(m_abort.get_token());
    if(auto band=m_config.Number("force_data_band")) pDevice->SetForceDataBand(*band);
	pDevice->SetObject(pImage, pComm, m_pLog);

	{
		std::vector<STRUCT_UPGRADE_STEP> vecStep;
		CRKUpgrade::CreateDownloadIDBStep(dev.emDeviceType, vecStep);
		pUpgrade = new CRKUpgrade(vecStep, WORKFLOW_UPGRADE, reboot);
		pUpgrade->SetObject(pDevice, m_pScan);
		pUpgrade->SetCallBackPointer(PromptThunk);
		bSuccess = pUpgrade->Run();
		if (bSuccess)
			Emit(LogLevel::Info, "Upgrade loader ok.\r\n");
		else
			Emit(LogLevel::Info, "Upgrade loader fail.\r\n");
	}
exit_ul:
	if (pUpgrade) delete pUpgrade;
	if (pImage) delete pImage;
	if (pDevice) { CDeviceFactory::FreeDevice(pDevice); pComm = NULL; }
	if (pComm) delete pComm;
	return bSuccess;
}

bool UpgradeEngine::EraseFlash(STRUCT_RKDEVICE_DESC &dev, char *szLoader)
{
	if (!CheckDeviceType(dev, RKUSB_LOADER | RKUSB_MASKROM))
		return false;
	bool bRet = false, bSuccess = false;
	CRKImage *pImage = new CRKImage();
	Emit(LogLevel::Info, "Loading loader...\r\n");
	int iRet = pImage->ObjectInit(szLoader);
	if (iRet != 0) {
		Emit(LogLevel::Error, "Loading loader failed,err=%d!", iRet);
		delete pImage;
		return false;
	}
	CRKBoot *pBoot = (CRKBoot *)pImage->m_bootObject;
	if (pBoot)
		dev.emDeviceType = pBoot->SupportDevice;

	CRKUsbComm *pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Error, "Creating Comm Object failed!");
		delete pImage;
		return false;
	}
	CRKDevice *pDevice = CDeviceFactory::CreateDevice(dev);
	pDevice->SetCancellation(m_abort.get_token());
    if(auto band=m_config.Number("force_data_band")) pDevice->SetForceDataBand(*band);
	pDevice->SetObject(pImage, pComm, m_pLog);

	std::vector<STRUCT_UPGRADE_STEP> vecStep;
	CRKUpgrade::CreateEraseAllBlocksStep(dev.emDeviceType, vecStep);
	CRKUpgrade *pUpgrade = new CRKUpgrade(vecStep, WORKFLOW_DOWNLOAD_IDB);
	pUpgrade->SetObject(pDevice, m_pScan);
	pUpgrade->SetCallBackPointer(PromptThunk);
	Emit(LogLevel::Info, "Start to erase flash...\r\n");
	bSuccess = pUpgrade->Run();
	delete pUpgrade;

	CDeviceFactory::FreeDevice(pDevice);
	delete pImage;
	return bSuccess;
}

bool UpgradeEngine::UpgradeFirmware(STRUCT_RKDEVICE_DESC &dev, char *szFirmware, bool bNoReset)
{
	if (!CheckDeviceType(dev, RKUSB_MASKROM | RKUSB_LOADER))
		return false;
	bool bRet = false, bSuccess = false;
	CRKAndroidDevice *pDevice = NULL;
	CRKComm *pComm = NULL;
	CRKUpgrade *pUpgrade = NULL;
	CRKImage *pImage = new CRKImage();
	Emit(LogLevel::Info, "Loading firmware...\r\n");
	int iRet = pImage->ObjectInit(szFirmware);
	if (iRet != 0) {
		Emit(LogLevel::Error, "Loading firmware failed!");
		std::string strNote = GetErrString(pImage->GetError());
		if (!strNote.empty())
			Emit(LogLevel::Info, "%s\r\n", strNote.c_str());
		delete pImage;
		return false;
	}
	FILE *file = platform::OpenFile(szFirmware, "rb");
	BYTE hdr[0x40];
	if (!file || fread(hdr, 1, sizeof(hdr), file) != sizeof(hdr)) {
		if (file) fclose(file);
		delete pImage;
		return false;
	}
	UINT ver = *(UINT *)(hdr + 6);
	STRUCT_RKTIME t;
	t.usYear = *(USHORT *)(hdr + 14);
	t.ucMonth = hdr[16]; t.ucDay = hdr[17]; t.ucHour = hdr[18];
	t.ucMinute = hdr[19]; t.ucSecond = hdr[20];
	UINT chip = *(UINT *)(hdr + 21);
	UINT fwOff = *(UINT *)(hdr + 0x21);
	std::string szChip = FirmwareService::ChipName(chip);

	Emit(LogLevel::Info, "Support Type:%s\tFW Ver:%x.%x.%02x\tFW Time:%d-%02d-%02d %02d:%02d:%02d\r\n",
	     szChip.c_str(), (ver >> 24) & 0xFF, (ver >> 16) & 0xFF, (ver >> 8) & 0xFF,
	     t.usYear, t.ucMonth, t.ucDay, t.ucHour, t.ucMinute, t.ucSecond);
	CRKBoot *pBoot = pImage->m_bootObject;
	if (pBoot) {
		STRUCT_RKTIME lt = pBoot->ReleaseTime;
		Emit(LogLevel::Info, "Loader ver:%u.%02u\tLoader Time:%d-%02d-%02d %02d:%02d:%02d\r\n",
		     (pBoot->Version >> 8) & 0xFF, pBoot->Version & 0xFF,
		     lt.usYear, lt.ucMonth, lt.ucDay, lt.ucHour, lt.ucMinute, lt.ucSecond);
	}
	(void)fwOff;
	Emit(LogLevel::Info, "Start to upgrade firmware...\r\n");

	pComm = new CRKUsbComm(dev, m_pLog, bRet);
	if (!bRet) {
		Emit(LogLevel::Info, "Create comm failed!\r\n");
		fclose(file); delete pImage;
		return false;
	}
	pDevice = (CRKAndroidDevice *)CDeviceFactory::CreateDevice(dev);
	pDevice->SetCancellation(m_abort.get_token());
    if(auto band=m_config.Number("force_data_band")) pDevice->SetForceDataBand(*band);
	pDevice->SetObject(pImage, pComm, m_pLog);

	ResetDownloadProgress();
	pDevice->CallBackPointer = ProgressThunk;
	pDevice->LoadFirmwarePackage(szFirmware);

	{
		std::vector<STRUCT_UPGRADE_STEP> vecStep;
		CRKUpgrade::CreateUpgradeStep(dev.emDeviceType, vecStep, 0);
		// Reset through the live connection refreshed by the upgrade workflow.
		pUpgrade = new CRKUpgrade(vecStep, WORKFLOW_UPGRADE, !bNoReset);
		pUpgrade->SetObject(pDevice, m_pScan);
		pUpgrade->SetCallBackPointer(PromptThunk);
		bSuccess = pUpgrade->Run();
		if (bSuccess)
			Emit(LogLevel::Info, "Upgrade firmware ok.\r\n");
	}
	if (pUpgrade) delete pUpgrade;
	fclose(file);
	delete pImage;
	if (pDevice) { CDeviceFactory::FreeDevice(pDevice); pComm = NULL; }
	if (pComm) delete pComm;
	return bSuccess;
}
}
