#ifndef APPLICATION_TASK_SERVICE_H
#define APPLICATION_TASK_SERVICE_H
#include "application/TaskRunner.h"
#include "application/BackendRegistry.h"
namespace core {
// Owns orchestration only. Backend/session lifetimes belong to the application
// composition root and outlive Run. Calls are synchronous and serialized.
class TaskService {
public:
    explicit TaskService(BackendRegistry &backends) : backends(backends) {}
    TaskSummary Run(const TaskJob &, IUpgradeObserver *, const TaskRunner::Started & = {}) const;
private:
    BackendRegistry &backends;
};
}
#endif
