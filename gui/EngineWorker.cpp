#include "gui/EngineWorker.h"
#include "application/BuildFeatures.h"
#if UPGRADE_TOOL_ALLWINNER
#include "backends/allwinner/AllwinnerBackend.h"
#endif
#include "backends/BackendFactory.h"
#include "application/TaskService.h"
#include <QVariantMap>
#include <QCoreApplication>
#include "backends/qualcomm/OperationPolicy.h"
#include "gui/UsbAccess.h"
#include "gui/WindowsUsbDriver.h"
namespace gui {
struct EngineWorker::Runtime {
    std::vector<std::unique_ptr<core::IDeviceBackend>> backends;
    core::BackendRegistry registry;
    explicit Runtime(platform::FileLog *log) : backends(core::CreateBackends(log)) {
        for(auto &backend:backends) registry.Add(*backend);
    }
};
EngineWorker::EngineWorker(platform::FileLog *log,QObject *parent) : QObject(parent),m_runtime(std::make_unique<Runtime>(log)) {}
EngineWorker::~EngineWorker() = default;
void EngineWorker::SetObserver(core::IUpgradeObserver *observer) { m_observer=observer; }
void EngineWorker::prepareFelSwitch(const FelSwitchRequest &request) {
#if UPGRADE_TOOL_ALLWINNER
    auto *backend=dynamic_cast<core::allwinner::AllwinnerBackend *>(m_runtime->registry.Find(core::Vendor::Allwinner));
    if(backend && !m_felMonitor) {
        AllwinnerSwitchMonitor::Hooks hooks;
        hooks.scan=[backend] {
#ifdef Q_OS_WIN
            std::lock_guard<std::recursive_mutex> lock(UsbProbeMutex());
#endif
            return backend->Scan();
        };
        hooks.connect=[backend](const core::DeviceRef &target,const core::Cancellation &cancel,auto deadline) {
#ifdef Q_OS_WIN
            std::lock_guard<std::recursive_mutex> lock(UsbProbeMutex());
#endif
            return backend->Connect(target,cancel,deadline);
        };
        hooks.release=[backend](const core::DeviceRef &target) { backend->ReleaseSession(target); };
        m_felMonitor=new AllwinnerSwitchMonitor(std::move(hooks),this);
        connect(m_felMonitor,&AllwinnerSwitchMonitor::ready,this,&EngineWorker::felMonitorReady);
        connect(m_felMonitor,&AllwinnerSwitchMonitor::trace,this,&EngineWorker::traceMessage);
        connect(m_felMonitor,&AllwinnerSwitchMonitor::completed,this,
            [this,backend](quint64 id,bool ok,const QString &message,const core::DeviceRef &target) {
                // The queued snapshot reaches the GUI before it unlocks the switch.
                scanDevices();
                if(ok && !backend->HasSession(target))
                    emit felMonitorFinished(id,false,tr("The target device disconnected after the FEL/FES handshake."),{});
                else emit felMonitorFinished(id,ok,message,target);
            });
    }
    if(m_felMonitor) { m_felMonitor->prepare(request); return; }
#endif
    emit felMonitorFinished(request.id,false,tr("Allwinner support is unavailable."),{});
}
void EngineWorker::cancelFelSwitch(quint64 id) {
    if(m_felMonitor) m_felMonitor->cancel(id);
}
void EngineWorker::releaseAllwinnerSessions() {
#if UPGRADE_TOOL_ALLWINNER
    auto *backend=dynamic_cast<core::allwinner::AllwinnerBackend *>(m_runtime->registry.Find(core::Vendor::Allwinner));
    if(backend) backend->ReleaseSessions();
#endif
}
void EngineWorker::scanDevices()
{
#ifdef Q_OS_WIN
    std::lock_guard<std::recursive_mutex> accessLock(UsbProbeMutex());
#endif
	QVariantList list;
    for (auto &backend : m_runtime->backends) {
        try {
            for (const auto &dev : backend->Scan()) {
                QVariantMap map;
                map["index"] = dev.index; map["vid"] = dev.vid; map["pid"] = dev.pid;
                map["locationId"] = dev.location; map["usbType"] = dev.usbType;
                map["mode"] = QString::fromStdString(dev.mode);
                if(dev.reference.vendor()==core::Vendor::Qualcomm) {
                    const auto source=QString::fromStdString(dev.mode);
                    const auto label=QCoreApplication::translate("QualcommBackend",core::qualcomm::ModeLabel(static_cast<qtq_mode>(dev.modeCode)));
                    map["mode"]=label+source.mid(source.indexOf(" / "));
                } map["serial"] = QString::fromStdString(dev.serial);
                map["deviceInstance"]=QString::fromStdString(dev.deviceInstance);
                map["physicalLocation"]=QString::fromStdString(dev.physicalLocation);
                map["devicePath"]=QString::fromStdString(dev.devicePath);
                map["transport"]=static_cast<int>(dev.transport);
                map["qualcommMode"] = static_cast<int>(dev.modeCode); map["accessError"] = dev.accessError;
                map["usbAddress"] = dev.usbAddress;
                map["interfaceNumber"] = dev.interfaceNumber; map["alternate"] = dev.alternate;
                map["inEndpoint"] = dev.inEndpoint; map["outEndpoint"] = dev.outEndpoint;
                map["vendor"] = static_cast<int>(dev.reference.vendor()); map["bus"] = dev.bus;
                QVariantList ports;
                for (auto p : dev.ports) ports.append(p);
                map["ports"] = ports;
                map["key"]=QString::fromStdString(dev.reference.key());
                map["reference"]=QVariant::fromValue(dev.reference);
                list.append(map);
            }
        } catch(const std::exception &e) {
            if(m_observer) m_observer->OnOutput(core::LogLevel::Warn, e.what());
        }
    }
	emit devicesReady(list);
}
void EngineWorker::execute(const core::TaskJob &job)
{
#ifdef Q_OS_WIN
    std::lock_guard<std::recursive_mutex> accessLock(UsbProbeMutex());
    const bool simulation = !job.steps.empty() && core::IsSimulation(job.steps.front());
    if (!simulation) {
        try {
            for (const auto &target : job.targets) {
                if (job.cancelled->stop_requested()) break;
                const auto error = CheckUsbAccess(target);
                if (!error.isEmpty()) {
                    if (m_observer) m_observer->OnOutput(core::LogLevel::Error, error.toStdString());
                    emit taskFinished({core::OperationStatus::Failed, 0, 1}); return;
                }
            }
        } catch (const std::exception &error) {
            if (m_observer) m_observer->OnOutput(core::LogLevel::Error, error.what());
            emit taskFinished({core::OperationStatus::Failed, 0, 1}); return;
        }
    }
#endif
    core::TaskService service(m_runtime->registry);
    auto summary=service.Run(job,m_observer,[this,&job](size_t step,size_t target,const core::DeviceRef &device) {
        emit stepStarted(static_cast<int>(step),static_cast<int>(target),
                         static_cast<int>(job.targets.size()),QString::fromStdString(device.serial()));
    });
    if(summary.status!=core::OperationStatus::Succeeded && !job.steps.empty() &&
        job.steps.front().vendor()==core::Vendor::Allwinner) releaseAllwinnerSessions();
    emit taskFinished(summary);
}
core::OperationResult EngineWorker::inspectResult(const core::InspectionRequest &inspection) {
    core::OperationResult result; result.operation=inspection.request.operation();
    if (inspection.cancelled->stop_requested()) return result;
    try {
        if(inspection.directory) {
            auto *backend=m_runtime->registry.Find(inspection.request.vendor());
            if(backend) result.firmware=backend->InspectDirectory(inspection.request.file().path,inspection.cancelled);
            result.status=result.firmware.valid?core::OperationStatus::Succeeded:core::OperationStatus::Failed;
        } else result=m_runtime->registry.Execute(inspection.request,{},inspection.cancelled,nullptr);
    }
    catch(const std::exception &error) { result.error=core::ErrorCode::OperationFailed; result.diagnostic=error.what(); }
    return result;
}
void EngineWorker::inspect(const core::InspectionRequest &inspection) {
    const auto result = inspectResult(inspection);
    if (!inspection.cancelled->stop_requested()) emit inspectionFinished(inspection.context, inspection.generation, result);
}

}
