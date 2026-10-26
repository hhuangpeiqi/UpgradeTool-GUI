#pragma once
#include "gui/WindowsUsbDriver.h"
#include "gui/backend/Retranslatable.h"
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QVariantList>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>

class QThread;
namespace gui {
struct DriverHooks {
    std::function<DriverScan(bool)> scan = ScanWindowsUsbDrivers;
    std::function<DriverInstallResult(const DriverDevice &, const QVector<DriverDevice> &)> install;
    int verificationTimeoutMs = 15000;
    std::function<DriverInstallResult(const DriverDevice &, const QVector<DriverDevice> &, const DriverProgress &)> installWithProgress;
    std::function<bool()> qualcommInstalled = QualcommDriverInstalled;
    std::function<DriverInstallResult(DriverOperation, const DriverDevice &, const QVector<DriverDevice> &, const DriverProgress &)>
        driverOperation = RunWindowsDriverOperationWithProgress;
};
class DriverController : public QObject, public Retranslatable, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(int attentionCount READ attentionCount NOTIFY devicesChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString progressText READ progressText NOTIFY changed)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY changed)
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY changed)
    Q_PROPERTY(QString summaryLevel READ summaryLevel NOTIFY changed)
    Q_PROPERTY(QVariantList confirmation READ confirmation NOTIFY confirmationChanged)
    Q_PROPERTY(bool hasPending READ hasPending NOTIFY changed)
    Q_PROPERTY(bool waitingForReconnect READ waitingForReconnect NOTIFY changed)
    Q_PROPERTY(bool canResume READ canResume NOTIFY changed)
    Q_PROPERTY(QString pendingText READ pendingText NOTIFY changed)
    Q_PROPERTY(bool canUninstallQualcomm READ canUninstallQualcomm NOTIFY changed)
    Q_PROPERTY(QString confirmationTitle READ confirmationTitle NOTIFY confirmationChanged)
    Q_PROPERTY(QString confirmationText READ confirmationText NOTIFY confirmationChanged)
    Q_PROPERTY(bool canConfirm READ canConfirm NOTIFY confirmationChanged)
public:
    enum class Outcome { Succeeded, PendingReconnect, Failed };
    Q_ENUM(Outcome)
    static constexpr bool hostAvailable() {
#ifdef Q_OS_WIN
        return true;
#else
        return false;
#endif
    }
    explicit DriverController(QObject *parent = nullptr);
    DriverController(DriverHooks hooks, bool available, QObject *parent = nullptr);
    ~DriverController() override;
    bool available() const { return m_available; }
    QVariantList devices() const;
    int attentionCount() const;
    bool checking() const { return m_scanThread != nullptr; }
    bool busy() const { return m_busy; }
    bool locked() const { return m_suspended || m_busy; }
    QString message() const { return m_message; }
    QString progressText() const;
    int elapsedSeconds() const { return m_elapsedSeconds; }
    QString summaryText() const;
    QString summaryLevel() const;
    QVariantList confirmation() const;
    bool hasPending() const { return !m_pending.instance.isEmpty(); }
    bool waitingForReconnect() const { return m_waitingForReconnect; }
    bool canResume() const;
    QString pendingText() const;
    bool canUninstallQualcomm() const;
    QString confirmationTitle() const;
    QString confirmationText() const;
    bool canConfirm() const;
    void setVendor(int);
    void setSuspended(bool);
    void poll(bool probe = false);
    void shutdown();
    bool stopped() const;
    void beginInstall();
    void retranslate() override;
    Q_INVOKABLE void refresh(bool probe = true);
    Q_INVOKABLE void prepareInstall(const QString &instance);
    Q_INVOKABLE void confirmInstall();
    Q_INVOKABLE void resumePending();
    Q_INVOKABLE void forgetPending();
    Q_INVOKABLE void prepareQualcommUninstall();
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
signals:
    void changed();
    void devicesChanged();
    void busyChanged();
    void confirmationChanged();
    void confirmationRequested();
    void maintenanceRequested();
    void finished(Outcome outcome, const QString &message);
    void diagnostic(const QString &message);
private:
    QVariantMap describe(const DriverDevice &) const;
    void scan(bool probe);
    void finish(Outcome outcome, const QString &message);
    void setMessage(const QString &);
    void setPhase(DriverPhase);
    QString phaseText() const;
    void rememberPending();
    void savePending();
    QString reconnectText() const;
    DriverHooks m_hooks;
    bool m_available = false, m_suspended = false, m_stopping = false, m_busy = false;
    bool m_scanAgain = false, m_probeAgain = false, m_installPending = false, m_verifying = false;
    bool m_probeOnPoll = false;
    bool m_detectionFailed = false;
    bool m_qualcommInstalled = false;
    DriverOperation m_operation = DriverOperation::AllwinnerInstall;
    int m_vendor = 0;
    unsigned m_generation = 0;
    QThread *m_scanThread = nullptr, *m_installThread = nullptr;
    QTimer m_debounce, m_verifyTimer, m_progressTimer;
    QElapsedTimer m_lastScan, m_verifyElapsed, m_installElapsed;
    int m_elapsedSeconds = 0;
    DriverPhase m_phase = DriverPhase::Preparing;
    QVector<DriverDevice> m_devices, m_affected;
    DriverDevice m_target, m_pending;
    bool m_rebootRequired = false;
    bool m_waitingForReconnect = false;
    QString m_message;
};
}
