#include "tests/support/TestFiles.h"
#include "backends/DeviceIdentity.h"
#include "backends/rockchip/UpgradeEngine.h"
#include "backends/rockchip/FirmwareService.h"
#include "domain/DeviceSelection.h"
#include "RKDevice.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
class Observer : public core::IUpgradeObserver {
public:
	int results = 0, progress = 0;
	long long lastTotal = 0, lastCurrent = 0;
	core::OperationResult last;
	void OnOutput(core::LogLevel, const std::string &) override {}
	void OnProgress(core::ProgressPrompt, long long total, long long current, core::CallStep) override { ++progress; lastTotal = total; lastCurrent = current; }
	void OnTransferProgress(core::TransferStage, long long total, long long current) override { ++progress; lastTotal = total; lastCurrent = current; }
	void OnUpgradePrompt(core::UpgradePrompt, unsigned int) override {}
	int OnSelectStorage(const std::vector<std::string> &, int) override { return 0; }
	void OnResult(const core::OperationResult &r) override { ++results; last = r; }
};
int main() {
    TemporaryDirectory temporary;
	core::UpgradeEngine engine;
	Observer observer; engine.SetObserver(&observer); engine.BeginTask();
	core::OperationRequest request;
	CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	CHECK(observer.results == 0);
	request=core::OperationRequest(core::Operation::CreateGpt);
	CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	request.file().path = temporary.filePath("missing_parameter");
	CHECK(engine.Execute(request).error == core::ErrorCode::FileAccess);
	const auto parameterStorage = temporary.filePath("\u6d4b\u8bd5 parameter.txt");
	const char *parameter = parameterStorage.c_str();
	const auto outputStorage = temporary.filePath("\u8f93\u51fa output.gpt");
	const char *output = outputStorage.c_str();
	{ std::ofstream file(platform::Path(parameter), std::ios::binary);
	  file << "TYPE: GPT\nCMDLINE:mtdparts=rk29xxnand:0x1000@0x2000(uboot),0x2000@0x3000(boot)\nuuid:uboot=00112233-4455-6677-8899-aabbccddeeff\n";
	  file.close(); CHECK(bool(file)); }
	request.file().path = parameter; request.file().output = output;
	CHECK(engine.Execute(request).ok());
	{ std::ifstream file(platform::Path(output), std::ios::binary);
	  unsigned char head[0x4400] = {};
	  CHECK(bool(file.read(reinterpret_cast<char *>(head), sizeof(head))));
	  CHECK(head[0x200] == 'E' && head[0x201] == 'F' && head[0x202] == 'I');
	  const unsigned char uuid[] = {0x33, 0x22, 0x11, 0x00, 0x55, 0x44, 0x77, 0x66,
	                                0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
	  CHECK(std::memcmp(head + 0x410, uuid, sizeof(uuid)) == 0); }
	request.file().output.clear(); CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	request=core::OperationRequest(core::Operation::FirmwareInfo); request.file().path=parameter; CHECK(engine.Execute(request).error == core::ErrorCode::InvalidFirmware);
	core::FirmwareInfo info; CHECK(!core::FirmwareService::Inspect(parameter, info)); CHECK(!info.valid);
	request=core::OperationRequest(core::Operation::ResetPipe); request.rockchip().pipe = 256; CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	request=core::OperationRequest(core::Operation::ResetDevice); request.rockchip().subcode = 6; CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	request=core::OperationRequest(core::Operation::EraseLba); request.rockchip().lba.count = 0; CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	request.rockchip().lba.begin = UINT32_MAX; request.rockchip().lba.count = 2; CHECK(engine.Execute(request).error == core::ErrorCode::InvalidInput);
	request=core::OperationRequest(core::Operation::TestDevice); CHECK(engine.Execute(request).error == core::ErrorCode::DeviceMissing);
	engine.RequestAbort(); CHECK(engine.IsAborted());
	CHECK(engine.Execute(request).status == core::OperationStatus::Cancelled);
	CHECK(engine.IsAborted()); // An operation must never erase cancellation between queue steps.
	engine.BeginTask(); CHECK(!engine.IsAborted());
	core::Cancellation cancellation = [] { auto c=std::make_shared<std::stop_source>(); c->request_stop(); return c; }();
	engine.SetCancellation(cancellation); engine.BeginTask(); CHECK(engine.IsAborted());
	CHECK(engine.Execute(request).status == core::OperationStatus::Cancelled);
	cancellation=std::make_shared<std::stop_source>(); engine.SetCancellation(cancellation); CHECK(!engine.IsAborted());
	engine.RequestAbort(); engine.SetCancellation(cancellation); CHECK(engine.IsAborted());
	CHECK(engine.Execute(request).status == core::OperationStatus::Cancelled);
	cancellation=std::make_shared<std::stop_source>(); engine.SetCancellation(cancellation); CHECK(!engine.IsAborted());
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
	for (const auto vendor : {core::Vendor::Rockchip, core::Vendor::Allwinner, core::Vendor::Qualcomm}) {
		core::DeviceIdentity original; original.vendor = vendor; original.serial = "72407970c1202880214e";
		auto upper = original; upper.serial = "72407970C1202880214E";
		CHECK(core::MatchDevice({upper}, original) == 0);
		CHECK(core::MatchDevice({original}, upper) == 0);
		CHECK(core::MatchDevice({original, upper}, original) == -2);
		upper.serial = "another-device";
		CHECK(core::MatchDevice({upper}, original) == -1);
	}
	std::filesystem::remove(platform::Path(parameter)); std::filesystem::remove(platform::Path(output));
	std::printf("test_engine: %s\n", failures ? "FAIL" : "PASS"); return failures ? 1 : 0;
}
