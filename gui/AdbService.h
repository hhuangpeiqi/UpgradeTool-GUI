#pragma once
#include "gui/ProcessAdapter.h"
#include "gui/AdbTiming.h"
#include "domain/Operation.h"
#include <QElapsedTimer>
#include <QSet>
#include <QTimer>
#include <deque>
#include <functional>
#include <mutex>

namespace gui {
struct AdbDevice {
    QString serial, state, usb;
    bool operator==(const AdbDevice &) const = default;
    bool authorized() const { return state == "device"; }
};
struct AdbCommandResult {
    bool started = false, normalExit = false, timedOut = false, cancelled = false;
    int exitCode = -1;
    QByteArray output;
    QString error;
    bool succeeded() const { return started && normalExit && exitCode == 0 && !timedOut && !cancelled; }
};

// Owns the shared ADB monitor and serializes client commands. Never retries a command.
class AdbService : public QObject {
    Q_OBJECT
public:
    explicit AdbService(QObject *parent = nullptr, platform::ProcessFactory factory = platform::MakeAdbProcess,
                        AdbPollTiming timing = {});
    ~AdbService() override;
    QString AdbPath() const;
    QString RecoveryMessage() const;
    QStringList Devices() const { return m_strDevices; }
    QVector<AdbDevice> snapshot() const;
    static QVector<AdbDevice> ParseDevices(const QByteArray &);
    static QStringList AuthorizedDevices(const QVector<AdbDevice> &);
    void Start();
    void Stop();
    void Resume();
    void setPaused(bool paused) { m_paused = paused; if (!paused) SchedulePoll(0); }
    quint64 execute(const QString &, const QStringList &, core::Cancellation, int timeoutMs);
    // Worker threads only; completion never requires blocking the GUI thread.
    AdbCommandResult executeBlocking(const QString &, const QStringList &, const core::Cancellation &, int timeoutMs);
    // Standalone transport/test entrypoint, sharing parsing and command result semantics.
    static AdbCommandResult RunCommand(const QString &, const QStringList &, const core::Cancellation &, int timeoutMs);
signals:
    void snapshotChanged();
    void devicesChanged(const QStringList &);
    void availabilityChanged(bool, const QString &);
    void recoveryStateChanged(bool, const QString &);
    void diagnosticMessage(const QString &);
    void traceMessage(const QString &);
    void commandFinished(quint64, const gui::AdbCommandResult &);
private:
    struct Command {
        quint64 id;
        QString executable;
        QStringList args;
        core::Cancellation cancel;
        int timeoutMs;
        std::function<void(AdbCommandResult)> complete;
    };
    void Enqueue(Command);
    void StartNextCommand();
    void FinishCommand(AdbCommandResult);
    void CancelCommands();
    void UpdateDevices(const QByteArray &);
    void RunPoll();
    Q_SLOT void onPollTick();
    void onPollFinished(int, bool);
    void onPollError(const QString &);
    Q_SLOT void onPollKillTimeout();
    void UpdateAvailability(bool, const QString &);
    void PollFailed(const QString &);
    void ClearPollDevices();
    void SetRecovery(bool, bool starting = false);
    void SchedulePoll(int);
    void TracePoll(const QString &, const QByteArray & = {}, const QString & = {});
    ProcessAdapter *m_pPollProc, *m_commandProc;
    QTimer *m_pPollTimer, *m_pPollKill;
    QTimer m_commandTimer;
    QElapsedTimer m_commandElapsed;
    std::deque<Command> m_commands;
    std::unique_ptr<Command> m_activeCommand;
    quint64 m_commandSequence = 0;
    mutable std::mutex m_snapshotMutex;
    QVector<AdbDevice> m_snapshot;
    QStringList m_strDevices;
    QString m_availabilityMessage, m_pollPath, m_pollSource;
    QElapsedTimer m_pollElapsed, m_pollRuntime, m_retryElapsed;
    quint64 m_pollSequence = 0;
    bool m_tracePoll = false;
    QSet<QString> m_versionChecked;
    enum class PollCommand { Version, StartServer, Devices };
    PollCommand m_pollCommand = PollCommand::Version;
    AdbPollTiming m_pollTiming;
    int m_retryDelayMs = 0, m_failureCount = 0;
    qint64 m_spawnElapsedMs = 0;
    bool m_serverReady = false, m_pollActive = false, m_pollCancelled = false, m_pollSlow = false;
    bool m_recovering = false, m_recoveryStarting = false, m_pollTimedOut = false;
    bool m_bAvailable = false, m_bAvailabilityKnown = false, m_stopped = false, m_paused = false;
};
}
