#include "gui/QtObserver.h"

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

void QtObserver::OnProgress(ENUM_PROGRESS_PROMPT emPrompt, long long llTotal,
                            long long llCurrent, ENUM_CALL_STEP emCall)
{
	QString strPrompt;
	switch (emPrompt) {
	case TESTDEVICE_PROGRESS:     strPrompt = tr("Test device"); break;
	case DOWNLOADIMAGE_PROGRESS:  strPrompt = tr("Download Image"); break;
	case CHECKIMAGE_PROGRESS:     strPrompt = tr("Verify image"); break;
	case TAGBADBLOCK_PROGRESS:    strPrompt = tr("Mark bad block"); break;
	case TESTBLOCK_PROGRESS:      strPrompt = tr("Test block"); break;
	case ERASEFLASH_PROGRESS:     strPrompt = tr("Erase Flash"); break;
	case ERASESYSTEM_PROGRESS:    strPrompt = tr("Erase system"); break;
	case LOWERFORMAT_PROGRESS:    strPrompt = tr("Low-level format"); break;
	case ERASEUSERDATA_PROGRESS:  strPrompt = tr("Erase user data"); break;
	default:                      strPrompt = tr("Progress"); break;
	}
	emit progressReceived(strPrompt, llTotal, llCurrent, (int)emCall);
}

void QtObserver::OnTransferProgress(core::TransferStage stage, long long total, long long current)
{
	QString label = stage == core::TransferStage::Read ? tr("Read image") :
	                stage == core::TransferStage::Write ? tr("Write image") : tr("Extract Firmware");
	emit progressReceived(label, total, current, current == 0 ? CALL_FIRST : CALL_MIDDLE);
}

void QtObserver::OnUpgradePrompt(ENUM_UPGRADE_PROMPT prompt, unsigned int)
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
