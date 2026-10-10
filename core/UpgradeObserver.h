#ifndef CORE_UPGRADE_OBSERVER_H
#define CORE_UPGRADE_OBSERVER_H
#include <string>
#include <vector>
#include "DefineHeader.h"
#include "core/OperationResult.h"

namespace core {

enum class LogLevel { Info, Warn, Error };
enum class TransferStage { Read, Write, Extract };

class IUpgradeObserver
{
public:
	virtual ~IUpgradeObserver() {}
	virtual void OnOutput(LogLevel emLevel, const std::string &strText) = 0;
	virtual void OnProgress(ENUM_PROGRESS_PROMPT emPrompt, long long llTotal,
	                        long long llCurrent, ENUM_CALL_STEP emCall) = 0;

	virtual void OnTransferProgress(TransferStage stage, long long total, long long current) = 0;
	virtual void OnUpgradePrompt(ENUM_UPGRADE_PROMPT emPrompt, unsigned int uiParam) = 0;
	virtual int OnSelectStorage(const std::vector<std::string> &vecOptions, int iCurrent) = 0;
	virtual void OnStageProgress(const std::string &stage, uint64_t total, uint64_t current, int overallPercent = -1) { (void)stage; (void)total; (void)current; (void)overallPercent; }
	virtual void OnResult(const OperationResult &result) { (void)result; }
};

}

#endif
