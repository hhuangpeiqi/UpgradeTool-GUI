#include <chrono>
#include <thread>

#include "Upgrade.h"

// ---------------------------------------------------------------------------
// Step set helpers.
// ---------------------------------------------------------------------------
struct STEP_DEF {
	ENUM_UPGRADE_STEP emStep;
	UINT uiMask;
	USHORT usParam;
	BYTE bConditional;
	BYTE bSkipCount;
	BYTE bReset;
};

static void AppendSteps(vector<STRUCT_UPGRADE_STEP> &vecStep, const STEP_DEF *pDef, UINT uiCount)
{
	vecStep.clear();
	for (UINT i = 0; i < uiCount; i++) {
		STRUCT_UPGRADE_STEP step;
		memset(&step, 0, sizeof(step));
		step.uiStep = pDef[i].emStep;
		step.uiMask = pDef[i].uiMask;
		step.usParam = pDef[i].usParam;
		step.bConditional = pDef[i].bConditional;
		step.bSkipCount = pDef[i].bSkipCount;
		step.bReset = pDef[i].bReset;
		vecStep.push_back(step);
	}
}

void DestroyUpgradeSet(vector<STRUCT_UPGRADE_STEP> &vecStep)
{
	vecStep.clear();
}

// CreateDownloadImageStep (mask 0x02): test, check chip, read flash info,
// download the image, then reset.
static const STEP_DEF s_downloadImageStep[] = {
	{ UPGRADE_STEP_TEST_DEVICE,       0x02, 0,    0, 0, 0 },
	{ UPGRADE_STEP_CHECK_CHIP,        0x02, 0,    0, 0, 0 },
	{ UPGRADE_STEP_GET_FLASH_INFO,    0x02, 0,    0, 0, 0 },
	{ UPGRADE_STEP_DOWNLOAD_IMAGE,    0x02, 0,    0, 0, 0 },
	{ UPGRADE_STEP_RESET_DEVICE,      0x02, 0x100,0, 0, 0 },
};

// CreateDownloadIDBStep: wait for loader, then test/check/read, prepare + download
// the IDB, reset.
static const STEP_DEF s_downloadIdbStep[] = {
	{ UPGRADE_STEP_DOWNLOAD_BOOT,     0x01, 0x1,  0, 0, 0 },
	{ UPGRADE_STEP_WAIT_MASKROM,      0x01, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_TEST_DEVICE,       0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_GET_NEW_DISK_SIZE, 0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_CHECK_CHIP,        0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_GET_FLASH_INFO,    0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_PREPARE_IDB,       0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_DOWNLOAD_IDB,      0x03, 0,    0, 0, 1 },
	{ UPGRADE_STEP_RESET_DEVICE,      0x03, 0x100,0, 0, 0 },
};

// CreateUpgradeStep (uf): the IDB phases plus the image download phase. The
// Download IDB step is conditional with a 3-step skip.
static const STEP_DEF s_upgradeStep[] = {
	{ UPGRADE_STEP_DOWNLOAD_BOOT,     0x01, 0x1,  0, 0, 0 },
	{ UPGRADE_STEP_WAIT_MASKROM,      0x01, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_TEST_DEVICE,       0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_GET_NEW_DISK_SIZE, 0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_CHECK_CHIP,        0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_GET_FLASH_INFO,    0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_PREPARE_IDB,       0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_DOWNLOAD_IDB,      0x03, 0,    1, 3, 1 },
	{ UPGRADE_STEP_RESET_DEVICE,      0x03, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_WAIT_LOADER,       0x03, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_TEST_DEVICE,       0x02, 0,    0, 0, 0 },
	{ UPGRADE_STEP_DOWNLOAD_IMAGE,    0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_RESET_DEVICE,      0x03, 0x100,0, 0, 0 },
};

// CreateEraseAllBlocksStep (ef).
static const STEP_DEF s_eraseAllStep[] = {
	{ UPGRADE_STEP_DOWNLOAD_BOOT,     0x01, 0x1,  0, 0, 0 },
	{ UPGRADE_STEP_WAIT_MASKROM,      0x01, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_TEST_DEVICE,       0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_GET_FLASH_INFO,    0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_GET_NEW_DISK_SIZE, 0x03, 0,    1, 9, 0 },
	{ UPGRADE_STEP_PREPARE_IDB,       0x03, 0,    1, 8, 0 },
	{ UPGRADE_STEP_ERASE_IDB,         0x03, 0,    0, 0, 1 },
	{ UPGRADE_STEP_GET_BLOCK_STATE,   0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_RESET_DEVICE,      0x03, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_WAIT_MASKROM,      0x03, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_DOWNLOAD_BOOT,     0x01, 0x1,  0, 0, 0 },
	{ UPGRADE_STEP_WAIT_MASKROM,      0x01, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_TEST_DEVICE,       0x01, 0,    0, 0, 0 },
	{ UPGRADE_STEP_GET_FLASH_INFO,    0x01, 0,    0, 0, 0 },
	{ UPGRADE_STEP_ERASE_FLASH,       0x01, 0,    0, 0, 1 },
	{ UPGRADE_STEP_GET_BLOCK_STATE,   0x01, 0,    0, 0, 0 },
	{ UPGRADE_STEP_RESET_DEVICE,      0x01, 0x100,0, 0, 0 },
};

// CreateRestoreStep: the erase-all blocks sequence followed by the download IDB
// and image sequences, so the three workflows can be chained.
static const STEP_DEF s_restoreStep[] = {
	{ UPGRADE_STEP_DOWNLOAD_BOOT,     0x01, 0x1,  0, 0, 0 },
	{ UPGRADE_STEP_WAIT_MASKROM,      0x01, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_TEST_DEVICE,       0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_GET_FLASH_INFO,    0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_GET_NEW_DISK_SIZE, 0x03, 0,    1, 9, 0 },
	{ UPGRADE_STEP_PREPARE_IDB,       0x03, 0,    1, 8, 0 },
	{ UPGRADE_STEP_ERASE_IDB,         0x03, 0,    0, 0, 1 },
	{ UPGRADE_STEP_GET_BLOCK_STATE,   0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_RESET_DEVICE,      0x03, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_WAIT_MASKROM,      0x03, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_DOWNLOAD_BOOT,     0x01, 0x1,  0, 0, 0 },
	{ UPGRADE_STEP_WAIT_MASKROM,      0x01, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_TEST_DEVICE,       0x01, 0,    0, 0, 0 },
	{ UPGRADE_STEP_GET_FLASH_INFO,    0x01, 0,    0, 0, 0 },
	{ UPGRADE_STEP_ERASE_FLASH,       0x01, 0,    0, 0, 1 },
	{ UPGRADE_STEP_GET_BLOCK_STATE,   0x01, 0,    0, 0, 0 },
	{ UPGRADE_STEP_RESET_DEVICE,      0x01, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_WAIT_MASKROM,      0x01, 0,    0, 0, 0 },
	{ UPGRADE_STEP_DOWNLOAD_BOOT,     0x01, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_WAIT_MASKROM,      0x01, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_TEST_DEVICE,       0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_GET_NEW_DISK_SIZE, 0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_CHECK_CHIP,        0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_GET_FLASH_INFO,    0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_PREPARE_IDB,       0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_DOWNLOAD_IDB,      0x03, 0,    1, 3, 1 },
	{ UPGRADE_STEP_RESET_DEVICE,      0x03, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_WAIT_LOADER,       0x03, 0x100,0, 0, 0 },
	{ UPGRADE_STEP_TEST_DEVICE,       0x02, 0,    0, 0, 0 },
	{ UPGRADE_STEP_DOWNLOAD_IMAGE,    0x03, 0,    0, 0, 0 },
	{ UPGRADE_STEP_RESET_DEVICE,      0x03, 0x100,0, 0, 0 },
};

bool CRKUpgrade::CreateDownloadImageStep(ENUM_RKDEVICE_TYPE emDeviceType, vector<STRUCT_UPGRADE_STEP> &vecStep)
{
	(void)emDeviceType;
	AppendSteps(vecStep, s_downloadImageStep, sizeof(s_downloadImageStep) / sizeof(s_downloadImageStep[0]));
	return true;
}

bool CRKUpgrade::CreateDownloadIDBStep(ENUM_RKDEVICE_TYPE emDeviceType, vector<STRUCT_UPGRADE_STEP> &vecStep)
{
	(void)emDeviceType;
	AppendSteps(vecStep, s_downloadIdbStep, sizeof(s_downloadIdbStep) / sizeof(s_downloadIdbStep[0]));
	return true;
}

bool CRKUpgrade::CreateUpgradeStep(ENUM_RKDEVICE_TYPE emDeviceType, vector<STRUCT_UPGRADE_STEP> &vecStep, UINT uiFlag)
{
	(void)emDeviceType;
	(void)uiFlag;
	AppendSteps(vecStep, s_upgradeStep, sizeof(s_upgradeStep) / sizeof(s_upgradeStep[0]));
	return true;
}

bool CRKUpgrade::CreateEraseAllBlocksStep(ENUM_RKDEVICE_TYPE emDeviceType, vector<STRUCT_UPGRADE_STEP> &vecStep)
{
	(void)emDeviceType;
	AppendSteps(vecStep, s_eraseAllStep, sizeof(s_eraseAllStep) / sizeof(s_eraseAllStep[0]));
	return true;
}

bool CRKUpgrade::CreateRestoreStep(ENUM_RKDEVICE_TYPE emDeviceType, vector<STRUCT_UPGRADE_STEP> &vecStep, UINT uiFlag)
{
	(void)emDeviceType;
	(void)uiFlag;
	AppendSteps(vecStep, s_restoreStep, sizeof(s_restoreStep) / sizeof(s_restoreStep[0]));
	return true;
}

// ---------------------------------------------------------------------------
// Object lifecycle / configuration.
// ---------------------------------------------------------------------------
CRKUpgrade::CRKUpgrade(const vector<STRUCT_UPGRADE_STEP> &vecStep, ENUM_WORKFLOW emWorkFlow, bool reboot)
{
	m_pDevice = NULL;
	m_pScan = NULL;
	m_emWorkFlow = emWorkFlow;
	m_pCallBack = NULL;
	m_vecStep = vecStep;
	if (!reboot && !m_vecStep.empty() && m_vecStep.back().uiStep == UPGRADE_STEP_RESET_DEVICE)
		m_vecStep.pop_back();
}

CRKUpgrade::~CRKUpgrade()
{
	// The device and scan objects are owned by the caller (the application frees the
	// device through CDeviceFactory), so they are only detached here.
	m_pDevice = NULL;
	m_pScan = NULL;
	m_vecStep.clear();
}

void CRKUpgrade::SetDeviceObject(void *pDevice)
{
	if (pDevice)
		m_pDevice = (CRKDevice *)pDevice;
}

void CRKUpgrade::SetScanObject(void *pScan)
{
	if (pScan)
		m_pScan = (CRKScan *)pScan;
}

void CRKUpgrade::SetCallBackPointer(UpgradePromptCB pCallBack)
{
	if (pCallBack)
		m_pCallBack = pCallBack;
}

bool CRKUpgrade::SetObject(CRKDevice *pDevice, CRKScan *pScan)
{
	if (!pDevice)
		return false;
	if (!pScan)
		return false;
	m_pDevice = pDevice;
	m_pDevice->SetWorkFlow(m_emWorkFlow);
	m_pScan = pScan;
	return true;
}

bool CRKUpgrade::SetStep(ENUM_UPGRADE_STEP emStep, UINT uiMask)
{
	for (size_t i = 0; i < m_vecStep.size(); i++) {
		if ((ENUM_UPGRADE_STEP)m_vecStep[i].uiStep == emStep) {
			m_vecStep[i].uiMask = uiMask;
			return true;
		}
	}
	return false;
}

bool CRKUpgrade::RemoveStep(ENUM_UPGRADE_STEP emStep)
{
	for (vector<STRUCT_UPGRADE_STEP>::iterator it = m_vecStep.begin(); it != m_vecStep.end(); ++it) {
		if ((ENUM_UPGRADE_STEP)it->uiStep == emStep) {
			m_vecStep.erase(it);
			return true;
		}
	}
	return false;
}

bool CRKUpgrade::InitializeStep(const vector<STRUCT_UPGRADE_STEP> &vecStep, ENUM_WORKFLOW emWorkFlow)
{
	m_vecStep.assign(vecStep.begin(), vecStep.end());
	m_emWorkFlow = emWorkFlow;
	return true;
}

bool CRKUpgrade::Run()
{
	if (!m_pDevice)
		return false;

	UINT uiSkip = 0;
	bool bCondition = false;
	bool bLastOk = false;

	for (size_t i = 0; i < m_vecStep.size(); i++) {
		if (m_pDevice->IsAborted())
			return false;
		if (uiSkip) {
			uiSkip--;
			continue;
		}
		const STRUCT_UPGRADE_STEP &step = m_vecStep[i];
		// Step masks describe the live USB mode, refreshed after re-enumeration.
		UINT uiUsbType = (UINT)m_pDevice->GetUsbType();
		if ((step.uiMask & uiUsbType) != uiUsbType)
			continue;

		bLastOk = RunStep(step, bCondition);

		if (!bLastOk) {
			if (step.bReset) {
				CRKAndroidDevice *pAndroid = dynamic_cast<CRKAndroidDevice *>(m_pDevice);
				if (pAndroid)
					pAndroid->BufferWriteBack();
			}
			return false;
		}
		if (!step.bConditional || !bCondition)
			continue;
		uiSkip = step.bSkipCount;
	}
	return bLastOk;
}

void CRKUpgrade::Prompt(ENUM_UPGRADE_PROMPT emPrompt)
{
	if (m_pCallBack)
		m_pCallBack(m_pDevice->GetLocationID(), emPrompt, 0);
}

bool CRKUpgrade::RunStep(const STRUCT_UPGRADE_STEP &step, bool &bCondition)
{
	CRKAndroidDevice *pAndroid = dynamic_cast<CRKAndroidDevice *>(m_pDevice);
	STRUCT_RKDEVICE_DESC desc;
	memset(&desc, 0, sizeof(desc));
	desc.uiLocationID = m_pDevice->GetLocationID();
	desc.usVid = m_pDevice->GetVendorID();
	desc.usPid = m_pDevice->GetProductID();
	desc.usbcdUsb = m_pDevice->GetBcdUsb();
	desc.emUsbType = m_pDevice->GetUsbType();
	desc.emDeviceType = m_pDevice->GetDeviceType();
	int iRet;

	switch ((ENUM_UPGRADE_STEP)step.uiStep) {
	case UPGRADE_STEP_DOWNLOAD_BOOT:
		Prompt(PROMPT_DOWNLOAD_BOOT_START);
		iRet = m_pDevice->DownloadBoot();
		if (iRet != 0) { Prompt(PROMPT_DOWNLOAD_BOOT_FAIL); return false; }
		Prompt(PROMPT_DOWNLOAD_BOOT_SUCCESS);
		return true;

	case UPGRADE_STEP_DOWNLOAD_IDB:
		bCondition = false;
		Prompt(PROMPT_DOWNLOAD_IDB_START);
		if (!pAndroid || !pAndroid->DownloadIDBlock(bCondition)) { Prompt(PROMPT_DOWNLOAD_IDB_FAIL); return false; }
		Prompt(PROMPT_DOWNLOAD_IDB_SUCCESS);
		return true;

	case UPGRADE_STEP_DOWNLOAD_IMAGE:
		Prompt(PROMPT_DOWNLOAD_FIRMWARE_START);
		if (!pAndroid || pAndroid->DownloadImage() != 0) { Prompt(PROMPT_DOWNLOAD_FIRMWARE_FAIL); return false; }
		Prompt(PROMPT_DOWNLOAD_FIRMWARE_SUCCESS);
		return true;

	case UPGRADE_STEP_TEST_DEVICE:
		Prompt(PROMPT_TEST_DEVICE_START);
		if (!m_pDevice->TestDevice()) { Prompt(PROMPT_TEST_DEVICE_FAIL); return false; }
		Prompt(PROMPT_TEST_DEVICE_SUCCESS);
		return true;

	case UPGRADE_STEP_RESET_DEVICE:
		Prompt(PROMPT_RESET_DEVICE_START);
		if (!m_pDevice->ResetDevice()) { Prompt(PROMPT_RESET_DEVICE_FAIL); return false; }
		Prompt(PROMPT_RESET_DEVICE_SUCCESS);
		return true;

	case UPGRADE_STEP_WAIT_LOADER:
		if (!m_pScan) return false;
		Prompt(PROMPT_WAIT_LOADER_START);
		if (!m_pScan->Wait(desc, RKUSB_LOADER)) { Prompt(PROMPT_WAIT_LOADER_FAIL); return false; }
		// The device re-enumerated: rebuild the comm for the new USB address.
		if (!m_pDevice->RefreshComm(desc)) { Prompt(PROMPT_WAIT_LOADER_FAIL); return false; }
		Prompt(PROMPT_WAIT_LOADER_SUCCESS);
		return true;

	case UPGRADE_STEP_WAIT_MASKROM:
		if (!m_pScan) return false;
		Prompt(PROMPT_WAIT_MASKROM_START);
		if (!m_pScan->Wait(desc, RKUSB_MASKROM)) { Prompt(PROMPT_WAIT_MASKROM_FAIL); return false; }
		if (!m_pDevice->RefreshComm(desc)) { Prompt(PROMPT_WAIT_MASKROM_FAIL); return false; }
		Prompt(PROMPT_WAIT_MASKROM_SUCCESS);
		return true;

	case UPGRADE_STEP_ERASE_IDB:
		Prompt(PROMPT_ERASE_IDB_START);
		if (!pAndroid || !pAndroid->EraseIDB()) { Prompt(PROMPT_ERASE_IDB_FAIL); return false; }
		Prompt(PROMPT_ERASE_IDB_SUCCESS);
		return true;

	case UPGRADE_STEP_SWITCH_MSC:
		Prompt(PROMPT_SWITCH_MSC_START);
		if (!m_pDevice->SendExtCommand(0, 0xfffffffe, NULL, NULL, 0)) { Prompt(PROMPT_SWITCH_MSC_FAIL); return false; }
		Prompt(PROMPT_SWITCH_MSC_SUCCESS);
		return true;

	case UPGRADE_STEP_CHECK_CHIP:
		Prompt(PROMPT_CHECK_CHIP_START);
		if (!m_pDevice->CheckChip()) { Prompt(PROMPT_CHECK_CHIP_FAIL); return false; }
		Prompt(PROMPT_CHECK_CHIP_SUCCESS);
		return true;

	case UPGRADE_STEP_PREPARE_IDB:
	{
		Prompt(PROMPT_PREPARE_IDB_START);
		bool bIdb = false;
		if (!pAndroid || !pAndroid->PrepareIDB(bIdb)) { Prompt(PROMPT_PREPARE_IDB_FAIL); return false; }
		Prompt(PROMPT_PREPARE_IDB_SUCCESS);
		bCondition = false;
		return true;
	}

	case UPGRADE_STEP_ERASE_FLASH:
		Prompt(PROMPT_ERASE_FLASH_START);
		if (m_pDevice->EraseAllBlocks() != ERR_SUCCESS) { Prompt(PROMPT_ERASE_FLASH_FAIL); return false; }
		Prompt(PROMPT_ERASE_FLASH_SUCCESS);
		return true;

	case UPGRADE_STEP_GET_FLASH_INFO:
		Prompt(PROMPT_GET_FLASH_INFO_START);
		if (!m_pDevice->GetFlashInfo()) { Prompt(PROMPT_GET_FLASH_INFO_FAIL); return false; }
		Prompt(PROMPT_GET_FLASH_INFO_SUCCESS);
		return true;

	case UPGRADE_STEP_GET_BLOCK_STATE:
		if (!pAndroid)
			return false;
		return pAndroid->BufferWriteBack();

	case UPGRADE_STEP_LOWER_FORMAT:
		bCondition = false;
		Prompt(PROMPT_LOWER_FORMAT_START);
		if (!m_pDevice->LowerFormatDevice(bCondition)) { Prompt(PROMPT_LOWER_FORMAT_FAIL); return false; }
		Prompt(PROMPT_LOWER_FORMAT_SUCCESS);
		return true;

	case UPGRADE_STEP_SET_RESET_FLAG:
		if (!m_pDevice->SetResetFlag()) {
			Prompt((ENUM_UPGRADE_PROMPT)0x74);
			return false;
		}
		return true;

	case UPGRADE_STEP_RESET_DEVICE_EXT:
		Prompt((ENUM_UPGRADE_PROMPT)0x7f);
		std::this_thread::sleep_for(std::chrono::seconds(1));
		if (!m_pDevice->SendExtCommand(0, 0xfffffffd, NULL, NULL, 0)) {
			Prompt((ENUM_UPGRADE_PROMPT)0x80);
			return false;
		}
		return true;

	case UPGRADE_STEP_GET_NEW_DISK_SIZE:
		m_pDevice->ReadCapability();
		bCondition = false;
		return true;

	default:
		return false;
	}
}
