#include "backends/rockchip/MaskromTransition.h"
#include "backends/DeviceIdentity.h"
#include "application/TaskRunner.h"
#include "tests/support/Checks.h"
#include <cstdio>
#include <algorithm>

static int failures = 0;
using namespace core::rockchip;
using namespace std::chrono_literals;

struct Fixture {
    int resets = 0, scans = 0, waits = 0;
    bool cancelled = false, resetOk = true;
    BYTE subcode = 0;
    std::chrono::steady_clock::time_point clock{};
    std::vector<std::vector<ModeDevice>> snapshots{{{42, RKUSB_MASKROM}}};
    MaskromHooks hooks{
        [this](BYTE code) { ++resets; subcode = code; return resetOk; },
        [this] { return snapshots[std::min<size_t>(scans++, snapshots.size() - 1)]; },
        [this] { return cancelled; },
        [this] { ++waits; },
        [this] { return clock; },
        [this](auto duration) { clock += duration; }
    };
    MaskromResult run(int mode = RKUSB_LOADER) { return EnterMaskrom(mode, 42, 150ms, hooks); }
};

struct Batch : core::IOperationExecutor {
    int calls = 0;
    bool cancelFirst = false;
    core::Cancellation token;
    core::OperationResult Run(const core::OperationRequest &request, const core::DeviceRef &target) override {
        CHECK(request.operation() == core::Operation::EnterMaskrom);
        Fixture f;
        const auto location = core::DeviceAccess::Identity(target).location;
        if (location == 1) f.snapshots = {{}};
        else f.snapshots = {{{location, RKUSB_MASKROM}}};
        if (cancelFirst) f.hooks.sleep = [&](auto) { token->request_stop(); };
        f.hooks.cancelled = [&] { return token->stop_requested(); };
        ++calls;
        auto result = EnterMaskrom(RKUSB_LOADER, location, 150ms, f.hooks);
        core::OperationResult output;
        output.operation = request.operation();
        output.status = result == MaskromResult::Succeeded ? core::OperationStatus::Succeeded :
                        result == MaskromResult::Cancelled ? core::OperationStatus::Cancelled : core::OperationStatus::Failed;
        return output;
    }
};

int main() {
    {
        Fixture f;
        CHECK(f.run() == MaskromResult::Succeeded);
        CHECK(f.resets == 1 && f.subcode == 3 && f.waits == 1);
    }
    for (int mode : {RKUSB_MASKROM, RKUSB_NONE, RKUSB_MSC}) {
        Fixture f; CHECK(f.run(mode) == MaskromResult::UnsupportedMode);
        CHECK(f.resets == 0 && f.scans == 0);
    }
    {
        Fixture f; f.resetOk = false;
        CHECK(f.run() == MaskromResult::CommandFailed);
        CHECK(f.resets == 1 && f.scans == 0 && f.waits == 0);
    }
    {
        Fixture f;
        // Unrelated Maskrom devices and reordered enumeration cannot satisfy the wait.
        f.snapshots = {{{41, RKUSB_MASKROM}, {42, RKUSB_LOADER}},
                       {{43, RKUSB_MASKROM}}, {},
                       {{43, RKUSB_MASKROM}, {42, RKUSB_MASKROM}}};
        CHECK(f.run() == MaskromResult::Succeeded);
        CHECK(f.scans == 4 && f.resets == 1);
    }
    for (const auto &snapshot : std::vector<std::vector<ModeDevice>>{
             {}, {{42, RKUSB_LOADER}}, {{43, RKUSB_MASKROM}}}) {
        Fixture f; f.snapshots = {snapshot};
        CHECK(f.run() == MaskromResult::TimedOut);
        CHECK(f.clock.time_since_epoch() == 150ms && f.resets == 1);
    }
    {
        Fixture f; f.snapshots = {{{42, RKUSB_LOADER}, {42, RKUSB_MASKROM}}};
        CHECK(f.run() == MaskromResult::Ambiguous);
    }
    {
        Fixture f; f.cancelled = true;
        CHECK(f.run() == MaskromResult::Cancelled && f.resets == 0);
    }
    {
        Fixture f; f.snapshots = {{}};
        f.hooks.sleep = [&](auto) { f.cancelled = true; };
        CHECK(f.run() == MaskromResult::Cancelled && f.scans == 1);
    }
    {
        Fixture f;
        f.hooks.scan = [&] { f.cancelled = true; return std::vector<ModeDevice>{{42, RKUSB_MASKROM}}; };
        CHECK(f.run() == MaskromResult::Cancelled);
    }
    for (bool keepGoing : {false, true}) {
        core::TaskJob job;
        job.steps = {core::OperationRequest(core::Operation::EnterMaskrom)};
        CHECK(job.steps[0].vendor() == core::Vendor::Rockchip);
        for (unsigned location : {1, 2, 3}) {
            core::DeviceIdentity identity; identity.location = location;
            job.targets.push_back(identity);
        }
        job.continueOnError = keepGoing;
        Batch executor; executor.token = job.cancelled;
        auto result = core::TaskRunner{}.Run(job, executor);
        CHECK(result.failed == 1 && result.succeeded == (keepGoing ? 2 : 0));
        CHECK(executor.calls == (keepGoing ? 3 : 1));
        job.cancelled = std::make_shared<std::stop_source>();
        Batch cancel; cancel.token = job.cancelled; cancel.cancelFirst = true;
        result = core::TaskRunner{}.Run(job, cancel);
        CHECK(result.status == core::OperationStatus::Cancelled && cancel.calls == 1);
        CHECK(result.succeeded == 0 && result.failed == 0);
    }
    std::printf("test_maskrom: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
