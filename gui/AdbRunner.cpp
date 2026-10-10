#include "gui/AdbRunner.h"

#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <libusb.h>
#include <utility>

namespace gui
{

namespace
{

const char *kRebootTarget = "loader";
const int kPollIntervalMs = 1000;
const int kListTimeoutMs = 5000;
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

AdbRunner::AdbRunner(QObject *parent, UsbScanner scanner, int commandTimeoutMs, int felTimeoutMs)
    : QObject(parent), m_bRunning(false), m_bAwaitingChoice(false), m_bAvailable(false), m_bAvailabilityKnown(false),
      m_usbScanner(std::move(scanner)), m_commandTimeoutMs(commandTimeoutMs), m_felTimeoutMs(felTimeoutMs)
{
	m_felTimer = new QTimer(this);
	m_felTimer->setInterval(250);
	connect(m_felTimer, &QTimer::timeout, this, &AdbRunner::CheckFel);
	m_pProc = new QProcess(this);
	m_pProc->setProcessChannelMode(QProcess::MergedChannels);
	connect(m_pProc, SIGNAL(finished(int, QProcess::ExitStatus)), this,
	        SLOT(onRebootFinished(int, QProcess::ExitStatus)));
	connect(m_pProc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart && m_bRunning)
			Complete(false, tr("Cannot start ADB process: %1").arg(m_pProc->errorString()));
	});

	m_pTimeout = new QTimer(this);
	m_pTimeout->setSingleShot(true);
	connect(m_pTimeout, SIGNAL(timeout()), this, SLOT(onTimeout()));

	m_pPollProc = new QProcess(this);
	m_pPollProc->setProcessChannelMode(QProcess::MergedChannels);
	connect(m_pPollProc, SIGNAL(finished(int, QProcess::ExitStatus)), this,
	        SLOT(onPollFinished(int, QProcess::ExitStatus)));
	connect(m_pPollProc, SIGNAL(errorOccurred(QProcess::ProcessError)), this,
	        SLOT(onPollError(QProcess::ProcessError)));

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
		strPath = QStandardPaths::findExecutable("adb", QStringList() << "/opt/homebrew/bin" << "/usr/local/bin");
#endif
	return strPath;
}

AdbRunner::~AdbRunner()
{
	m_bRunning = false;
	for (auto *process : {m_pProc, m_pPollProc}) {
		if (process->state() != QProcess::NotRunning) {
			process->kill();
			process->waitForFinished(1000);
		}
	}
}

bool AdbRunner::IsAvailable() const { return !AdbPath().isEmpty(); }

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
		UpdateAvailability(false, tr("ADB executable not found; make sure ADB is installed and in PATH."));
		return;
	}

	m_pPollProc->start(strAdb, QStringList() << "devices");
	m_pPollKill->start(kListTimeoutMs);
}

void AdbRunner::onPollTick() { RunPoll(); }

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
	UpdateAvailability(false, tr("Cannot start ADB process: %1").arg(m_pPollProc->errorString()));
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
	if (m_bRunning || m_bAwaitingChoice)
		return;

	if (AdbPath().isEmpty()) {
		emit finished(false, tr("ADB executable not found; make sure ADB is installed and in PATH."));
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
	if (m_pProc->state() != QProcess::NotRunning) {
		m_pProc->kill();
		m_pProc->waitForFinished(1000);
	}
	emit finished(ok, message);
	RunPoll();
}

void AdbRunner::onRebootFinished(int code, QProcess::ExitStatus status)
{
	m_pTimeout->stop();
	const auto output = QString::fromLocal8Bit(m_pProc->readAll()).trimmed();
	if (!m_bRunning || m_stage == Stage::Waiting)
		return;
	if (m_felSwitch) {
		HandleFelCommand(code, status, output);
		return;
	}
	Complete(status == QProcess::NormalExit && code == 0,
	         status == QProcess::NormalExit && code == 0
	             ? tr("Switched to Loader mode.")
	             : tr("Switch to Loader failed: %1").arg(output.isEmpty() ? tr("ADB returned %1").arg(code) : output));
}

void AdbRunner::HandleFelCommand(int code, QProcess::ExitStatus status, const QString &output)
{
	if (m_stage == Stage::Rebooting) {
		if (output.contains("AW_SWITCH_ERROR")) {
			Complete(false, tr("Failed to set T3 FEL flag or reboot: %1").arg(output));
			return;
		}
		// A disconnected ADB process may fail even though the device is entering FEL.
		if (status != QProcess::NormalExit || code != 0)
			m_commandError = output;
		m_stage = Stage::Waiting;
		CheckFel();
		return;
	}
	if (status != QProcess::NormalExit || code != 0) {
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
		emit switchProgress(tr("FEL switch request sent; waiting for the USB device to appear…"));
		m_felElapsed.start();
		m_felTimer->start();
		StartCommand(command);
	}
}

void AdbRunner::CheckFel()
{
	if (!m_bRunning || (m_stage != Stage::Rebooting && m_stage != Stage::Waiting))
		return;
	if (!m_target.ports.isEmpty()) {
		for (const auto &d : m_usbScanner(false)) {
			if (d.fel && d.bus == m_target.bus && d.ports == m_target.ports) {
				// Drain the old command before allowing another switch to use this QProcess.
				if (m_pProc->state() != QProcess::NotRunning) {
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
		         m_target.ports.isEmpty()
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
