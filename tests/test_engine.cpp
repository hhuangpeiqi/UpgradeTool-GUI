#include "core/UpgradeEngine.h"
#include "core/DeviceSelection.h"
#include "RKDevice.h"
#include <cstdio>
#include <string>
#include <vector>
CRKLog *g_pLogObject = NULL;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
class Observer : public core::IUpgradeObserver {
public:
	int results = 0, progress = 0;
	long long lastTotal = 0, lastCurrent = 0;
	core::OperationResult last;
	void OnOutput(core::LogLevel, const std::string &) override {}
	void OnProgress(ENUM_PROGRESS_PROMPT, long long total, long long current, ENUM_CALL_STEP) override { ++progress; lastTotal = total; lastCurrent = current; }
	void OnTransferProgress(core::TransferStage, long long total, long long current) override { ++progress; lastTotal = total; lastCurrent = current; }
	void OnUpgradePrompt(ENUM_UPGRADE_PROMPT, unsigned int) override {}
	int OnSelectStorage(const std::vector<std::string> &, int) override { return 0; }
	void OnResult(const core::OperationResult &r) override { ++results; last = r; }
};
int main() {
	core::UpgradeEngine engine;
	Observer observer; engine.SetObserver(&observer); engine.BeginTask();
	core::OperationRequest request;
	CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	CHECK(observer.results == 1);
	request.operation = core::Operation::CreateGpt;
	CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	request.file.path = "/tmp/upgrade_engine_missing_parameter";
	CHECK(engine.Execute(request).error == core::ErrorCode::FileAccess);
	const char *parameter = "engine_test_parameter.txt";
	const char *output = "engine_test_output.gpt";
	FILE *file = std::fopen(parameter, "w"); CHECK(file != NULL);
	if (!file) return 1;
	std::fputs("TYPE: GPT\nCMDLINE:mtdparts=rk29xxnand:0x1000@0x2000(uboot),0x2000@0x3000(boot)\nuuid:uboot=00112233-4455-6677-8899-aabbccddeeff\n", file); std::fclose(file);
	request.file.path = parameter; request.file.output = output;
	CHECK(engine.Execute(request).ok());
	file = std::fopen(output, "rb"); CHECK(file != NULL);
	if (file) { unsigned char head[0x4400]; CHECK(std::fread(head, 1, sizeof(head), file) == sizeof(head)); CHECK(head[0x200] == 'E' && head[0x201] == 'F' && head[0x202] == 'I'); std::fclose(file); }
	request.file.output.clear(); CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	request.operation = core::Operation::FirmwareInfo; CHECK(engine.Execute(request).error == core::ErrorCode::InvalidFirmware);
	core::FirmwareInfo info; CHECK(!core::FirmwareService::Inspect(parameter, info)); CHECK(!info.valid);
	request.operation = core::Operation::ResetPipe; request.pipe = 256; CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	request.operation = core::Operation::ResetDevice; request.subcode = 6; CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	request.operation = core::Operation::EraseLba; request.lba.count = 0; CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	request.lba.begin = UINT32_MAX; request.lba.count = 2; CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	request.operation = core::Operation::TestDevice; CHECK(engine.Execute(request).error == core::ErrorCode::DeviceMissing);
	engine.RequestAbort(); CHECK(engine.IsAborted());
	CHECK(engine.Execute(request).status == core::OperationStatus::Cancelled);
	CHECK(engine.IsAborted()); // An operation must never erase cancellation between queue steps.
	engine.BeginTask(); CHECK(!engine.IsAborted());
	core::Cancellation cancellation = std::make_shared<std::atomic<bool> >(true);
	engine.SetCancellation(cancellation); engine.BeginTask(); CHECK(engine.IsAborted());
	CHECK(engine.Execute(request).status == core::OperationStatus::Cancelled);
	cancellation->store(false); engine.BeginTask();
	engine.ResetProgress(1000); engine.StepProgress(100); CHECK(observer.progress == 1); CHECK(observer.lastTotal == 1000 && observer.lastCurrent == 100);
	engine.StepProgress(5); CHECK(observer.progress == 1); engine.StepProgress(20); CHECK(observer.progress == 2);
	engine.ResetProgress(0); engine.StepProgress(10); CHECK(observer.progress == 2);
	core::DeviceIdentity a, b; a.serial = "same"; a.location = 1; b.serial = "same"; b.location = 2;
	std::vector<core::DeviceIdentity> devices{b, a};
	CHECK(core::MatchDevice(devices, a) == 1); // Reordering must not change the selected identity.
	core::DeviceIdentity target; target.serial = "same"; CHECK(core::MatchDevice(devices, target) == -2);
	target.location = 3; CHECK(core::MatchDevice(devices, target) == -1);
	a.serial.clear(); b.serial.clear(); devices = {b, a}; CHECK(core::MatchDevice(devices, a) == 1);
	target = core::DeviceIdentity(); CHECK(core::MatchDevice(devices, target) == -2);
	std::remove(parameter); std::remove(output);
	std::printf("test_engine: %s\n", failures ? "FAIL" : "PASS"); return failures ? 1 : 0;
}
