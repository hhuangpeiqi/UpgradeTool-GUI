#include "gui/QtObserver.h"
#include "gui/MessageDisplay.h"

#include <QMetaObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "gui/EngineWorker.h"

namespace gui {

QtObserver::QtObserver(QObject *parent)
	: QObject(parent),
	  m_iSelectResult(0),
	  m_bSelectActive(false)
{
}

void QtObserver::SetSelectResult(int result)
{
	QMutexLocker lock(&m_selectMutex);
	m_iSelectResult = result;
	m_bSelectActive = false;
	m_selectReady.wakeAll();
}
void QtObserver::CancelSelection()
{
	QMutexLocker lock(&m_selectMutex);
	m_selectionCancelled = true;
	m_iSelectResult = 0;
	m_bSelectActive = false;
	m_selectReady.wakeAll();
}
void QtObserver::BeginSelectionSession()
{
	QMutexLocker lock(&m_selectMutex);
	m_selectionCancelled = false;
}

void QtObserver::OnOutput(core::LogLevel emLevel, const std::string &strText)
{
	emit outputReceived((int)emLevel, QString::fromStdString(strText));
}

void QtObserver::OnMessage(core::LogLevel level,const core::Message &message) { emit outputReceived(static_cast<int>(level),Display(message)); }
void QtObserver::OnStageMessage(const core::Message &message,uint64_t total,uint64_t done,int overall) { emit stageProgressReceived(Display(message),total,done,overall); }

void QtObserver::OnProgress(core::ProgressPrompt emPrompt, long long llTotal,
                            long long llCurrent, core::CallStep emCall)
{
	QString strPrompt;
	switch (emPrompt) {
	case core::ProgressPrompt::TESTDEVICE_PROGRESS:     strPrompt = tr("Test device"); break;
	case core::ProgressPrompt::DOWNLOADIMAGE_PROGRESS:  strPrompt = tr("Download Image"); break;
	case core::ProgressPrompt::CHECKIMAGE_PROGRESS:     strPrompt = tr("Verify image"); break;
	case core::ProgressPrompt::TAGBADBLOCK_PROGRESS:    strPrompt = tr("Mark bad block"); break;
	case core::ProgressPrompt::TESTBLOCK_PROGRESS:      strPrompt = tr("Test block"); break;
	case core::ProgressPrompt::ERASEFLASH_PROGRESS:     strPrompt = tr("Erase Flash"); break;
	case core::ProgressPrompt::ERASESYSTEM_PROGRESS:    strPrompt = tr("Erase system"); break;
	case core::ProgressPrompt::LOWERFORMAT_PROGRESS:    strPrompt = tr("Low-level format"); break;
	case core::ProgressPrompt::ERASEUSERDATA_PROGRESS:  strPrompt = tr("Erase user data"); break;
	default:                      strPrompt = tr("Progress"); break;
	}
	emit progressReceived(strPrompt, llTotal, llCurrent, (int)emCall);
}

void QtObserver::OnTransferProgress(core::TransferStage stage, long long total, long long current)
{
	QString label = stage == core::TransferStage::Read ? tr("Read image") :
	                stage == core::TransferStage::Write ? tr("Write image") : tr("Extract Firmware");
	emit progressReceived(label, total, current, current == 0 ? static_cast<int>(core::CallStep::CALL_FIRST) : static_cast<int>(core::CallStep::CALL_MIDDLE));
}

void QtObserver::OnUpgradePrompt(core::UpgradePrompt prompt, unsigned int)
{
	emit upgradePromptReceived(static_cast<int>(prompt));
}

int QtObserver::OnSelectStorage(const std::vector<std::string> &options, int current)
{
	QStringList labels;
	for (const auto &option : options) labels << QString::fromStdString(option);
	QMutexLocker lock(&m_selectMutex);
	if (m_selectionCancelled) return 0;
	m_iSelectResult = 0;
	m_bSelectActive = true;
	emit storageSelectionRequested(labels, current);
	while (m_bSelectActive && !m_selectionCancelled) m_selectReady.wait(&m_selectMutex);
	return m_iSelectResult;
}

void QtObserver::OnStageProgress(const std::string &stage, uint64_t total, uint64_t current, int overallPercent)
{
	emit stageProgressReceived(QString::fromStdString(stage), total, current, overallPercent);
}

void QtObserver::OnResult(const core::OperationResult &result)
{
	emit resultReceived(result);
}

}
