#include "platform/WindowsUsb.h"
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
#include <QCryptographicHash>
#include <QFileInfo>
#include <algorithm>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <winusb.h>
#include <shellapi.h>
#include <shlobj.h>
#include "application/QualcommDriverPackage.h"
#include "application/RockusbDriverPackage.h"
#include "application/AllwinnerDriverPackage.h"
#if UPGRADE_TOOL_ALLWINNER
#include "backends/allwinner/WindowsAllwinner.h"
#endif
#if UPGRADE_TOOL_ROCKCHIP
#include "backends/rockchip/UsbTransport.h"
#endif
#endif

namespace gui {
QString RockusbDriverPackageError(const QString &directory) {
#ifdef Q_OS_WIN
    for (const auto &[relative, expected] : rockusbPackageFiles) {
        QFile file(QDir(directory).filePath(QString::fromUtf8(relative)));
        if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("Bundled Rockusb driver file is missing or unreadable: %1").arg(QString::fromUtf8(relative));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file) || hash.result().toHex() != expected)
            return QStringLiteral("Bundled Rockusb driver file failed SHA256 verification: %1").arg(QString::fromUtf8(relative));
    }
    return {};
#else
    Q_UNUSED(directory);
    return QStringLiteral("Rockusb driver installation is unavailable on this platform.");
#endif
}
QString AllwinnerDriverPackageError(const QString &directory) {
#ifdef Q_OS_WIN
    for (const auto &[relative, expected] : allwinnerPackageFiles) {
        QFile file(QDir(directory).filePath(QString::fromUtf8(relative)));
        if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("Bundled Allwinner driver file is missing or unreadable: %1").arg(QString::fromUtf8(relative));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file) || hash.result().toHex() != expected)
            return QStringLiteral("Bundled Allwinner driver file failed SHA256 verification: %1").arg(QString::fromUtf8(relative));
    }
    return {};
#else
    Q_UNUSED(directory);
    return QStringLiteral("Allwinner driver installation is unavailable on this platform.");
#endif
}
bool QualcommDriverInstalled() {
#ifdef Q_OS_WIN
    wchar_t directory[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, directory))) return false;
    return QFileInfo(QDir(QString::fromWCharArray(directory)).filePath("Qualcomm/Qualcomm USB Drivers/qdinstall.exe")).isFile();
#else
    return false;
#endif
}
QString QualcommDriverPackageError(const QString &path) {
#ifdef Q_OS_WIN
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("Bundled Qualcomm driver tool is missing or unreadable: %1").arg(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || hash.result().toHex() != QByteArray(qualcommPackageSha256))
        return QStringLiteral("Bundled Qualcomm driver tool failed SHA256 verification.");
    return {};
#else
    Q_UNUSED(path);
    return QStringLiteral("Qualcomm driver installation is unavailable on this platform.");
#endif
}
QStringList QualcommDriverArguments(DriverOperation operation) {
    return operation == DriverOperation::QualcommUninstall ? QStringList{"-u"} : QStringList{};
}
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
        const bool vendorInterface = classes.contains("USB\\CLASS_FF") &&
            !classes.contains("SUBCLASS_42&PROT_01");
        const bool diag = qtq_known_diag(d.vid, d.pid, d.interfaceNumber) ||
            qtq_diag_name(d.label.toUtf8().constData());
        if (d.vendor < 0 && vendorInterface && (d.vid == 0x05c6 || diag)) {
            d.vendor = 2;
            d.verifiedInterface = diag;
            if (qtq_download_identity(d.vid, d.pid))
                for (const auto &id : compatibleIds) d.verifiedInterface |= qtq_download_compatible_id(id.toLatin1().constData());
        }
#endif
    return d.vendor >= 0 && (d.verifiedInterface || d.vendor == 2);
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
    QStringList result;
    for (const auto &value : platform::WindowsUsbProperty(set, info, key)) result.append(QString::fromStdWString(value));
    return result;
}
QString instanceId(HDEVINFO set, SP_DEVINFO_DATA &info) {
    return QString::fromStdWString(platform::WindowsUsbInstance(set, info));
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
    if (d.vendor == 1) return alt.bInterfaceClass == 0xff && alt.bInterfaceSubClass != 0x42;
    if (d.interfaceNumber >= 0 && alt.bInterfaceNumber != d.interfaceNumber) return false;
    if (!qtq_download_identity(d.vid, d.pid))
        return alt.bInterfaceClass == 0xff &&
            !(alt.bInterfaceSubClass == 0x42 && alt.bInterfaceProtocol == 1);
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
}
#endif
bool WindowsUsbServiceCompatible(const QString &service) {
    if (service.compare("WinUSB", Qt::CaseInsensitive) == 0) return true;
    if (service.compare("libusbK", Qt::CaseInsensitive) && service.compare("libusb0", Qt::CaseInsensitive)) return false;
    QLibrary library("libusbK");
    return library.load();
}

#ifdef Q_OS_WIN
void ClassifyWindowsUsbDriver(DriverDevice &d, unsigned long status, unsigned long problem) {
    const bool hasProblem = (status & DN_HAS_PROBLEM) != 0;
    d.problem = hasProblem ? problem : 0;
    if (d.problem == CM_PROB_NEED_RESTART) d.state = DriverState::RebootRequired;
    else if (d.problem == CM_PROB_FAILED_INSTALL || (d.service.isEmpty() && !hasProblem)) d.state = DriverState::Missing;
    else if (hasProblem) d.state = DriverState::DeviceError;
    // Auxiliary Qualcomm interfaces (for example WWAN) are not flashing transports.
    // Their started flag can change without Windows reporting a device problem.
    else if (d.vendor == 2 && !d.verifiedInterface) d.state = DriverState::Bound;
    else if (!(status & DN_STARTED)) d.state = DriverState::DeviceError;
    else if ((d.vendor == 2 && !d.port.isEmpty() && !d.service.isEmpty()) ||
        (d.vendor == 0 ? !d.service.compare("Rockusb", Qt::CaseInsensitive) :
         d.vendor == 1 ? !d.service.compare("usbUDisc", Qt::CaseInsensitive) : WindowsUsbServiceCompatible(d.service))) d.state = DriverState::Bound;
    else d.state = DriverState::Incompatible;
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
        d.label = property(set, info, SPDRP_FRIENDLYNAME).value(0);
        if (d.label.isEmpty()) d.label = property(set, info, SPDRP_DEVICEDESC).value(0);
        const auto compatible = property(set, info, SPDRP_COMPATIBLEIDS);
        const QString classes = compatible.join('|').toUpper();
        if (classes.contains("USB\\COMPOSITE") || !d.service.compare("usbccgp", Qt::CaseInsensitive)) continue;
        if (!RecognizeWindowsFlashingInterface(d, compatible)) continue;
        d.instance = instanceId(set, info);
        if (d.instance.isEmpty()) continue;
        d.location = property(set, info, SPDRP_LOCATION_PATHS).value(0);
        d.port = registryStrings(set, info, L"PortName").value(0);
        ULONG status = 0, problem = 0;
        const auto statusResult = CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0);
        if (statusResult != CR_SUCCESS) {
            d.state = DriverState::AccessFailed;
            d.diagnostic = QStringLiteral("CM_Get_DevNode_Status: %1").arg(statusResult);
            result.devices.append(d); continue;
        }
        ClassifyWindowsUsbDriver(d, status, problem);
        if (probe && d.state == DriverState::Bound && (d.vendor != 2 || d.verifiedInterface) &&
            !d.service.compare("WinUSB", Qt::CaseInsensitive)) probeWinusb(set, info, d);
#if UPGRADE_TOOL_ROCKCHIP
        if (probe && d.vendor == 0 && d.state == DriverState::Bound) {
            const int error = core::rockchip::ProbeRockusb(d.instance.toStdString());
            d.state = error ? DriverState::AccessFailed : DriverState::Ready;
            if (error) d.diagnostic = QString::fromStdString(core::rockchip::UsbError(error));
        }
#endif
#if UPGRADE_TOOL_ALLWINNER
        if (probe && d.vendor == 1 && d.state == DriverState::Bound) {
            const int error = core::allwinner::ProbeAllwinner(d.instance.toStdString());
            d.state = error ? DriverState::AccessFailed : DriverState::Ready;
            if (error) d.diagnostic = QStringLiteral("Allwinner driver access failed (%1).").arg(error);
        }
#endif
        result.devices.append(d);
    }
    SetupDiDestroyDeviceInfoList(set);
    std::sort(result.devices.begin(), result.devices.end(), [](const auto &a, const auto &b) { return a.instance < b.instance; });
#else
    Q_UNUSED(probe);
#endif
    return result;
}

DriverInstallResult RunWindowsDriverOperationWithProgress(DriverOperation operation, const DriverDevice &target,
    const QVector<DriverDevice> &affected, const DriverProgress &progress) {
#ifdef Q_OS_WIN
    DriverInstallResult result;
    QTemporaryDir directory(QDir::tempPath() + "/upgrade-driver-request-XXXXXX");
    if (!directory.isValid()) { result.error = QStringLiteral("Cannot create the driver request directory."); return result; }
    QJsonArray targets;
    for (const auto &d : affected) targets.append(d.toJson());
    QFile request(directory.filePath("request.json"));
    const QString action = operation == DriverOperation::QualcommInstall ? "qualcomm-install" :
        operation == DriverOperation::QualcommUninstall ? "qualcomm-uninstall" :
        operation == DriverOperation::RockusbAssistant ? "rockusb-assistant" : "allwinner-install";
    const auto bytes = QJsonDocument(QJsonObject{{"operation", action}, {"target", target.toJson()}, {"affected", targets}}).toJson();
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
            if (phase >= int(DriverPhase::Installing) && phase <= int(DriverPhase::Bound) && phase != previousPhase) {
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
    Q_UNUSED(operation); Q_UNUSED(target); Q_UNUSED(affected); Q_UNUSED(progress);
    return {false, false, QStringLiteral("Windows driver installation is unavailable on this platform.")};
#endif
}
}
