#include "application/TaskRunner.h"
namespace core {
TaskSummary TaskRunner::Run(const TaskJob &job, IOperationExecutor &executor,
                           const Started &started, const Completed &completed) const
{
	TaskSummary summary;
	const bool local = !job.steps.empty() && !RequiresEnumeratedDevice(job.steps.front());
	for (const auto &step : job.steps) {
		if ((!RequiresEnumeratedDevice(step)) != local || step.vendor() != job.steps.front().vendor()) { summary.status = OperationStatus::Failed; return summary; }
	}
    if(!job.steps.empty()) for(const auto &target:job.targets) {
        if(target.vendor() != job.steps.front().vendor()) { summary.status=OperationStatus::Failed; return summary; }
    }
	const size_t count = local || job.targets.empty() ? 1 : job.targets.size();
	std::vector<size_t> finished(count, 0);
	std::vector<bool> failed(count, false);
	bool stopped = job.steps.empty();
	if (stopped) summary.status = OperationStatus::Failed;
	auto cancelled = [&]() { return job.cancelled && job.cancelled->stop_requested(); };
	auto run = [&](const OperationRequest &request, size_t step, size_t target) {
		DeviceRef identity = job.targets.empty() ? DeviceRef() : job.targets[target];
		if (started) started(step, target, identity);
		if (cancelled()) return;
		OperationResult result = executor.Run(request, identity);
		if (completed) completed(target, result);
		if (result.status == OperationStatus::Cancelled) {
			if (job.cancelled) job.cancelled->request_stop();
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
		const size_t targets = !RequiresEnumeratedDevice(request) ? 1 : count;
		for (size_t target = 0; target < targets && !stopped && !cancelled(); ++target)
			run(request, step, target);
	}
	if (job.rebootAfter && !local && !stopped && !cancelled()) {
		OperationRequest reset; reset=core::OperationRequest(Operation::ResetDevice);
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
