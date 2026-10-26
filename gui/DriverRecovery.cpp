#include "gui/DriverRecovery.h"
#include <QElapsedTimer>

namespace gui {
const DriverDevice *FindDriverDevice(const QVector<DriverDevice> &devices, const DriverDevice &target) {
    const DriverDevice *found = nullptr;
    for (const auto &d : devices) {
        if (d.bindingId() != target.bindingId() || d.interfaceNumber != target.interfaceNumber) continue;
        if (d.instance.compare(target.instance, Qt::CaseInsensitive) &&
            (target.location.isEmpty() || d.location.compare(target.location, Qt::CaseInsensitive))) continue;
        if (found) return nullptr;
        found = &d;
    }
    return found;
}

static DriverInstallResult RecoverNativeDriver(const DriverDevice &target, const DriverRecoveryHooks &hooks, int vendor, const QString &service) {
    DriverInstallResult result{true, false, {}};
    QElapsedTimer elapsed; elapsed.start();
    auto record = [&](const QString &text) {
        result.log += QStringLiteral("%1 recovery [%2 ms]: %3\n").arg(service).arg(elapsed.elapsed()).arg(text);
    };
    if (hooks.progress) hooks.progress(DriverPhase::Verifying);
    DriverScan snapshot;
    for (int waited = 0; waited <= 3000; waited += 500) {
        snapshot = hooks.scan();
        const auto *device = snapshot.error.isEmpty() ? FindDriverDevice(snapshot.devices, target) : nullptr;
        if (!snapshot.error.isEmpty()) record("scan: " + snapshot.error);
        if (device && device->state == DriverState::RebootRequired) {
            result.rebootRequired = true;
            record("Windows restart required");
            return result;
        }
        if (device && device->state == DriverState::Ready) {
            record("device ready without restart");
            return result;
        }
        if (waited < 3000) hooks.wait(500);
    }
    const auto *device = snapshot.error.isEmpty() ? FindDriverDevice(snapshot.devices, target) : nullptr;
    if (!device || device->vendor != vendor || !device->verifiedInterface ||
        ((vendor == 1 || !device->service.isEmpty()) && device->service.compare(service, Qt::CaseInsensitive))) {
        record("restart skipped: target absent, ambiguous, or driver changed");
        return result;
    }
    // Pass the post-install snapshot: service/problem changes are expected.
    // The restart implementation checks this snapshot again before acting.
    if (hooks.progress) hooks.progress(DriverPhase::Recovering);
    record("restarting " + device->instance);
    const auto restart = hooks.restart(*device);
    result.log += restart.log;
    record(QStringLiteral("restart ok=%1 reboot=%2 error=%3")
        .arg(restart.ok).arg(restart.rebootRequired).arg(restart.error));
    result.rebootRequired = restart.rebootRequired;
    // A failed restart is recoverable by physically reconnecting the device.
    // The controller always probes again before reporting the final outcome.
    return result;
}

DriverInstallResult RecoverRockusbDriver(const DriverDevice &target, const DriverRecoveryHooks &hooks) {
    return RecoverNativeDriver(target, hooks, 0, QStringLiteral("Rockusb"));
}
DriverInstallResult RecoverAllwinnerDriver(const DriverDevice &target, const DriverRecoveryHooks &hooks) {
    return RecoverNativeDriver(target, hooks, 1, QStringLiteral("usbUDisc"));
}
}
