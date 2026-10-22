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

DriverInstallResult RecoverWindowsUsbDriver(const DriverDevice &target, const QVector<DriverDevice> &affected,
                                           const DriverRecoveryHooks &hooks) {
    QElapsedTimer elapsed; elapsed.start();
    QString log;
    int installAttempts = 0;
    DriverScan snapshot;
    auto record = [&](const QString &text) { log += QStringLiteral("[%1 ms] %2\n").arg(elapsed.elapsed()).arg(text); };
    auto scan = [&] {
        snapshot = hooks.scan();
        if (!snapshot.error.isEmpty()) record("scan: " + snapshot.error);
        for (const auto &d : snapshot.devices) if (d.bindingId() == target.bindingId())
            record(QStringLiteral("%1 service=%2 problem=%3 state=%4 diagnostic=%5")
                .arg(d.instance, d.service).arg(d.problem).arg(int(d.state)).arg(d.diagnostic));
    };
    auto safe = [&] {
        if (!snapshot.error.isEmpty() || affected.isEmpty()) return false;
        int count = 0;
        for (const auto &d : snapshot.devices) {
            if (d.bindingId() != target.bindingId()) continue;
            ++count;
            const auto *before = FindDriverDevice(affected, d);
            if (!before || !d.verifiedInterface || d.vendor < 0 || d.vendor >= 2 ||
                (d.service.compare(before->service, Qt::CaseInsensitive) && d.service.compare("WinUSB", Qt::CaseInsensitive))) return false;
        }
        if (count != affected.size()) return false;
        for (const auto &d : affected) if (!FindDriverDevice(snapshot.devices, d)) return false;
        return FindDriverDevice(snapshot.devices, target) != nullptr;
    };
    auto done = [&](DriverInstallResult result) { result.log = log + result.log; return result; };
    auto changed = [&] { return done({false, false, QStringLiteral("Device or driver changed; refresh and select it again.")}); };
    auto install = [&] {
        DriverInstallResult result;
        for (int attempt = 0; attempt < 2; ++attempt) {
            scan();
            if (!safe()) return DriverInstallResult{false, false, QStringLiteral("Device or driver changed; refresh and select it again.")};
            for (const auto &d : snapshot.devices) if (d.bindingId() == target.bindingId() && d.state == DriverState::RebootRequired) {
                result.ok = true; result.rebootRequired = true; return result;
            }
            if (hooks.progress) hooks.progress(DriverPhase::Installing);
            record("install attempt " + QString::number(++installAttempts));
            result = hooks.install(*FindDriverDevice(snapshot.devices, target));
            log += result.log; result.log.clear();
            record(QStringLiteral("install ok=%1 reboot=%2 error=%3").arg(result.ok).arg(result.rebootRequired).arg(result.error));
            if (!result.retryable || result.ok || result.cancelled || result.rebootRequired) break;
            if (attempt == 0) hooks.wait(1000);
        }
        return result;
    };
    auto waitReady = [&](int timeout) {
        if (hooks.progress) hooks.progress(DriverPhase::Verifying);
        for (int waited = 0;; waited += 500) {
            scan();
            const auto *device = snapshot.error.isEmpty() ? FindDriverDevice(snapshot.devices, target) : nullptr;
            if (device && (device->state == DriverState::Ready || device->state == DriverState::RebootRequired)) return;
            if (waited >= timeout) return;
            hooks.wait(500);
        }
    };
    auto reboot = [&] {
        const auto *d = snapshot.error.isEmpty() ? FindDriverDevice(snapshot.devices, target) : nullptr;
        return d && d->state == DriverState::RebootRequired;
    };
    auto ready = [&] {
        const auto *d = snapshot.error.isEmpty() ? FindDriverDevice(snapshot.devices, target) : nullptr;
        return d && d->state == DriverState::Ready;
    };
    auto result = install();
    if (!result.ok || result.cancelled || result.rebootRequired) return done(result);
    waitReady(10000);
    if (reboot()) { result.rebootRequired = true; return done(result); }
    if (ready()) return done(result);
    if (!safe()) return changed();
    const auto device = *FindDriverDevice(snapshot.devices, target);
    if (hooks.progress) hooks.progress(DriverPhase::Recovering);
    record("restarting " + device.instance);
    const auto restart = hooks.restart(device);
    log += restart.log;
    record(QStringLiteral("restart ok=%1 reboot=%2 error=%3").arg(restart.ok).arg(restart.rebootRequired).arg(restart.error));
    if (restart.rebootRequired) { result.rebootRequired = true; return done(result); }
    if (!restart.ok) return done(restart);
    waitReady(20000);
    if (reboot()) { result.rebootRequired = true; return done(result); }
    if (ready()) return done(result);
    if (!safe()) return changed();
    const auto *current = FindDriverDevice(snapshot.devices, target);
    // Reinstall only when Windows is still bound to the original incompatible service.
    if (current->canInstall() && !current->service.compare(target.service, Qt::CaseInsensitive)) {
        result = install();
        if (result.ok && !result.rebootRequired) {
            waitReady(20000);
            result.rebootRequired = reboot();
        }
    }
    // The GUI always performs the final probe using the user's own permissions.
    return done(result);
}
}
