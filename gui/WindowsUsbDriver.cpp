#include "gui/WindowsUsbDriver.h"
#include "application/BuildFeatures.h"
#include "backends/qualcomm/DevicePolicy.h"
#include <QDir>
#include <QLibrary>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <algorithm>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <winusb.h>
#include <shellapi.h>
#endif

namespace gui {
std::recursive_mutex &UsbProbeMutex() { static std::recursive_mutex mutex; return mutex; }
bool RecognizeWindowsFlashingInterface(DriverDevice &d, const QStringList &compatibleIds) {
    d.vendor = -1; d.verifiedInterface = false;
    const QString classes = compatibleIds.join('|').toUpper();
    if (classes.contains("USB\\COMPOSITE") || !d.service.compare("usbccgp", Qt::CaseInsensitive)) return false;
#if UPGRADE_TOOL_ROCKCHIP
        if ((d.vid == 0x2207 && d.pid >= 0x100) ||
            (d.vid == 0x071b && (d.pid == 0x3201 || d.pid == 0x3228 || d.pid == 0x3226))) {
            d.vendor = 0;
            d.verifiedInterface = classes.contains("USB\\CLASS_FF&SUBCLASS_06&PROT_05");
        }
#endif
#if UPGRADE_TOOL_ALLWINNER
        if (d.vid == 0x1f3a && d.pid == 0xefe8) {
            d.vendor = 1;
            d.verifiedInterface = classes.contains("USB\\CLASS_FF") && !classes.contains("SUBCLASS_42");
        }
#endif
#if UPGRADE_TOOL_QUALCOMM
        if (qtq_download_identity(d.vid, d.pid)) {
            d.vendor = 2;
            for (const auto &id : compatibleIds) d.verifiedInterface |= qtq_download_compatible_id(id.toLatin1().constData());
        }
#endif
    return d.vendor >= 0 && d.verifiedInterface;
}

QString DriverDevice::fingerprint() const {
    return instance.toUpper() + '|' + hardwareId.toUpper() + '|' + service.toUpper() + '|' +
        QString::number(problem) + '|' + QString::number(interfaceNumber);
}
QString DriverDevice::bindingId() const {
    QString id = hardwareId.toUpper();
    id.remove(QRegularExpression("&REV_[0-9A-F]{4}"));
    return id;
}
QJsonObject DriverDevice::toJson() const {
    return {{"instance", instance}, {"hardwareId", hardwareId}, {"service", service}, {"location", location},
        {"problem", problem}, {"interfaceNumber", interfaceNumber}};
}
DriverDevice DriverDevice::fromJson(const QJsonObject &o) {
    DriverDevice d; d.instance = o["instance"].toString(); d.hardwareId = o["hardwareId"].toString();
    d.location = o["location"].toString();
    d.service = o["service"].toString(); d.problem = o["problem"].toInt();
    d.interfaceNumber = o["interfaceNumber"].toInt(-1); return d;
}
#ifdef Q_OS_WIN
namespace {
QStringList property(HDEVINFO set, SP_DEVINFO_DATA &info, DWORD key) {
    DWORD bytes = 0, type = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &info, key, &type, nullptr, 0, &bytes);
    if (!bytes || bytes > 65536) return {};
    std::vector<wchar_t> data(bytes / sizeof(wchar_t) + 2, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, &info, key, &type,
        reinterpret_cast<PBYTE>(data.data()), bytes, nullptr)) return {};
    QStringList result;
    for (const wchar_t *p = data.data(); *p; p += wcslen(p) + 1) {
        result.append(QString::fromWCharArray(p));
        if (type != REG_MULTI_SZ) break;
    }
    return result;
}
QString instanceId(HDEVINFO set, SP_DEVINFO_DATA &info) {
    wchar_t id[MAX_DEVICE_ID_LEN] = {};
    return SetupDiGetDeviceInstanceIdW(set, &info, id, MAX_DEVICE_ID_LEN, nullptr)
        ? QString::fromWCharArray(id) : QString();
}
QStringList registryStrings(HDEVINFO set, SP_DEVINFO_DATA &info, const wchar_t *name) {
    HKEY key = SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE) return {};
    DWORD bytes = 0, type = 0;
    QStringList result;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS && bytes < 65536 &&
        (type == REG_SZ || type == REG_MULTI_SZ)) {
        std::vector<wchar_t> data(bytes / sizeof(wchar_t) + 2, 0);
        if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(data.data()), &bytes) == ERROR_SUCCESS)
            for (const wchar_t *p = data.data(); *p; p += wcslen(p) + 1) {
                result.append(QString::fromWCharArray(p));
                if (type == REG_SZ) break;
            }
    }
    RegCloseKey(key);
    return result;
}
bool matchesInterface(const DriverDevice &d, const USB_INTERFACE_DESCRIPTOR &alt) {
    if (d.vendor == 0) return alt.bInterfaceClass == 0xff && alt.bInterfaceSubClass == 6 && alt.bInterfaceProtocol == 5;
    if (d.vendor == 1) return alt.bInterfaceClass == 0xff && alt.bInterfaceSubClass != 0x42;
    return qtq_download_interface(alt.bInterfaceClass, alt.bInterfaceSubClass, alt.bInterfaceProtocol, 0x81, 1, 1, 1);
}
void probeWinusb(HDEVINFO devices, SP_DEVINFO_DATA &info, DriverDevice &d) {
    DWORD lastError = ERROR_NOT_FOUND;
    for (const auto &text : registryStrings(devices, info, L"DeviceInterfaceGUIDs")) {
        GUID guid;
        if (CLSIDFromString(reinterpret_cast<LPCOLESTR>(text.utf16()), &guid) != S_OK) continue;
        HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) continue;
        SP_DEVICE_INTERFACE_DATA iface = {}; iface.cbSize = sizeof(iface);
        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &guid, i, &iface); ++i) {
            DWORD bytes = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &bytes, nullptr);
            if (bytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || bytes > 65536) continue;
            std::vector<BYTE> data(bytes);
            auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(data.data());
            detail->cbSize = sizeof(*detail);
            SP_DEVINFO_DATA target = {}; target.cbSize = sizeof(target);
            if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, bytes, nullptr, &target) ||
                instanceId(set, target).compare(d.instance, Qt::CaseInsensitive)) continue;
            HANDLE file = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (file == INVALID_HANDLE_VALUE) { lastError = GetLastError(); continue; }
            WINUSB_INTERFACE_HANDLE usb = nullptr;
            if (WinUsb_Initialize(file, &usb)) {
                USB_INTERFACE_DESCRIPTOR alt = {};
                bool in = false, out = false;
                if (WinUsb_QueryInterfaceSettings(usb, 0, &alt) && matchesInterface(d, alt)) {
                    for (UCHAR p = 0; p < alt.bNumEndpoints; ++p) {
                        WINUSB_PIPE_INFORMATION pipe = {};
                        if (WinUsb_QueryPipe(usb, 0, p, &pipe) && pipe.PipeType == UsbdPipeTypeBulk) {
                            in |= (pipe.PipeId & 0x80) != 0; out |= (pipe.PipeId & 0x80) == 0;
                        }
                    }
                }
                WinUsb_Free(usb);
                if (in && out) d.state = DriverState::Ready;
                else lastError = ERROR_INVALID_DATA;
            } else lastError = GetLastError();
            CloseHandle(file);
            if (d.state == DriverState::Ready) break;
        }
        SetupDiDestroyDeviceInfoList(set);
        if (d.state == DriverState::Ready) return;
    }
    d.state = DriverState::AccessFailed;
    d.diagnostic = QStringLiteral("WinUSB: %1").arg(lastError);
}
bool compatibleService(const QString &service) {
    if (service.compare("WinUSB", Qt::CaseInsensitive) == 0) return true;
    if (service.compare("libusbK", Qt::CaseInsensitive) && service.compare("libusb0", Qt::CaseInsensitive)) return false;
    QLibrary library("libusbK");
    return library.load();
}
}
#endif

DriverScan ScanWindowsUsbDrivers(bool probe) {
    DriverScan result;
#ifdef Q_OS_WIN
    std::unique_lock<std::recursive_mutex> probeLock(UsbProbeMutex(), std::defer_lock);
    if (probe) probeLock.lock();
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        result.error = QStringLiteral("SetupDiGetClassDevs: %1").arg(GetLastError()); return result;
    }
    SP_DEVINFO_DATA info = {}; info.cbSize = sizeof(info);
    const QRegularExpression ids("VID_([0-9A-F]{4})&PID_([0-9A-F]{4})(?:&MI_([0-9A-F]{2}))?", QRegularExpression::CaseInsensitiveOption);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        DriverDevice d;
        const auto hardware = property(set, info, SPDRP_HARDWAREID);
        if (hardware.isEmpty()) continue;
        d.hardwareId = hardware.first();
        const auto match = ids.match(d.hardwareId);
        if (!match.hasMatch()) continue;
        d.vid = match.captured(1).toInt(nullptr, 16); d.pid = match.captured(2).toInt(nullptr, 16);
        const auto interfaceMatch = QRegularExpression("&MI_([0-9A-F]{2})", QRegularExpression::CaseInsensitiveOption).match(d.hardwareId);
        if (interfaceMatch.hasMatch()) d.interfaceNumber = interfaceMatch.captured(1).toInt(nullptr, 16);
        d.service = property(set, info, SPDRP_SERVICE).value(0);
        const auto compatible = property(set, info, SPDRP_COMPATIBLEIDS);
        const QString classes = compatible.join('|').toUpper();
        if (classes.contains("USB\\COMPOSITE") || !d.service.compare("usbccgp", Qt::CaseInsensitive)) continue;
        if (!RecognizeWindowsFlashingInterface(d, compatible)) continue;
        if (d.vendor < 0 || !d.verifiedInterface) continue;
        d.instance = instanceId(set, info);
        if (d.instance.isEmpty()) continue;
        d.label = property(set, info, SPDRP_FRIENDLYNAME).value(0);
        if (d.label.isEmpty()) d.label = property(set, info, SPDRP_DEVICEDESC).value(0);
        d.location = property(set, info, SPDRP_LOCATION_PATHS).value(0);
        d.port = registryStrings(set, info, L"PortName").value(0);
        ULONG status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) != CR_SUCCESS) continue;
        d.problem = problem;
        if (problem == CM_PROB_NEED_RESTART) d.state = DriverState::RebootRequired;
        else if (problem == CM_PROB_FAILED_INSTALL || (d.service.isEmpty() && problem == 0)) d.state = DriverState::Missing;
        else if (problem) d.state = DriverState::DeviceError;
        else if ((d.vendor == 2 && !d.port.isEmpty()) || compatibleService(d.service)) d.state = DriverState::Bound;
        else d.state = DriverState::Incompatible;
        if (probe && d.state == DriverState::Bound && !d.service.compare("WinUSB", Qt::CaseInsensitive)) probeWinusb(set, info, d);
        result.devices.append(d);
    }
    SetupDiDestroyDeviceInfoList(set);
    std::sort(result.devices.begin(), result.devices.end(), [](const auto &a, const auto &b) { return a.instance < b.instance; });
#else
    Q_UNUSED(probe);
#endif
    return result;
}

QVector<QPair<QString, QString>> WindowsAdbLocations() {
    QVector<QPair<QString, QString>> result;
#ifdef Q_OS_WIN
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return result;
    SP_DEVINFO_DATA info = {}; info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        const QString classes = property(set, info, SPDRP_COMPATIBLEIDS).join('|').toUpper();
        if (!classes.contains("USB\\CLASS_FF&SUBCLASS_42&PROT_01")) continue;
        const auto id = instanceId(set, info);
        QString serial = id.section('\\', -1);
        DEVINST parent = info.DevInst;
        for (int depth = 0; depth < 8; ++depth) {
            wchar_t name[MAX_DEVICE_ID_LEN] = {};
            if (CM_Get_Device_IDW(parent, name, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) break;
            const auto instance = QString::fromWCharArray(name);
            if (instance.startsWith("USB\\", Qt::CaseInsensitive) && !instance.contains("&MI_", Qt::CaseInsensitive)) {
                serial = instance.section('\\', -1); break;
            }
            DEVINST next;
            if (CM_Get_Parent(&next, parent, 0) != CR_SUCCESS) break;
            parent = next;
        }
        QString location = property(set, info, SPDRP_LOCATION_PATHS).value(0).toUpper();
        location.remove(QRegularExpression("#USBMI\\([^)]*\\).*$"));
        if (!serial.isEmpty() && !serial.contains('&') && !location.isEmpty() && !result.contains({serial, location}))
            result.append({serial, location});
    }
    SetupDiDestroyDeviceInfoList(set);
#endif
    return result;
}


DriverInstallResult InstallWindowsUsbDriver(const DriverDevice &target, const QVector<DriverDevice> &affected) {
    return InstallWindowsUsbDriverWithProgress(target, affected, {});
}
DriverInstallResult InstallWindowsUsbDriverWithProgress(const DriverDevice &target, const QVector<DriverDevice> &affected,
                                                       const DriverProgress &progress) {
#ifdef Q_OS_WIN
    DriverInstallResult result;
    QTemporaryDir directory(QDir::tempPath() + "/upgrade-driver-request-XXXXXX");
    if (!directory.isValid()) { result.error = QStringLiteral("Cannot create the driver request directory."); return result; }
    QJsonArray targets;
    for (const auto &d : affected) targets.append(d.toJson());
    QFile request(directory.filePath("request.json"));
    const auto bytes = QJsonDocument(QJsonObject{{"target", target.toJson()}, {"affected", targets}}).toJson();
    if (!request.open(QIODevice::WriteOnly) || request.write(bytes) != bytes.size()) {
        result.error = QStringLiteral("Cannot write the driver installation request."); return result;
    }
    request.close();
    const QString executable = QDir(QCoreApplication::applicationDirPath()).filePath("upgrade_driver_installer.exe");
    const QString arguments = '"' + QDir::toNativeSeparators(directory.path()) + '"';
    SHELLEXECUTEINFOW execute = {}; execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    execute.lpVerb = L"runas";
    execute.lpFile = reinterpret_cast<LPCWSTR>(executable.utf16());
    execute.lpParameters = reinterpret_cast<LPCWSTR>(arguments.utf16());
    execute.nShow = SW_HIDE;
    if (!ShellExecuteExW(&execute)) {
        const DWORD error = GetLastError(); result.cancelled = error == ERROR_CANCELLED;
        result.error = QStringLiteral("Cannot start driver installer (%1).").arg(error); return result;
    }
    if (!execute.hProcess) { result.error = QStringLiteral("The driver installer did not start."); return result; }
    int previousPhase = -1;
    DWORD wait;
    do {
        wait = WaitForSingleObject(execute.hProcess, 200);
        QFile phaseFile(directory.filePath("progress.json"));
        if (progress && phaseFile.open(QIODevice::ReadOnly) && phaseFile.size() <= 1024) {
            const auto object = QJsonDocument::fromJson(phaseFile.readAll()).object();
            const int phase = object.value("phase").toInt(-1);
            if (phase >= int(DriverPhase::Installing) && phase <= int(DriverPhase::Verifying) && phase != previousPhase) {
                previousPhase = phase; progress(static_cast<DriverPhase>(phase));
            }
        }
    } while (wait == WAIT_TIMEOUT);
    CloseHandle(execute.hProcess);
    if (wait != WAIT_OBJECT_0) { result.error = QStringLiteral("Cannot wait for the driver installer."); return result; }
    QFile output(directory.filePath("result.json"));
    if (!output.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("The driver installer did not return a result."); return result;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(output.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || !document.object().contains("ok")) {
        result.error = QStringLiteral("The driver installer returned an invalid result."); return result;
    }
    const auto value = document.object();
    result.ok = value["ok"].toBool(); result.cancelled = value["cancelled"].toBool(); result.error = value["error"].toString();
    result.log = value["log"].toString();
    result.rebootRequired = value["rebootRequired"].toBool();
    return result;
#else
    Q_UNUSED(target); Q_UNUSED(affected); Q_UNUSED(progress);
    return {false, false, QStringLiteral("Windows driver installation is unavailable on this platform.")};
#endif
}
}
