#pragma once
#include "backends/allwinner/Transport.h"
#include "platform/UsbLocation.h"
#include <QElapsedTimer>
#include <QObject>
#include "gui/ModeSwitchFailure.h"
#include <QTimer>
#include <functional>

Q_DECLARE_METATYPE(core::DeviceRef)

namespace gui {
struct FelSwitchRequest {
    quint64 id = 0;
    platform::UsbLocation target;
    core::Cancellation cancelled;
    int timeoutMs = 45000;
};
// Lives on the USB worker. The hooks use the same backend/session owner as tasks.
class AllwinnerSwitchMonitor : public QObject {
    Q_OBJECT
  public:
    struct Hooks {
        std::function<std::vector<core::DeviceDescriptor>()> scan;
        std::function<core::allwinner::DeviceDetails(const core::DeviceRef &, const core::Cancellation &,
                                                     std::chrono::steady_clock::time_point)>
            connect;
        std::function<void(const core::DeviceRef &)> release;
    };
    explicit AllwinnerSwitchMonitor(Hooks, QObject *parent = nullptr);
    void prepare(const FelSwitchRequest &);
    void cancel(quint64);
  signals:
    void ready(quint64 id);
    void completed(quint64 id, bool ok, const QString &message, const core::DeviceRef &target, gui::ModeSwitchFailure failure = gui::ModeSwitchFailure::Generic);
    void trace(const QString &message);

  private:
    void poll();
    void finish(bool, const QString &, const core::DeviceRef & = {}, ModeSwitchFailure = ModeSwitchFailure::Generic);
    Hooks m_hooks;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
    FelSwitchRequest m_request;
    QString m_lastError;
    bool m_seen = false, m_present = false, m_accessBlocked = false;
    quint64 m_retainedId = 0;
    core::DeviceRef m_retained;
};
} // namespace gui
Q_DECLARE_METATYPE(gui::FelSwitchRequest)
