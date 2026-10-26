#include "gui/backend/TaskController.h"
#include "domain/Progress.h"
#include <QCoreApplication>
namespace gui {
namespace {
QString AppTr(const char *text) { return QCoreApplication::translate("gui::AppController",text); }
QString FormatRate(double dPerSec)
{
	if (dPerSec >= 1024.0 * 1024.0)
		return QString("%1M").arg(dPerSec / (1024.0 * 1024.0), 0, 'f', 1);
	if (dPerSec >= 1024.0)
		return QString("%1K").arg(dPerSec / 1024.0, 0, 'f', 1);
	return QString("%1").arg(dPerSec, 0, 'f', 0);
}

QString FormatEta(double dSeconds)
{
	int iSec = (int)(dSeconds + 0.5);
	if (iSec < 0)
		iSec = 0;
	if (iSec >= 3600)
		return QString("%1:%2:%3")
			.arg(iSec / 3600)
			.arg((iSec / 60) % 60, 2, 10, QChar('0'))
			.arg(iSec % 60, 2, 10, QChar('0'));
	return QString("%1:%2").arg(iSec / 60).arg(iSec % 60, 2, 10, QChar('0'));
}


}
void TaskController::setProgress(int iPct, const QString &strText, const QString &stage)
{
	m_progressValue = iPct;
	m_progressText = strText;
    m_progressStage = stage.isEmpty() ? strText : stage;
	emit progressChanged();
}

void TaskController::resetProgress()
{
	m_progressTimer.invalidate();
	m_progressStep.clear();
	m_progressBase = 0;
	m_progressSpeed.clear();
	setProgress(0, QString());
}

void TaskController::onProgress(const QString &strPrompt, long long llTotal,
                               long long llCurrent, int iCall)
{
	if (llTotal <= 0)
		return;
	int iPct = static_cast<int>(static_cast<double>(llCurrent) * 100.0 / llTotal);
	if (iPct > 100)
		iPct = 100;

	if (strPrompt != m_progressStep || iCall == static_cast<int>(core::CallStep::CALL_FIRST)) {
		m_progressStep = strPrompt;
		m_progressBase = llCurrent;
		m_progressTimer.start();
	}

	QString strText = AppTr("%1  %2%").arg(strPrompt).arg(iPct);
	m_progressSpeed.clear();
	if (m_progressTimer.isValid()) {
		long long llDone = llCurrent - m_progressBase;
		qint64 llElapsed = m_progressTimer.elapsed();
		if (llElapsed > 0 && llDone > 0) {
			double dRate = llDone * 1000.0 / (double)llElapsed;
			strText += AppTr("  %1/s").arg(FormatRate(dRate));
			m_progressSpeed = AppTr("%1/s").arg(FormatRate(dRate));
			if (dRate > 0.0 && llCurrent < llTotal) {
				QString strEta = AppTr("  remaining %1").arg(FormatEta((llTotal - llCurrent) / dRate));
				strText += strEta;
				m_progressSpeed += strEta;
			}
		}
	}
	setProgress(iPct, strText, strPrompt);
}

void TaskController::setBusy(bool bBusy)
{
	if (m_busy == bBusy)
		return;
	m_busy = bBusy;
	emit busyChanged();
}
void TaskController::start(core::TaskJob job) {
    m_job=std::move(job); m_cancelRequested=false; setBusy(true); resetProgress();
}
void TaskController::cancel() { m_cancelRequested=true; if(m_job.cancelled) m_job.cancelled->request_stop(); }
void TaskController::finish() { setBusy(false); m_cancelRequested=false; }
void TaskController::onStageProgress(const QString &stage,long long total,long long current,int overall) {
    const bool simulation = !m_job.steps.empty() && core::IsSimulation(m_job.steps.front());
    const QString label = simulation ? AppTr("Simulation: %1").arg(stage) : stage;
    // Stage counters can be sectors, commands or files; they are not byte rates.
    m_progressSpeed.clear();
    m_progressTimer.invalidate();
    setProgress(total > 0 ? qBound(0, static_cast<int>(100.0 * current / total), 100) : -1, label, label);
    if (overall >= 0) setProgress(qBound(0, overall, 100), m_progressText, m_progressStage);
}
}
