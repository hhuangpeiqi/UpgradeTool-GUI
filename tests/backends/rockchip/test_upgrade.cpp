#include "Upgrade.h"

#include <cstdio>
#include <stop_token>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

// DownloadBoot is virtual, so the real runner can be exercised without USB I/O.
class ProbeDevice : public CRKDevice {
public:
    explicit ProbeDevice(STRUCT_RKDEVICE_DESC &desc) : CRKDevice(desc) {}
    int calls = 0;
    int result = 0;
    bool enterLoader = false;
    std::stop_source *cancelAfterBoot = nullptr;

    int DownloadBoot() override {
        ++calls;
        if (enterLoader) SetUsbType(RKUSB_LOADER);
        if (cancelAfterBoot) cancelAfterBoot->request_stop();
        return result;
    }
};

static STRUCT_RKDEVICE_DESC descriptor(ENUM_RKUSB_TYPE mode) {
    STRUCT_RKDEVICE_DESC desc{};
    desc.emUsbType = mode;
    desc.emDeviceType = RK32_DEVICE;
    return desc;
}

static STRUCT_UPGRADE_STEP bootStep(UINT mask, USHORT param = 0) {
    STRUCT_UPGRADE_STEP step{};
    step.uiStep = UPGRADE_STEP_DOWNLOAD_BOOT;
    step.uiMask = mask;
    step.usParam = param;
    return step;
}

static void test_mode_filter() {
    // Workflow and USB mode must remain independent, including restore (4).
    for (auto workflow : {WORKFLOW_DOWNLOAD_IDB, WORKFLOW_DOWNLOAD_IMAGE,
                          WORKFLOW_UPGRADE, WORKFLOW_RESTORE}) {
        for (auto mode : {RKUSB_MASKROM, RKUSB_LOADER}) {
            for (UINT mask : {1u, 2u, 3u}) {
                auto desc = descriptor(mode);
                ProbeDevice device(desc);
                CRKUpgrade upgrade({bootStep(mask)}, workflow);
                upgrade.SetDeviceObject(&device);
                device.SetWorkFlow(workflow);
                const bool selected = (mask & mode) == (UINT)mode;
                CHECK(upgrade.Run() == selected);
                CHECK(device.calls == (selected ? 1 : 0));
            }
        }
    }
}

static void test_live_mode_and_phase_params() {
    auto desc = descriptor(RKUSB_MASKROM);
    ProbeDevice device(desc);
    device.enterLoader = true;
    CRKUpgrade upgrade({bootStep(RKUSB_MASKROM, 1),
                        bootStep(RKUSB_MASKROM),
                        bootStep(RKUSB_LOADER, 0x100),
                        bootStep(RKUSB_MASKROM | RKUSB_LOADER)}, WORKFLOW_UPGRADE);
    upgrade.SetDeviceObject(&device);
    device.SetWorkFlow(WORKFLOW_UPGRADE);
    CHECK(upgrade.Run());
    CHECK(device.calls == 3);
    CHECK(device.GetUsbType() == RKUSB_LOADER);
}

static void test_failure_and_cancellation() {
    auto desc = descriptor(RKUSB_MASKROM);
    {
        ProbeDevice device(desc);
        device.result = ERR_FAILED;
        CRKUpgrade upgrade({bootStep(3, 1), bootStep(3)}, WORKFLOW_UPGRADE);
        upgrade.SetDeviceObject(&device);
        CHECK(!upgrade.Run());
        CHECK(device.calls == 1);
    }
    for (bool cancelBefore : {false, true}) {
        ProbeDevice device(desc);
        std::stop_source cancellation;
        device.SetCancellation(cancellation.get_token());
        device.cancelAfterBoot = &cancellation;
        if (cancelBefore) cancellation.request_stop();
        CRKUpgrade upgrade({bootStep(3, 1), bootStep(3)}, WORKFLOW_UPGRADE);
        upgrade.SetDeviceObject(&device);
        CHECK(!upgrade.Run());
        CHECK(device.calls == (cancelBefore ? 0 : 1));
    }
}

static void test_required_wait_without_scanner() {
    // A missing scanner must fail a required wait, rather than silently
    // skipping its non-zero parameter and reporting a successful upgrade.
    for (auto wait : {UPGRADE_STEP_WAIT_MASKROM, UPGRADE_STEP_WAIT_LOADER}) {
        auto desc = descriptor(RKUSB_MASKROM);
        ProbeDevice device(desc);
        STRUCT_UPGRADE_STEP step{};
        step.uiStep = wait;
        step.uiMask = 3;
        step.usParam = 0x100;
        CRKUpgrade upgrade({bootStep(3), step, bootStep(3)}, WORKFLOW_UPGRADE);
        upgrade.SetDeviceObject(&device);
        CHECK(!upgrade.Run());
        CHECK(device.calls == 1);
    }
}

static void test_workflow_entry_steps() {
    for (int flow = 0; flow < 3; ++flow) {
        std::vector<STRUCT_UPGRADE_STEP> steps;
        if (flow == 0) CRKUpgrade::CreateUpgradeStep(RK32_DEVICE, steps, 0);
        if (flow == 1) CRKUpgrade::CreateDownloadIDBStep(RK32_DEVICE, steps);
        if (flow == 2) CRKUpgrade::CreateEraseAllBlocksStep(RK32_DEVICE, steps);
        CHECK(steps.size() >= 2);
        CHECK(steps[0].uiStep == UPGRADE_STEP_DOWNLOAD_BOOT);
        CHECK(steps[1].uiStep == UPGRADE_STEP_WAIT_MASKROM);

        // Exercise each real workflow's entry with no scanner attached. A
        // Maskrom boot failure must stop before waiting or issuing flash I/O.
        {
            auto desc = descriptor(RKUSB_MASKROM);
            ProbeDevice device(desc);
            device.result = ERR_FAILED;
            CRKUpgrade upgrade(steps, WORKFLOW_UPGRADE);
            upgrade.SetDeviceObject(&device);
            device.SetWorkFlow(WORKFLOW_UPGRADE);
            CHECK(!upgrade.Run());
            CHECK(device.calls == 1);
        }
        // Loader must skip both Maskrom entry steps. The sentinel represents
        // subsequent work, without invoking any actual device operations.
        steps.resize(2);
        steps.push_back(bootStep(RKUSB_LOADER));
        auto desc = descriptor(RKUSB_LOADER);
        ProbeDevice device(desc);
        CRKUpgrade upgrade(steps, WORKFLOW_UPGRADE);
        upgrade.SetDeviceObject(&device);
        device.SetWorkFlow(WORKFLOW_UPGRADE);
        CHECK(upgrade.Run());
        CHECK(device.calls == 1);
    }
}

int main() {
    test_mode_filter();
    test_live_mode_and_phase_params();
    test_failure_and_cancellation();
    test_required_wait_without_scanner();
    test_workflow_entry_steps();
    std::printf("test_upgrade: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
