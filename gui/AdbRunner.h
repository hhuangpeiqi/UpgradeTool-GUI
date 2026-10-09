#ifndef GUI_ADB_RUNNER_H
#define GUI_ADB_RUNNER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class QTimer;

namespace gui {

class AdbRunner : public QObject
{
	Q_OBJECT
public:
	explicit AdbRunner(QObject *parent = NULL);

	bool IsAvailable() const;
	QString AdbPath() const;

	void Start();

	QStringList Devices() const { return m_strDevices; }

	void SwitchToLoader();

	void ChooseDevice(const QString &strSerial);

	void CancelSwitch();

signals:
	void finished(bool bOk, const QString &strMessage);
	void deviceChoiceRequested(const QStringList &strDevices);

	void devicesChanged(const QStringList &strDevices);

	void availabilityChanged(bool bAvailable, const QString &strMessage);

private slots:
	void onRebootFinished(int iExitCode, QProcess::ExitStatus eStatus);
	void onTimeout();
	void onPollTick();
	void onPollFinished(int iExitCode, QProcess::ExitStatus eStatus);
	void onPollError(QProcess::ProcessError eError);
	void onPollKillTimeout();

private:
	void StartReboot(const QString &strSerial);
	void RunPoll();
	void UpdateAvailability(bool bAvailable, const QString &strMessage);

	QProcess *m_pProc;
	QTimer *m_pTimeout;

	QProcess *m_pPollProc;
	QTimer *m_pPollTimer;
	QTimer *m_pPollKill;

	QStringList m_strDevices;
	bool m_bRunning;
	bool m_bAwaitingChoice;
	bool m_bAvailable;
	bool m_bAvailabilityKnown;
};

}

#endif
