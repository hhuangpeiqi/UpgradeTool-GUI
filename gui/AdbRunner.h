#ifndef GUI_ADB_RUNNER_H
#define GUI_ADB_RUNNER_H

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include "gui/AdbService.h"
#include "gui/AdbTiming.h"
#include "gui/AllwinnerSwitchMonitor.h"
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

class QTimer;

namespace gui
{

struct AdbUsbDevice {
	int bus = 0;
	QVector<int> ports;
	QString serial;
    QString location;
    bool loader = false;
    int accessError = 0;
    QString accessReason;
};

class AdbRunner : public QObject
{
	Q_OBJECT
  public:
	explicit AdbRunner(QObject *parent = NULL);
	~AdbRunner() override;
	using UsbScanner = std::function<QVector<AdbUsbDevice>(bool readSerial)>;
	AdbRunner(QObject *parent, UsbScanner scanner, int commandTimeoutMs, int modeTimeoutMs,
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
    void FelMonitorReady(quint64 id);
    void FelMonitorFinished(quint64 id, bool ok, const QString &message, const core::DeviceRef &target, gui::ModeSwitchFailure failure = gui::ModeSwitchFailure::Generic);

  signals:
    void felMonitorRequested(const gui::FelSwitchRequest &request);
    void felMonitorCancelled(quint64 id);
    void felTargetReady(const core::DeviceRef &target);
	void finished(bool bOk, const QString &strMessage, bool cancelled = false, gui::ModeSwitchFailure failure = gui::ModeSwitchFailure::Generic);
	void deviceChoiceRequested(const QStringList &strDevices);

	void devicesChanged(const QStringList &strDevices);

	void availabilityChanged(bool bAvailable, const QString &strMessage);
	void recoveryStateChanged(bool recovering, const QString &message);
	void switchProgress(const core::Message &message);
	void diagnosticMessage(const QString &message);
	void traceRecord(const QJsonObject &record);

  private:
    void CheckLoaderMode();
    void StartReboot(const QString &);
    void BeginSwitch();
    void StartCommand(const QStringList &);
    void Complete(bool ok, const core::Message &message, bool cancelled = false, ModeSwitchFailure failure = ModeSwitchFailure::Generic, const QString &displayText = {});
    void HandleSwitchCommand(int, bool, const QString &);
    AdbService *m_service;
    quint64 m_commandId = 0;
    core::Cancellation m_commandCancel;
    bool m_bRunning = false, m_bAwaitingChoice = false, m_stopped = false;
    enum class Stage { Idle, Listing, Probing, Arming, Rebooting, Waiting };
    Stage m_stage = Stage::Idle;
    bool m_felSwitch = false;
    FelMethod m_felMethod = FelMethod::Auto;
    QString m_serial, m_loaderCommandError;
    quint64 m_nextFelAttempt = 0, m_felAttempt = 0;
    core::Cancellation m_felCancel;
    QStringList m_felCommand;
    platform::UsbLocation m_target;
    UsbScanner m_usbScanner;
    QTimer *m_loaderTimer;
    QElapsedTimer m_loaderElapsed;
    int m_commandTimeoutMs, m_modeTimeoutMs;
};
} // namespace gui
#endif
