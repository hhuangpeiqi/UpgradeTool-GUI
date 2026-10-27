#include "tests/fixtures/allwinner/FirmwareBuilder.h"
#include "application/BackendRegistry.h"
#include "backends/DeviceIdentity.h"
#include "backends/allwinner/AllwinnerBackend.h"
#include "domain/DeviceSelection.h"
#include <cstdio>
#include "tests/support/TestFiles.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace platform { class LogService; }
using namespace core;
using namespace core::allwinner;
static int failures=0;
#define CHECK(c) do { if(!(c)) { std::printf("FAIL line %d: %s\n",__LINE__,#c); ++failures; } } while(0)
template<class F> bool Throws(F f) { try { f(); return false; } catch(const std::exception &) { return true; } }
using namespace test_support::allwinner;
struct MockState {
    std::vector<uint32_t> tags, addresses;
    std::vector<DeviceIdentity> opened;
    std::vector<std::string> actions;
    bool mismatch=false, failWrite=false, failStatus=false, cancelWrite=false, startedFel=false;
    Cancellation cancel;
    bool failQuery=false;
    std::vector<Cancellation> contexts;
    std::vector<std::chrono::steady_clock::time_point> deadlines;
    uint32_t lastSum=0;
};
class Mock : public ITransport {
    std::shared_ptr<MockState> s; uint16_t mode;
public:
    explicit Mock(std::shared_ptr<MockState> state) : s(state),mode(s->startedFel?1:2) {}
    void Open(const DeviceIdentity &d,const Cancellation &c) override { s->opened.push_back(d); s->cancel=c; s->actions.push_back("open"); }
    void SetIoContext(const Cancellation &cancel,std::chrono::steady_clock::time_point deadline) override {
        s->cancel=cancel; s->contexts.push_back(cancel); s->deadlines.push_back(deadline);
    }
    void Close() override { s->actions.push_back("close"); mode=2; }
    DeviceDetails Details() const override { DeviceDetails d; d.mode=mode; return d; }
    void FelWrite(uint32_t,const Bytes &) override { s->actions.push_back("fel_write"); }
    Bytes FelRead(uint32_t,size_t n) override { Bytes b(n); Put32(b.data(),2); return b; }
    void FelExec(uint32_t) override { s->actions.push_back("fel_exec"); }
    uint32_t QueryStorage() override {
        if(s->failQuery || (s->cancel && s->cancel->stop_requested())) throw std::runtime_error("query failed");
        return 2;
    }
    uint32_t QuerySecure() override { return 4; }
    uint32_t QuerySize() override { return 0x1000000; }
    void FlashAccess(bool on) override { s->actions.push_back(on?"on":"off"); }
    void Download(uint32_t addr,uint32_t tag,const Bytes &b) override {
        s->tags.push_back(tag); s->addresses.push_back(addr); s->lastSum=0;
        for(size_t i=0;i<b.size();i+=4) { uint32_t w=0; for(size_t j=0;j<4&&i+j<b.size();++j) w|=uint32_t(b[i+j])<<(j*8); s->lastSum+=w; }
        if(tag==0x8000) { if(s->cancelWrite) s->cancel->request_stop(); if(s->failWrite) throw std::runtime_error("write failed"); }
    }
    VerifyResult Verify(uint32_t,uint64_t,bool status) override { VerifyResult r; r.flag=s->failStatus?0:0x6a617603; r.checksum=status?0:s->lastSum+(s->mismatch?1:0); return r; }
    void Finish(PostAction action) override { s->actions.push_back(action==PostAction::Reboot?"reboot":"poweroff"); }
};
static OperationRequest Request(const std::string &path,Operation op=Operation::AllwinnerFlash) { OperationRequest r;  r=core::OperationRequest(op); r.file().path=path; return r; }
int main() {
    TemporaryDirectory dir; std::string path=dir.filePath("固件.img"); Write(path,Package(Entries()));
    try {
        {
            DeviceIdentity original; original.vendor=Vendor::Allwinner;
            original.physicalLocation="PCIROOT(0)#PCI(1)#USBROOT(0)#USB(3)#USB(2)";
            original.devicePath="fel-path"; original.deviceInstance="fel-instance";
            auto fes=original; fes.devicePath="fes-path"; fes.deviceInstance="fes-instance";
            CHECK(MatchDevice({fes},original)==0);
            CHECK(DeviceAccess::Reference(fes).key()==DeviceAccess::Reference(original).key());
            auto other=fes; other.physicalLocation="PCIROOT(0)#PCI(2)#USBROOT(0)#USB(3)#USB(2)";
            CHECK(MatchDevice({other},original)==-1);
            CHECK(MatchDevice({fes,fes},original)==-2);
        }
        for(uint32_t v:{0x100,0x200,0x300}) {
            Write(path,Package(Entries(),v)); Firmware fw(path); CHECK(fw.entries.size()==11); CHECK(fw.headerVersion==v);
            CHECK(fw.Partitions()[0].name=="rootfs"); CHECK(fw.Partitions()[0].address==128); CHECK(fw.ConfigPartitions()[0].sectors==4);
        }
        CHECK(Crc32(reinterpret_cast<const uint8_t *>("123456789"),9)==0xcbf43926);
        CHECK(ParseCapacity("8GB")==8ULL*1024*1024*1024); CHECK(Throws([]{ParseCapacity("-1");})); CHECK(Throws([]{ParseCapacity("18446744073709551615GB");}));
        CHECK(Throws([]{Add(INT64_MAX,1);})); CHECK(Throws([]{Mul(INT64_MAX,512);}));
        for(uint64_t addr:{0ULL,0xffff6000ULL,0x100000000ULL}) { auto raw=RawMbr(addr,1,4); auto parts=ParseMbr(raw); CHECK(parts[0].address==addr); for(unsigned i=0;i<4;++i) CHECK(U32(raw.data()+i*16384)==Crc32(raw.data()+i*16384+4,16380)); }
        CHECK(Throws([]{RawMbr(0x100000001ULL,1,1);}));
        auto invalid=Package(Entries()); Put64(invalid.data()+1024+308,UINT64_MAX); Write(path,invalid); CHECK(Throws([&]{Firmware fw(path);}));
        auto entries=Entries(); entries[0].name="../outside"; Write(path,Package(entries));
        { Firmware fw(path); CHECK(Throws([&]{fw.Extract(dir.filePath("extract"),{},{});})); CHECK(!std::filesystem::exists(dir.filePath("extract"))); }
        uint64_t expanded=0;
        { Write(path,Package(Entries(true))); Firmware sparse(path); auto layout=SparseLayout(sparse,sparse.entries[0],expanded); CHECK(expanded==1536&&layout.size()==3); }
        auto malformed=Entries(true); Put32(malformed[0].data.data()+28+8,12); Write(path,Package(malformed));
            { Firmware fw(path); CHECK(Throws([&]{SparseLayout(fw,fw.entries[0],expanded);})); }
        Write(path,Package(Entries()));
        DeviceIdentity a,b; a.vendor=b.vendor=Vendor::Allwinner; a.bus=b.bus=1; a.ports={5,4}; b.ports={2,4};
        CHECK(MatchDevice({a,b},a)==0); a.ports={7,4}; CHECK(MatchDevice({b},a)==-1); a.ports={5,4}; CHECK(MatchDevice({a,a},a)==-2);
        {
            auto state=std::make_shared<MockState>();
            std::vector<DeviceDescriptor> devices{Describe(a),Describe(b)};
            AllwinnerBackend backend([state] { return std::make_unique<Mock>(state); },[&] { return devices; });
            auto switching=std::make_shared<std::stop_source>();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(1500);
            CHECK(backend.Connect(a,switching,deadline).mode==2);
            CHECK(backend.HasSession(a) && state->opened.size()==1);
            CHECK(state->deadlines.front()==deadline);
            switching->request_stop();
            auto reading=std::make_shared<std::stop_source>();
            CHECK(backend.Execute(Request(path,Operation::AllwinnerDeviceInfo),a,reading,nullptr).ok());
            CHECK(state->opened.size()==1 && backend.HasSession(a));
            CHECK(std::find(state->contexts.begin(),state->contexts.end(),reading)!=state->contexts.end());
            backend.Scan(); CHECK(backend.HasSession(a));
            auto flashing=std::make_shared<std::stop_source>();
            CHECK(backend.Execute(Request(path),a,flashing,nullptr).ok());
            CHECK(state->opened.size()==1 && !backend.HasSession(a));
            CHECK(std::find(state->contexts.begin(),state->contexts.end(),flashing)!=state->contexts.end());
            backend.Connect(a,{}); backend.Connect(b,{});
            CHECK(backend.HasSession(a) && backend.HasSession(b));
            devices={Describe(b)}; backend.Scan();
            CHECK(!backend.HasSession(a) && backend.HasSession(b));
            auto incarnation=b; incarnation.usbAddress=8; devices={Describe(incarnation)};
            backend.Scan(); CHECK(!backend.HasSession(b));
            backend.Connect(incarnation,{}); state->failQuery=true;
            CHECK(!backend.Execute(Request(path,Operation::AllwinnerDeviceInfo),incarnation,{},nullptr).ok());
            CHECK(!backend.HasSession(incarnation));
            state->failQuery=false;
            backend.Connect(a,{}); backend.Connect(b,{}); backend.ReleaseSessions();
            CHECK(!backend.HasSession(a) && !backend.HasSession(b));
            backend.Connect(a,{});
            auto cancelled=std::make_shared<std::stop_source>(); cancelled->request_stop();
            CHECK(Throws([&] { backend.Connect(a,cancelled); })); CHECK(!backend.HasSession(a));
        }
        for(int variant=0;variant<7;++variant) {
            auto state=std::make_shared<MockState>(); state->mismatch=variant==1; state->failWrite=variant==2; state->cancelWrite=variant==3;
            auto cancel=std::make_shared<std::stop_source>(); AllwinnerBackend backend([state]{return std::unique_ptr<ITransport>(new Mock(state));});
            auto r=Request(path); if(variant==4) { r.allwinner().mode=AllwinnerFlashMode::Partition; r.allwinner().partitions={"rootfs"}; }
            if(variant==5) { r.allwinner().mode=AllwinnerFlashMode::Partition; r.allwinner().partitions={"missing"}; }
            if(variant==6) { state->failStatus=true; }
            auto result=backend.Execute(r,a,cancel,nullptr);
            if(variant==0||variant==4) { CHECK(result.ok()); CHECK(state->actions[state->actions.size()-2]=="reboot"); }
            else { CHECK(!result.ok()); CHECK(std::find(state->actions.begin(),state->actions.end(),"reboot")==state->actions.end()); }
            if(variant==1||variant==2||variant==3) CHECK(std::find(state->actions.begin(),state->actions.end(),"off")!=state->actions.end());
            if(variant==3) CHECK(result.status==OperationStatus::Cancelled);
            if(variant==4) {
                // Selected-partition mode retains OpenixCLI MBR and boot writes.
                CHECK(state->tags==std::vector<uint32_t>({0x7f01,0x8000,0x7f02,0x7f03}));
            }
            if(variant==5) CHECK(state->tags.empty()&&state->opened.empty());
            if(variant==6) CHECK(std::find(state->tags.begin(),state->tags.end(),0x7f01)==state->tags.end());
        }
        { auto entries=Entries(); entries.erase(entries.begin()+7); Write(path,Package(entries)); auto state=std::make_shared<MockState>(); AllwinnerBackend backend([state]{return std::unique_ptr<ITransport>(new Mock(state));}); CHECK(!backend.Execute(Request(path),a,{},nullptr).ok()); CHECK(state->tags.empty()); }
        Write(path,Package(Entries()));
        { auto state=std::make_shared<MockState>(); state->startedFel=true; AllwinnerBackend backend([state]{return std::unique_ptr<ITransport>(new Mock(state));});
          CHECK(backend.Execute(Request(path),a,{},nullptr).ok()); CHECK(state->opened.size()==2);
          CHECK(state->opened.front().ports==state->opened.back().ports && state->opened.front().bus==state->opened.back().bus); }
        // Reboot is only accepted in FES mode; a FEL-mode device is rejected.
        { auto state=std::make_shared<MockState>(); AllwinnerBackend backend([state]{return std::unique_ptr<ITransport>(new Mock(state));});
          CHECK(backend.Execute(Request(path,Operation::AllwinnerReboot),a,{},nullptr).ok());
          CHECK(state->actions==std::vector<std::string>({"open","reboot","close"})); }
        { auto state=std::make_shared<MockState>(); state->startedFel=true; AllwinnerBackend backend([state]{return std::unique_ptr<ITransport>(new Mock(state));});
          CHECK(!backend.Execute(Request(path,Operation::AllwinnerReboot),a,{},nullptr).ok());
          CHECK(std::find(state->actions.begin(),state->actions.end(),"reboot")==state->actions.end()); }
        for(auto rawMode:{RawMode::Command,RawMode::LogicOffset}) {
            auto rawPath=dir.filePath("raw.bin"); Write(rawPath,Bytes(513,0x45));
            auto state=std::make_shared<MockState>(); AllwinnerBackend backend([state]{return std::unique_ptr<ITransport>(new Mock(state));});
            auto r=Request(path,Operation::AllwinnerRaw); r.allwinner().rawImage=rawPath; r.allwinner().rawMode=rawMode;
            CHECK(backend.Execute(r,a,{},nullptr).ok());
            auto index=std::find(state->tags.begin(),state->tags.end(),0x8000)-state->tags.begin();
            CHECK(state->addresses[index]==(rawMode==RawMode::Command?0:0xffff6000));
        }
        Write(path,Package(Entries(true)));
        { auto state=std::make_shared<MockState>(); AllwinnerBackend backend([state]{return std::unique_ptr<ITransport>(new Mock(state));}); CHECK(backend.Execute(Request(path),a,{},nullptr).ok()); CHECK(std::count(state->tags.begin(),state->tags.end(),0x8000)==2); }
        Write(path,Package(Entries()));
        for(auto target:{ConvertTarget::Spinor,ConvertTarget::Sdcard,ConvertTarget::Emmc,ConvertTarget::Sdnand,ConvertTarget::Ufs}) {
            Firmware fw(path); auto r=Request(path,Operation::AllwinnerConvert); r.allwinner().convertTarget=target; r.file().output=dir.filePath("out.bin");
            OperationResult result; Convert(fw,r,{}, {},result); auto b=Read(r.file().output);
            uint64_t offset=target==ConvertTarget::Spinor?1024*512+16384:40960*512+65536;
            CHECK(b.size()==(target==ConvertTarget::Spinor?16*1024*1024:offset+2048)); CHECK(b[offset]==0x12);
            if(target==ConvertTarget::Spinor) CHECK(b.back()==0xff); else CHECK(b.back()==0);
        }
        Write(path,Package(AdvancedEntries()));
        for(auto target:{ConvertTarget::Emmc,ConvertTarget::Ufs,ConvertTarget::Spinor}) {
            Firmware fw(path); auto r=Request(path,Operation::AllwinnerConvert); r.allwinner().convertTarget=target; r.file().output=dir.filePath("advanced.bin");
            OperationResult result; Convert(fw,r,{}, {},result); auto b=Read(r.file().output);
            if(target!=ConvertTarget::Spinor) {
                CHECK(b[256*512]==0x73); CHECK(b[8192*512+65536]==0x12);
                CHECK(U64(b.data()+512+32)==(8192*512+65536+2048)/512-1);
                CHECK(Crc32(b.data()+512,16)!=0);
            } else CHECK(b[2048*512+16384]==0x3a); // NOR retains reference sparse payload.
        }
        { Firmware fw(path); auto r=Request(path,Operation::AllwinnerConvert); r.file().output=path; OperationResult result; CHECK(Throws([&]{Convert(fw,r,{}, {},result);})); }
        { auto cancel=[] { auto c=std::make_shared<std::stop_source>(); c->request_stop(); return c; }(); Firmware fw(path); auto r=Request(path,Operation::AllwinnerConvert); r.file().output=dir.filePath("cancel.bin"); OperationResult result; CHECK(Throws([&]{Convert(fw,r,cancel,{},result);})); CHECK(!std::filesystem::exists(r.file().output)); }
        { AllwinnerBackend backend; BackendRegistry registry; registry.Add(backend); TaskJob job; job.steps={Request(path)}; job.targets={a,b}; CHECK(registry.Validate(job)); job.targets[1]=DeviceIdentity{}; CHECK(!registry.Validate(job)); job.targets.clear(); job.steps={Request(path,Operation::AllwinnerInspect)}; CHECK(registry.Validate(job)); }

    } catch(const std::exception &e) { std::printf("Unexpected: %s\n",e.what()); ++failures; }
    std::printf("Allwinner tests: %d failures\n",failures); return failures?1:0;
}
