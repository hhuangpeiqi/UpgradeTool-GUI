#include "gui/EngineWorker.h"
#include "core/UpgradeEngine.h"
#include "RKDevice.h"
#include <QVariantMap>
namespace gui {
EngineWorker::EngineWorker(CRKLog *log, QObject *parent)
	: QObject(parent), m_pEngine(new core::UpgradeEngine()), m_pScan(new CRKScan())
{
	m_pScan->SetVidPid(); m_pEngine->SetLogObject(log); m_pEngine->SetScan(m_pScan);
}
EngineWorker::~EngineWorker() { delete m_pEngine; delete m_pScan; }
void EngineWorker::SetObserver(core::IUpgradeObserver *observer) { m_pEngine->SetObserver(observer); }
void EngineWorker::abortTask() { CRKDevice::RequestAbort(); }
void EngineWorker::scanDevices()
{
	QVariantList list;
	m_pScan->Search(RKUSB_MASKROM | RKUSB_LOADER);
	for (const auto &dev : m_pEngine->ScanDevices()) {
		QVariantMap map;
		map["index"] = dev.iIndex; map["vid"] = dev.uiVid; map["pid"] = dev.uiPid;
		map["locationId"] = dev.uiLocationID; map["usbType"] = dev.iUsbType;
		map["mode"] = QString::fromStdString(dev.strMode); map["serial"] = QString::fromStdString(dev.strSerial);
		map["key"] = QString::number(dev.uiLocationID, 16) + ":" + QString::fromStdString(dev.strSerial);
		list.append(map);
	}
	emit devicesReady(list);
}
namespace {
class Executor : public core::IOperationExecutor {
	core::UpgradeEngine &engine;
public:
	explicit Executor(core::UpgradeEngine &value) : engine(value) {}
	core::OperationResult Run(const core::OperationRequest &request, const core::DeviceIdentity &target) override {
		engine.SetTarget(target); return engine.Execute(request);
	}
};
}
void EngineWorker::execute(const core::TaskJob &job)
{
	m_pEngine->SetCancellation(job.cancelled);
	m_pEngine->BeginTask();
	Executor executor(*m_pEngine);
	core::TaskRunner runner;
	core::TaskSummary summary = runner.Run(job, executor, [this, &job](size_t step, size_t target, const core::DeviceIdentity &device) {
		emit stepStarted(static_cast<int>(step), static_cast<int>(target),
		                 static_cast<int>(job.targets.size()), QString::fromStdString(device.serial));
	});
	emit taskFinished(summary);
}
}
