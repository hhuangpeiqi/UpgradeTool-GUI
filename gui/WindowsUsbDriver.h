#pragma once
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QStringList>
#include <mutex>
#include <functional>

namespace gui {
std::recursive_mutex &UsbProbeMutex();
enum class DriverState { Missing, Incompatible, Bound, Ready, AccessFailed, DeviceError, RebootRequired };
struct DriverDevice {
    QString instance, hardwareId, label, service, location, port;
    int vendor = -1, vid = 0, pid = 0, interfaceNumber = -1, problem = 0;
    bool verifiedInterface = false;
    DriverState state = DriverState::DeviceError;
    QString diagnostic;
    bool canInstall() const {
        return verifiedInterface && (((vendor == 0 || vendor == 1) && state == DriverState::Missing) ||
            (vendor == 2 && vid == 0x05c6 && state == DriverState::Missing));
    }
    bool needsAttention() const { return state != DriverState::Ready && state != DriverState::Bound; }
    QString fingerprint() const;
    QString bindingId() const;
    QJsonObject toJson() const;
    static DriverDevice fromJson(const QJsonObject &);
    bool operator==(const DriverDevice &) const = default;
};
struct DriverScan { QVector<DriverDevice> devices; QString error; };
bool RecognizeWindowsFlashingInterface(DriverDevice &, const QStringList &compatibleIds);
bool WindowsUsbServiceCompatible(const QString &service);
#ifdef Q_OS_WIN
void ClassifyWindowsUsbDriver(DriverDevice &, unsigned long status, unsigned long problem);
#endif
struct DriverInstallResult {
    bool ok = false, cancelled = false;
    QString error;
    QString log = {};
    bool rebootRequired = false, retryable = false;
};
#ifdef Q_OS_WIN
DriverInstallResult RestartWindowsUsbDevice(const DriverDevice &);
#endif
enum class DriverPhase { Installing, Recovering, Verifying, Preparing, Bound };
enum class DriverOperation { AllwinnerInstall, RockusbAssistant, QualcommInstall, QualcommUninstall };
using DriverProgress = std::function<void(DriverPhase)>;
bool QualcommDriverInstalled();
QString QualcommDriverPackageError(const QString &path);
QStringList QualcommDriverArguments(DriverOperation);
QString AllwinnerDriverPackageError(const QString &directory);
DriverInstallResult RunAllwinnerDriverElevated(const DriverDevice &, const DriverProgress &);
QString RockusbDriverPackageError(const QString &directory);
DriverInstallResult RunRockusbDriverElevated(const DriverDevice &, const DriverProgress &);
DriverInstallResult RunWindowsDriverOperationWithProgress(DriverOperation, const DriverDevice &,
    const QVector<DriverDevice> &, const DriverProgress &);
DriverInstallResult RunQualcommDriverElevated(DriverOperation, const DriverDevice &, const DriverProgress &);
const DriverDevice *FindDriverDevice(const QVector<DriverDevice> &, const DriverDevice &);
DriverScan ScanWindowsUsbDrivers(bool probe = false);
QVector<QPair<QString, QString>> WindowsAdbLocations();
}
