#include "application/TaskService.h"
#include "domain/DeviceSelection.h"
#include <exception>
namespace core {
namespace {
class Executor final : public IOperationExecutor {
public:
    Executor(BackendRegistry &b,const TaskJob &j,IUpgradeObserver *o) : backends(b),job(j),observer(o) {}
    OperationResult Run(const OperationRequest &request,const DeviceRef &target) override {
        try { return backends.Execute(request,target,job.cancelled,observer); }
        catch(const std::exception &error) {
            OperationResult result; result.operation=request.operation();
            result.status=job.cancelled && job.cancelled->stop_requested()?OperationStatus::Cancelled:OperationStatus::Failed;
            result.error=ErrorCode::OperationFailed; result.diagnostic=error.what(); return result;
        }
    }
private:
    BackendRegistry &backends;
    const TaskJob &job;
    IUpgradeObserver *observer;
};
}
TaskSummary TaskService::Run(const TaskJob &job,IUpgradeObserver *observer,const TaskRunner::Started &started) const {
    auto fail=[&](ErrorCode code,const std::string &text) {
        OperationResult result; result.operation=job.steps.empty()?Operation::Count:job.steps.front().operation();
        result.error=code; result.diagnostic=text;
        if(observer) observer->OnResult(result);
        TaskSummary summary; summary.status=OperationStatus::Failed; summary.failed=1; return summary;
    };
    if(job.cancelled && job.cancelled->stop_requested()) { TaskSummary s; s.status=OperationStatus::Cancelled; return s; }
    if(!backends.Validate(job)) return fail(ErrorCode::InvalidInput,"Invalid or mixed-vendor task");
    if(RequiresEnumeratedDevice(job.steps.front())) {
        std::vector<DeviceRef> identities;
        try { for(const auto &d:backends.Find(job.steps.front().vendor())->Scan()) identities.push_back(d.reference); }
        catch(const std::exception &error) { return fail(ErrorCode::OperationFailed,error.what()); }
        for(const auto &target:job.targets) {
            const int match=MatchDevice(identities,target);
            if(match==-2) return fail(ErrorCode::DeviceAmbiguous,"Ambiguous device identity");
            if(match<0) return fail(ErrorCode::DeviceMissing,"A selected device disconnected before the task started");
        }
    }
    Executor executor(backends,job,observer);
    return TaskRunner{}.Run(job,executor,started,[&](size_t,const OperationResult &result) { if(observer) observer->OnResult(result); });
}
}
