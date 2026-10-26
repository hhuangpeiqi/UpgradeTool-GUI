#pragma once
#include "application/Inspection.h"
#include "domain/OperationResult.h"
#include <QObject>
#include <QHash>
#include <QThread>
#include <functional>
namespace platform { class FileLog; }
namespace gui {
class EngineWorker;
// One cancellable file-inspection queue, separate from device operations.
class InspectionQueue : public QObject {
    Q_OBJECT
public:
    using Completion = std::function<void()>;
    using Work = std::function<Completion(std::stop_token)>;
    explicit InspectionQueue(QObject *parent = nullptr, platform::FileLog *log = nullptr);
    ~InspectionQueue() override;
    void submit(QObject *owner, const QString &key, core::Cancellation, Work);
    void inspect(QObject *owner, const core::InspectionRequest &);
    void shutdown();
    bool stopped() const { return m_thread.isFinished(); }
signals:
    void inspectionFinished(int context, quint64 generation, const core::OperationResult &result);
private:
    QThread m_thread;
    QObject *m_context;
    EngineWorker *m_engine = nullptr;
    platform::FileLog *m_log;
    QHash<QString, core::Cancellation> m_requests;
    bool m_stopping = false;
};
}
