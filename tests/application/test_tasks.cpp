#include "backends/DeviceIdentity.h"
#include "application/TaskRunner.h"
#include <cstdio>
#include <vector>
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
class FakeExecutor : public core::IOperationExecutor {
public:
	std::vector<core::Operation> operations;
	std::vector<uint32_t> locations;
	int failAt = -1, cancelAt = -1;
	core::Cancellation cancel;
	core::OperationResult Run(const core::OperationRequest &r, const core::DeviceRef &target) override {
		int index = static_cast<int>(operations.size()); operations.push_back(r.operation()); locations.push_back(core::DeviceAccess::Identity(target).location);
		core::OperationResult result; result.operation = r.operation();
		result.status = index == failAt ? core::OperationStatus::Failed : core::OperationStatus::Succeeded;
		if (index == cancelAt) cancel->request_stop();
		return result;
	}
};
static core::TaskJob Job() {
	core::TaskJob job;
	core::OperationRequest loader, image;
	loader=core::OperationRequest(core::Operation::LoaderUpgrade); image=core::OperationRequest(core::Operation::DownloadImage);
	job.steps = {loader, image}; job.rebootAfter = true;
	core::DeviceIdentity a, b; a.location = 11; b.location = 22; job.targets = {a, b};
	return job;
}
int main() {
	core::TaskRunner runner;
	{
		auto job = Job(); FakeExecutor executor; int callbacks = 0;
		auto result = runner.Run(job, executor, {}, [&](size_t, const core::OperationResult &) { ++callbacks; });
		CHECK(result.status == core::OperationStatus::Succeeded && result.succeeded == 2);
		CHECK(executor.operations.size() == 6 && callbacks == 6);
		CHECK(executor.operations[0] == core::Operation::LoaderUpgrade && executor.operations[2] == core::Operation::DownloadImage);
		CHECK(executor.locations == std::vector<uint32_t>({11, 22, 11, 22, 11, 22}));
	}
	{
		auto job = Job(); FakeExecutor executor; executor.failAt = 0;
		auto result = runner.Run(job, executor);
		CHECK(result.status == core::OperationStatus::Failed && result.failed == 1);
		CHECK(executor.operations.size() == 1);
	}
	{
		auto job = Job(); job.continueOnError = true; FakeExecutor executor; executor.failAt = 0;
		auto result = runner.Run(job, executor);
		CHECK(result.failed == 1 && result.succeeded == 1);
		CHECK(executor.operations.size() == 5); CHECK(executor.locations.back() == 22);
		CHECK(executor.operations.back() == core::Operation::ResetDevice);
	}
	{
		auto job = Job(); FakeExecutor executor; executor.cancel = job.cancelled; executor.cancelAt = 0;
		auto result = runner.Run(job, executor);
		CHECK(result.status == core::OperationStatus::Cancelled && result.succeeded == 0);
		CHECK(executor.operations.size() == 1);
	}
	{
		auto job = Job(); job.cancelled->request_stop(); FakeExecutor executor;
		CHECK(runner.Run(job, executor).status == core::OperationStatus::Cancelled); CHECK(executor.operations.empty());
	}
	{
		auto job = Job(); job.steps.resize(1); job.steps[0]=core::OperationRequest(core::Operation::CreateGpt); job.rebootAfter = false;
		FakeExecutor executor; CHECK(runner.Run(job, executor).status == core::OperationStatus::Succeeded); CHECK(executor.operations.size() == 1);
	}
	{
		auto job = Job(); job.steps.resize(1); job.steps[0]=core::OperationRequest(core::Operation::FirmwareInfo);
		FakeExecutor executor; CHECK(runner.Run(job, executor).succeeded == 1); CHECK(executor.operations.size() == 1); // Local jobs never reboot devices.
	}
	{
		auto job = Job(); job.steps.resize(1); FakeExecutor executor; executor.failAt = 1;
		auto result = runner.Run(job, executor); CHECK(result.succeeded == 1 && result.failed == 1);
	}
	{
		core::TaskJob job; FakeExecutor executor;
		CHECK(runner.Run(job, executor).status == core::OperationStatus::Failed); CHECK(executor.operations.empty());
	}

	{
		auto job = Job(); job.steps[0]=core::OperationRequest(core::Operation::CreateGpt); FakeExecutor executor;
		CHECK(runner.Run(job, executor).status == core::OperationStatus::Failed); CHECK(executor.operations.empty());
	}
	{
		auto job = Job(); FakeExecutor executor;
		auto result = runner.Run(job, executor, [&](size_t, size_t, const core::DeviceRef &) { job.cancelled->request_stop(); });
		CHECK(result.status == core::OperationStatus::Cancelled); CHECK(executor.operations.empty());
	}
	std::printf("test_tasks: %s\n", failures ? "FAIL" : "PASS"); return failures ? 1 : 0;
}
