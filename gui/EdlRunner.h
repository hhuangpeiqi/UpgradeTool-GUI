#ifndef GUI_EDL_RUNNER_H
#define GUI_EDL_RUNNER_H
#include <QObject>
#include <QVariantList>
#include <QVector>
#include <QString>
#include <QByteArray>
#include <atomic>
#include <functional>
#include <memory>
class QThread;
namespace gui {
// A transport endpoint, never a flash target until EDL has been observed.
struct EdlEndpoint {
    QString id, label, path, serial, location;
    enum Kind { Serial, Usb, Adb } kind = Serial;
    quint16 vid = 0, pid = 0;
    int bus = 0, interfaceNumber = -1, alternate = 0, in = 0, out = 0;
    QVector<int> ports;
    bool suggested = false, edl = false;
};
using EdlCancel = std::shared_ptr<std::atomic_bool>;
struct EdlHooks {
    std::function<QVector<EdlEndpoint>(const QString &, const EdlCancel &, QString &)> discover;
    // Exchanges an unframed DIAG payload; implementations validate HDLC and CRC.
    std::function<QByteArray(const EdlEndpoint &, const QByteArray &, const EdlCancel &, QString &, bool &sent)> exchange;
    // Returns whether the reboot process was started (disconnect can make its exit code unreliable).
    std::function<bool(const QString &, const QString &, const EdlCancel &, QString &)> reboot;
    std::function<QVector<EdlEndpoint>()> scan;
};
class EdlRunner : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList targets READ targets NOTIFY targetsChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
public:
    explicit EdlRunner(QObject *parent = nullptr);
    EdlRunner(EdlHooks hooks, int waitMs, QObject *parent = nullptr);
    ~EdlRunner() override;
    QVariantList targets() const { return m_targets; }
    bool discovering() const { return m_discovering; }
    bool running() const { return m_running; }
    QString message() const { return m_message; }
    void refresh(const QString &adbPath);
    // method: 0 auto, 1 DIAG, 2 ADB. endpointId is required for ambiguous DIAG interfaces.
    void start(const QString &targetId, int method, const QString &endpointId, const QString &adbPath);
    void cancel();
    static QByteArray encode(const QByteArray &payload);
    static bool decode(QByteArray &buffer, QByteArray &payload);
    static bool validVersion(const QByteArray &payload);
    static bool sameLocation(const EdlEndpoint &a, const EdlEndpoint &b);
signals:
    void targetsChanged();
    void changed();
    void started();
    void finished(bool ok, const QString &message, const QString &path, int bus, const QVector<int> &ports);
private:
    void setMessage(const QString &message);
    void rebuildTargets();
    EdlHooks m_hooks;
    int m_waitMs;
    QThread *m_thread = nullptr;
    EdlCancel m_cancel;
    QVector<EdlEndpoint> m_endpoints;
    QVariantList m_targets;
    bool m_discovering = false, m_running = false;
    QString m_message;
};
EdlHooks DefaultEdlHooks();
}
#endif
