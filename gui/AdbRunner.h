#ifndef GUI_ADB_RUNNER_H
#define GUI_ADB_RUNNER_H

#include <QElapsedTimer>
#include <QObject>
#include "gui/AdbService.h"
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
    bool loader = false;
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

	QStringList Devices() const;
    AdbService *service() const { return m_service; }

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

  private:
    void onRebootFinished(int, bool);
    void CheckTargetMode();
    void StartReboot(const QString &);
    void BeginSwitch();
    void StartCommand(const QStringList &);
    void Complete(bool, const QString &);
    void HandleSwitchCommand(int, bool, const QString &);
    AdbService *m_service;
    quint64 m_commandId = 0;
    core::Cancellation m_commandCancel;
    QByteArray m_commandOutput;
    bool m_bRunning = false, m_bAwaitingChoice = false, m_stopped = false;
    enum class Stage { Idle, Listing, Probing, Rebooting, Waiting };
    Stage m_stage = Stage::Idle;
    bool m_felSwitch = false;
    FelMethod m_felMethod = FelMethod::Auto;
    QString m_serial, m_commandError;
    AdbUsbDevice m_target;
    UsbScanner m_usbScanner;
    QTimer *m_modeTimer;
    QElapsedTimer m_modeElapsed;
    int m_commandTimeoutMs, m_modeTimeoutMs;
};
} // namespace gui
#endif
