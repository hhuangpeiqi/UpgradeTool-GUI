#include "application/BuildFeatures.h"
#include "platform/UsbLocation.h"
#if UPGRADE_TOOL_ROCKCHIP
#include "backends/rockchip/legacy/RKScan.h"
#include "backends/rockchip/UsbTransport.h"
#endif
#include "gui/AdbRunner.h"

#include <QRegularExpression>
#include <QCoreApplication>
#include <QTimer>
#include <QScopeGuard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

// ADB source locations and Rockchip Loader targets are separate enumerations.
// Allwinner targets are enumerated exclusively by the USB worker's backend.
QVector<AdbUsbDevice> ScanUsb(bool readSerial)
{
    QVector<AdbUsbDevice> result;
#ifdef Q_OS_WIN
    if (readSerial) {
        for (const auto &native : platform::NativeUsbLocations()) {
            AdbUsbDevice device;
            device.serial = QString::fromStdString(native.serial);
            device.location = QString::fromStdString(native.location);
            result.append(device);
        }
        return result;
    }
#endif
    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) != 0) return result;
    const auto releaseContext = qScopeGuard([&] { libusb_exit(ctx); });
    if (!readSerial) {
#if UPGRADE_TOOL_ROCKCHIP
        CRKScan scan(30, 20, ctx);
        scan.SetVidPid();
        const int count = scan.Search(RKUSB_LOADER);
        for (int i = 0; i < count; ++i) {
            STRUCT_RKDEVICE_DESC row{};
            if (!scan.GetDevice(row, i) || row.emUsbType != RKUSB_LOADER || !row.usbDevice) continue;
            AdbUsbDevice device;
            device.loader = true;
            device.location = QString::fromStdString(platform::NormalizeUsbLocation(row.usbDevice->physicalLocation));
#ifndef Q_OS_WIN
            auto *native = static_cast<libusb_device *>(row.usbDevice->native.get());
            device.bus = libusb_get_bus_number(native);
            unsigned char ports[16];
            const int depth = libusb_get_port_numbers(native, ports, sizeof(ports));
            for (int j = 0; j < depth; ++j) device.ports.append(ports[j]);
            device.location = QString::fromStdString(platform::UsbTopologyLocation(device.bus, {device.ports.begin(), device.ports.end()}));
#endif
            result.append(device);
        }
#endif
        return result;
    }
    libusb_device **list = nullptr;
    const auto count = libusb_get_device_list(ctx, &list);
    const auto releaseList = qScopeGuard([&] { if (list) libusb_free_device_list(list, 1); });
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor desc;
        libusb_config_descriptor *config = nullptr;
        if (libusb_get_device_descriptor(list[i], &desc) || libusb_get_active_config_descriptor(list[i], &config)) continue;
        bool adb = false;
        for (int j = 0; j < config->bNumInterfaces; ++j)
            for (int k = 0; k < config->interface[j].num_altsetting; ++k) {
                const auto &alt = config->interface[j].altsetting[k];
                adb |= alt.bInterfaceClass == 0xff && alt.bInterfaceSubClass == 0x42 && alt.bInterfaceProtocol == 1;
            }
        libusb_free_config_descriptor(config);
        if (!adb) continue;
        AdbUsbDevice device;
        device.bus = libusb_get_bus_number(list[i]);
        uint8_t ports[16];
        const int depth = libusb_get_port_numbers(list[i], ports, sizeof(ports));
        if (depth <= 0) continue;
        for (int j = 0; j < depth; ++j) device.ports.append(ports[j]);
        device.location = QString::fromStdString(platform::UsbTopologyLocation(device.bus, {device.ports.begin(), device.ports.end()}));
        libusb_device_handle *handle = nullptr;
        if (desc.iSerialNumber && !libusb_open(list[i], &handle)) {
            unsigned char serial[256];
            const int length = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, serial, sizeof(serial));
            if (length > 0) device.serial = QString::fromUtf8(reinterpret_cast<char *>(serial), length);
            libusb_close(handle);
        }
        result.append(device);
    }
    for (const auto &native : platform::NativeUsbLocations()) {
        bool mapped = false;
        for (auto &device : result) if (device.serial.compare(QString::fromStdString(native.serial), Qt::CaseInsensitive) == 0) {
            device.location = QString::fromStdString(native.location); mapped = true;
        }
        if (!mapped) {
            AdbUsbDevice device;
            device.serial = QString::fromStdString(native.serial);
            device.location = QString::fromStdString(native.location);
            result.append(device);
        }
    }
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

AdbRunner::AdbRunner(QObject *parent, UsbScanner scanner, int commandTimeoutMs, int modeTimeoutMs,
                     platform::ProcessFactory factory, AdbPollTiming timing)
    : QObject(parent), m_usbScanner(std::move(scanner)), m_commandTimeoutMs(commandTimeoutMs), m_modeTimeoutMs(modeTimeoutMs)
{
    m_service = new AdbService(this, std::move(factory), timing);
    m_loaderTimer = new QTimer(this);
    m_loaderTimer->setInterval(250);
    connect(m_loaderTimer, &QTimer::timeout, this, &AdbRunner::CheckLoaderMode);
    connect(m_service, &AdbService::devicesChanged, this, &AdbRunner::devicesChanged);
    connect(m_service, &AdbService::availabilityChanged, this, &AdbRunner::availabilityChanged);
    connect(m_service, &AdbService::recoveryStateChanged, this, &AdbRunner::recoveryStateChanged);
    connect(m_service, &AdbService::diagnosticMessage, this, &AdbRunner::diagnosticMessage);
    connect(m_service, &AdbService::traceMessage, this, &AdbRunner::traceMessage);
    connect(m_service, &AdbService::commandFinished, this, [this](quint64 id, const AdbCommandResult &result) {
        if (id != m_commandId || !m_bRunning) return;
        m_commandId = 0;
        if (!result.started) { Complete(false, tr("Cannot start ADB process: %1").arg(result.error)); return; }
        if (result.timedOut && m_stage != Stage::Rebooting) {
            Complete(false, tr("ADB operation timed out; mode switch not completed.")); return;
        }
        if (m_stage != Stage::Waiting)
            HandleSwitchCommand(result.exitCode, result.succeeded(), QString::fromLocal8Bit(result.output).trimmed());
    });
}

QString AdbRunner::AdbPath() const { return m_service->AdbPath(); }
QString AdbRunner::RecoveryMessage() const { return m_service->RecoveryMessage(); }
QStringList AdbRunner::Devices() const { return m_service->Devices(); }
void AdbRunner::Start() { m_service->Start(); }
void AdbRunner::Resume() { m_stopped=false; m_service->Resume(); }
void AdbRunner::Stop() {
    m_stopped=true;
    if(m_felCancel) m_felCancel->request_stop();
    if(m_felAttempt) emit felMonitorCancelled(m_felAttempt);
    m_felAttempt=0;
    m_service->setPaused(false);
    m_bRunning=false; m_bAwaitingChoice=false;
    m_loaderTimer->stop();
    m_service->Stop();
}
AdbRunner::~AdbRunner() { Stop(); }

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

	QStringList strDevices = Devices();
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
	QStringList matches;
	for (const auto &serial : Devices())
		if (serial.compare(strSerial.trimmed(), Qt::CaseInsensitive) == 0)
			matches.append(serial);
	if (matches.size() != 1) {
		Complete(false, tr("The selected ADB device is no longer available."));
		return;
	}
	m_bAwaitingChoice = false;
	StartReboot(matches.front());
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
	m_loaderCommandError.clear();
	m_bRunning = true;
    m_service->setPaused(true);
    m_stage = Stage::Listing;
    emit switchProgress(tr("Checking ADB devices and USB ports…"));
    StartCommand({"devices", "-l"});
}

void AdbRunner::StartCommand(const QStringList &args)
{
	m_commandCancel = std::make_shared<std::stop_source>();
    m_commandId = m_service->execute(AdbPath(), args, m_commandCancel,
        args.value(0) == "devices" ? kAdbReadTimeoutMs : m_commandTimeoutMs);
}

void AdbRunner::Complete(bool ok, const QString &message)
{
    if(m_felAttempt && !ok) {
        if(m_felCancel) m_felCancel->request_stop();
        emit felMonitorCancelled(m_felAttempt);
    }
    m_felAttempt=0; m_felCommand.clear();
	m_stage = Stage::Idle;
	m_bRunning = false;
	m_bAwaitingChoice = false;
	m_loaderTimer->stop();
    if (m_commandCancel) m_commandCancel->request_stop();
    m_commandId = 0;
    m_service->setPaused(false);

	emit finished(ok, message);

}

void AdbRunner::HandleSwitchCommand(int code, bool status, const QString &output)
{
	if (m_stage == Stage::Rebooting) {
		if (m_felSwitch && output.contains("AW_SWITCH_ERROR")) {
			Complete(false, tr("Failed to set T3 FEL flag or reboot: %1").arg(output));
			return;
		}
		// A disconnect can make ADB fail even when the device enters the requested mode.
		if (!m_felSwitch && (status != true || code != 0))
			m_loaderCommandError = output;
		m_stage = Stage::Waiting;
		if(!m_felSwitch) CheckLoaderMode();
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
			if (fields.size() >= 2 && fields[0].compare(m_serial, Qt::CaseInsensitive) == 0 && fields[1] == "device")
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
				if (!d.loader && d.bus == path.captured(1).toInt() && d.ports == ports)
					matches.append(d);
		}
        if(matches.isEmpty() && m_felSwitch && path.hasMatch()) {
            AdbUsbDevice device; device.bus=path.captured(1).toInt(); device.serial=m_serial;
            for(const auto &port:path.captured(2).split('.')) device.ports.append(port.toInt());
            device.location=QString::fromStdString(platform::UsbTopologyLocation(device.bus,{device.ports.begin(),device.ports.end()}));
            matches.append(device);
        }
#endif
		if (matches.isEmpty())
			for (const auto &d : devices)
				if (!d.loader && d.serial.compare(m_serial, Qt::CaseInsensitive) == 0)
					matches.append(d);

        QVector<AdbUsbDevice> unique;
        bool missingLocation = false;
        for(const auto &device:matches) {
            const platform::UsbLocation location{device.location.toStdString(),device.bus,{device.ports.begin(),device.ports.end()}};
            if(!location.known()) { missingLocation = true; continue; }
            if(std::none_of(unique.begin(),unique.end(),[&](const auto &other) {
                return location.matches({other.location.toStdString(),other.bus,{other.ports.begin(),other.ports.end()}});
            })) unique.append(device);
        }
        if(unique.size()==1) {
            const auto &device=unique.front();
            m_target={device.location.toStdString(),device.bus,{device.ports.begin(),device.ports.end()}};
        }
        const QString resolution = matches.isEmpty() ? QStringLiteral("unmapped")
            : missingLocation ? QStringLiteral("missing_location")
            : unique.size() != 1 ? QStringLiteral("ambiguous") : QStringLiteral("resolved");
        QJsonArray candidates;
        for (const auto &device : devices) {
            QJsonArray chain;
            for (const auto port : device.ports) chain.append(port);
            candidates.append(QJsonObject{{"serial", device.serial}, {"native", device.location},
                {"bus", device.bus}, {"ports", chain}, {"selected", device.serial.compare(m_serial, Qt::CaseInsensitive) == 0}});
        }
        QJsonArray ports;
        for(const auto port:m_target.ports) ports.append(port);
        emit traceMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
            {"event","switch_target"},{"gui_pid",QCoreApplication::applicationPid()},
            {"serial",m_serial},{"native",QString::fromStdString(m_target.native)},{"bus",m_target.bus},{"ports",ports},
            {"mode",m_felSwitch?"fel":"loader"},{"resolution",resolution},{"candidates",candidates}}).toJson(QJsonDocument::Compact)));
        if (m_felSwitch && resolution != "resolved") {
            const auto message = matches.isEmpty()
                ? tr("Cannot map the selected ADB serial number to a physical USB device; no switch command was sent.")
                : missingLocation
                    ? tr("The physical USB port of the selected ADB device could not be read; no switch command was sent.")
                    : tr("The selected ADB serial number matches multiple physical USB ports; no switch command was sent.");
            Complete(false, message);
            return;
        }
        if (!m_felSwitch) {
            m_stage = Stage::Rebooting;
            emit switchProgress(tr("Loader switch request sent; waiting for the USB device…"));
            m_loaderElapsed.start(); m_loaderTimer->start();
            StartCommand({"-s", m_serial, "reboot", kRebootTarget});
            return;
        }
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
        m_stage=Stage::Arming;
        m_felCommand=command;
        m_felAttempt=++m_nextFelAttempt;
        m_felCancel=std::make_shared<std::stop_source>();
        emit switchProgress(tr("Preparing to detect and connect the target FEL/FES device…"));
        emit felMonitorRequested(FelSwitchRequest{m_felAttempt,m_target,m_felCancel,m_modeTimeoutMs});
	}
}

void AdbRunner::FelMonitorReady(quint64 id) {
    if(!m_bRunning || m_stage!=Stage::Arming || id!=m_felAttempt) return;
    m_stage=Stage::Rebooting;
    emit switchProgress(tr("FEL switch request sent; waiting for the USB device…"));
    StartCommand(m_felCommand);
}
void AdbRunner::FelMonitorFinished(quint64 id,bool ok,const QString &message,const core::DeviceRef &target) {
    if(!m_bRunning || id!=m_felAttempt) return;
    if(ok) emit felTargetReady(target);
    Complete(ok,message);
}

void AdbRunner::CheckLoaderMode()
{
	if (m_felSwitch || !m_bRunning || (m_stage != Stage::Rebooting && m_stage != Stage::Waiting))
		return;
    if (m_target.known()) {
        int matches = 0;
        for (const auto &device : m_usbScanner(false)) {
            const platform::UsbLocation candidate{device.location.toStdString(), device.bus,
                {device.ports.begin(), device.ports.end()}};
            if (device.loader && m_target.matches(candidate)) ++matches;
        }
        if (matches > 1) {
            Complete(false, tr("Multiple flashing interfaces appeared at the target USB port; refresh and select one explicitly."));
            return;
        }
        if (matches == 1) {
            if (m_commandId != 0) {
                m_stage = Stage::Waiting;
                if (m_commandCancel) m_commandCancel->request_stop();
                return;
            }
            Complete(true,tr("Loader device detected on the target USB port."));
            return;
        }
    }
	if (m_loaderElapsed.elapsed() >= m_modeTimeoutMs) {
        Complete(false, m_target.known()
                ? tr("No Loader device detected on the target USB port. %1").arg(m_loaderCommandError)
                : tr("Switch request sent, but the target USB port could not be uniquely identified to confirm success; check the device list."));
	}
}

} // namespace gui
