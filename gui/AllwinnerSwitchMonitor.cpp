#include "gui/AllwinnerSwitchMonitor.h"
#include "backends/DeviceIdentity.h"
#include <algorithm>

namespace gui {
AllwinnerSwitchMonitor::AllwinnerSwitchMonitor(Hooks hooks, QObject *parent)
    : QObject(parent), m_hooks(std::move(hooks)) {
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &AllwinnerSwitchMonitor::poll);
}
void AllwinnerSwitchMonitor::prepare(const FelSwitchRequest &request) {
    if (m_request.id)
        cancel(m_request.id);
    m_request = request;
    m_lastError.clear();
    m_seen = false;
    m_present = false;
    m_accessBlocked = false;
    if (request.cancelled && request.cancelled->stop_requested()) {
        cancel(request.id);
        return;
    }
    if (!request.id || !request.target.known() || !m_hooks.scan || !m_hooks.connect || !m_hooks.release) {
        finish(false, tr("Cannot uniquely identify the target USB port; no switch command was sent."));
        return;
    }
    m_elapsed.start();
    m_timer.start();
    emit trace(QString("fel_monitor_ready attempt=%1 native=%2 bus=%3")
                   .arg(request.id)
                   .arg(QString::fromStdString(request.target.native))
                   .arg(request.target.bus));
    emit ready(request.id);
}
void AllwinnerSwitchMonitor::cancel(quint64 id) {
    if (id == m_request.id && id) {
        if (m_request.cancelled)
            m_request.cancelled->request_stop();
        m_timer.stop();
        m_request.id = 0;
    }
    if (id == m_retainedId && m_retained) {
        m_hooks.release(m_retained);
        m_retained = {};
        m_retainedId = 0;
    }
}
void AllwinnerSwitchMonitor::finish(bool ok, const QString &message, const core::DeviceRef &target, ModeSwitchFailure failure) {
    const auto id = m_request.id;
    m_timer.stop();
    m_request.id = 0;
    emit trace(QString("fel_monitor_finished attempt=%1 ok=%2 elapsed_ms=%3 message=%4")
                   .arg(id)
                   .arg(ok)
                   .arg(m_elapsed.isValid() ? m_elapsed.elapsed() : 0)
                   .arg(message));
    emit completed(id, ok, message, target, failure);
}
void AllwinnerSwitchMonitor::poll() {
    if (!m_request.id)
        return;
    if (m_request.cancelled && m_request.cancelled->stop_requested()) {
        cancel(m_request.id);
        return;
    }
    const bool timedOut = m_elapsed.elapsed() >= m_request.timeoutMs;
    auto timeout = [this] {
        finish(false, !m_seen ? tr("No FEL/FES device appeared at the target USB port.")
                              : tr("The target FEL/FES device appeared, but a usable connection could not be "
                                   "established: %1").arg(m_lastError), {},
               m_present && m_accessBlocked ? ModeSwitchFailure::DeviceAccess : ModeSwitchFailure::Generic);
    };
    try {
        m_accessBlocked = false;
        std::vector<core::DeviceDescriptor> matches;
        for (const auto &device : m_hooks.scan()) {
            const auto &identity = core::DeviceAccess::Identity(device.reference);
            const std::vector<int> ports(identity.ports.begin(), identity.ports.end());
            const auto native = identity.physicalLocation.empty()
                                    ? platform::UsbTopologyLocation(identity.bus, ports)
                                    : identity.physicalLocation;
            const platform::UsbLocation location{native, identity.bus, ports};
            if (identity.vendor == core::Vendor::Allwinner && m_request.target.matches(location))
                matches.push_back(device);
        }
        if (matches.size() > 1) {
            finish(false, tr("Multiple flashing interfaces appeared at the target USB port; refresh and "
                             "select one explicitly."));
            return;
        }
        if (matches.empty()) {
            if (m_present) {
                m_lastError = tr("The target device disconnected before the FEL/FES handshake completed.");
                emit trace(QString("fel_disconnected attempt=%1 elapsed_ms=%2")
                               .arg(m_request.id)
                               .arg(m_elapsed.elapsed()));
            }
            m_present = false;
            if (timedOut) timeout();
            return;
        }
        m_seen = true;
        m_present = true;
        const auto &device = matches.front();
        emit trace(QString("fel_candidate attempt=%1 key=%2 instance=%3 access_error=%4 elapsed_ms=%5")
                       .arg(m_request.id)
                       .arg(QString::fromStdString(device.reference.key()))
                       .arg(QString::fromStdString(device.deviceInstance))
                       .arg(device.accessError)
                       .arg(m_elapsed.elapsed()));
        if (device.accessError) {
            m_accessBlocked = true;
            m_lastError = tr("USB driver or permissions are not ready (%1).").arg(device.accessError);
            if (timedOut) timeout();
            return;
        }
        if (timedOut) { timeout(); return; }
        const auto remaining = std::min<qint64>(1500, m_request.timeoutMs - m_elapsed.elapsed());
        if (remaining <= 0)
            return;
        const auto started = std::chrono::steady_clock::now();
        const auto details = m_hooks.connect(device.reference, m_request.cancelled,
                                             started + std::chrono::milliseconds(remaining));
        if (m_request.cancelled && m_request.cancelled->stop_requested()) {
            m_hooks.release(device.reference);
            cancel(m_request.id);
            return;
        }
        if (details.mode != 1 && details.mode != 2) {
            m_hooks.release(device.reference);
            m_lastError = tr("Unsupported Allwinner device mode.");
            return;
        }
        emit trace(QString("fel_handshake attempt=%1 mode=%2 elapsed_ms=%3")
                       .arg(m_request.id)
                       .arg(details.mode)
                       .arg(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count()));
        m_retained = device.reference;
        m_retainedId = m_request.id;
        finish(true,
               tr("%1 device connected on the target USB port.")
                   .arg(details.mode == 1 ? QStringLiteral("FEL") : QStringLiteral("FES")),
               device.reference);
    } catch (const std::exception &error) {
        const auto message = QString::fromUtf8(error.what());
        if (message != m_lastError)
            emit trace(QString("fel_retry attempt=%1 error=%2").arg(m_request.id).arg(message));
        m_lastError = message;
        if (timedOut) timeout();
    }
}
} // namespace gui
