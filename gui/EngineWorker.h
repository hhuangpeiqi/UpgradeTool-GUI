#ifndef GUI_ENGINE_WORKER_H
#define GUI_ENGINE_WORKER_H
#include <QObject>
#include <QVariantList>
#include "core/TaskRunner.h"
#include "core/UpgradeObserver.h"
class CRKLog;
class CRKScan;
namespace core { class UpgradeEngine; }
Q_DECLARE_METATYPE(core::TaskJob)
Q_DECLARE_METATYPE(core::OperationResult)
Q_DECLARE_METATYPE(core::TaskSummary)
namespace gui {
class EngineWorker : public QObject {
	Q_OBJECT
public:
	explicit EngineWorker(CRKLog *log, QObject *parent = nullptr);
	~EngineWorker();
	void SetObserver(core::IUpgradeObserver *observer);
public slots:
	void scanDevices();
	void execute(const core::TaskJob &job);
	void abortTask();
signals:
	void devicesReady(const QVariantList &devices);
	void taskFinished(const core::TaskSummary &summary);
	void stepStarted(int step, int target, int total, const QString &device);
private:
	core::UpgradeEngine *m_pEngine;
	CRKScan *m_pScan;
};
}
#endif
