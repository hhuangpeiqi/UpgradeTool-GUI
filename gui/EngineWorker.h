#ifndef GUI_ENGINE_WORKER_H
#define GUI_ENGINE_WORKER_H
#include <QObject>
#include "application/Inspection.h"
#include <QVariantList>
#include "application/TaskRunner.h"
#include "domain/Backend.h"
#include "domain/UpgradeObserver.h"
namespace platform { class FileLog; }
class CRKScan;
namespace core { class UpgradeEngine; }
Q_DECLARE_METATYPE(core::InspectionRequest)
Q_DECLARE_METATYPE(core::DeviceRef)
Q_DECLARE_METATYPE(core::TaskJob)
Q_DECLARE_METATYPE(core::OperationResult)
Q_DECLARE_METATYPE(core::TaskSummary)
namespace gui {
class EngineWorker : public QObject {
	Q_OBJECT
public:
	explicit EngineWorker(platform::FileLog *log, QObject *parent = nullptr);
	~EngineWorker();
	void SetObserver(core::IUpgradeObserver *observer);
    core::OperationResult inspectResult(const core::InspectionRequest &);
public slots:
	void scanDevices();
	void execute(const core::TaskJob &job);
    void inspect(const core::InspectionRequest &);
signals:
    void inspectionFinished(int context,quint64 generation,const core::OperationResult &result);
	void devicesReady(const QVariantList &devices);
	void taskFinished(const core::TaskSummary &summary);
	void stepStarted(int step, int target, int total, const QString &device);
private:
    struct Runtime;
    std::unique_ptr<Runtime> m_runtime;
	core::IUpgradeObserver *m_observer = nullptr;
};
}
#endif
