#include "gui/EngineWorker.h"
#include "backends/BackendFactory.h"
#include "application/TaskService.h"
#include <QVariantMap>
#include <QCoreApplication>
#include "backends/qualcomm/OperationPolicy.h"
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
void EngineWorker::scanDevices()
{
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
                map["devicePath"]=QString::fromStdString(dev.devicePath);
                map["transport"]=static_cast<int>(dev.transport);
                map["qualcommMode"] = static_cast<int>(dev.modeCode); map["accessError"] = dev.accessError;
                map["usbAddress"] = dev.usbAddress;
                map["interfaceNumber"] = dev.interfaceNumber; map["alternate"] = dev.alternate;
                map["inEndpoint"] = dev.inEndpoint; map["outEndpoint"] = dev.outEndpoint;
                map["vendor"] = static_cast<int>(dev.reference.vendor()); map["bus"] = dev.bus;
                QVariantList ports; QStringList labels;
                for (auto p : dev.ports) { ports.append(p); labels.append(QString::number(p)); }
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
    core::TaskService service(m_runtime->registry);
    auto summary=service.Run(job,m_observer,[this,&job](size_t step,size_t target,const core::DeviceRef &device) {
        emit stepStarted(static_cast<int>(step),static_cast<int>(target),
                         static_cast<int>(job.targets.size()),QString::fromStdString(device.serial()));
    });
    emit taskFinished(summary);
}
void EngineWorker::inspect(const core::InspectionRequest &inspection) {
    if(inspection.cancelled->stop_requested()) return;
    core::OperationResult result; result.operation=inspection.request.operation();
    try {
        if(inspection.directory) {
            auto *backend=m_runtime->registry.Find(inspection.request.vendor());
            if(backend) result.firmware=backend->InspectDirectory(inspection.request.file().path,inspection.cancelled);
            result.status=result.firmware.valid?core::OperationStatus::Succeeded:core::OperationStatus::Failed;
        } else result=m_runtime->registry.Execute(inspection.request,{},inspection.cancelled,nullptr);
    }
    catch(const std::exception &error) { result.error=core::ErrorCode::OperationFailed; result.diagnostic=error.what(); }
    if(!inspection.cancelled->stop_requested()) emit inspectionFinished(inspection.context,inspection.generation,result);
}

}
