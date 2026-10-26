#ifndef GUI_EDL_RUNNER_H
#define GUI_EDL_RUNNER_H
#include <QObject>
#include "backends/qualcomm/DevicePolicy.h"
#include <QVariantList>
#include <QVector>
#include <QString>
#include <QByteArray>
#include "domain/Operation.h"
#include <functional>
#include <memory>
class QThread;
namespace gui {
class AdbService;
// An enumeration snapshot. Candidate modes never confirm a protocol session.
struct EdlEndpoint {
    QString id, label, path, serial, location, instance, driverService, accessReason;
    enum Kind { Serial, Usb, Adb } kind = Serial;
    quint16 vid = 0, pid = 0;
    int bus = 0, interfaceNumber = -1, alternate = 0, in = 0, out = 0;
    QVector<int> ports;
    bool suggested = false, downloadInterface = false;
    unsigned int address = 0;
    int accessError = 0;
    qtq_mode mode() const {
        return qtq_classify(vid, pid, downloadInterface, kind == Adb, suggested);
    }
};
using EdlCancel = core::Cancellation;
class DiagSession {
public:
    virtual ~DiagSession() = default;
    virtual QByteArray exchange(const QByteArray &, const EdlCancel &, QString &, bool &sent) = 0;
};
struct EdlHooks {
    std::function<QVector<EdlEndpoint>(const QString &, const EdlCancel &, QString &)> discover;
    // Verification and reboot use the same claimed connection. A session is
    // never retained beyond one switch attempt.
    std::function<std::unique_ptr<DiagSession>(const EdlEndpoint &, const EdlCancel &, QString &)> openDiag;
    // Returns whether the reboot process was started (disconnect can make its exit code unreliable).
    std::function<bool(const QString &, const QString &, const EdlCancel &, QString &)> reboot;
    std::function<QVector<EdlEndpoint>()> scan;
    std::function<QVector<EdlEndpoint>(const QString &, const EdlCancel &, QString &)> discoverFresh;
};
class EdlRunner : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList targets READ targets NOTIFY targetsChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(int method READ method WRITE setMethod NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY changed)
    Q_PROPERTY(QString discoveryError READ discoveryError NOTIFY changed)
public:
    enum Method { Diag = 1, Adb = 2 };
    Q_ENUM(Method)
    enum State { Idle, Checking, Verifying, Sending, Waiting, Completed, Failed, Cancelled };
    Q_ENUM(State)
    explicit EdlRunner(QObject *parent = nullptr);
    EdlRunner(EdlHooks hooks, int waitMs, QObject *parent = nullptr);
    ~EdlRunner() override;
    QVariantList targets() const { return m_targets; }
    bool discovering() const { return m_discovering; }
    bool running() const { return m_state >= Checking && m_state <= Waiting; }
    State state() const { return m_state; }
    QString message() const { return m_message; }
    int method() const { return m_method; }
    void setMethod(int method);
    bool ready() const { return m_discovered && !running() && !m_targets.isEmpty(); }
    QString unavailableReason() const;
    QString discoveryError() const { return m_method == Adb ? m_discoveryError : QString(); }
    void refresh(const QString &adbPath);
    void requestSwitch(const QString &adbPath);
    Q_INVOKABLE bool canStart(const QString &targetId, const QString &endpointId) const;
    void retranslate() { emit changed(); }
    // Only DIAG and ADB are supported; the chosen transport never falls back.
    void start(const QString &targetId, int method, const QString &endpointId, const QString &adbPath);
    void cancel();
    void shutdown();
    bool stopped() const;
    static QByteArray encode(const QByteArray &payload);
    static bool decode(QByteArray &buffer, QByteArray &payload);
    static bool validVersion(const QByteArray &payload);
    static bool sameLocation(const EdlEndpoint &a, const EdlEndpoint &b);
signals:
    void targetsChanged();
    void changed();
    void messageChanged();
    void started();
    void selectionRequested();
    void errorOccurred(const QString &message);
    void finished(bool ok, const QString &message, const gui::EdlEndpoint &endpoint);
private:
    void setMessage(const QString &message);
    void setState(State state);
    void rebuildTargets();
    bool candidate(const EdlEndpoint &endpoint) const;
    bool eligible(const EdlEndpoint &endpoint) const;
    QVector<EdlEndpoint> selectedEndpoints(const QString &targetId, const QString &endpointId) const;
    void reject(const QString &message);
    EdlHooks m_hooks;
    int m_waitMs;
    QThread *m_thread = nullptr;
    QThread *m_discoveryThread = nullptr;
    EdlCancel m_cancel;
    EdlCancel m_discoveryCancel;
    unsigned m_generation = 0;
    int m_method = Diag;
    QVector<EdlEndpoint> m_endpoints;
    QVariantList m_targets;
    bool m_discovering = false, m_discovered = false;
    bool m_stopping = false;
    State m_state = Idle;
    QString m_message, m_discoveryError;
};
EdlHooks DefaultEdlHooks(AdbService *service = nullptr);
// Match a USB interface to one native binding, never to a sibling interface.
bool BindEdlNativeInterface(EdlEndpoint &, const QVector<EdlEndpoint> &);
}
Q_DECLARE_METATYPE(gui::EdlEndpoint)
#endif
