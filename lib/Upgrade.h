#ifndef RKUPGRADE_HEADER
#define RKUPGRADE_HEADER

#include "AndroidDevice.h"
#include "RKScan.h"

class CRKUpgrade
{
public:
	CRKUpgrade(const vector<STRUCT_UPGRADE_STEP> &vecStep, ENUM_WORKFLOW emWorkFlow);
	virtual ~CRKUpgrade();

	void SetDeviceObject(void *pDevice);
	void SetScanObject(void *pScan);
	void SetCallBackPointer(UpgradePromptCB pCallBack);
	bool SetObject(CRKDevice *pDevice, CRKScan *pScan);
	bool SetStep(ENUM_UPGRADE_STEP emStep, UINT uiMask);
	bool RemoveStep(ENUM_UPGRADE_STEP emStep);
	bool InitializeStep(const vector<STRUCT_UPGRADE_STEP> &vecStep, ENUM_WORKFLOW emWorkFlow);
	bool Run();

	// Controls whether steps carrying a non-zero usParam are treated as inactive
	// phase markers (UF/UL) or executed as-is (EF). Defaults to enabled.
	void SetSkipPhaseSteps(bool bEnable) { m_bSkipPhaseSteps = bEnable; }

	static bool CreateDownloadImageStep(ENUM_RKDEVICE_TYPE emDeviceType, vector<STRUCT_UPGRADE_STEP> &vecStep);
	static bool CreateDownloadIDBStep(ENUM_RKDEVICE_TYPE emDeviceType, vector<STRUCT_UPGRADE_STEP> &vecStep);
	static bool CreateUpgradeStep(ENUM_RKDEVICE_TYPE emDeviceType, vector<STRUCT_UPGRADE_STEP> &vecStep, UINT uiFlag);
	static bool CreateEraseAllBlocksStep(ENUM_RKDEVICE_TYPE emDeviceType, vector<STRUCT_UPGRADE_STEP> &vecStep);
	static bool CreateRestoreStep(ENUM_RKDEVICE_TYPE emDeviceType, vector<STRUCT_UPGRADE_STEP> &vecStep, UINT uiFlag);

	UINT GetStepCount() const { return (UINT)m_vecStep.size(); }

private:
	void Prompt(ENUM_UPGRADE_PROMPT emPrompt);
	bool RunStep(const STRUCT_UPGRADE_STEP &step, bool &bCondition);

	CRKDevice     *m_pDevice;
	CRKScan       *m_pScan;
	vector<STRUCT_UPGRADE_STEP> m_vecStep;
	ENUM_WORKFLOW  m_emWorkFlow;
	UpgradePromptCB m_pCallBack;
	bool m_bSkipPhaseSteps;
};

void DestroyUpgradeSet(vector<STRUCT_UPGRADE_STEP> &vecStep);

#endif
