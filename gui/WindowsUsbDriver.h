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
        return vendor >= 0 && vendor < 2 && verifiedInterface &&
            (state == DriverState::Missing || state == DriverState::Incompatible);
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
struct DriverInstallResult {
    bool ok = false, cancelled = false;
    QString error;
    QString log = {};
    bool rebootRequired = false, retryable = false;
};
enum class DriverPhase { Installing, Recovering, Verifying };
using DriverProgress = std::function<void(DriverPhase)>;
const DriverDevice *FindDriverDevice(const QVector<DriverDevice> &, const DriverDevice &);
DriverScan ScanWindowsUsbDrivers(bool probe = false);
QVector<QPair<QString, QString>> WindowsAdbLocations();
DriverInstallResult InstallWindowsUsbDriver(const DriverDevice &, const QVector<DriverDevice> &affected);
DriverInstallResult InstallWindowsUsbDriverWithProgress(const DriverDevice &, const QVector<DriverDevice> &, const DriverProgress &);
DriverInstallResult InstallWindowsUsbDriverElevated(const DriverDevice &, const QVector<DriverDevice> &, const DriverProgress & = {});
}
