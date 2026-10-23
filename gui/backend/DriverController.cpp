#include "gui/backend/DriverController.h"
#include <QCoreApplication>
#include <QThread>
#include <QSettings>
#include <QJsonDocument>
#include <QDateTime>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dbt.h>
#endif

namespace gui {
namespace {
DriverOperation operationForDevice(const DriverDevice &device) {
    return device.vendor == 0 ? DriverOperation::RockusbAssistant :
           device.vendor == 2 ? DriverOperation::QualcommInstall : DriverOperation::WinUsb;
}
}

DriverController::DriverController(QObject *parent) : DriverController(DriverHooks{},
#ifdef Q_OS_WIN
    true,
#else
    false,
#endif
    parent) {}
DriverController::DriverController(DriverHooks hooks, bool available, QObject *parent)
    : QObject(parent), m_hooks(std::move(hooks)), m_available(available) {
    if (m_available) {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "upgrade_tool", "upgrade_tool");
        const auto saved = QJsonDocument::fromJson(settings.value("drivers/pending").toByteArray()).object();
        if (saved["schema"].toInt() == 1) m_pending = DriverDevice::fromJson(saved["device"].toObject());
    }
    m_debounce.setSingleShot(true); m_debounce.setInterval(500);
    connect(&m_debounce, &QTimer::timeout, this, [this] { refresh(false); });
    m_verifyTimer.setSingleShot(true); m_verifyTimer.setInterval(500);
    connect(&m_verifyTimer, &QTimer::timeout, this, [this] { scan(true); });
    m_progressTimer.setInterval(1000);
    connect(&m_progressTimer, &QTimer::timeout, this, [this] {
        m_elapsedSeconds = int(m_installElapsed.elapsed() / 1000);
        emit changed();
    });
    if (m_available) QCoreApplication::instance()->installNativeEventFilter(this);
}
DriverController::~DriverController() {
    QCoreApplication::instance()->removeNativeEventFilter(this);
    if (m_scanThread) m_scanThread->wait();
    if (m_installThread) m_installThread->wait();
}
QVariantMap DriverController::describe(const DriverDevice &d) const {
    QString state;
    switch (d.state) {
    case DriverState::Missing: state = tr("Driver missing"); break;
    case DriverState::Incompatible: state = tr("Incompatible driver"); break;
    case DriverState::Bound: state = tr("Driver installed"); break;
    case DriverState::Ready: state = tr("Ready"); break;
    case DriverState::AccessFailed: state = tr("Cannot access device"); break;
    case DriverState::DeviceError: state = tr("Device error (%1)").arg(d.problem); break;
    case DriverState::RebootRequired: state = tr("Restart Windows required"); break;
    }
    const bool installing = m_busy && d.bindingId() == m_target.bindingId();
    if (installing) state = phaseText();
    QString diagnostic = d.diagnostic;
    if (d.vendor == 2 && d.state == DriverState::Missing && !d.canInstall())
        diagnostic = tr("Install the device manufacturer's driver for this interface.");
    return {{"instance", d.instance}, {"hardwareId", d.hardwareId}, {"label", d.label}, {"service", installing ? QString{} : d.service},
        {"location", d.location}, {"stateText", state}, {"diagnostic", diagnostic}, {"attention", !installing && d.needsAttention()},
        {"canInstall", !installing && !m_detectionFailed && d.canInstall()}, {"replace", d.vendor == 1 && !d.service.isEmpty()}, {"vendor", d.vendor},
        {"identity", QString("%1:%2").arg(d.vid, 4, 16, QChar('0')).arg(d.pid, 4, 16, QChar('0'))}};
}
QVariantList DriverController::devices() const {
    QVariantList result;
    for (const auto &d : m_devices) if (d.vendor == m_vendor) result.append(describe(d));
    return result;
}
int DriverController::attentionCount() const {
    int count = 0;
    for (const auto &d : m_devices) if (d.vendor == m_vendor && d.needsAttention()) ++count;
    return count;
}
QVariantList DriverController::confirmation() const {
    QVariantList result;
    for (const auto &d : m_affected) result.append(describe(d));
    return result;
}
bool DriverController::canUninstallQualcomm() const {
    return m_available && m_vendor == 2 && m_qualcommInstalled && !locked() && !checking() && !m_stopping;
}
QString DriverController::confirmationTitle() const {
    if (m_operation == DriverOperation::QualcommUninstall) return tr("Uninstall Qualcomm drivers?");
    if (m_operation == DriverOperation::QualcommInstall) return tr("Install Qualcomm drivers?");
    if (m_operation == DriverOperation::RockusbAssistant) return tr("Install Rockchip drivers?");
    return tr("Install WinUSB?");
}
QString DriverController::confirmationText() const {
    if (m_operation == DriverOperation::RockusbAssistant)
        return tr("Install the driver in the Rockchip driver assistant, then close its window. This tool will check the connected device afterward.");
    if (m_operation == DriverOperation::QualcommUninstall)
        return tr("This removes the system Qualcomm driver package and may affect other Qualcomm devices.");
    return {};
}
bool DriverController::canConfirm() const {
    return m_available && !locked() && !checking() && !m_stopping &&
        (m_operation == DriverOperation::QualcommUninstall ? canUninstallQualcomm() :
            !m_detectionFailed && !m_affected.isEmpty());
}
QString DriverController::summaryLevel() const {
    if (!m_available) return QStringLiteral("neutral");
    if (m_busy) return QStringLiteral("busy");
    return m_detectionFailed || attentionCount() > 0 || hasPending() ? QStringLiteral("warning") : QStringLiteral("neutral");
}
QString DriverController::summaryText() const {
    if (!m_available) return {};
    if (m_busy) return phaseText();
    if (m_detectionFailed) return tr("Driver detection failed");
    const int count = attentionCount();
    if (count > 1) return tr("Drivers need attention (%1)").arg(count);
    for (const auto &device : m_devices)
        if (device.vendor == m_vendor && device.needsAttention())
            return describe(device).value("stateText").toString();
    if (hasPending()) return tr("Driver setup needs attention");
    return {};
}
QString DriverController::phaseText() const {
    if (m_operation == DriverOperation::RockusbAssistant && m_phase == DriverPhase::Installing)
        return tr("Complete installation in the Rockchip driver assistant, then close it…");
    if (m_operation == DriverOperation::QualcommUninstall) {
        if (m_phase == DriverPhase::Preparing) return tr("Preparing driver removal…");
        return tr("Windows is removing the Qualcomm drivers…");
    }
    switch (m_phase) {
    case DriverPhase::Preparing: return tr("Preparing driver installation…");
    case DriverPhase::Installing: return tr("Windows is installing the driver…");
    case DriverPhase::Bound: return tr("Driver bound. Waiting for Windows to finish…");
    case DriverPhase::Recovering: return tr("Recovering device…");
    case DriverPhase::Verifying: return tr("Checking driver…");
    }
    return {};
}
QString DriverController::progressText() const {
    return m_busy ? tr("%1 · %2 s elapsed").arg(m_message).arg(m_elapsedSeconds) : m_message;
}
void DriverController::setPhase(DriverPhase phase) {
    if (m_phase != phase) {
        m_phase = phase;
        emit diagnostic(QStringLiteral("Phase [%1 ms]: %2").arg(m_installElapsed.elapsed()).arg(phaseText()));
        emit devicesChanged();
    }
    setMessage(phaseText());
}
void DriverController::retranslate() {
    if (m_busy) setMessage(phaseText());
    emit devicesChanged(); emit changed(); emit confirmationChanged();
}
void DriverController::setVendor(int vendor) {
    if (m_vendor == vendor) return;
    m_vendor = vendor; m_affected.clear(); m_operation = DriverOperation::WinUsb;
    emit devicesChanged(); emit changed(); emit confirmationChanged();
    refresh(false);
}
void DriverController::setSuspended(bool suspended) {
    if (m_suspended == suspended) return;
    m_suspended = suspended; ++m_generation;
    emit changed();
    if (!suspended && !m_busy) refresh(false);
}
void DriverController::poll(bool probe) {
    m_probeOnPoll |= probe;
    for (const auto &d : m_devices) m_probeOnPoll |= d.state == DriverState::AccessFailed;
    if (!m_lastScan.isValid() || m_lastScan.elapsed() >= 5000) refresh(m_probeOnPoll);
}
void DriverController::refresh(bool probe) {
    if (!m_available || m_stopping || m_suspended || m_busy) return;
    scan(probe || hasPending());
}
void DriverController::scan(bool probe) {
    if (m_stopping) return;
    if (m_scanThread) { m_scanAgain = true; m_probeAgain |= probe; return; }
    if (probe) m_probeOnPoll = false;
    const unsigned generation = m_generation;
    const auto result = std::make_shared<DriverScan>();
    const auto scanner = m_hooks.scan;
    auto *thread = QThread::create([result, scanner, probe] {
        try { *result = scanner(probe); }
        catch (const std::exception &e) { result->error = QString::fromUtf8(e.what()); }
    });
    thread->setParent(this); m_scanThread = thread; m_lastScan.start();
    connect(thread, &QThread::finished, this, [this, thread, result, generation, probe] {
        m_scanThread = nullptr; thread->deleteLater();
        m_qualcommInstalled = m_hooks.qualcommInstalled();
        if (!m_stopping && generation == m_generation) {
            if (result->error.isEmpty()) {
                if (!probe) for (auto &d : result->devices) {
                    if (d.state != DriverState::Bound || (d.vendor == 2 && !d.verifiedInterface)) continue;
                    for (const auto &previous : m_devices) if (previous.fingerprint() == d.fingerprint() &&
                        (previous.state == DriverState::Ready || previous.state == DriverState::AccessFailed)) {
                        d.state = previous.state; d.diagnostic = previous.diagnostic; break;
                    }
                }
                if (m_devices != result->devices) { m_devices = result->devices; emit devicesChanged(); }
                if (m_detectionFailed) {
                    m_detectionFailed = false; setMessage({}); emit devicesChanged();
                }
            } else {
                m_detectionFailed = true;
                emit devicesChanged();
                setMessage(tr("Driver detection failed: %1").arg(result->error));
            }
        }
        emit changed();
        if (m_stopping) return;
        if (m_installPending) { beginInstall(); return; }
        const bool fresh = generation == m_generation && result->error.isEmpty() && probe;
        if (m_verifying) {
            const auto *device = fresh ? FindDriverDevice(result->devices, m_target) : nullptr;
            emit diagnostic(QStringLiteral("Verification [%1 ms]: %2").arg(m_verifyElapsed.elapsed()).arg(device
                ? QStringLiteral("%1 service=%2 problem=%3 state=%4 %5").arg(device->instance, device->service)
                    .arg(device->problem).arg(int(device->state)).arg(device->diagnostic)
                : result->error.isEmpty() ? QStringLiteral("Device absent or ambiguous") : result->error));
            if (device && device->state == DriverState::RebootRequired) {
                m_rebootRequired = true;
                finish(false, tr("Restart Windows, then open this tool to check the driver.")); return;
            }
            if (device && (device->state == DriverState::Ready ||
                (device->vendor == 2 && device->state == DriverState::Bound && !device->port.isEmpty() && device->problem == 0))) {
                finish(true, tr("Driver installed")); return;
            }
            if (m_verifyElapsed.elapsed() >= m_hooks.verificationTimeoutMs) {
                QString message = tr("Device is not ready. Close other device tools, reconnect it, and refresh.");
                if (m_detectionFailed) message = tr("Driver detection failed: %1").arg(result->error);
                else if (m_operation == DriverOperation::QualcommInstall || m_operation == DriverOperation::RockusbAssistant) {
                    if (!device) message = tr("Device disconnected. Reconnect it and refresh to check the driver.");
                    else if (device->state == DriverState::Missing)
                        message = tr("The device driver is still missing. See the driver log for details.");
                }
                finish(false, message); return;
            }
            m_verifyTimer.start(); return;
        }
        if (fresh && hasPending() && !m_busy && !m_rebootRequired) {
            const auto *device = FindDriverDevice(result->devices, m_pending);
            if (device && (device->state == DriverState::Ready ||
                (device->vendor == 2 && device->state == DriverState::Bound && !device->port.isEmpty() && device->problem == 0))) {
                forgetPending(); setMessage(tr("Driver installed"));
            }
        }
        if (m_scanAgain && !m_suspended && !m_busy) {
            const bool probe = m_probeAgain; m_scanAgain = m_probeAgain = false; scan(probe);
        }
    });
    thread->start(); emit changed();
}
void DriverController::prepareInstall(const QString &instance) {
    if (!m_available || locked() || checking() || m_stopping || m_detectionFailed) return;
    m_affected.clear();
    m_target = {};
    for (const auto &d : m_devices) if (d.instance == instance && d.vendor == m_vendor && d.canInstall()) m_target = d;
    if (m_target.instance != instance || !m_target.canInstall()) return;
    m_operation = operationForDevice(m_target);
    for (const auto &d : m_devices)
        if (d.bindingId() == m_target.bindingId()) m_affected.append(d);
    emit confirmationChanged(); emit confirmationRequested();
}
void DriverController::prepareQualcommUninstall() {
    if (!canUninstallQualcomm()) return;
    m_operation = DriverOperation::QualcommUninstall;
    m_target = {}; m_affected.clear();
    emit confirmationChanged(); emit confirmationRequested();
}
void DriverController::confirmInstall() {
    if (!canConfirm()) return;
    m_busy = true; ++m_generation; m_scanAgain = m_probeAgain = false;
    m_installElapsed.start(); m_elapsedSeconds = 0; m_phase = DriverPhase::Preparing;
    m_progressTimer.start(); setMessage(phaseText());
    emit diagnostic(QStringLiteral("Installation started %1 target=%2 binding=%3 affected=%4")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), m_target.instance, m_target.bindingId()).arg(m_affected.size()));
    emit busyChanged(); emit devicesChanged(); emit maintenanceRequested();
}
void DriverController::beginInstall() {
    if (!m_busy || m_installThread || m_verifying || m_stopping) return;
    if (m_scanThread) { m_installPending = true; return; }
    m_installPending = false;
    if (m_operation != DriverOperation::QualcommUninstall) rememberPending();
    const auto result = std::make_shared<DriverInstallResult>();
    const auto installer = m_hooks.install;
    const auto installerWithProgress = m_hooks.installWithProgress;
    const auto operation = m_operation;
    const auto driverOperation = m_hooks.driverOperation;
    const auto scanner = m_hooks.scan;
    const auto target = m_target; const auto affected = m_affected;
    auto *thread = QThread::create([this, result, installer, installerWithProgress, target, affected, operation, driverOperation, scanner] {
        try {
            const DriverProgress progress = [this](DriverPhase phase) {
                QMetaObject::invokeMethod(this, [this, phase] {
                    if (!m_busy || m_stopping || m_verifying) return;
                    setPhase(phase);
                }, Qt::QueuedConnection);
            };
            if (operation == DriverOperation::QualcommInstall || operation == DriverOperation::RockusbAssistant) {
                const auto current = scanner(false);
                const auto *device = FindDriverDevice(current.devices, target);
                if (!current.error.isEmpty() || !device || device->vendor != (operation == DriverOperation::RockusbAssistant ? 0 : 2) || !device->canInstall() ||
                    device->fingerprint() != target.fingerprint()) {
                    result->error = tr("Device or driver changed; refresh and select it again."); return;
                }
            }
            if (operation != DriverOperation::WinUsb) *result = driverOperation(operation, target, affected, progress);
            else if (installerWithProgress) *result = installerWithProgress(target, affected, progress);
            else if (installer) { progress(DriverPhase::Installing); *result = installer(target, affected); }
            else *result = InstallWindowsUsbDriverWithProgress(target, affected, progress);
        }
        catch (const std::exception &e) { result->error = QString::fromUtf8(e.what()); }
    });
    thread->setParent(this); m_installThread = thread;
    setPhase(DriverPhase::Preparing);
    connect(thread, &QThread::finished, this, [this, thread, result] {
        m_installThread = nullptr; thread->deleteLater();
        if (!result->log.isEmpty()) emit diagnostic(result->log);
        if (m_stopping) { finish(false, tr("Driver installation cancelled")); return; }
        if (result->rebootRequired) {
            m_rebootRequired = true;
            finish(false, tr("Restart Windows, then open this tool to check the driver.")); return;
        }
        if (!result->ok) {
            if (result->cancelled && m_operation != DriverOperation::QualcommUninstall) forgetPending();
            finish(false, result->cancelled ? tr("Driver setup cancelled") : tr("Driver setup failed: %1").arg(result->error)); return;
        }
        if (m_operation == DriverOperation::QualcommUninstall) {
            m_qualcommInstalled = m_hooks.qualcommInstalled();
            if (m_pending.hardwareId.contains("VID_05C6", Qt::CaseInsensitive)) forgetPending();
            finish(true, tr("Qualcomm drivers uninstalled"));
            refresh(true); return;
        }
        m_verifying = true; m_verifyElapsed.start(); setPhase(DriverPhase::Verifying); scan(true);
    });
    thread->start();
}
void DriverController::finish(bool ok, const QString &message) {
    m_progressTimer.stop();
    m_elapsedSeconds = int(m_installElapsed.elapsed() / 1000);
    emit diagnostic(QStringLiteral("Installation finished [%1 ms]: ok=%2 reboot=%3 %4")
        .arg(m_installElapsed.elapsed()).arg(ok).arg(m_rebootRequired).arg(message));
    m_verifyTimer.stop(); m_verifying = m_installPending = false; m_busy = false;
    if (ok && !m_stopping && m_operation != DriverOperation::QualcommUninstall) forgetPending();
    m_affected.clear(); setMessage(message); emit busyChanged(); emit devicesChanged(); emit confirmationChanged(); emit finished(ok, message);
}
void DriverController::rememberPending() {
    m_pending = m_target;
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "upgrade_tool", "upgrade_tool");
    settings.setValue("drivers/pending", QJsonDocument(QJsonObject{{"schema", 1}, {"device", m_pending.toJson()}}).toJson(QJsonDocument::Compact));
    settings.sync();
    emit changed();
}
void DriverController::forgetPending() {
    m_pending = {}; m_rebootRequired = false;
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "upgrade_tool", "upgrade_tool");
    settings.remove("drivers/pending"); settings.sync(); emit changed();
}
bool DriverController::canResume() const {
    if (!hasPending() || locked() || checking() || m_detectionFailed || m_rebootRequired) return false;
    const auto *device = FindDriverDevice(m_devices, m_pending);
    return device && device->canInstall();
}
QString DriverController::pendingText() const {
    if (!hasPending()) return {};
    const auto *device = FindDriverDevice(m_devices, m_pending);
    QString text;
    if (checking()) text = tr("Checking driver…");
    else if (m_detectionFailed) text = tr("Driver detection failed");
    else if (m_rebootRequired || (device && device->state == DriverState::RebootRequired))
        text = tr("Restart Windows, then open this tool to check the driver.");
    else if (!device) text = tr("Connect the original device and refresh. If its identity changed, select it again.");
    else if (device->canInstall()) text = tr("Driver setup is unfinished. Continue to review driver setup.");
    else text = tr("Device is not ready. Close other device tools, reconnect it, and refresh.");
    return m_pending.hardwareId + '\n' + text;
}
void DriverController::resumePending() {
    if (!canResume()) return;
    const auto *device = FindDriverDevice(m_devices, m_pending);
    // Resumption may concern a different vendor than the currently selected workspace.
    m_affected.clear(); m_target = *device;
    m_operation = operationForDevice(*device);
    for (const auto &d : m_devices) if (d.bindingId() == m_target.bindingId()) m_affected.append(d);
    emit confirmationChanged(); emit confirmationRequested();
}
void DriverController::setMessage(const QString &message) {
    if (message == m_message) return;
    m_message = message; emit changed();
}
void DriverController::shutdown() {
    m_stopping = true; ++m_generation; m_debounce.stop(); m_verifyTimer.stop();
    if (m_busy && !m_installThread) finish(false, tr("Driver installation cancelled"));
}
bool DriverController::stopped() const { return !m_scanThread && !m_installThread && !m_busy; }
bool DriverController::nativeEventFilter(const QByteArray &, void *message, qintptr *) {
#ifdef Q_OS_WIN
    const auto *event = static_cast<MSG *>(message);
    if (event->message == WM_DEVICECHANGE && (event->wParam == DBT_DEVNODES_CHANGED ||
        event->wParam == DBT_DEVICEARRIVAL || event->wParam == DBT_DEVICEREMOVECOMPLETE)) m_debounce.start();
#else
    Q_UNUSED(message);
#endif
    return false;
}
}
