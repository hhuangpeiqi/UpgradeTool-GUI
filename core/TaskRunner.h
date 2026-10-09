#ifndef CORE_TASK_RUNNER_H
#define CORE_TASK_RUNNER_H
#include "core/OperationResult.h"
#include <functional>
namespace core {
class IOperationExecutor {
public:
	virtual ~IOperationExecutor() {}
	virtual OperationResult Run(const OperationRequest &, const DeviceIdentity &) = 0;
};
struct TaskSummary {
	OperationStatus status = OperationStatus::Succeeded;
	int succeeded = 0;
	int failed = 0;
};
class TaskRunner {
public:
	using Started = std::function<void(size_t, size_t, const DeviceIdentity &)>;
	using Completed = std::function<void(size_t, const OperationResult &)>;
	// Jobs contain either local steps or device steps; mixing both is rejected.
	TaskSummary Run(const TaskJob &job, IOperationExecutor &executor,
	                const Started &started = Started(), const Completed &completed = Completed()) const;
};
}
#endif
