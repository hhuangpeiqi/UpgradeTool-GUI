#include "core/TaskRunner.h"
namespace core {
TaskSummary TaskRunner::Run(const TaskJob &job, IOperationExecutor &executor,
                           const Started &started, const Completed &completed) const
{
	TaskSummary summary;
	const bool local = !job.steps.empty() && IsLocalOperation(job.steps.front().operation);
	for (const auto &step : job.steps) {
		if (IsLocalOperation(step.operation) != local) { summary.status = OperationStatus::Failed; return summary; }
	}
	const size_t count = local || job.targets.empty() ? 1 : job.targets.size();
	std::vector<size_t> finished(count, 0);
	std::vector<bool> failed(count, false);
	bool stopped = job.steps.empty();
	if (stopped) summary.status = OperationStatus::Failed;
	auto cancelled = [&]() { return job.cancelled && job.cancelled->load(); };
	auto run = [&](const OperationRequest &request, size_t step, size_t target) {
		DeviceIdentity identity = job.targets.empty() ? DeviceIdentity() : job.targets[target];
		if (started) started(step, target, identity);
		if (cancelled()) return;
		OperationResult result = executor.Run(request, identity);
		if (completed) completed(target, result);
		if (result.status == OperationStatus::Cancelled) {
			if (job.cancelled) job.cancelled->store(true);
			summary.status = OperationStatus::Cancelled;
			stopped = true;
		} else if (result.ok()) {
			if (step < job.steps.size()) ++finished[target];
		} else {
			failed[target] = true;
			summary.status = OperationStatus::Failed;
			if (!job.continueOnError) stopped = true;
		}
	};
	for (size_t step = 0; step < job.steps.size() && !stopped && !cancelled(); ++step) {
		const OperationRequest &request = job.steps[step];
		const size_t targets = IsLocalOperation(request.operation) ? 1 : count;
		for (size_t target = 0; target < targets && !stopped && !cancelled(); ++target)
			run(request, step, target);
	}
	if (job.rebootAfter && !local && !stopped && !cancelled()) {
		OperationRequest reset; reset.operation = Operation::ResetDevice;
		for (size_t target = 0; target < count && !stopped && !cancelled(); ++target)
			if (!failed[target]) run(reset, job.steps.size(), target);
	}
	if (cancelled()) summary.status = OperationStatus::Cancelled;
	for (size_t i = 0; i < count; ++i) {
		if (failed[i]) ++summary.failed;
		else if (finished[i] == job.steps.size() && !job.steps.empty()) ++summary.succeeded;
	}
	return summary;
}
}
