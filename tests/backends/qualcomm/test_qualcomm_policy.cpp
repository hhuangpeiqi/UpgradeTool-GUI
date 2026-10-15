#include "backends/DeviceIdentity.h"
#include "backends/qualcomm/QualcommBackend.h"
#include "backends/qualcomm/OperationPolicy.h"
#include "domain/DeviceSelection.h"
#include "qdl_bridge.h"
#include <cstdio>
#include <cerrno>
namespace platform { class FileLog; }
static int failures;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(0)
using namespace core;
class FakeBackend : public qualcomm::QualcommBackend {
public:
    std::vector<DeviceDescriptor> devices;
    std::vector<DeviceDescriptor> Scan() override { return devices; }
};
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    // Descriptor resemblance alone cannot turn DIAG, an OEM PID or another
    // vendor's 9008 into a download target.
    for(int protocol : {0xff,0x10,0x11,0x13}) {
        bool signature=qtq_download_interface(0xff,0xff,protocol,0x81,1,512,512);
        CHECK(signature);
        CHECK(qtq_classify(0x05c6,0x9008,signature,0,0)==QTQ_EDL);
        CHECK(qtq_classify(0x05c6,0x900e,signature,0,0)==QTQ_DUMP);
        CHECK(qtq_classify(0x05c6,0x9091,signature,0,1)==QTQ_DIAG);
        CHECK(qtq_classify(0x05c6,0x9091,signature,0,0)==QTQ_UNKNOWN);
        CHECK(qtq_classify(0x1234,0x9008,signature,0,0)==QTQ_UNKNOWN);
        CHECK(qtq_classify(0x05c6,0x1234,signature,0,0)==QTQ_UNKNOWN);
    }
    CHECK(qtq_download_compatible_id("USB\\Class_FF&SubClass_FF&Prot_10"));
    CHECK(qtq_download_compatible_id("usb\\class_ff&subclass_ff&prot_ff"));
    CHECK(!qtq_download_compatible_id("USB\\Class_FF&SubClass_42&Prot_01"));
    CHECK(!qtq_download_compatible_id("USB\\Class_FF&SubClass_FF&Prot_FFgarbage"));
    CHECK(!qtq_download_interface(0xff,0x42,1,0x81,1,512,512));
    CHECK(!qtq_download_interface(0xff,0xff,0xff,0,1,512,512));
    CHECK(!qtq_download_interface(0xff,0xff,0xff,0x81,1,512,0));
    CHECK(qtq_classify(0x05c6,0x9008,0,0,1)==QTQ_UNKNOWN);
    CHECK(qtq_classify(0x05c6,0x9008,1,1,1)==QTQ_ADB);
    CHECK(qtq_known_diag(0x05c6,0x9091,0));
    CHECK(!qtq_known_diag(0x05c6,0x9091,1));
    CHECK(qtq_diag_name("Qualcomm DIAG (COM5)"));
    CHECK(qtq_diag_name("QCDM"));
    CHECK(!qtq_diag_name("diagnosis"));
    CHECK(!qtq_diag_name("Qualcomm QDLoader 9008"));
    for(int op=int(Operation::QualcommFlash);op<=int(Operation::QualcommProvision);++op) {
        CHECK(!qualcomm::EligibilityReason(Operation(op),QTQ_EDL).size());
        CHECK(!qualcomm::EligibilityReason(Operation(op),QTQ_DIAG).empty());
        CHECK(!qualcomm::EligibilityReason(Operation(op),QTQ_UNKNOWN).empty());
        CHECK(qualcomm::EligibilityReason(Operation(op),QTQ_DUMP).empty()==(Operation(op)==Operation::QualcommRamdump));
        CHECK(!qualcomm::EligibilityReason(Operation(op),QTQ_EDL,-3).empty());
    }
    FakeBackend backend;
    DeviceDescriptor d; DeviceIdentity identity; identity.vendor=Vendor::Qualcomm;
    identity.vid=0x05c6; identity.pid=0x9091; identity.bus=1; identity.ports={2};
    identity.interfaceNumber=0; identity.usbAddress=5; identity.inEndpoint=0x81; identity.outEndpoint=1;
    d.modeCode=DeviceMode::Diag; d.reference=identity; backend.devices={d};
    OperationRequest r;  r=core::OperationRequest(Operation::QualcommFlash);
    CHECK(backend.Execute(r,identity,{},nullptr).error==ErrorCode::UnsupportedDevice);
    identity.pid=0x900e; d.modeCode=DeviceMode::CrashDump; d.reference=identity; backend.devices={d};
    for(auto op : {Operation::QualcommFlash,Operation::QualcommRaw,Operation::QualcommReset,Operation::QualcommChipInfo,Operation::QualcommProvision}) {
        r=core::OperationRequest(op);
        CHECK(backend.Execute(r,identity,{},nullptr).error==ErrorCode::UnsupportedDevice);
    }
    r=core::OperationRequest(Operation::QualcommFlash);
    identity.pid=0x9008; d.modeCode=DeviceMode::Download; d.accessError=-3; d.reference=identity; backend.devices={d};
    CHECK(backend.Execute(r,identity,{},nullptr).error==ErrorCode::UnsupportedDevice);
    d.accessError=0; d.reference=identity; backend.devices={d};
    auto stale=identity; stale.pid=0x9091;
    CHECK(backend.Execute(r,stale,{},nullptr).error==ErrorCode::DeviceMissing);
    stale=identity; ++stale.usbAddress;
    CHECK(backend.Execute(r,stale,{},nullptr).error==ErrorCode::DeviceMissing);
    stale=identity; stale.ports.clear(); ++stale.bus;
    CHECK(backend.Execute(r,stale,{},nullptr).error==ErrorCode::DeviceMissing);
    stale=identity; ++stale.interfaceNumber;
    CHECK(backend.Execute(r,stale,{},nullptr).error==ErrorCode::DeviceMissing);
    stale=identity; stale.deviceInstance="recycled-COM";
    CHECK(backend.Execute(r,stale,{},nullptr).error==ErrorCode::DeviceMissing);
    backend.devices.push_back(d);
    CHECK(backend.Execute(r,identity,{},nullptr).error==ErrorCode::DeviceAmbiguous);
    // The C transport entry point independently refuses an explicitly supplied
    // DIAG or crash-dump write target before opening any hardware.
    qtq_request raw; qtq_defaults(&raw); raw.command=QTQ_CHIPINFO;
    raw.target.vid=0x05c6; raw.target.pid=0x9091;
    CHECK(qtq_execute(&raw,nullptr)==-ENODEV);
    raw.target.pid=0x900e; CHECK(qtq_execute(&raw,nullptr)==-ENODEV);
    raw.target.vid=0x1234; raw.target.pid=0x9008; CHECK(qtq_execute(&raw,nullptr)==-ENODEV);
    std::printf("Qualcomm policy: %d failures\n",failures);
    return failures ? 1 : 0;
}
