#ifndef GUI_QT_OBSERVER_H
#define GUI_QT_OBSERVER_H

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "domain/UpgradeObserver.h"
#include "platform/LogService.h"

namespace gui {

class QtObserver : public QObject, public core::IUpgradeObserver
{
	Q_OBJECT
public:
	explicit QtObserver(platform::LogService &log, QObject *parent = nullptr);

	virtual void OnOutput(core::LogLevel emLevel, const std::string &strText);
    void OnMessage(core::LogLevel,const core::Message &) override;
    void OnStageMessage(const core::Message &,uint64_t,uint64_t,int) override;
	virtual void OnProgress(core::ProgressPrompt emPrompt, long long llTotal,
	                        long long llCurrent, core::CallStep emCall);
	void OnTransferProgress(core::TransferStage stage, long long total, long long current) override;
	virtual void OnUpgradePrompt(core::UpgradePrompt emPrompt, unsigned int uiParam);
	virtual int OnSelectStorage(const std::vector<std::string> &vecOptions, int iCurrent);
	void OnStageProgress(const std::string &, uint64_t, uint64_t, int) override;
	virtual void OnResult(const core::OperationResult &result);

	void SetSelectResult(int iResult);
	void CancelSelection();
	void BeginSelectionSession();

signals:
    void stageProgressReceived(const QString &stage, long long total, long long current, int overallPercent);
	void progressReceived(const QString &strPrompt, long long llTotal,
	                      long long llCurrent, int iCall);
	void upgradePromptReceived(int prompt);
	void storageSelectionRequested(const QStringList &strOptions, int iCurrent);
	void resultReceived(const core::OperationResult &result);

private:
    platform::LogService &m_log;
    std::vector<std::string> m_loggedErrors;
    std::string m_lastStage;
	int m_iSelectResult;
	bool m_bSelectActive;
	QMutex m_selectMutex;
	QWaitCondition m_selectReady;
	bool m_selectionCancelled = false;
};

}

#endif
