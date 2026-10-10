#include "core/allwinner/AllwinnerBackend.h"
#include "core/DeviceSelection.h"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <cstdio>
#include <QFileInfo>
#include <algorithm>
#include <cstring>
#include <stdexcept>
class CRKLog;
CRKLog *g_pLogObject = nullptr;
using namespace core;
using namespace core::allwinner;
static int failures=0;
#define CHECK(c) do { if(!(c)) { std::printf("FAIL line %d: %s\n",__LINE__,#c); ++failures; } } while(0)
template<class F> bool Throws(F f) { try { f(); return false; } catch(const std::exception &) { return true; } }
struct TestEntry { std::string name,main,sub; Bytes data; };
static void Write(const QString &path,const Bytes &b) { QFile file(path); if(!file.open(QIODevice::WriteOnly)||file.write(reinterpret_cast<const char *>(b.data()),b.size())!=qint64(b.size())) throw std::runtime_error("fixture write"); }
static Bytes Read(const QString &path) { QFile f(path); if(!f.open(QIODevice::ReadOnly)) throw std::runtime_error("fixture read"); auto b=f.readAll(); return Bytes(b.begin(),b.end()); }
static Bytes Package(const std::vector<TestEntry> &entries,uint32_t version=0x300) {
    size_t size=(entries.size()+1)*1024; for(const auto &e:entries) size+=e.data.size();
    Bytes b(size); std::memcpy(b.data(),"IMAGEWTY",8); Put32(b.data()+8,version); Put32(b.data()+12,1024);
    Put32(b.data()+24,uint32_t(size)); Put32(b.data()+(version==0x300?60:56),entries.size());
    size_t position=(entries.size()+1)*1024;
    for(size_t i=0;i<entries.size();++i) {
        const auto &e=entries[i]; auto *p=b.data()+(i+1)*1024;
        std::memcpy(p+8,e.main.data(),std::min<size_t>(8,e.main.size())); std::memcpy(p+16,e.sub.data(),std::min<size_t>(16,e.sub.size()));
        std::memcpy(p+(version==0x300?36:52),e.name.data(),e.name.size());
        if(version==0x300) { Put64(p+292,e.data.size()); Put64(p+300,e.data.size()); Put64(p+308,position); }
        else { Put32(p+36,e.data.size()); Put32(p+40,e.data.size()); Put32(p+44,position); }
        std::copy(e.data.begin(),e.data.end(),b.begin()+position); position+=e.data.size();
    }
    return b;
}
static Bytes Mbr() { auto b=RawMbr(128,4,1); std::memset(b.data()+64,0,16); std::memcpy(b.data()+64,"rootfs",6); Put32(b.data(),Crc32(b.data()+4,16380)); return b; }
static Bytes Sparse() {
    Bytes b(28+12+512+12+4+12,0); Put32(b.data(),0xed26ff3a); b[4]=1; b[8]=28; b[10]=12;
    Put32(b.data()+12,512); Put32(b.data()+16,3); Put32(b.data()+20,3);
    size_t p=28; b[p]=0xc1; b[p+1]=0xca; Put32(b.data()+p+4,1); Put32(b.data()+p+8,524);
    std::fill(b.begin()+p+12,b.begin()+p+524,0x12); p+=524;
    b[p]=0xc2; b[p+1]=0xca; Put32(b.data()+p+4,1); Put32(b.data()+p+8,16); Put32(b.data()+p+12,0x44332211); p+=16;
    b[p]=0xc3; b[p+1]=0xca; Put32(b.data()+p+4,1); Put32(b.data()+p+8,12); return b;
}
static std::vector<TestEntry> Entries(bool sparse=false) {
    std::string cfg="[partition_start]\n[partition]\nname=rootfs\nsize=4\ndownloadfile=rootfs.img\n";
    Bytes boot(2048,0); Put32(boot.data()+44,0x40000000);
    Bytes fes(48,0); Put32(fes.data()+28,0x2000); Put32(fes.data()+32,0x1000);
    return {{"rootfs.img","RFSFAT16",Firmware::Subtype("rootfs.img"),sparse?Sparse():Bytes(1024,0x12)},
        {"sys_partition.fex","COMMON","SYS_CONFIG000000",Bytes(cfg.begin(),cfg.end())},
        {"sunxi_mbr.fex","12345678","1234567890___MBR",Mbr()},
        {"fes1.fex","FES","FES_1-0000000000",fes},
        {"u-boot.fex","12345678","UBOOT_0000000000",boot},
        {"config.fex","COMMON","SYS_CONFIG_BIN00",Bytes(16)},
        {"boot0.fex","12345678","1234567890BOOT_0",boot},
        {"bootpkg.fex","12345678","BOOTPKG-00000000",boot},
        {"bootnor.fex","12345678","1234567890BNOR_0",boot},
        {"pkgnor.fex","12345678","BOOTPKG-NOR00000",boot},
        {"bootufs.fex","12345678","1234567890BUFS_0",boot}};
}
static Bytes Gpt() {
    Bytes b(16384,0); auto *h=b.data()+512; std::memcpy(h,"EFI PART",8);
    Put32(h+8,0x10000); Put32(h+12,92); Put64(h+24,1); Put64(h+32,1000000);
    Put64(h+40,34); Put64(h+48,999999); Put64(h+72,2); Put32(h+80,4); Put32(h+84,128);
    Put32(h+16,Crc32(h,92)); return b;
}
static Bytes Dtb() {
    Bytes nodes;
    auto be=[&](uint32_t v) { nodes.push_back(uint8_t(v>>24)); nodes.push_back(uint8_t(v>>16)); nodes.push_back(uint8_t(v>>8)); nodes.push_back(uint8_t(v)); };
    auto node=[&](const char *name) { be(1); while(*name) nodes.push_back(*name++); nodes.push_back(0); while(nodes.size()%4) nodes.push_back(0); };
    auto property=[&](uint32_t key,uint32_t value) { be(3); be(4); be(key); be(value); };
    node(""); node("soc"); node("sunxi_flashmap"); node("sdmmc_map"); property(0,8192); be(2);
    node("nor_map"); property(0,2048); property(13,64); be(2); be(2); be(2); be(2); be(9);
    const char strings[]="logic_offset\0uboot_start\0";
    Bytes b(56+nodes.size()+sizeof(strings),0); std::copy(nodes.begin(),nodes.end(),b.begin()+56); std::copy(strings,strings+sizeof(strings),b.begin()+56+nodes.size());
    auto header=[&](size_t offset,uint32_t v) { b[offset]=v>>24; b[offset+1]=v>>16; b[offset+2]=v>>8; b[offset+3]=v; };
    header(0,0xd00dfeed); header(4,b.size()); header(8,56); header(12,56+nodes.size()); header(16,40);
    header(20,17); header(24,16); header(32,sizeof(strings)); header(36,nodes.size()); return b;
}
static std::vector<TestEntry> AdvancedEntries() {
    auto entries=Entries(true);
    entries.push_back({"sunxi_gpt.fex","12345678","1234567890___GPT",Gpt()});
    entries.push_back({"sunxi.fex","COMMON","DTB_CONFIG000000",Dtb()});
    Bytes secureBoot(2048,0x73);
    entries.push_back({"toc0.fex","12345678","TOC0_SDCARD00000",secureBoot});
    entries.push_back({"toc0ufs.fex","12345678","TOC0_UFS00000000",secureBoot});
    entries.push_back({"toc1.fex","12345678","TOC1_00000000000",secureBoot});
    entries[4].data[1286]=1;
    return entries;
}
struct MockState {
    std::vector<uint32_t> tags, addresses;
    std::vector<DeviceIdentity> opened;
    std::vector<std::string> actions;
    bool mismatch=false, failWrite=false, failStatus=false, cancelWrite=false, startedFel=false;
    Cancellation cancel;
    uint32_t lastSum=0;
};
class Mock : public ITransport {
    std::shared_ptr<MockState> s; uint16_t mode;
public:
    explicit Mock(std::shared_ptr<MockState> state) : s(state),mode(s->startedFel?1:2) {}
    void Open(const DeviceIdentity &d,const Cancellation &c) override { s->opened.push_back(d); s->cancel=c; s->actions.push_back("open"); }
    void Close() override { s->actions.push_back("close"); mode=2; }
    DeviceDetails Details() const override { DeviceDetails d; d.mode=mode; return d; }
    void FelWrite(uint32_t,const Bytes &) override { s->actions.push_back("fel_write"); }
    Bytes FelRead(uint32_t,size_t n) override { Bytes b(n); Put32(b.data(),2); return b; }
    void FelExec(uint32_t) override { s->actions.push_back("fel_exec"); }
    uint32_t QueryStorage() override { return 2; }
    uint32_t QuerySecure() override { return 4; }
    uint32_t QuerySize() override { return 0x1000000; }
    void FlashAccess(bool on) override { s->actions.push_back(on?"on":"off"); }
    void Download(uint32_t addr,uint32_t tag,const Bytes &b) override {
        s->tags.push_back(tag); s->addresses.push_back(addr); s->lastSum=0;
        for(size_t i=0;i<b.size();i+=4) { uint32_t w=0; for(size_t j=0;j<4&&i+j<b.size();++j) w|=uint32_t(b[i+j])<<(j*8); s->lastSum+=w; }
        if(tag==0x8000) { if(s->cancelWrite) s->cancel->store(true); if(s->failWrite) throw std::runtime_error("write failed"); }
    }
    VerifyResult Verify(uint32_t,uint64_t,bool status) override { VerifyResult r; r.flag=s->failStatus?0:0x6a617603; r.checksum=status?0:s->lastSum+(s->mismatch?1:0); return r; }
    void Finish(PostAction action) override { s->actions.push_back(action==PostAction::Reboot?"reboot":"poweroff"); }
};
static OperationRequest Request(const QString &path,Operation op=Operation::AllwinnerFlash) { OperationRequest r; r.vendor=Vendor::Allwinner; r.operation=op; r.file.path=path.toStdString(); return r; }
int main(int argc,char **argv) {
    QTemporaryDir dir; QString path=dir.filePath("固件.img"); Write(path,Package(Entries()));
    try {
        for(uint32_t v:{0x100,0x200,0x300}) {
            Write(path,Package(Entries(),v)); Firmware fw(path.toStdString()); CHECK(fw.entries.size()==11); CHECK(fw.headerVersion==v);
            CHECK(fw.Partitions()[0].name=="rootfs"); CHECK(fw.Partitions()[0].address==128); CHECK(fw.ConfigPartitions()[0].sectors==4);
        }
        CHECK(Crc32(reinterpret_cast<const uint8_t *>("123456789"),9)==0xcbf43926);
        CHECK(ParseCapacity("8GB")==8ULL*1024*1024*1024); CHECK(Throws([]{ParseCapacity("-1");})); CHECK(Throws([]{ParseCapacity("18446744073709551615GB");}));
        CHECK(Throws([]{Add(INT64_MAX,1);})); CHECK(Throws([]{Mul(INT64_MAX,512);}));
        for(uint64_t addr:{0ULL,0xffff6000ULL,0x100000000ULL}) { auto raw=RawMbr(addr,1,4); auto parts=ParseMbr(raw); CHECK(parts[0].address==addr); for(unsigned i=0;i<4;++i) CHECK(U32(raw.data()+i*16384)==Crc32(raw.data()+i*16384+4,16380)); }
        CHECK(Throws([]{RawMbr(0x100000001ULL,1,1);}));
        auto invalid=Package(Entries()); Put64(invalid.data()+1024+308,UINT64_MAX); Write(path,invalid); CHECK(Throws([&]{Firmware fw(path.toStdString());}));
        auto entries=Entries(); entries[0].name="../outside"; Write(path,Package(entries));
        { Firmware fw(path.toStdString()); CHECK(Throws([&]{fw.Extract(dir.filePath("extract").toStdString(),{},{});})); CHECK(!QFileInfo::exists(dir.filePath("extract"))); }
        Write(path,Package(Entries(true))); Firmware sparse(path.toStdString()); uint64_t expanded=0; auto layout=SparseLayout(sparse,sparse.entries[0],expanded); CHECK(expanded==1536&&layout.size()==3);
        auto malformed=Entries(true); Put32(malformed[0].data.data()+28+8,12); Write(path,Package(malformed));
        { Firmware fw(path.toStdString()); CHECK(Throws([&]{SparseLayout(fw,fw.entries[0],expanded);})); }
        Write(path,Package(Entries()));
        DeviceIdentity a,b; a.vendor=b.vendor=Vendor::Allwinner; a.bus=b.bus=1; a.ports={5,4}; b.ports={2,4};
        CHECK(MatchDevice({a,b},a)==0); a.ports={7,4}; CHECK(MatchDevice({b},a)==-1); a.ports={5,4}; CHECK(MatchDevice({a,a},a)==-2);
        for(int variant=0;variant<7;++variant) {
            auto state=std::make_shared<MockState>(); state->mismatch=variant==1; state->failWrite=variant==2; state->cancelWrite=variant==3;
            auto cancel=std::make_shared<std::atomic<bool>>(false); AllwinnerBackend backend([state]{return std::unique_ptr<ITransport>(new Mock(state));});
            auto r=Request(path); if(variant==4) { r.allwinner.mode=AllwinnerFlashMode::Partition; r.allwinner.partitions={"rootfs"}; }
            if(variant==5) { r.allwinner.mode=AllwinnerFlashMode::Partition; r.allwinner.partitions={"missing"}; }
            if(variant==6) { state->failStatus=true; }
            auto result=backend.Execute(r,a,cancel,nullptr);
            if(variant==0||variant==4) { CHECK(result.ok()); CHECK(state->actions.back()=="reboot"); }
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
          CHECK(state->actions.size()==2 && state->actions.back()=="reboot"); }
        { auto state=std::make_shared<MockState>(); state->startedFel=true; AllwinnerBackend backend([state]{return std::unique_ptr<ITransport>(new Mock(state));});
          CHECK(!backend.Execute(Request(path,Operation::AllwinnerReboot),a,{},nullptr).ok());
          CHECK(std::find(state->actions.begin(),state->actions.end(),"reboot")==state->actions.end()); }
        for(auto rawMode:{RawMode::Command,RawMode::LogicOffset}) {
            auto rawPath=dir.filePath("raw.bin"); Write(rawPath,Bytes(513,0x45));
            auto state=std::make_shared<MockState>(); AllwinnerBackend backend([state]{return std::unique_ptr<ITransport>(new Mock(state));});
            auto r=Request(path,Operation::AllwinnerRaw); r.allwinner.rawImage=rawPath.toStdString(); r.allwinner.rawMode=rawMode;
            CHECK(backend.Execute(r,a,{},nullptr).ok());
            auto index=std::find(state->tags.begin(),state->tags.end(),0x8000)-state->tags.begin();
            CHECK(state->addresses[index]==(rawMode==RawMode::Command?0:0xffff6000));
        }
        Write(path,Package(Entries(true)));
        { auto state=std::make_shared<MockState>(); AllwinnerBackend backend([state]{return std::unique_ptr<ITransport>(new Mock(state));}); CHECK(backend.Execute(Request(path),a,{},nullptr).ok()); CHECK(std::count(state->tags.begin(),state->tags.end(),0x8000)==2); }
        Write(path,Package(Entries()));
        for(auto target:{ConvertTarget::Spinor,ConvertTarget::Sdcard,ConvertTarget::Emmc,ConvertTarget::Sdnand,ConvertTarget::Ufs}) {
            Firmware fw(path.toStdString()); auto r=Request(path,Operation::AllwinnerConvert); r.allwinner.convertTarget=target; r.file.output=dir.filePath("out.bin").toStdString();
            OperationResult result; Convert(fw,r,{}, {},result); auto b=Read(QString::fromStdString(r.file.output));
            uint64_t offset=target==ConvertTarget::Spinor?1024*512+16384:40960*512+65536;
            CHECK(b.size()==(target==ConvertTarget::Spinor?16*1024*1024:offset+2048)); CHECK(b[offset]==0x12);
            if(target==ConvertTarget::Spinor) CHECK(b.back()==0xff); else CHECK(b.back()==0);
        }
        Write(path,Package(AdvancedEntries()));
        for(auto target:{ConvertTarget::Emmc,ConvertTarget::Ufs,ConvertTarget::Spinor}) {
            Firmware fw(path.toStdString()); auto r=Request(path,Operation::AllwinnerConvert); r.allwinner.convertTarget=target; r.file.output=dir.filePath("advanced.bin").toStdString();
            OperationResult result; Convert(fw,r,{}, {},result); auto b=Read(QString::fromStdString(r.file.output));
            if(target!=ConvertTarget::Spinor) {
                CHECK(b[256*512]==0x73); CHECK(b[8192*512+65536]==0x12);
                CHECK(U64(b.data()+512+32)==(8192*512+65536+2048)/512-1);
                CHECK(Crc32(b.data()+512,16)!=0);
            } else CHECK(b[2048*512+16384]==0x3a); // NOR retains reference sparse payload.
        }
        { Firmware fw(path.toStdString()); auto r=Request(path,Operation::AllwinnerConvert); r.file.output=path.toStdString(); OperationResult result; CHECK(Throws([&]{Convert(fw,r,{}, {},result);})); }
        { auto cancel=std::make_shared<std::atomic<bool>>(true); Firmware fw(path.toStdString()); auto r=Request(path,Operation::AllwinnerConvert); r.file.output=dir.filePath("cancel.bin").toStdString(); OperationResult result; CHECK(Throws([&]{Convert(fw,r,cancel,{},result);})); CHECK(!QFileInfo::exists(QString::fromStdString(r.file.output))); }
        { AllwinnerBackend backend; BackendRegistry registry; registry.Add(backend); TaskJob job; job.steps={Request(path)}; job.targets={a,b}; CHECK(registry.Validate(job)); job.targets[1].vendor=Vendor::Rockchip; CHECK(!registry.Validate(job)); job.targets.clear(); job.steps={Request(path,Operation::AllwinnerInspect)}; CHECK(registry.Validate(job)); }
        if(argc>1) {
            QDir out(QString::fromLocal8Bit(argv[1])); out.mkpath(".");
            Write(out.filePath("normal.img"),Package(Entries())); Write(out.filePath("sparse.img"),Package(Entries(true)));
            Write(out.filePath("advanced.img"),Package(AdvancedEntries()));
        }
    } catch(const std::exception &e) { std::printf("Unexpected: %s\n",e.what()); ++failures; }
    std::printf("Allwinner tests: %d failures\n",failures); return failures?1:0;
}
