#include "gui/AdbRunner.h"
#include "gui/AdbExecutable.h"
#include "gui/WindowsUsbDriver.h"

#include <QRegularExpression>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <libusb.h>
#include <utility>
#include <algorithm>

namespace gui
{

namespace
{

const char *kRebootTarget = "loader";
const int kRebootTimeoutMs = 10000;

// BusyBox `reboot` ignores the `efex` argument, so T3 Buildroot writes the FEL flag via devmem instead.
const char *kT3FelCommand =
    "{ devmem 0x01c20508 32 0x5AA5A55A && value=$(devmem 0x01c20508 32) && "
    "case \"$value\" in 0x5AA5A55A|0x5aa5a55a) sync && reboot;; *) echo 'FEL flag verification "
    "failed'; false;; esac; } "
    "|| { echo AW_SWITCH_ERROR; exit 1; }";

QStringList ParseDevices(const QByteArray &data)
{
	QStringList list;
	QString strOut = QString::fromLocal8Bit(data);
	QStringList strLines = strOut.split('\n', Qt::SkipEmptyParts);
	for (int i = 0; i < strLines.size(); i++) {
		QStringList strFields = strLines[i].split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

		if (strFields.size() < 2 || strFields[1] != "device")
			continue;
		list << strFields[0];
	}
	return list;
}

// Enumeration uses its own context and never claims a device interface.
QVector<AdbUsbDevice> ScanUsb(bool readSerial)
{
	QVector<AdbUsbDevice> result;
	libusb_context *ctx = nullptr;
	if (libusb_init(&ctx) != 0)
		return result;
	libusb_device **list = nullptr;
	const auto count = libusb_get_device_list(ctx, &list);
	for (ssize_t i = 0; i < count; ++i) {
		libusb_device_descriptor desc;
		if (libusb_get_device_descriptor(list[i], &desc))
			continue;
		AdbUsbDevice d;
		d.bus = libusb_get_bus_number(list[i]);
		uint8_t ports[16];
		const int n = libusb_get_port_numbers(list[i], ports, sizeof(ports));
		if (n <= 0)
			continue;
		for (int j = 0; j < n; ++j)
			d.ports.append(ports[j]);
		d.fel = desc.idVendor == 0x1f3a && desc.idProduct == 0xefe8;
		if (readSerial && !d.fel && desc.iSerialNumber) {
			libusb_config_descriptor *config = nullptr;
			bool adb = false;
			if (!libusb_get_active_config_descriptor(list[i], &config)) {
				for (int j = 0; j < config->bNumInterfaces; ++j)
					for (int k = 0; k < config->interface[j].num_altsetting; ++k) {
						const auto &alt = config->interface[j].altsetting[k];
						adb |= alt.bInterfaceClass == 0xff && alt.bInterfaceSubClass == 0x42 &&
						       alt.bInterfaceProtocol == 1;
					}
				libusb_free_config_descriptor(config);
			}
			libusb_device_handle *handle = nullptr;
			if (adb && !libusb_open(list[i], &handle)) {
				unsigned char serial[256];
				const int length =
				    libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, serial, sizeof(serial));
				if (length > 0)
					d.serial = QString::fromUtf8(reinterpret_cast<char *>(serial), length);
				libusb_close(handle);
			}
		}
		result.append(d);
	}
	if (list)
		libusb_free_device_list(list, 1);
	libusb_exit(ctx);
#ifdef Q_OS_WIN
    if (readSerial) {
        for (const auto &native : WindowsAdbLocations()) {
            bool mapped = false;
            for (auto &d : result) if (!d.fel && d.serial == native.first) { d.location = native.second; mapped = true; }
            if (!mapped) { AdbUsbDevice d; d.serial = native.first; d.location = native.second; result.append(d); }
        }
    } else {
        for (const auto &native : ScanWindowsUsbDrivers().devices) {
            if (native.vendor != 1) continue;
            AdbUsbDevice d; d.fel = true; d.location = native.location.toUpper();
            d.location.remove(QRegularExpression("#USBMI\\([^)]*\\).*$")); result.append(d);
        }
    }
#endif
	return result;
}

const char *kProbe = R"SH(
printf 'AW_ANDROID='; if command -v getprop >/dev/null 2>&1; then getprop ro.build.version.release; else echo; fi
printf 'AW_BUILDROOT='; if test -r /etc/os-release && grep -Eq '^ID="?buildroot"?$' /etc/os-release; then echo 1; else echo 0; fi
printf 'AW_SOC='; if test -r /proc/device-tree/compatible; then tr '\000' '\n' < /proc/device-tree/compatible; fi
printf '\nAW_UID='; id -u
printf 'AW_DEVMEM='; if command -v devmem >/dev/null 2>&1; then echo 1; else echo 0; fi
printf 'AW_REBOOT='; command -v reboot
printf 'AW_REBOOT_LINK='; if command -v reboot >/dev/null 2>&1; then ls -l "$(command -v reboot)"; else echo; fi
printf 'AW_BUSYBOX='; if command -v reboot >/dev/null 2>&1 && command -v busybox >/dev/null 2>&1 && test "$(command -v reboot)" -ef "$(command -v busybox)"; then echo 1; else echo 0; fi
)SH";

QString ProbeValue(const QString &output, const QString &key)
{
	const auto match = QRegularExpression(QStringLiteral("(?:^|\\n)%1=([^\\r\\n]*)").arg(key)).match(output);
	return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

// The T3 devmem entry needs a non-Android Buildroot SoC with root and devmem available.
bool IsT3FelCapable(bool android, bool buildroot, bool t3, const QString &output)
{
	return !android && buildroot && t3 && ProbeValue(output, "AW_UID") == "0" &&
	       ProbeValue(output, "AW_DEVMEM") == "1";
}

} // namespace

AdbRunner::AdbRunner(QObject *parent) : AdbRunner(parent, ScanUsb, kRebootTimeoutMs, 45000) {}

AdbRunner::AdbRunner(QObject *parent, UsbScanner scanner, int commandTimeoutMs, int felTimeoutMs,
                     platform::ProcessFactory factory, AdbPollTiming timing)
    : QObject(parent), m_bRunning(false), m_bAwaitingChoice(false), m_bAvailable(false), m_bAvailabilityKnown(false),
      m_usbScanner(std::move(scanner)), m_commandTimeoutMs(commandTimeoutMs), m_felTimeoutMs(felTimeoutMs)
{
	m_pollTiming = timing;
	m_felTimer = new QTimer(this);
	m_felTimer->setInterval(250);
	connect(m_felTimer, &QTimer::timeout, this, &AdbRunner::CheckFel);
    m_pProc=new ProcessAdapter(this,factory);
    connect(m_pProc,&ProcessAdapter::finished,this,&AdbRunner::onRebootFinished);
    connect(m_pProc,&ProcessAdapter::failed,this,[this](const QString &error) {
        if(m_bRunning) Complete(false,tr("Cannot start ADB process: %1").arg(error));
    });

	m_pTimeout = new QTimer(this);
	m_pTimeout->setSingleShot(true);
	connect(m_pTimeout, SIGNAL(timeout()), this, SLOT(onTimeout()));

    m_pPollProc=new ProcessAdapter(this,factory);
    connect(m_pPollProc,&ProcessAdapter::finished,this,&AdbRunner::onPollFinished);
    connect(m_pPollProc,&ProcessAdapter::failed,this,&AdbRunner::onPollError);

	m_pPollTimer = new QTimer(this);
	m_pPollTimer->setSingleShot(true);
	connect(m_pPollTimer, SIGNAL(timeout()), this, SLOT(onPollTick()));

	m_pPollKill = new QTimer(this);
	m_pPollKill->setSingleShot(true);
	connect(m_pPollKill, SIGNAL(timeout()), this, SLOT(onPollKillTimeout()));
}

QString AdbRunner::AdbPath() const
{
	if (m_stopped) return QString();
	return FindAdbExecutable().path;
}

AdbRunner::~AdbRunner()
{
	Stop();
}

void AdbRunner::Stop()
{
	m_stopped = true;
	m_pollCancelled = true;
	m_serverReady = false;
	m_recovering = false;
	m_recoveryStarting = false;
	m_bRunning = false;
	m_bAwaitingChoice = false;
	for (auto *timer : {m_felTimer, m_pTimeout, m_pPollTimer, m_pPollKill}) timer->stop();
	for (auto *process : {m_pProc, m_pPollProc}) {
		if (process->state() != ProcessAdapter::NotRunning) {
			process->kill();
			process->waitForFinished(1000);
		}
	}
}

void AdbRunner::Start()
{
	if (m_stopped) return;
	RunPoll();
}

void AdbRunner::Resume()
{
	m_stopped = false;
	m_retryDelayMs = 0;
	m_failureCount = 0;
	ClearPollDevices();
	UpdateAvailability(false, {});
	Start();
}

void AdbRunner::UpdateAvailability(bool bAvailable, const QString &strMessage)
{
	if (m_bAvailabilityKnown && m_bAvailable == bAvailable && m_availabilityMessage == strMessage)
		return;
	m_bAvailabilityKnown = true;
	m_bAvailable = bAvailable;
	m_availabilityMessage = strMessage;
	emit availabilityChanged(m_bAvailable, strMessage);
}

void AdbRunner::ClearPollDevices()
{
	if (!m_strDevices.isEmpty()) {
		m_strDevices.clear();
		emit devicesChanged(m_strDevices);
	}
}

void AdbRunner::SetRecovery(bool recovering, bool starting)
{
	if (m_recovering == recovering && m_recoveryStarting == starting) return;
	m_recovering = recovering;
	m_recoveryStarting = starting;
	emit recoveryStateChanged(recovering, RecoveryMessage());
}

QString AdbRunner::RecoveryMessage() const
{
	return !m_recovering ? QString() : m_recoveryStarting
	    ? tr("Starting ADB…") : tr("ADB connection interrupted; retrying automatically…");
}

void AdbRunner::SchedulePoll(int delayMs)
{
	if (!m_stopped) m_pPollTimer->start(delayMs);
}

void AdbRunner::PollFailed(const QString &message)
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

void AdbRunner::TracePoll(const QString &event, const QByteArray &output, const QString &detail)
{
	constexpr int outputLimit = 64 * 1024;
	const QJsonObject record{
	    {"event", event}, {"gui_pid", QCoreApplication::applicationPid()},
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
	emit traceMessage(QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Compact)));
}

void AdbRunner::RunPoll()
{
	if (m_stopped) return;
	if (m_pollActive || m_pPollProc->state() != ProcessAdapter::NotRunning)
		return;
	if (m_bRunning || m_bAwaitingChoice) {
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
		if (!m_bAvailabilityKnown || m_availabilityMessage != tr("ADB executable not found; reinstall the application or install ADB and check PATH / ADB."))
			TracePoll(QStringLiteral("executable_missing"));
		PollFailed(tr("ADB executable not found; reinstall the application or install ADB and check PATH / ADB."));
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
	if (m_tracePoll) TracePoll(QStringLiteral("start"));
	const QString command = m_pollCommand == PollCommand::Version ? QStringLiteral("version")
	    : m_pollCommand == PollCommand::StartServer ? QStringLiteral("start-server") : QStringLiteral("devices");
	m_pPollProc->start(m_pollPath, {command});
	m_spawnElapsedMs = m_pollElapsed.elapsed();
	m_pollRuntime.start();
	m_pPollKill->start(m_pollCommand == PollCommand::Devices
	    ? m_pollTiming.slowQueryMs : m_pollTiming.hardTimeoutMs);
}

void AdbRunner::onPollTick() { RunPoll(); }

void AdbRunner::onPollFinished(int iExitCode, bool eStatus)
{
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
		PollFailed(tr("ADB command timed out."));
		TracePoll(QStringLiteral("timeout"), output, QStringLiteral("client_exited=1"));
		return;
	}

	if (eStatus != true || iExitCode != 0) {
		PollFailed(tr("ADB command failed (%1): %2").arg(iExitCode).arg(QString::fromLocal8Bit(output).trimmed().left(1024)));
		TracePoll(QStringLiteral("failed"), output,
		          QStringLiteral("exit_code=%1 normal_exit=%2").arg(iExitCode).arg(eStatus));
		return;
	}
	if (m_pollCommand == PollCommand::Version) {
		if (!output.contains("Android Debug Bridge version")) {
			PollFailed(tr("Invalid ADB version output: %1").arg(QString::fromLocal8Bit(output).trimmed().left(1024)));
			TracePoll(QStringLiteral("invalid_version"), output);
			return;
		}
		m_versionChecked.insert(m_pollPath);
		TracePoll(QStringLiteral("finished"), output, QStringLiteral("exit_code=0 normal_exit=1"));
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
		TracePoll(QStringLiteral("finished"), output, QStringLiteral("exit_code=0 normal_exit=1"));
		SchedulePoll(0);
		return;
	}

	QStringList strDevices = ParseDevices(output);
	if (m_tracePoll || strDevices != m_strDevices || m_pollElapsed.elapsed() >= m_pollTiming.pollIntervalMs)
		TracePoll(QStringLiteral("finished"), output, QStringLiteral("exit_code=0 normal_exit=1"));
	if (strDevices != m_strDevices) {
		m_strDevices = strDevices;
		emit devicesChanged(m_strDevices);
	}
	m_failureCount = 0;
	m_retryDelayMs = 0;
	if (m_recovering) TracePoll(QStringLiteral("recovered"));
	SetRecovery(false);
	UpdateAvailability(true, QString());
	SchedulePoll(m_pollTiming.pollIntervalMs);
}

void AdbRunner::onPollError(const QString &error)
{
	m_pPollKill->stop();
	m_pollActive = false;
	if (m_stopped) return;
	if (m_pollCancelled || FindAdbExecutable().path != m_pollPath) { SchedulePoll(0); return; }
	PollFailed(tr("Cannot start ADB process: %1").arg(error));
	TracePoll(QStringLiteral("start_failed"), m_pPollProc->readAll(), error);
}

void AdbRunner::onPollKillTimeout()
{
	if (m_stopped || !m_pollActive || m_pPollProc->state() == ProcessAdapter::NotRunning)
		return;
	if (m_pollCommand == PollCommand::Devices && !m_pollSlow) {
		m_pollSlow = true;
		ClearPollDevices();
		SetRecovery(true);
		UpdateAvailability(false, {});
		TracePoll(QStringLiteral("slow_query"));
		m_pPollKill->start(std::max(1, m_pollTiming.hardTimeoutMs - int(m_pollRuntime.elapsed())));
		return;
	}
	m_pollTimedOut = true;
	TracePoll(QStringLiteral("timeout_kill_requested"));
	m_pPollProc->kill();
}

void AdbRunner::SwitchToLoader()
{
	if (m_bRunning || m_bAwaitingChoice)
		return;
	m_felSwitch = false;
	BeginSwitch();
}

void AdbRunner::SwitchToFel(FelMethod method)
{
	if (m_bRunning || m_bAwaitingChoice)
		return;
	m_felSwitch = true;
	m_felMethod = method;
	BeginSwitch();
}

void AdbRunner::BeginSwitch()
{
	if (m_stopped) return;
	if (m_bRunning || m_bAwaitingChoice)
		return;

	if (AdbPath().isEmpty()) {
		emit finished(false, tr("ADB executable not found; reinstall the application or install ADB and check PATH / ADB."));
		return;
	}

	QStringList strDevices = m_strDevices;
	if (strDevices.isEmpty()) {
		emit finished(false, tr("No device in ADB mode found."));
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
	if (!m_strDevices.contains(strSerial.trimmed())) {
		Complete(false, tr("The selected ADB device is no longer available."));
		return;
	}
	m_bAwaitingChoice = false;
	StartReboot(strSerial.trimmed());
}

void AdbRunner::CancelSwitch()
{
	if (!m_bAwaitingChoice && !m_bRunning)
		return;
	Complete(false, tr("Switch request cancelled; if the device already began rebooting, check the device list."));
}

void AdbRunner::StartReboot(const QString &serial)
{
	m_serial = serial;
	m_target = {};
	m_commandError.clear();
	m_bRunning = true;
	if (m_felSwitch) {
		m_stage = Stage::Listing;
		emit switchProgress(tr("Checking ADB devices and USB ports…"));
		StartCommand({"devices", "-l"});
	} else {
		m_stage = Stage::Loader;
		StartCommand({"-s", serial, "reboot", kRebootTarget});
	}
}

void AdbRunner::StartCommand(const QStringList &args)
{
	m_pProc->start(AdbPath(), args);
	m_pTimeout->start(m_commandTimeoutMs);
}

void AdbRunner::Complete(bool ok, const QString &message)
{
	m_stage = Stage::Idle;
	m_bRunning = false;
	m_bAwaitingChoice = false;
	m_pTimeout->stop();
	m_felTimer->stop();
	if (m_pProc->state() != ProcessAdapter::NotRunning) {
		m_pProc->kill();
		m_pProc->waitForFinished(1000);
	}
	emit finished(ok, message);
	RunPoll();
}

void AdbRunner::onRebootFinished(int code, bool status)
{
	m_pTimeout->stop();
	const auto output = QString::fromLocal8Bit(m_pProc->readAll()).trimmed();
	if (!m_bRunning || m_stage == Stage::Waiting)
		return;
	if (m_felSwitch) {
		HandleFelCommand(code, status, output);
		return;
	}
	Complete(status == true && code == 0,
	         status == true && code == 0
	             ? tr("Switched to Loader mode.")
	             : tr("Switch to Loader failed: %1").arg(output.isEmpty() ? tr("ADB returned %1").arg(code) : output));
}

void AdbRunner::HandleFelCommand(int code, bool status, const QString &output)
{
	if (m_stage == Stage::Rebooting) {
		if (output.contains("AW_SWITCH_ERROR")) {
			Complete(false, tr("Failed to set T3 FEL flag or reboot: %1").arg(output));
			return;
		}
		// A disconnected ADB process may fail even though the device is entering FEL.
		if (status != true || code != 0)
			m_commandError = output;
		m_stage = Stage::Waiting;
		CheckFel();
		return;
	}
	if (status != true || code != 0) {
		Complete(false, tr("ADB check failed: %1").arg(output.isEmpty() ? QString::number(code) : output));
		return;
	}
	if (m_stage == Stage::Listing) {
		QString line;
		for (const auto &candidate : output.split('\n')) {
			const auto fields = candidate.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
			if (fields.size() >= 2 && fields[0] == m_serial && fields[1] == "device")
				line = candidate;
		}
		if (line.isEmpty()) {
			Complete(false, tr("The selected ADB device is disconnected or unauthorized."));
			return;
		}
		const auto devices = m_usbScanner(true);
		QVector<AdbUsbDevice> matches;
#ifdef Q_OS_LINUX
		const auto path = QRegularExpression("(?:^|\\s)usb:([0-9]+)-([0-9.]+)(?:\\s|$)").match(line);
		if (path.hasMatch()) {
			QVector<int> ports;
			for (const auto &port : path.captured(2).split('.'))
				ports.append(port.toInt());
			for (const auto &d : devices)
				if (!d.fel && d.bus == path.captured(1).toInt() && d.ports == ports)
					matches.append(d);
		}
#endif
		if (matches.isEmpty())
			for (const auto &d : devices)
				if (!d.fel && d.serial == m_serial)
					matches.append(d);
		if (matches.size() == 1)
			m_target = matches[0];
		m_stage = Stage::Probing;
		emit switchProgress(tr("Detecting device system and switch capability…"));
		StartCommand({"-s", m_serial, "shell", QString::fromLatin1(kProbe)});
		return;
	}
	if (m_stage == Stage::Probing) {
		const bool android = !ProbeValue(output, "AW_ANDROID").isEmpty();
		const bool buildroot = ProbeValue(output, "AW_BUILDROOT") == "1";
		const bool t3 =
		    QRegularExpression("(?:^|[\\r\\n])(?:arm,)?sun8iw11p1(?:[\\r\\n]|$)").match(output).hasMatch() ||
		    ProbeValue(output, "AW_SOC") == "arm,sun8iw11p1" || ProbeValue(output, "AW_SOC") == "sun8iw11p1";
		const bool busybox = ProbeValue(output, "AW_BUSYBOX") == "1" ||
		                     ProbeValue(output, "AW_REBOOT_LINK").contains("busybox", Qt::CaseInsensitive);
		if (ProbeValue(output, "AW_UID").isEmpty() || ProbeValue(output, "AW_BUILDROOT").isEmpty()) {
			Complete(false, tr("Could not fully identify the device system; no switch command was sent."));
			return;
		}
		QStringList command{"-s", m_serial};
		const bool t3Capable = IsT3FelCapable(android, buildroot, t3, output);
		if (m_felMethod == FelMethod::T3Buildroot) {
			if (!t3Capable) {
				Complete(false,
				         tr("The alternate requires sun8iw11p1, Buildroot, root and devmem."));
				return;
			}
			command << "shell" << kT3FelCommand;
		} else if (!android && busybox) {
			// BusyBox ignores `reboot efex`; use the T3 devmem entry automatically when possible.
			if (t3Capable) {
				command << "shell" << kT3FelCommand;
			} else {
				Complete(false, buildroot && t3
				                    ? tr("This T3 Buildroot lacks root or devmem; automatic switching is unavailable. Enter flash mode manually.")
				                    : tr("No supported FEL switch entry was found in this BusyBox environment; enter flash mode manually."));
				return;
			}
		} else if (!android && !ProbeValue(output, "AW_REBOOT").isEmpty()) {
			command << "shell" << "reboot efex";
		} else {
			command << "reboot" << "efex";
		}
		m_stage = Stage::Rebooting;
		emit switchProgress(tr("FEL switch request sent; waiting for the USB device…"));
		m_felElapsed.start();
		m_felTimer->start();
		StartCommand(command);
	}
}

void AdbRunner::CheckFel()
{
	if (!m_bRunning || (m_stage != Stage::Rebooting && m_stage != Stage::Waiting))
		return;
	if (!m_target.ports.isEmpty() || !m_target.location.isEmpty()) {
		for (const auto &d : m_usbScanner(false)) {
			if (d.fel && ((!m_target.location.isEmpty() && d.location == m_target.location) ||
                (!m_target.ports.isEmpty() && d.bus == m_target.bus && d.ports == m_target.ports))) {
				// Drain the old command before allowing another switch to use this process.
				if (m_pProc->state() != ProcessAdapter::NotRunning) {
					m_stage = Stage::Waiting;
					m_pTimeout->stop();
					m_pProc->kill();
					return;
				}
				Complete(true, tr("FEL flash device detected on the target USB port."));
				return;
			}
		}
	}
	if (m_felElapsed.elapsed() >= m_felTimeoutMs) {
		Complete(false,
		         m_target.ports.isEmpty() && m_target.location.isEmpty()
		             ? tr("Switch request sent, but the target USB port could not be uniquely identified to confirm success; check the device list.")
		             : tr("No FEL device detected on the target USB port. Check system support, or choose \"Alternate\". %1")
		                   .arg(m_commandError.isEmpty() ? QString() : "\n" + m_commandError));
	}
}

void AdbRunner::onTimeout()
{
	if (!m_bRunning)
		return;
	if (m_stage == Stage::Rebooting) {
		m_commandError = tr("The ADB reboot command did not return in time.");
		m_stage = Stage::Waiting;
		m_pProc->kill();
		return;
	}
	Complete(false, tr("ADB operation timed out; mode switch not completed."));
}

} // namespace gui
