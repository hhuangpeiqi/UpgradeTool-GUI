#include "gui/AdbRunner.h"

#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace gui {

namespace {

const char *kRebootTarget = "loader";
const int kPollIntervalMs = 1000;
const int kListTimeoutMs = 5000;
const int kRebootTimeoutMs = 10000;

QStringList ParseDevices(const QByteArray &data)
{
	QStringList list;
	QString strOut = QString::fromLocal8Bit(data);
	QStringList strLines = strOut.split('\n', Qt::SkipEmptyParts);
	for (int i = 0; i < strLines.size(); i++) {
		QStringList strFields = strLines[i].split(QRegularExpression("\\s+"),
		                                          Qt::SkipEmptyParts);

		if (strFields.size() < 2 || strFields[1] != "device")
			continue;
		list << strFields[0];
	}
	return list;
}

}

AdbRunner::AdbRunner(QObject *parent)
	: QObject(parent),
	  m_bRunning(false),
	  m_bAwaitingChoice(false),
	  m_bAvailable(false),
	  m_bAvailabilityKnown(false)
{
	m_pProc = new QProcess(this);
	m_pProc->setProcessChannelMode(QProcess::MergedChannels);
	connect(m_pProc, SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(onRebootFinished(int, QProcess::ExitStatus)));

	m_pTimeout = new QTimer(this);
	m_pTimeout->setSingleShot(true);
	connect(m_pTimeout, SIGNAL(timeout()), this, SLOT(onTimeout()));

	m_pPollProc = new QProcess(this);
	m_pPollProc->setProcessChannelMode(QProcess::MergedChannels);
	connect(m_pPollProc, SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(onPollFinished(int, QProcess::ExitStatus)));
	connect(m_pPollProc, SIGNAL(errorOccurred(QProcess::ProcessError)),
	        this, SLOT(onPollError(QProcess::ProcessError)));

	m_pPollTimer = new QTimer(this);
	connect(m_pPollTimer, SIGNAL(timeout()), this, SLOT(onPollTick()));

	m_pPollKill = new QTimer(this);
	m_pPollKill->setSingleShot(true);
	connect(m_pPollKill, SIGNAL(timeout()), this, SLOT(onPollKillTimeout()));
}

QString AdbRunner::AdbPath() const
{
	QString strEnv = qEnvironmentVariable("ADB");
	if (!strEnv.isEmpty())
		return strEnv;
	QString strPath = QStandardPaths::findExecutable("adb");
#ifdef Q_OS_MACOS
	if (strPath.isEmpty())
		strPath = QStandardPaths::findExecutable("adb", QStringList()
		                                       << "/opt/homebrew/bin" << "/usr/local/bin");
#endif
	return strPath;
}

bool AdbRunner::IsAvailable() const
{
	return !AdbPath().isEmpty();
}

void AdbRunner::Start()
{
	RunPoll();
	m_pPollTimer->start(kPollIntervalMs);
}

void AdbRunner::UpdateAvailability(bool bAvailable, const QString &strMessage)
{
	if (m_bAvailabilityKnown && m_bAvailable == bAvailable)
		return;
	m_bAvailabilityKnown = true;
	m_bAvailable = bAvailable;
	emit availabilityChanged(m_bAvailable, strMessage);
}

void AdbRunner::RunPoll()
{
	if (m_pPollProc->state() != QProcess::NotRunning)
		return;
	if (m_bRunning || m_bAwaitingChoice)
		return;

	QString strAdb = AdbPath();
	if (strAdb.isEmpty()) {
		UpdateAvailability(false,
			tr("未找到 ADB 可执行文件，请确认已安装 ADB 且在 PATH 中。"));
		return;
	}

	m_pPollProc->start(strAdb, QStringList() << "devices");
	m_pPollKill->start(kListTimeoutMs);
}

void AdbRunner::onPollTick()
{
	RunPoll();
}

void AdbRunner::onPollFinished(int iExitCode, QProcess::ExitStatus eStatus)
{
	m_pPollKill->stop();

	if (eStatus != QProcess::NormalExit || iExitCode != 0)
		return;

	QStringList strDevices = ParseDevices(m_pPollProc->readAll());
	if (strDevices != m_strDevices) {
		m_strDevices = strDevices;
		emit devicesChanged(m_strDevices);
	}
	UpdateAvailability(true, QString());
}

void AdbRunner::onPollError(QProcess::ProcessError eError)
{
	if (eError != QProcess::FailedToStart)
		return;
	m_pPollKill->stop();
	UpdateAvailability(false,
		tr("无法启动 ADB 进程：%1").arg(m_pPollProc->errorString()));
}

void AdbRunner::onPollKillTimeout()
{
	if (m_pPollProc->state() == QProcess::NotRunning)
		return;
	m_pPollProc->kill();
	m_pPollProc->waitForFinished(1000);
}

void AdbRunner::SwitchToLoader()
{
	if (m_bRunning || m_bAwaitingChoice)
		return;

	if (AdbPath().isEmpty()) {
		emit finished(false, tr("未找到 ADB 可执行文件，请确认已安装 ADB 且在 PATH 中。"));
		return;
	}

	QStringList strDevices = m_strDevices;
	if (strDevices.isEmpty()) {
		emit finished(false, tr("未找到处于 ADB 模式的设备。"));
		return;
	}

	if (strDevices.size() == 1) {
		StartReboot(strDevices[0]);
		return;
	}

	m_bAwaitingChoice = true;
	emit deviceChoiceRequested(strDevices);
}

void AdbRunner::ChooseDevice(const QString &strSerial)
{
	if (!m_bAwaitingChoice)
		return;
	m_bAwaitingChoice = false;
	StartReboot(strSerial.trimmed());
}

void AdbRunner::CancelSwitch()
{
	if (!m_bAwaitingChoice)
		return;
	m_bAwaitingChoice = false;
	emit finished(false, tr("已取消切换。"));
}

void AdbRunner::StartReboot(const QString &strSerial)
{
	QStringList strArgs;
	if (!strSerial.isEmpty())
		strArgs << "-s" << strSerial;
	strArgs << "reboot" << kRebootTarget;

	m_bRunning = true;
	m_pProc->start(AdbPath(), strArgs);
	if (!m_pProc->waitForStarted(5000)) {
		m_bRunning = false;
		emit finished(false, tr("无法启动 ADB 进程：%1").arg(m_pProc->errorString()));
		return;
	}
	m_pTimeout->start(kRebootTimeoutMs);
}

void AdbRunner::onRebootFinished(int iExitCode, QProcess::ExitStatus eStatus)
{
	m_pTimeout->stop();
	if (!m_bRunning)
		return;
	m_bRunning = false;

	QString strOut = QString::fromLocal8Bit(m_pProc->readAll()).trimmed();
	if (eStatus == QProcess::NormalExit && iExitCode == 0) {
		emit finished(true, tr("已切换到 Loader 模式。"));
		return;
	}
	if (strOut.isEmpty())
		strOut = tr("ADB 返回 %1").arg(iExitCode);
	emit finished(false, tr("切换到 Loader 失败：%1").arg(strOut));
}

void AdbRunner::onTimeout()
{
	if (!m_bRunning)
		return;
	m_bRunning = false;
	m_pProc->kill();
	emit finished(false, tr("切换到 Loader 超时：ADB 未在预期时间内返回。"));
}

}
