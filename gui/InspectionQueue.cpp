#include "gui/InspectionQueue.h"
#include "gui/EngineWorker.h"
#include <QPointer>
namespace gui {
InspectionQueue::InspectionQueue(QObject *parent, platform::FileLog *log)
    : QObject(parent), m_context(new QObject), m_log(log) {
    m_context->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_context, &QObject::deleteLater);
    m_thread.start();
}
InspectionQueue::~InspectionQueue() { shutdown(); m_thread.wait(); }
void InspectionQueue::shutdown() {
    m_stopping = true;
    for (const auto &cancel : m_requests) cancel->request_stop();
    m_thread.quit();
}
void InspectionQueue::submit(QObject *owner, const QString &key, core::Cancellation cancel, Work work) {
    if (m_stopping) { cancel->request_stop(); return; }
    // Contexts belong to their owner; independent inspectors may use the same key.
    const QString id = QString::number(quintptr(owner), 16) + ':' + key;
    if (const auto old = m_requests.value(id)) old->request_stop();
    m_requests[id] = cancel;
    const QPointer<QObject> receiver(owner);
    QMetaObject::invokeMethod(m_context, [this, receiver, cancel, work = std::move(work)] {
        if (cancel->stop_requested()) return;
        auto complete = work(cancel->get_token());
        if (cancel->stop_requested() || !complete) return;
        QMetaObject::invokeMethod(this, [receiver, cancel, complete = std::move(complete)] {
            if (receiver && !cancel->stop_requested()) complete();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
void InspectionQueue::inspect(QObject *owner, const core::InspectionRequest &request) {
    submit(owner, QString::number(request.context), request.cancelled, [this, request](std::stop_token) -> Completion {
        if (!m_engine) {
            m_engine = new EngineWorker(m_log, m_context);
        }
        const auto result = m_engine->inspectResult(request);
        return [this, request, result] { emit inspectionFinished(request.context, request.generation, result); };
    });
}
}
