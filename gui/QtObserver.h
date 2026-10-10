#ifndef GUI_QT_OBSERVER_H
#define GUI_QT_OBSERVER_H

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "core/UpgradeObserver.h"

namespace gui {

class QtObserver : public QObject, public core::IUpgradeObserver
{
	Q_OBJECT
public:
	explicit QtObserver(QObject *parent = NULL);

	virtual void OnOutput(core::LogLevel emLevel, const std::string &strText);
	virtual void OnProgress(ENUM_PROGRESS_PROMPT emPrompt, long long llTotal,
	                        long long llCurrent, ENUM_CALL_STEP emCall);
	void OnTransferProgress(core::TransferStage stage, long long total, long long current) override;
	virtual void OnUpgradePrompt(ENUM_UPGRADE_PROMPT emPrompt, unsigned int uiParam);
	virtual int OnSelectStorage(const std::vector<std::string> &vecOptions, int iCurrent);
	void OnStageProgress(const std::string &, uint64_t, uint64_t, int) override;
	virtual void OnResult(const core::OperationResult &result);

	void SetSelectResult(int iResult);
	void CancelSelection();
	void BeginSelectionSession();

signals:
    void stageProgressReceived(const QString &stage, long long total, long long current, int overallPercent);
	void outputReceived(int iLevel, const QString &strText);
	void progressReceived(const QString &strPrompt, long long llTotal,
	                      long long llCurrent, int iCall);
	void upgradePromptReceived(int prompt);
	void storageSelectionRequested(const QStringList &strOptions, int iCurrent);
	void resultReceived(const core::OperationResult &result);

private:
	int m_iSelectResult;
	bool m_bSelectActive;
	QMutex m_selectMutex;
	QWaitCondition m_selectReady;
	bool m_selectionCancelled = false;
};

}

#endif
