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
	case TESTDEVICE_PROGRESS:     strPrompt = tr("检测设备"); break;
	case DOWNLOADIMAGE_PROGRESS:  strPrompt = tr("下载镜像"); break;
	case CHECKIMAGE_PROGRESS:     strPrompt = tr("校验镜像"); break;
	case TAGBADBLOCK_PROGRESS:    strPrompt = tr("标记坏块"); break;
	case TESTBLOCK_PROGRESS:      strPrompt = tr("测试块"); break;
	case ERASEFLASH_PROGRESS:     strPrompt = tr("擦除 Flash"); break;
	case ERASESYSTEM_PROGRESS:    strPrompt = tr("擦除系统"); break;
	case LOWERFORMAT_PROGRESS:    strPrompt = tr("低级格式化"); break;
	case ERASEUSERDATA_PROGRESS:  strPrompt = tr("擦除用户数据"); break;
	default:                      strPrompt = tr("进度"); break;
	}
	emit progressReceived(strPrompt, llTotal, llCurrent, (int)emCall);
}

void QtObserver::OnTransferProgress(core::TransferStage stage, long long total, long long current)
{
	QString label = stage == core::TransferStage::Read ? tr("读取镜像") :
	                stage == core::TransferStage::Write ? tr("写入镜像") : tr("解包固件");
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
