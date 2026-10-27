#include "gui/AdbService.h"
#include "gui/AdbExecutable.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <QRegularExpression>
#include <QThread>
#include <algorithm>
#include <future>
#include <thread>

namespace gui {

AdbService::AdbService(QObject *parent, platform::ProcessFactory factory, AdbPollTiming timing)
    : QObject(parent), m_pollTiming(timing)
{
    m_pPollProc = new ProcessAdapter(this, factory);
    m_commandProc = new ProcessAdapter(this, std::move(factory));
    m_pPollTimer = new QTimer(this);
    m_pPollTimer->setSingleShot(true);
    m_pPollKill = new QTimer(this);
    m_pPollKill->setSingleShot(true);
    connect(m_pPollTimer, &QTimer::timeout, this, &AdbService::onPollTick);
    connect(m_pPollKill, &QTimer::timeout, this, &AdbService::onPollKillTimeout);
    connect(m_pPollProc, &ProcessAdapter::finished, this, &AdbService::onPollFinished);
    connect(m_pPollProc, &ProcessAdapter::failed, this, &AdbService::onPollError);
    connect(m_commandProc, &ProcessAdapter::finished, this, [this](int code, bool normal) {
        if (!m_activeCommand) return;
        AdbCommandResult result;
        result.started = true; result.normalExit = normal; result.exitCode = code;
        result.output = m_commandProc->readAll();
        result.cancelled = m_activeCommand->cancel->stop_requested();
        result.timedOut = m_commandElapsed.elapsed() >= m_activeCommand->timeoutMs;
        if (!result.succeeded()) result.error = result.cancelled || result.timedOut
            ? QCoreApplication::translate("gui::AdbRunner", "ADB command cancelled or timed out.")
            : QCoreApplication::translate("gui::AdbRunner", "ADB failed: %1").arg(QString::fromLocal8Bit(result.output));
        FinishCommand(std::move(result));
    });
    connect(m_commandProc, &ProcessAdapter::failed, this, [this](const QString &error) {
        AdbCommandResult result; result.error = error; FinishCommand(std::move(result));
    });
    m_commandTimer.setInterval(10);
    connect(&m_commandTimer, &QTimer::timeout, this, [this] {
        if (m_activeCommand && (m_activeCommand->cancel->stop_requested() ||
            m_commandElapsed.elapsed() >= m_activeCommand->timeoutMs)) m_commandProc->kill();
    });
}

QVector<AdbDevice> AdbService::ParseDevices(const QByteArray &data) {
    QVector<AdbDevice> result;
    for (const auto &line : QString::fromUtf8(data).split('\n')) {
        const auto fields = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (fields.size() < 2 || (fields[1] != "device" && fields[1] != "offline" && fields[1] != "unauthorized")) continue;
        AdbDevice device{fields[0], fields[1], {}};
        for (const auto &field : fields) if (field.startsWith("usb:")) device.usb = field;
        result.append(device);
    }
    return result;
}
QStringList AdbService::AuthorizedDevices(const QVector<AdbDevice> &devices) {
    QStringList result;
    for (const auto &device : devices) if (device.authorized()) result.append(device.serial);
    return result;
}
QVector<AdbDevice> AdbService::snapshot() const {
    std::lock_guard lock(m_snapshotMutex); return m_snapshot;
}
void AdbService::UpdateDevices(const QByteArray &data) {
    const auto devices = ParseDevices(data);
    bool changed;
    { std::lock_guard lock(m_snapshotMutex); changed = m_snapshot != devices; m_snapshot = devices; }
    const auto authorized = AuthorizedDevices(devices);
    if (authorized != m_strDevices) { m_strDevices = authorized; emit devicesChanged(m_strDevices); }
    if (changed) emit snapshotChanged();
}
quint64 AdbService::execute(const QString &exe, const QStringList &args, core::Cancellation cancel, int timeoutMs) {
    const auto id = ++m_commandSequence;
    Enqueue({{}, id, exe, args, std::move(cancel), timeoutMs, {}});
    return id;
}
void AdbService::Enqueue(Command command) {
    command.logContext = m_commandLogContext;
    if (!command.cancel) command.cancel = std::make_shared<std::stop_source>();
    m_commands.push_back(std::move(command));
    // Always defer completion: callers must store the returned command ID first.
    QTimer::singleShot(0, this, &AdbService::StartNextCommand);
}
void AdbService::StartNextCommand() {
    if (m_activeCommand || m_pollActive || m_commands.empty()) return;
    m_activeCommand = std::make_unique<Command>(std::move(m_commands.front()));
    m_commands.pop_front();
    if (m_stopped || m_activeCommand->cancel->stop_requested()) {
        AdbCommandResult result; result.cancelled = true;
        result.error = QCoreApplication::translate("gui::AdbRunner", "ADB command cancelled or timed out."); FinishCommand(std::move(result)); return;
    }
    m_commandElapsed.start();
    m_commandProc->start(m_activeCommand->executable, m_activeCommand->args);
    m_commandTimer.start();
}
void AdbService::FinishCommand(AdbCommandResult result) {
    if (!m_activeCommand) return;
    m_commandTimer.stop();
    auto command = std::move(m_activeCommand);
    if (command->args.value(0) == "devices") {
        if (result.succeeded() && command->executable == AdbPath()) UpdateDevices(result.output);
        else if (!result.cancelled) PollFailed(result.error);
    }
    emit traceRecord(QJsonObject{
        {"log_task", qint64(command->logContext.task)}, {"log_device", QString::fromStdString(command->logContext.device)},
        {"log_level", int(!result.succeeded() && !result.cancelled ? core::LogLevel::Warn : core::LogLevel::Debug)},
        {"event", "command_finished"}, {"command", command->args.join(' ')},
        {"started", result.started}, {"exit_code", result.exitCode},
        {"cancelled", result.cancelled}, {"timed_out", result.timedOut},
        {"elapsed_ms", m_commandElapsed.elapsed()}, {"gui_pid", QCoreApplication::applicationPid()},
        {"path", command->executable}, {"timeout_ms", command->timeoutMs},
        {"output_bytes", qint64(result.output.size())}, {"output_truncated", result.output.size() > 64 * 1024},
        {"output", QString::fromLocal8Bit(result.output.left(64 * 1024))}});
    if (command->complete) command->complete(result);
    emit commandFinished(command->id, result);
    QTimer::singleShot(0, this, &AdbService::StartNextCommand);
    SchedulePoll(0);
}
void AdbService::CancelCommands() {
    if (m_activeCommand) {
        m_activeCommand->cancel->request_stop();
        m_commandProc->kill(); m_commandProc->waitForFinished(1000);
        AdbCommandResult result; result.cancelled = true; FinishCommand(std::move(result));
    }
    while (!m_commands.empty()) {
        auto command = std::move(m_commands.front()); m_commands.pop_front();
        AdbCommandResult result; result.cancelled = true;
        if (command.complete) command.complete(result);
        emit commandFinished(command.id, result);
    }
}
AdbCommandResult AdbService::executeBlocking(const QString &exe, const QStringList &args,
                                           const core::Cancellation &cancel, int timeoutMs) {
    Q_ASSERT(QThread::currentThread() != thread());
    auto promise = std::make_shared<std::promise<AdbCommandResult>>();
    auto future = promise->get_future();
    QMetaObject::invokeMethod(this, [this, exe, args, cancel, timeoutMs, promise] {
        Enqueue({{}, ++m_commandSequence, exe, args, cancel, timeoutMs,
                 [promise](AdbCommandResult result) { promise->set_value(std::move(result)); }});
    }, Qt::QueuedConnection);
    while (future.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready) {
        if (cancel && cancel->stop_requested()) { AdbCommandResult result; result.cancelled = true; return result; }
    }
    return future.get();
}
AdbCommandResult AdbService::RunCommand(const QString &exe, const QStringList &args,
                                      const core::Cancellation &cancel, int timeoutMs) {
    AdbCommandResult result;
    if (cancel && cancel->stop_requested()) { result.cancelled = true; return result; }
    auto child = platform::MakeAdbProcess();
    std::vector<std::string> arguments;
    for (const auto &arg : args) arguments.push_back(arg.toStdString());
    try { child->Start(exe.toStdString(), arguments); result.started = true; }
    catch (const std::exception &error) { result.error = QString::fromUtf8(error.what()); return result; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (child->Running()) {
        child->Poll();
        result.cancelled = cancel && cancel->stop_requested();
        result.timedOut = std::chrono::steady_clock::now() >= deadline;
        if (result.cancelled || result.timedOut) { child->Kill(); break; }
        if (child->Running()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    result.output = QByteArray::fromStdString(child->TakeOutput());
    result.normalExit = child->NormalExit(); result.exitCode = child->ExitCode();
    if (!result.succeeded()) result.error = result.cancelled || result.timedOut
        ? QCoreApplication::translate("gui::AdbRunner", "ADB command cancelled or timed out.") : QCoreApplication::translate("gui::AdbRunner", "ADB failed: %1").arg(QString::fromLocal8Bit(result.output));
    return result;
}
QString AdbService::AdbPath() const
{
	if (m_stopped) return QString();
	return FindAdbExecutable().path;
}

AdbService::~AdbService()
{
	Stop();
}

void AdbService::Stop()
{
	m_stopped = true;
	CancelCommands();
	m_pollCancelled = true;
	m_serverReady = false;
	m_recovering = false;
	m_recoveryStarting = false;

	for (auto *timer : {m_pPollTimer, m_pPollKill}) timer->stop();
	for (auto *process : {m_pPollProc}) {
		if (process->state() != ProcessAdapter::NotRunning) {
			process->kill();
			process->waitForFinished(1000);
		}
	}
}

void AdbService::Start()
{
	if (m_stopped) return;
	RunPoll();
}

void AdbService::Resume()
{
	m_stopped = false;
    m_paused = false;
	m_retryDelayMs = 0;
	m_failureCount = 0;
	ClearPollDevices();
	UpdateAvailability(false, {});
	Start();
}

void AdbService::UpdateAvailability(bool bAvailable, const QString &strMessage)
{
	if (m_bAvailabilityKnown && m_bAvailable == bAvailable && m_availabilityMessage == strMessage)
		return;
	m_bAvailabilityKnown = true;
	m_bAvailable = bAvailable;
	m_availabilityMessage = strMessage;
	emit availabilityChanged(m_bAvailable, strMessage);
}

void AdbService::ClearPollDevices()
{
    bool changed;
    { std::lock_guard lock(m_snapshotMutex); changed = !m_snapshot.isEmpty(); m_snapshot.clear(); }
    if (!m_strDevices.isEmpty()) { m_strDevices.clear(); emit devicesChanged(m_strDevices); }
    if (changed) emit snapshotChanged();
}

void AdbService::SetRecovery(bool recovering, bool starting)
{
	if (m_recovering == recovering && m_recoveryStarting == starting) return;
	m_recovering = recovering;
	m_recoveryStarting = starting;
	emit recoveryStateChanged(recovering, RecoveryMessage());
}

QString AdbService::RecoveryMessage() const
{
	return !m_recovering ? QString() : m_recoveryStarting
	    ? QCoreApplication::translate("gui::AdbRunner", "Starting ADB…") : QCoreApplication::translate("gui::AdbRunner", "ADB connection interrupted; retrying automatically…");
}

void AdbService::SchedulePoll(int delayMs)
{
	if (!m_stopped) m_pPollTimer->start(delayMs);
}

void AdbService::PollFailed(const QString &message)
{
	m_serverReady = false;
	ClearPollDevices();
	SetRecovery(true);
	m_retryDelayMs = m_failureCount == 0 ? m_pollTiming.initialRetryMs
	    : std::min(m_retryDelayMs * 2, m_pollTiming.maximumRetryMs);
	// Only the first error in an outage is surfaced; every attempt is traced on disk.
	if (m_failureCount == 0) UpdateAvailability(false, message);
	if (m_failureCount < 32) ++m_failureCount;
	m_retryElapsed.start();
	SchedulePoll(m_retryDelayMs);
}

void AdbService::TracePoll(core::LogLevel level, const QString &event, const QByteArray &output, const QString &detail)
{
	constexpr int outputLimit = 64 * 1024;
	const QJsonObject record{
	    {"log_level", int(level)}, {"event", event}, {"gui_pid", QCoreApplication::applicationPid()},
	    {"attempt", qint64(m_pollSequence)}, {"path", m_pollPath}, {"source", m_pollSource},
	    {"command", m_pollCommand == PollCommand::Version ? "version"
	        : m_pollCommand == PollCommand::StartServer ? "start-server" : "devices"},
	    {"phase", m_pollCommand == PollCommand::Devices ? "query" : "startup"},
	    {"elapsed_ms", m_pollElapsed.isValid() ? m_pollElapsed.elapsed() : 0},
	    {"runtime_ms", m_pollRuntime.isValid() ? m_pollRuntime.elapsed() : 0},
	    {"spawn_elapsed_ms", m_spawnElapsedMs},
	    {"timeout_ms", m_pollTiming.hardTimeoutMs}, {"slow_query_ms", m_pollTiming.slowQueryMs},
	    {"retry_ms", m_retryDelayMs}, {"failure_count", m_failureCount}, {"detail", detail},
	    {"output_bytes", qint64(output.size())}, {"output_truncated", output.size() > outputLimit},
	    {"output", QString::fromLocal8Bit(output.left(outputLimit))}};
	emit traceRecord(record);
}

void AdbService::RunPoll()
{
	if (m_stopped) return;
	if (m_pollActive || m_pPollProc->state() != ProcessAdapter::NotRunning)
		return;
	if (m_paused || m_activeCommand || !m_commands.empty()) {
		SchedulePoll(m_pollTiming.pollIntervalMs);
		return;
	}

	const auto executable = FindAdbExecutable();
	if (m_pollPath != executable.path) {
		ClearPollDevices();
		m_serverReady = false;
		m_failureCount = 0;
		m_retryDelayMs = 0;
		UpdateAvailability(false, {});
	}
	if (m_retryDelayMs && m_retryElapsed.isValid() && m_retryElapsed.elapsed() < m_retryDelayMs) {
		SchedulePoll(m_retryDelayMs - int(m_retryElapsed.elapsed()));
		return;
	}
	if (executable.path.isEmpty()) {
		m_pollPath.clear();
		m_pollSource.clear();
		if (!m_bAvailabilityKnown || m_availabilityMessage != QCoreApplication::translate("gui::AdbRunner", "ADB executable not found; reinstall the application or install ADB and check PATH / ADB."))
			TracePoll(core::LogLevel::Warn, QStringLiteral("executable_missing"));
		PollFailed(QCoreApplication::translate("gui::AdbRunner", "ADB executable not found; reinstall the application or install ADB and check PATH / ADB."));
		return;
	}

	m_pollPath = executable.path;
	m_pollSource = executable.source;
	m_pollCommand = !m_versionChecked.contains(m_pollPath) ? PollCommand::Version
	    : !m_serverReady ? PollCommand::StartServer : PollCommand::Devices;
	m_pollTimedOut = false;
	m_pollCancelled = false;
	m_pollSlow = false;
	m_pollActive = true;
	++m_pollSequence;
	m_pollElapsed.start();
	m_pollRuntime.invalidate();
	m_spawnElapsedMs = 0;
	m_tracePoll = m_pollCommand != PollCommand::Devices || !m_bAvailabilityKnown || !m_bAvailable;
	if (!m_serverReady) SetRecovery(true, m_failureCount == 0);
	if (m_tracePoll) TracePoll(core::LogLevel::Debug, QStringLiteral("start"));
	const QString command = m_pollCommand == PollCommand::Version ? QStringLiteral("version")
	    : m_pollCommand == PollCommand::StartServer ? QStringLiteral("start-server") : QStringLiteral("devices");
	m_pPollProc->start(m_pollPath, m_pollCommand == PollCommand::Devices ? QStringList{command, "-l"} : QStringList{command});
	m_spawnElapsedMs = m_pollElapsed.elapsed();
	m_pollRuntime.start();
	m_pPollKill->start(m_pollCommand == PollCommand::Devices
	    ? m_pollTiming.slowQueryMs : m_pollTiming.hardTimeoutMs);
}

void AdbService::onPollTick() { RunPoll(); }

void AdbService::onPollFinished(int iExitCode, bool eStatus)
{
	QTimer::singleShot(0, this, &AdbService::StartNextCommand);
	m_pPollKill->stop();
	m_pollActive = false;
	if (m_stopped) return;
	if (m_pollCancelled || FindAdbExecutable().path != m_pollPath) {
		m_serverReady = false;
		SchedulePoll(0);
		return;
	}
	const auto output = m_pPollProc->readAll();
	if (m_pollTimedOut) {
		PollFailed(QCoreApplication::translate("gui::AdbRunner", "ADB command timed out."));
		TracePoll(core::LogLevel::Warn, QStringLiteral("timeout"), output, QStringLiteral("client_exited=1"));
		return;
	}

	if (eStatus != true || iExitCode != 0) {
		PollFailed(QCoreApplication::translate("gui::AdbRunner", "ADB command failed (%1): %2").arg(iExitCode).arg(QString::fromLocal8Bit(output).trimmed().left(1024)));
		TracePoll(core::LogLevel::Warn, QStringLiteral("failed"), output,
		          QStringLiteral("exit_code=%1 normal_exit=%2").arg(iExitCode).arg(eStatus));
		return;
	}
	if (m_pollCommand == PollCommand::Version) {
		if (!output.contains("Android Debug Bridge version")) {
			PollFailed(QCoreApplication::translate("gui::AdbRunner", "Invalid ADB version output: %1").arg(QString::fromLocal8Bit(output).trimmed().left(1024)));
			TracePoll(core::LogLevel::Warn, QStringLiteral("invalid_version"), output);
			return;
		}
		m_versionChecked.insert(m_pollPath);
		TracePoll(core::LogLevel::Debug, QStringLiteral("finished"), output, QStringLiteral("exit_code=0 normal_exit=1"));
		QStringList versionLines;
		for (const auto &line : QString::fromLocal8Bit(output).split('\n')) {
			const auto text = line.trimmed();
			if (text.startsWith(QStringLiteral("Android Debug Bridge version ")) ||
			    text.startsWith(QStringLiteral("Version ")))
				versionLines.append(text);
		}
		emit diagnosticMessage(versionLines.join('\n'));
		SchedulePoll(0);
		return;
	}
	if (m_pollCommand == PollCommand::StartServer) {
		m_serverReady = true;
		TracePoll(core::LogLevel::Debug, QStringLiteral("finished"), output, QStringLiteral("exit_code=0 normal_exit=1"));
		SchedulePoll(0);
		return;
	}

	QStringList strDevices = AuthorizedDevices(ParseDevices(output));
	if (m_tracePoll || strDevices != m_strDevices || m_pollElapsed.elapsed() >= m_pollTiming.pollIntervalMs)
		TracePoll(core::LogLevel::Debug, QStringLiteral("finished"), output, QStringLiteral("exit_code=0 normal_exit=1"));
	UpdateDevices(output);
	m_failureCount = 0;
	m_retryDelayMs = 0;
	if (m_recovering) TracePoll(core::LogLevel::Info, QStringLiteral("recovered"));
	SetRecovery(false);
	UpdateAvailability(true, QString());
	SchedulePoll(m_pollTiming.pollIntervalMs);
}

void AdbService::onPollError(const QString &error)
{
	QTimer::singleShot(0, this, &AdbService::StartNextCommand);
	m_pPollKill->stop();
	m_pollActive = false;
	if (m_stopped) return;
	if (m_pollCancelled || FindAdbExecutable().path != m_pollPath) { SchedulePoll(0); return; }
	PollFailed(QCoreApplication::translate("gui::AdbRunner", "Cannot start ADB process: %1").arg(error));
	TracePoll(core::LogLevel::Warn, QStringLiteral("start_failed"), m_pPollProc->readAll(), error);
}

void AdbService::onPollKillTimeout()
{
	if (m_stopped || !m_pollActive || m_pPollProc->state() == ProcessAdapter::NotRunning)
		return;
	if (m_pollCommand == PollCommand::Devices && !m_pollSlow) {
		m_pollSlow = true;
		ClearPollDevices();
		SetRecovery(true);
		UpdateAvailability(false, {});
		TracePoll(core::LogLevel::Warn, QStringLiteral("slow_query"));
		m_pPollKill->start(std::max(1, m_pollTiming.hardTimeoutMs - int(m_pollRuntime.elapsed())));
		return;
	}
	m_pollTimedOut = true;
	TracePoll(core::LogLevel::Warn, QStringLiteral("timeout_kill_requested"));
	m_pPollProc->kill();
}


}
