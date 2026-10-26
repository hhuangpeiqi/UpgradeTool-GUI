#ifndef GUI_ADB_RUNNER_H
#define GUI_ADB_RUNNER_H

#include <QElapsedTimer>
#include <QObject>
#include "gui/ProcessAdapter.h"
#include "gui/AdbTiming.h"
#include <QString>
#include <QStringList>
#include <QVector>
#include <QSet>
#include <functional>

class QTimer;

namespace gui
{

struct AdbUsbDevice {
	int bus = 0;
	QVector<int> ports;
	QString serial;
	bool fel = false;
    QString location;
};

class AdbRunner : public QObject
{
	Q_OBJECT
  public:
	explicit AdbRunner(QObject *parent = NULL);
	~AdbRunner() override;
	using UsbScanner = std::function<QVector<AdbUsbDevice>(bool readSerial)>;
	AdbRunner(QObject *parent, UsbScanner scanner, int commandTimeoutMs, int felTimeoutMs,
	          platform::ProcessFactory factory=platform::MakeAdbProcess, AdbPollTiming timing = {});
	enum class FelMethod { Auto = 0, T3Buildroot = 1 };

	QString AdbPath() const;
	QString RecoveryMessage() const;
	void Stop();
	void Resume();

	void Start();

	QStringList Devices() const { return m_strDevices; }

	void SwitchToLoader();
	void SwitchToFel(FelMethod method);

	void ChooseDevice(const QString &strSerial);

	void CancelSwitch();

  signals:
	void finished(bool bOk, const QString &strMessage);
	void deviceChoiceRequested(const QStringList &strDevices);

	void devicesChanged(const QStringList &strDevices);

	void availabilityChanged(bool bAvailable, const QString &strMessage);
	void recoveryStateChanged(bool recovering, const QString &message);
	void switchProgress(const QString &message);
	void diagnosticMessage(const QString &message);
	void traceMessage(const QString &message);

  private slots:
	void onRebootFinished(int iExitCode, bool eStatus);
	void onTimeout();
	void onPollTick();
	void onPollFinished(int iExitCode, bool eStatus);
	void onPollError(const QString &error);
	void onPollKillTimeout();
	void CheckFel();

  private:
	void StartReboot(const QString &strSerial);
	void BeginSwitch();
	void StartCommand(const QStringList &args);
	void Complete(bool ok, const QString &message);
	void HandleFelCommand(int code, bool status, const QString &output);
	void RunPoll();
	void UpdateAvailability(bool bAvailable, const QString &strMessage);
	void PollFailed(const QString &message);
	void ClearPollDevices();
	void SetRecovery(bool recovering, bool starting = false);
	void SchedulePoll(int delayMs);
	void TracePoll(const QString &event, const QByteArray &output = {}, const QString &detail = {});

	ProcessAdapter *m_pProc;
	QTimer *m_pTimeout;

	ProcessAdapter *m_pPollProc;
	QTimer *m_pPollTimer;
	QTimer *m_pPollKill;

	QStringList m_strDevices;
	QString m_availabilityMessage;
	QString m_pollPath;
	QString m_pollSource;
	QElapsedTimer m_pollElapsed;
	quint64 m_pollSequence = 0;
	bool m_tracePoll = false;
	QSet<QString> m_versionChecked;
	enum class PollCommand { Version, StartServer, Devices };
	PollCommand m_pollCommand = PollCommand::Version;
	AdbPollTiming m_pollTiming;
	QElapsedTimer m_pollRuntime;
	QElapsedTimer m_retryElapsed;
	int m_retryDelayMs = 0;
	int m_failureCount = 0;
	qint64 m_spawnElapsedMs = 0;
	bool m_serverReady = false;
	bool m_pollActive = false;
	bool m_pollCancelled = false;
	bool m_pollSlow = false;
	bool m_recovering = false;
	bool m_recoveryStarting = false;
	bool m_pollTimedOut = false;
	bool m_bRunning;
	bool m_bAwaitingChoice;
	bool m_bAvailable;
	bool m_bAvailabilityKnown;
	bool m_stopped = false;
	enum class Stage { Idle, Loader, Listing, Probing, Rebooting, Waiting };
	Stage m_stage = Stage::Idle;
	bool m_felSwitch = false;
	FelMethod m_felMethod = FelMethod::Auto;
	QString m_serial;
	QString m_commandError;
	AdbUsbDevice m_target;
	UsbScanner m_usbScanner;
	QTimer *m_felTimer;
	QElapsedTimer m_felElapsed;
	int m_commandTimeoutMs;
	int m_felTimeoutMs;
};

} // namespace gui

#endif
