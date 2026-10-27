#include "gui/QtObserver.h"
#include "gui/MessageDisplay.h"
#include "gui/UpgradePromptMessage.h"

#include <QMetaObject>
#include <algorithm>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "gui/EngineWorker.h"

namespace gui {

QtObserver::QtObserver(platform::LogService &log, QObject *parent)
	: QObject(parent),
      m_log(log),
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
    m_loggedErrors.clear();
    m_lastStage.clear();
	QMutexLocker lock(&m_selectMutex);
	m_selectionCancelled = false;
}

void QtObserver::OnOutput(core::LogLevel emLevel, const std::string &strText)
{
    if (emLevel == core::LogLevel::Error) m_loggedErrors.push_back(QString::fromStdString(strText).trimmed().toStdString());
    m_log.write(emLevel, {}, {{}, strText, {}});
}

void QtObserver::OnMessage(core::LogLevel level, const core::Message &message) {
    if (level == core::LogLevel::Error) m_loggedErrors.push_back(QString::fromStdString(message.English()).trimmed().toStdString());
    m_log.write(level, {}, message);
}
void QtObserver::OnStageMessage(const core::Message &message,uint64_t total,uint64_t done,int overall) {
    if (m_lastStage != message.English()) { m_lastStage = message.English(); OnMessage(core::LogLevel::Info, message); }
    emit stageProgressReceived(Display(message),total,done,overall);
}

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
    bool failed = false;
    const auto message = UpgradePromptMessage(static_cast<int>(prompt), failed);
    if (!message.key.empty()) OnMessage(failed ? core::LogLevel::Error : core::LogLevel::Info, message);
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
    if (m_lastStage != stage) { m_lastStage = stage; OnOutput(core::LogLevel::Info, stage); }
	emit stageProgressReceived(QString::fromStdString(stage), total, current, overallPercent);
}

void QtObserver::OnResult(const core::OperationResult &result)
{
    const auto diagnostic = result.diagnosticMessage.key.empty()
        ? core::Message{{}, result.diagnostic, {}} : result.diagnosticMessage;
    const auto text = QString::fromStdString(diagnostic.English()).trimmed().toStdString();
    const bool alreadyLogged = std::any_of(m_loggedErrors.begin(), m_loggedErrors.end(), [&](const auto &error) {
        // Rockchip's diagnostic is a captured transcript, not a new error event.
        return text == error || (core::ActiveLogContext().source == "Rockchip" && !error.empty() && text.find(error) != std::string::npos);
    });
    if (result.status == core::OperationStatus::Failed && !alreadyLogged && !text.empty())
        OnMessage(core::LogLevel::Error, diagnostic);
    m_log.writef(core::LogLevel::Debug, core::ActiveLogContext().source.c_str(),
        "Result: operation=%d status=%d error=%d", int(result.operation), int(result.status), int(result.error));
    m_log.write(result.status == core::OperationStatus::Failed ? core::LogLevel::Error : core::LogLevel::Info,
                 {}, {"gui::AppController", result.status == core::OperationStatus::Cancelled ? "Cancelled" :
                     result.ok() ? "Step succeeded" : "Step failed", {}});
    m_loggedErrors.clear();
    m_lastStage.clear();
	emit resultReceived(result);
}

}
