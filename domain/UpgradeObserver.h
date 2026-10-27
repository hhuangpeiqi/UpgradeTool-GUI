#ifndef CORE_UPGRADE_OBSERVER_H
#define CORE_UPGRADE_OBSERVER_H
#include <string>
#include <vector>
#include "domain/Progress.h"
#include "domain/Message.h"
#include "domain/LogEntry.h"
#include "domain/OperationResult.h"

namespace core {

enum class TransferStage { Read, Write, Extract };

class IUpgradeObserver
{
public:
	virtual ~IUpgradeObserver() {}
	virtual void OnOutput(LogLevel emLevel, const std::string &strText) = 0;
    virtual void OnMessage(LogLevel level,const Message &message) { OnOutput(level,message.English()); }
    virtual void OnStageMessage(const Message &message,uint64_t total,uint64_t done,int overall=-1) { OnStageProgress(message.English(),total,done,overall); }
	virtual void OnProgress(ProgressPrompt emPrompt, long long llTotal,
	                        long long llCurrent, CallStep emCall) = 0;

	virtual void OnTransferProgress(TransferStage stage, long long total, long long current) = 0;
	virtual void OnUpgradePrompt(UpgradePrompt emPrompt, unsigned int uiParam) = 0;
	virtual int OnSelectStorage(const std::vector<std::string> &vecOptions, int iCurrent) = 0;
	virtual void OnStageProgress(const std::string &stage, uint64_t total, uint64_t current, int overallPercent = -1) { (void)stage; (void)total; (void)current; (void)overallPercent; }
	virtual void OnResult(const OperationResult &result) { (void)result; }
};

}

#endif
