#include "platform/UsbLocation.h"
#include "gui/EdlRunner.h"
#include "gui/UsbAccess.h"
#include "gui/MessageDisplay.h"
#include <QElapsedTimer>
#include <QThread>
#include <QMap>
#include <utility>
#include <libusb.h>
namespace gui {
namespace {
core::Message Text(const char *key, std::vector<std::string> args = {}) {
    return {"gui::EdlRunner", key, std::move(args)};
}
quint16 crc(const QByteArray &data) {
    quint16 value = 0xffff;
    for (unsigned char byte : data) {
        value ^= byte;
        for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) ? 0x8408 : 0);
    }
    return value ^ 0xffff;
}
QString targetKey(const EdlEndpoint &e) { return e.location.isEmpty() ? e.id : e.location; }
}
QByteArray EdlRunner::encode(const QByteArray &payload) {
    QByteArray data = payload, frame;
    auto sum = crc(data); data.append(char(sum & 255)); data.append(char(sum >> 8));
    for (unsigned char c : data) {
        if (c == 0x7d || c == 0x7e) { frame.append(char(0x7d)); frame.append(char(c ^ 0x20)); }
        else frame.append(char(c));
    }
    frame.append(char(0x7e)); return frame;
}
bool EdlRunner::decode(QByteArray &buffer, QByteArray &payload) {
    int end;
    while ((end = buffer.indexOf(char(0x7e))) >= 0) {
        const auto frame = buffer.left(end); buffer.remove(0, end + 1);
        QByteArray raw; bool escape = false;
        for (unsigned char c : frame) {
            if (escape) { raw.append(char(c ^ 0x20)); escape = false; }
            else if (c == 0x7d) escape = true;
            else raw.append(char(c));
        }
        if (escape || raw.size() < 3) continue;
        const auto data = raw.left(raw.size() - 2);
        const quint16 sum = quint8(raw[raw.size()-2]) | (quint16(quint8(raw.back())) << 8);
        if (crc(data) == sum) { payload = data; return true; }
    }
    if (buffer.size() > 65536) buffer.clear();
    return false;
}
bool EdlRunner::validVersion(const QByteArray &payload) {
    // DIAG_VERNO_F contains the 55-byte packed version-information response.
    return payload.size() >= 55 && payload[0] == char(0);
}
bool EdlRunner::sameLocation(const EdlEndpoint &a, const EdlEndpoint &b) {
    return platform::UsbLocation{a.location.toStdString(), a.bus, {a.ports.begin(), a.ports.end()}}.matches(
        {b.location.toStdString(), b.bus, {b.ports.begin(), b.ports.end()}});
}
bool BindEdlNativeInterface(EdlEndpoint &endpoint, const QVector<EdlEndpoint> &native) {
    const EdlEndpoint *binding = nullptr;
    for (const auto &row : native) {
        if (row.vid != endpoint.vid || row.pid != endpoint.pid ||
            row.interfaceNumber != endpoint.interfaceNumber || row.instance.isEmpty()) continue;
        if (!endpoint.instance.isEmpty() && endpoint.instance.compare(row.instance, Qt::CaseInsensitive)) continue;
        if (!endpoint.serial.isEmpty() && !row.serial.isEmpty() && endpoint.serial.compare(row.serial, Qt::CaseInsensitive) != 0) continue;
        if (!endpoint.ports.isEmpty() && !row.ports.isEmpty() && endpoint.ports != row.ports) continue;
        if (endpoint.bus && row.bus && endpoint.bus != row.bus) continue;
        if (endpoint.instance.isEmpty() && !EdlRunner::sameLocation(endpoint, row) &&
            (endpoint.serial.isEmpty() || endpoint.serial.compare(row.serial, Qt::CaseInsensitive) != 0) &&
            (endpoint.ports.isEmpty() || endpoint.ports != row.ports)) continue;
        if (binding) { binding = nullptr; break; }
        binding = &row;
    }
    if (!binding) {
        endpoint.accessError = LIBUSB_ERROR_NOT_FOUND;
        endpoint.accessReason = EdlRunner::tr("Cannot uniquely identify the interface driver. Reconnect the device and refresh.");
        return false;
    }
    endpoint.instance = binding->instance;
    if (!binding->location.isEmpty()) endpoint.location = binding->location;
    endpoint.driverService = binding->driverService;
    endpoint.suggested |= binding->suggested;
    if (binding->accessError) {
        endpoint.accessError = binding->accessError;
        endpoint.accessReason = binding->accessReason;
    } else if (!binding->path.isEmpty()) {
        // An official COM binding is usable through the serial endpoint only.
        endpoint.accessError = LIBUSB_ERROR_NOT_SUPPORTED;
        endpoint.accessReason = EdlRunner::tr("Use the diagnostic COM port for this interface.");
    }
    return true;
}
EdlRunner::EdlRunner(QObject *parent) : EdlRunner(DefaultEdlHooks(), 45000, parent) {}
EdlRunner::EdlRunner(EdlHooks hooks, int waitMs, QObject *parent, platform::LogService *log)
    : QObject(parent), m_log(log), m_hooks(std::move(hooks)), m_waitMs(waitMs) {}
EdlRunner::~EdlRunner() {
    shutdown();
    if (m_discoveryThread) { m_discoveryThread->wait(); delete m_discoveryThread; }
    if (m_thread) { m_thread->wait(); delete m_thread; }
}
void EdlRunner::shutdown() {
    m_stopping = true;
    cancel();
    if (m_discoveryCancel) m_discoveryCancel->request_stop();
}
bool EdlRunner::stopped() const {
    return (!m_discoveryThread || m_discoveryThread->isFinished()) && (!m_thread || m_thread->isFinished());
}
void EdlRunner::cancel() { if (m_cancel) m_cancel->request_stop(); }
void EdlRunner::setState(State state) { m_state = state; emit changed(); }
void EdlRunner::setMessage(const QString &message) { m_message = message; emit messageChanged(); emit changed(); }
void EdlRunner::reject(const QString &message) { setMessage(message); emit errorOccurred(message); }
void EdlRunner::setMethod(int method) {
    if (running() || (method != Diag && method != Adb) || method == m_method) return;
    m_method = method;
    rebuildTargets(); emit changed();
}
QString EdlRunner::unavailableReason() const {
    if (running()) return tr("Switching device to EDL mode…");
    if (!m_discovered) return tr("Discovering diagnostic ports and ADB devices…");
    if (!m_targets.isEmpty()) return {};
    for (const auto &endpoint : m_endpoints)
        if (candidate(endpoint) && endpoint.accessError)
            return endpoint.accessReason.isEmpty()
                ? tr("Cannot access the selected interface; check its USB driver and close other device tools.")
                : endpoint.accessReason;
    return m_method == Diag ? tr("No diagnostic interface detected")
                            : tr("No authorized ADB device detected; check ADB and USB debugging");
}
bool EdlRunner::candidate(const EdlEndpoint &e) const {
    if (qtq_download_identity(e.vid,e.pid)) return false;
    if (m_method == Adb) return e.kind == EdlEndpoint::Adb && !e.serial.isEmpty();
    // Host UARTs are not diagnostic candidates. Unknown USB interfaces remain
    // selectable, but require explicit selection and a valid DIAG version reply.
    return e.kind != EdlEndpoint::Adb && (e.suggested ||
        (e.vid != 0 && (!e.location.isEmpty() || !e.ports.isEmpty())));
}
bool EdlRunner::eligible(const EdlEndpoint &e) const {
    return candidate(e) && !e.accessError;
}
QVector<EdlEndpoint> EdlRunner::selectedEndpoints(const QString &targetId, const QString &endpointId) const {
    QVector<EdlEndpoint> selected;
    for (const auto &e : m_endpoints) {
        if (!eligible(e) || targetKey(e) != targetId) continue;
        const bool matches = endpointId.isEmpty() ? (m_method == Adb || e.suggested) : e.id == endpointId;
        if (matches) selected.append(e);
    }
    return selected;
}
bool EdlRunner::canStart(const QString &targetId, const QString &endpointId) const {
    return ready() && selectedEndpoints(targetId, endpointId).size() == 1;
}
void EdlRunner::requestSwitch(const QString &adbPath) {
    if (running()) return;
    if (!ready()) { reject(unavailableReason()); return; }
    // Prefer recognized DIAG devices, but never guess between recognized targets.
    QVariantList preferred;
    for (const auto &value : m_targets)
        if (value.toMap().value("suggested").toBool()) preferred.append(value);
    if (preferred.size() == 1) {
        const QString target = preferred.first().toMap().value("id").toString();
        if (canStart(target, {})) { start(target, m_method, {}, adbPath); return; }
    }
    emit selectionRequested();
}
void EdlRunner::rebuildTargets() {
    QMap<QString, QVariantMap> groups;
    for (const auto &e : m_endpoints) {
        if (!eligible(e)) continue;
        const QString key = targetKey(e);
        auto &group = groups[key];
        group["id"] = key;
        if (!group.contains("label") || e.suggested || e.kind == EdlEndpoint::Adb)
            group["label"] = e.label + (e.location.isEmpty() ? QString() : " — " + e.location);
        auto endpoints = group.value("endpoints").toList();
        endpoints.append(QVariantMap{{"id", e.id}, {"label", e.label}, {"adb", e.kind == EdlEndpoint::Adb}, {"suggested", e.suggested}});
        group["endpoints"] = endpoints;
        group["suggested"] = group.value("suggested").toBool() || e.suggested || e.kind == EdlEndpoint::Adb;
    }
    QVariantList targets;
    for (bool suggested : {true, false})
        for (const auto &group : groups) if (group.value("suggested").toBool() == suggested) targets.append(group);
    if (targets == m_targets) return;
    m_targets = targets;
    emit targetsChanged();
}
void EdlRunner::refresh(const QString &adbPath) {
    if (m_stopping || m_discoveryThread || running()) return;
    m_discovering = true;
    const auto cancel = std::make_shared<std::stop_source>();
    m_discoveryCancel = cancel;
    const auto generation = m_generation;
    auto endpoints = std::make_shared<QVector<EdlEndpoint>>();
    auto error = std::make_shared<QString>();
    m_discoveryThread = QThread::create([=, this] { *endpoints = m_hooks.discover(adbPath, cancel, *error); });
    connect(m_discoveryThread, &QThread::finished, this, [=, this] {
        m_discoveryThread->deleteLater(); m_discoveryThread = nullptr;
        m_discovering = false;
        if (!cancel->stop_requested() && generation == m_generation && !running()) {
            m_endpoints = *endpoints; m_discovered = true; m_discoveryError = *error;
            rebuildTargets();
        }
        emit changed();
    });
    emit changed();
    m_discoveryThread->start();
}
void EdlRunner::start(const QString &targetId, int method, const QString &endpointId, const QString &adbPath) {
    if (m_stopping || running()) return;
    if (method != m_method || !canStart(targetId, endpointId)) {
        reject(tr("No matching switch endpoint is available. Refresh the device list.")); return;
    }
    const auto selected = selectedEndpoints(targetId, endpointId).first();
    ++m_generation;
    if (m_discoveryCancel) m_discoveryCancel->request_stop();
    const auto cancel = std::make_shared<std::stop_source>();
    m_cancel = cancel;
    setState(Checking);
    emit started(); setMessage(tr("Checking the selected device…"));
    struct Result { bool ok = false; core::Message message; QString displayOverride, detail; EdlEndpoint edl; ModeSwitchFailure failure = ModeSwitchFailure::Generic; };
    auto result = std::make_shared<Result>();
    const auto context = m_logContext;
    m_thread = QThread::create([=, this] {
        core::ScopedLogContext logContext(context);
        auto progress = [this](State state, const core::Message &text) {
            if (m_log) m_log->write(core::LogLevel::Info, "Qualcomm", text);
            QMetaObject::invokeMethod(this, [this,state,text] {
                if (running()) { setState(state); setMessage(Display(text)); }
            }, Qt::QueuedConnection);
        };
        QString error;
        EdlEndpoint target;
        bool found = false;
        const auto fresh = (method == Adb && m_hooks.discoverFresh ? m_hooks.discoverFresh : m_hooks.discover)(adbPath, cancel, error);
        if (cancel->stop_requested()) return;
        for (const auto &e : fresh) {
            if (!qtq_download_identity(e.vid,e.pid) && e.id == selected.id && targetKey(e) == targetId &&
                e.kind == selected.kind && e.vid == selected.vid && e.pid == selected.pid &&
                e.bus == selected.bus && e.ports == selected.ports &&
                e.instance == selected.instance && e.serial.compare(selected.serial, Qt::CaseInsensitive) == 0 && e.interfaceNumber == selected.interfaceNumber &&
                e.alternate == selected.alternate && e.path == selected.path &&
                e.in == selected.in && e.out == selected.out && e.address == selected.address &&
                e.downloadInterface == selected.downloadInterface) {
                if (found) { result->message = Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "The selected switch endpoint is no longer available. Refresh and select it again.")); return; }
                target = e; found = true;
            }
        }
        if (!found) { result->message = Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "The selected switch endpoint is no longer available. Refresh and select it again.")); return; }
        if (target.accessError) {
            result->failure = ScanSwitchAccess(2, target.instance, target.location, target.interfaceNumber);
            result->failure.sourceInterface = true;
            result->message = Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "Cannot access the selected interface; check its USB driver and close other device tools."));
            result->displayOverride = target.accessReason;
            return;
        }
        const bool useDiag = method == Diag;
        std::unique_ptr<DiagSession> session;
        if (useDiag) {
            progress(Verifying, Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "Verifying the selected DIAG interface…")));
            session = m_hooks.openDiag(target, cancel, error);
            if (!session) { result->message = Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "DIAG verification failed: %1"), {error.toStdString()}); return; }
            bool sent = false;
            const auto response = session->exchange(QByteArray(1, char(0)), cancel, error, sent);
            if (!validVersion(response)) {
                result->message = Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "DIAG verification failed: %1"), {error.isEmpty() ? "Invalid or unsupported version response" : error.toStdString()}); return;
            }
        }
        if (cancel->stop_requested()) return;
        if (useDiag) {
            progress(Sending, Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "Sending DIAG EDL request…")));
            bool sent = false;
            const auto reply = session->exchange(QByteArray::fromHex("4b650100"), cancel, error, sent);
            if (!sent) { result->message = Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "EDL request could not be sent: %1"), {error.toStdString()}); return; }
            if (!reply.isEmpty() && !reply.startsWith(QByteArray::fromHex("4b650100"))) {
                result->message = Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "The device rejected the DIAG EDL request.")); return;
            }
        } else {
            progress(Sending, Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "Sending ADB EDL request…")));
            if (!m_hooks.reboot(adbPath, target.serial, cancel, error)) { result->message = Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "EDL request could not be sent: %1"), {error.toStdString()}); return; }
        }
        if (cancel->stop_requested()) return;
        session.reset();
        progress(Waiting, Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "Request sent; waiting for the target device in 9008 mode…")));
        QElapsedTimer timer; timer.start();
        EdlEndpoint inaccessible;
        do {
            QVector<EdlEndpoint> matches;
            for (const auto &e : m_hooks.scan())
                if (e.mode() == QTQ_EDL && sameLocation(target, e)) matches.append(e);
            result->failure = ModeSwitchFailure::Generic;
            if (matches.size() > 1) {
                result->message = Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "Multiple EDL interfaces appeared at the target location; refresh and select one explicitly.")); return;
            }
            // Keep waiting through driver installation/rebinding; only a current
            // target may classify the final timeout as an access problem.
            result->message = {};
            result->displayOverride.clear();
            if (matches.size() == 1) {
                const auto &endpoint = matches.first();
                if (!endpoint.accessError) {
                    result->ok = true; result->edl = endpoint;
                    result->message = Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "Target device detected in 9008 mode.")); return;
                }
                result->failure = ModeSwitchFailure::DeviceAccess;
                inaccessible = endpoint;
                result->message = Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "Cannot access the selected interface; check its USB driver and close other device tools."));
                result->displayOverride = endpoint.accessReason;
            }
            if (cancel->stop_requested()) return;
            if (timer.elapsed() >= m_waitMs) break;
            QThread::msleep(100);
        } while (true);
        if (result->failure == ModeSwitchFailure::DeviceAccess) {
            result->failure = ScanSwitchAccess(2, inaccessible.instance, inaccessible.location, inaccessible.interfaceNumber);
            return;
        }
        result->message = target.location.isEmpty() && target.ports.isEmpty()
            ? Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "Request sent, but the target USB location could not be identified; success cannot be confirmed."))
            : Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "The target device did not appear in 9008 mode before the timeout."));
        result->detail = error;
    });
    connect(m_thread, &QThread::finished, this, [=, this] {
        m_thread->deleteLater(); m_thread = nullptr;
        if (cancel->stop_requested()) { result->ok = false; result->displayOverride.clear(); result->message = Text(QT_TRANSLATE_NOOP("gui::EdlRunner", "Switch cancelled. A reboot request already sent may not be reversible.")); }
        m_endpoints.clear(); m_targets.clear(); m_discovered = false;
        setState(cancel->stop_requested() ? Cancelled : result->ok ? Completed : Failed);
        emit targetsChanged();
        core::ScopedLogContext logContext(context);
        if (m_log) {
            m_log->write(result->ok || cancel->stop_requested() ? core::LogLevel::Info : core::LogLevel::Error,
                         "Qualcomm", result->failure == ModeSwitchFailure::DeviceAccess
                             ? Text("EDL switch finished with an interface access failure") : result->message, false);
            if (!result->detail.isEmpty()) m_log->write(core::LogLevel::Debug, "Qualcomm", {{}, result->detail.toStdString(), {}}, false);
        }
        const auto message = (result->displayOverride.isEmpty() ? Display(result->message) : result->displayOverride)
            + (result->detail.isEmpty() ? QString() : "\n" + result->detail);
        setMessage(message);
        emit finished(result->ok, message, result->edl, result->failure);
    });
    m_thread->start();
}
}
