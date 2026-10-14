#include "gui/EdlRunner.h"
#include <QElapsedTimer>
#include <QThread>
#include <QMap>
#include <QSet>
namespace gui {
namespace {
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
    if (!a.location.isEmpty() && a.location == b.location) return true;
    return !a.ports.isEmpty() && a.bus == b.bus && a.ports == b.ports;
}
EdlRunner::EdlRunner(QObject *parent) : EdlRunner(DefaultEdlHooks(), 45000, parent) {}
EdlRunner::EdlRunner(EdlHooks hooks, int waitMs, QObject *parent)
    : QObject(parent), m_hooks(std::move(hooks)), m_waitMs(waitMs) {}
EdlRunner::~EdlRunner() { cancel(); if (m_thread) { m_thread->wait(); delete m_thread; } }
void EdlRunner::cancel() { if (m_cancel) m_cancel->store(true); }
void EdlRunner::setMessage(const QString &message) { m_message = message; emit changed(); }
void EdlRunner::rebuildTargets() {
    QMap<QString, QVariantMap> groups;
    for (const auto &e : m_endpoints) {
        if (e.edl) continue;
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
    m_targets.clear();
    for (bool suggested : {true, false})
        for (const auto &group : groups) if (group.value("suggested").toBool() == suggested) m_targets.append(group);
}
void EdlRunner::refresh(const QString &adbPath) {
    if (m_thread || m_running) return;
    m_discovering = true; m_cancel = std::make_shared<std::atomic_bool>(false);
    setMessage(tr("Discovering diagnostic ports and ADB devices…"));
    auto endpoints = std::make_shared<QVector<EdlEndpoint>>();
    auto error = std::make_shared<QString>();
    m_thread = QThread::create([=] { *endpoints = m_hooks.discover(adbPath, m_cancel, *error); });
    connect(m_thread, &QThread::finished, this, [=] {
        m_thread->deleteLater(); m_thread = nullptr;
        m_endpoints = *endpoints; rebuildTargets(); emit targetsChanged(); m_discovering = false;
        setMessage(error->isEmpty() ? tr("Select a device. Unknown interfaces require a successful DIAG query.") : *error);
    });
    m_thread->start();
}
void EdlRunner::start(const QString &targetId, int method, const QString &endpointId, const QString &adbPath) {
    if (m_thread || m_running || method < 0 || method > 2) return;
    QVector<EdlEndpoint> diag, adb;
    for (const auto &e : m_endpoints) {
        if (e.edl || targetKey(e) != targetId) continue;
        if (e.kind == EdlEndpoint::Adb) adb.append(e);
        else if (e.id == endpointId || (endpointId.isEmpty() && e.suggested)) diag.append(e);
    }
    if (method != 2 && diag.size() > 1) { setMessage(tr("Select one diagnostic interface before switching.")); return; }
    if ((method == 1 && diag.isEmpty()) || (method == 2 && adb.size() != 1) || (diag.isEmpty() && adb.size() != 1)) {
        setMessage(tr("No matching switch endpoint is available. Refresh the device list.")); return;
    }
    m_running = true; m_cancel = std::make_shared<std::atomic_bool>(false);
    setMessage(tr("Checking the selected device…")); emit started();
    struct Result { bool ok = false; QString message; EdlEndpoint edl; };
    auto result = std::make_shared<Result>();
    m_thread = QThread::create([=] {
        auto progress = [this](const QString &text) { QMetaObject::invokeMethod(this, [this,text] { setMessage(text); }, Qt::QueuedConnection); };
        EdlEndpoint target; bool useDiag = method != 2 && !diag.isEmpty(); QString error;
        if (useDiag) {
            target = diag.first(); progress(tr("Verifying the selected DIAG interface…"));
            bool sent = false;
            const auto response = m_hooks.exchange(target, QByteArray(1, char(0)), m_cancel, error, sent);
            if (!validVersion(response)) {
                if (method == 0 && adb.size() == 1 && !m_cancel->load()) useDiag = false;
                else { result->message = tr("DIAG verification failed: %1").arg(error.isEmpty() ? tr("Invalid or unsupported version response") : error); return; }
            }
        }
        if (m_cancel->load()) return;
        // Revalidate the selected identity immediately before sending a reboot.
        if (useDiag) {
            progress(tr("Sending DIAG EDL request…"));
            bool sent = false;
            const auto reply = m_hooks.exchange(target, QByteArray::fromHex("4b650100"), m_cancel, error, sent);
            if (!sent) { result->message = tr("EDL request could not be sent: %1").arg(error); return; }
            if (!reply.isEmpty() && !reply.startsWith(QByteArray::fromHex("4b650100"))) {
                result->message = tr("The device rejected the DIAG EDL request."); return;
            }
        } else {
            target = adb.first(); progress(tr("Sending ADB EDL request…"));
            if (!m_hooks.reboot(adbPath, target.serial, m_cancel, error)) { result->message = tr("EDL request could not be sent: %1").arg(error); return; }
        }
        if (m_cancel->load()) return;
        progress(tr("Request sent; waiting for the target device in 9008 mode…"));
        QElapsedTimer timer; timer.start();
        do {
            for (const auto &e : m_hooks.scan()) {
                if (e.edl && sameLocation(target, e)) {
                    result->ok = true; result->edl = e; result->message = tr("Target device detected in 9008 mode."); return;
                }
            }
            if (m_cancel->load()) return;
            QThread::msleep(100);
        } while (timer.elapsed() < m_waitMs);
        result->message = target.location.isEmpty() && target.ports.isEmpty()
            ? tr("Request sent, but the target USB location could not be identified; success cannot be confirmed.")
            : tr("The target device did not appear in 9008 mode before the timeout.");
        if (!error.isEmpty()) result->message += "\n" + error;
    });
    connect(m_thread, &QThread::finished, this, [=] {
        m_thread->deleteLater(); m_thread = nullptr; m_running = false;
        if (m_cancel->load()) { result->ok = false; result->message = tr("Switch cancelled. A reboot already sent cannot be undone."); }
        setMessage(result->message);
        emit finished(result->ok, result->message, result->edl.path, result->edl.bus, result->edl.ports);
    });
    m_thread->start();
}
}
