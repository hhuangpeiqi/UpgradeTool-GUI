#include "gui/EngineWorker.h"
#include "core/UpgradeEngine.h"
#include "RKDevice.h"
#include "core/DeviceSelection.h"
#include <QVariantMap>
namespace gui {
EngineWorker::EngineWorker(CRKLog *log, QObject *parent)
	: QObject(parent), m_pEngine(new core::UpgradeEngine()), m_pScan(new CRKScan())
{
	m_pScan->SetVidPid(); m_pEngine->SetLogObject(log); m_pEngine->SetScan(m_pScan);
	m_rockchip.reset(new core::RockchipBackend(*m_pEngine));
	m_backends.Add(*m_rockchip); m_backends.Add(m_allwinner);
}
EngineWorker::~EngineWorker() { delete m_pEngine; delete m_pScan; }
void EngineWorker::SetObserver(core::IUpgradeObserver *observer) { m_observer = observer; m_pEngine->SetObserver(observer); }
void EngineWorker::abortTask() { CRKDevice::RequestAbort(); }
void EngineWorker::scanDevices()
{
	QVariantList list;
    for (auto *backend : std::vector<core::IDeviceBackend *>{m_rockchip.get(), &m_allwinner}) {
        try {
            for (const auto &dev : backend->Scan()) {
                QVariantMap map;
                map["index"] = dev.index; map["vid"] = dev.vid; map["pid"] = dev.pid;
                map["locationId"] = dev.identity.location; map["usbType"] = dev.usbType;
                map["mode"] = QString::fromStdString(dev.mode); map["serial"] = QString::fromStdString(dev.identity.serial);
                map["vendor"] = static_cast<int>(dev.identity.vendor); map["bus"] = dev.identity.bus;
                QVariantList ports; QStringList labels;
                for (auto p : dev.identity.ports) { ports.append(p); labels.append(QString::number(p)); }
                map["ports"] = ports;
                map["key"] = dev.identity.vendor == core::Vendor::Rockchip
                    ? QString::number(dev.identity.location,16) + ":" + QString::fromStdString(dev.identity.serial)
                    : "allwinner:" + QString::number(dev.identity.bus) + "-" + labels.join('.');
                list.append(map);
            }
        } catch(const std::exception &e) {
            if(m_observer) m_observer->OnOutput(core::LogLevel::Warn, e.what());
        }
    }
	emit devicesReady(list);
}
namespace {
class Executor : public core::IOperationExecutor {
    core::BackendRegistry &backends;
    const core::TaskJob &job;
    core::IUpgradeObserver *observer;
public:
    Executor(core::BackendRegistry &b,const core::TaskJob &j,core::IUpgradeObserver *o) : backends(b),job(j),observer(o) {}
    core::OperationResult Run(const core::OperationRequest &request,const core::DeviceIdentity &target) override {
        return backends.Execute(request,target,job.cancelled,observer);
    }
};
}
void EngineWorker::execute(const core::TaskJob &job)
{
    if(job.cancelled && job.cancelled->load()) {
        core::TaskSummary summary; summary.status = core::OperationStatus::Cancelled;
        emit taskFinished(summary); return;
    }
	m_pEngine->SetCancellation(job.cancelled);
	m_pEngine->BeginTask();
    if (!m_backends.Validate(job)) {
        core::OperationResult result; result.operation = job.steps.empty() ? core::Operation::Count : job.steps.front().operation;
        result.error = core::ErrorCode::InvalidInput; result.diagnostic = "Invalid or mixed-vendor task";
        if(m_observer) m_observer->OnResult(result);
        core::TaskSummary summary; summary.status = core::OperationStatus::Failed; summary.failed = 1;
        emit taskFinished(summary); return;
    }
    if(!core::IsLocalOperation(job.steps.front().operation)) {
        std::vector<core::DeviceIdentity> identities;
        try {
            for(const auto &d:m_backends.Find(job.steps.front().vendor)->Scan()) identities.push_back(d.identity);
        } catch(const std::exception &e) {
            core::OperationResult result; result.operation=job.steps.front().operation;
            result.error=core::ErrorCode::OperationFailed; result.diagnostic=e.what();
            if(m_observer) m_observer->OnResult(result);
            core::TaskSummary summary; summary.status=core::OperationStatus::Failed; summary.failed=1;
            emit taskFinished(summary); return;
        }
        for(const auto &target:job.targets) {
            int match=core::MatchDevice(identities,target); if(match>=0) continue;
            core::OperationResult result; result.operation=job.steps.front().operation; result.error=match==-2?core::ErrorCode::DeviceAmbiguous:core::ErrorCode::DeviceMissing;
            result.diagnostic=match==-2?"Ambiguous device identity":"A selected device disconnected before the task started";
            if(m_observer) m_observer->OnResult(result);
            core::TaskSummary summary; summary.status=core::OperationStatus::Failed; summary.failed=1;
            emit taskFinished(summary); return;
        }
    }
    Executor executor(m_backends,job,m_observer);
	core::TaskRunner runner;
	core::TaskSummary summary = runner.Run(job, executor, [this, &job](size_t step, size_t target, const core::DeviceIdentity &device) {
		emit stepStarted(static_cast<int>(step), static_cast<int>(target),
		                 static_cast<int>(job.targets.size()), QString::fromStdString(device.serial));
	});
	emit taskFinished(summary);
}
}
