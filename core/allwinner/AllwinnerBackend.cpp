// Derived from OpenixCLI (MIT); see LICENSE.OpenixCLI.
#include "core/allwinner/AllwinnerBackend.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
#include <QThread>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace core { namespace allwinner {
namespace {
const uint32_t ValidVerify=0x6a617603;
const size_t ChunkSize=1024*1024;
void CheckCancel(const Cancellation &c) { if(c&&c->load()) throw std::runtime_error("Cancelled"); }
void Wait(unsigned ms,const Cancellation &c) { for(unsigned n=0;n<ms;n+=50) { CheckCancel(c); QThread::msleep(std::min(50u,ms-n)); } }
void Log(IUpgradeObserver *o,const std::string &text) { if(o) o->OnOutput(LogLevel::Info,text); }
// Progress stages use stable, untranslated keys for comparisons; their display
// text is resolved in DisplayStage() so every visible string is a translatable
// literal that lupdate can extract.
namespace Stage {
const char *const InitDram     = "stage.init-dram";
const char *const LoadUboot    = "stage.load-uboot";
const char *const WaitFes      = "stage.wait-fes";
const char *const QueryDevice  = "stage.query-device";
const char *const EraseMode    = "stage.erase-mode";
const char *const WriteTable   = "stage.write-table";
const char *const WritePreboot = "stage.write-preboot";
const char *const WriteBoot1   = "stage.write-boot1";
const char *const WriteBoot0   = "stage.write-boot0";
const char *const FinishAction = "stage.finish-action";
const char *const FlashDone    = "stage.flash-done";
const char *const Extract      = "stage.extract";
}
const char *const kWritePrefix = "stage.write/";
const char *const kSkipPrefix  = "stage.skip/";
std::string DisplayStage(const std::string &stage) {
    const size_t wl=std::strlen(kWritePrefix), sl=std::strlen(kSkipPrefix);
    if(stage.compare(0,wl,kWritePrefix)==0)
        return QCoreApplication::translate("Allwinner", "Writing / ").toStdString()+stage.substr(wl);
    if(stage.compare(0,sl,kSkipPrefix)==0)
        return QCoreApplication::translate("Allwinner", "Skipping blank / ").toStdString()+stage.substr(sl);
    if(stage==Stage::InitDram)     return QCoreApplication::translate("Allwinner", "Initializing DRAM").toStdString();
    if(stage==Stage::LoadUboot)    return QCoreApplication::translate("Allwinner", "Loading U-Boot").toStdString();
    if(stage==Stage::WaitFes)      return QCoreApplication::translate("Allwinner", "Waiting for FES reconnect").toStdString();
    if(stage==Stage::QueryDevice)  return QCoreApplication::translate("Allwinner", "Querying device").toStdString();
    if(stage==Stage::EraseMode)    return QCoreApplication::translate("Allwinner", "Setting erase mode").toStdString();
    if(stage==Stage::WriteTable)   return QCoreApplication::translate("Allwinner", "Writing partition table").toStdString();
    if(stage==Stage::WritePreboot) return QCoreApplication::translate("Allwinner", "Writing Preboot").toStdString();
    if(stage==Stage::WriteBoot1)   return QCoreApplication::translate("Allwinner", "Writing Boot1").toStdString();
    if(stage==Stage::WriteBoot0)   return QCoreApplication::translate("Allwinner", "Writing Boot0").toStdString();
    if(stage==Stage::FinishAction) return QCoreApplication::translate("Allwinner", "Finishing action").toStdString();
    if(stage==Stage::FlashDone)    return QCoreApplication::translate("Allwinner", "Flashing complete").toStdString();
    if(stage==Stage::Extract)      return QCoreApplication::translate("Allwinner", "Extracting firmware").toStdString();
    return stage;
}
void Progress(IUpgradeObserver *o,const std::string &stage,uint64_t total,uint64_t done) {
    int start=-1,width=0;
    if(stage==Stage::InitDram) { start=0; width=5; }
    else if(stage==Stage::LoadUboot) { start=5; width=5; }
    else if(stage==Stage::WaitFes) start=10;
    else if(stage==Stage::QueryDevice) start=15;
    else if(stage==Stage::EraseMode) start=18;
    else if(stage==Stage::WriteTable) start=20;
    else if(stage.compare(0,std::strlen(kWritePrefix),kWritePrefix)==0||stage.compare(0,std::strlen(kSkipPrefix),kSkipPrefix)==0) { start=22; width=73; }
    else if(stage==Stage::WritePreboot) start=95;
    else if(stage==Stage::WriteBoot1) start=96;
    else if(stage==Stage::WriteBoot0) start=98;
    else if(stage==Stage::FinishAction) start=99;
    else if(stage==Stage::FlashDone) start=100;
    int overall=start<0?-1:start+(total?int(double(done)/double(total)*width):0);
    if(o) o->OnStageProgress(DisplayStage(stage),total,done,overall);
}
uint32_t Sum(const Bytes &data) {
    uint32_t sum=0;
    for(size_t i=0;i<data.size();i+=4) { uint32_t word=0; for(size_t j=0;j<4&&i+j<data.size();++j) word|=uint32_t(data[i+j])<<(8*j); sum+=word; }
    return sum;
}
void Verify(ITransport &t,uint32_t addr,uint64_t size,uint32_t sum) {
    auto r=t.Verify(addr,size); if(r.flag!=ValidVerify || r.checksum!=sum) throw std::runtime_error("Write verification failed at sector "+std::to_string(addr));
}
void VerifyStatus(ITransport &t,uint32_t tag,bool zero,const Cancellation &c) {
    for(unsigned i=0;i<5;++i) {
        CheckCancel(c); auto r=t.Verify(tag,0,true);
        if(r.flag==ValidVerify) { if(zero&&r.checksum) throw std::runtime_error("Device rejected FES tag "+std::to_string(tag)); return; }
        Wait(100,c);
    }
    throw std::runtime_error("FES status verification timed out: "+std::to_string(tag));
}
struct Download {
    Partition part; const Entry *entry=nullptr;
    uint64_t size=0, expanded=0;
    std::vector<SparseChunk> chunks;
};
std::pair<const Entry *,const Entry *> BootEntries(Firmware &fw,uint32_t secure,uint32_t storage) {
    const Entry *boot1=nullptr,*boot0=nullptr;
    if(secure!=0&&secure!=1&&secure!=4) throw std::runtime_error("Unsupported boot mode");
    boot1=fw.Find(secure==1?"TOC1_00000000000":secure==0?"UBOOT_0000000000":storage==3?"BOOTPKG-NOR00000":"BOOTPKG-00000000");
    std::string sub;
    if(secure==1) {
        sub=(storage==0||storage==5)?"TOC0_NAND0000000":(storage==1||storage==6)?"TOC0_SDCARD00000":storage==3?"TOC0_SPINOR00000":storage==8?"TOC0_UFS00000000":"TOC0_00000000000";
    } else {
        sub=(storage==0||storage==5)?"BOOT0_0000000000":storage==3?"1234567890BNOR_0":storage==8?"1234567890BUFS_0":"1234567890BOOT_0";
    }
    boot0=fw.Find(sub); if(!boot0) boot0=fw.Find(secure==1?"TOC0_00000000000":"1234567890BOOT_0");
    if(!boot0||!boot1||boot0->size<48||boot1->size<48) throw std::runtime_error("Missing or invalid boot components for device storage/security mode");
    return {boot0,boot1};
}
void PrepareFel(Firmware &fw,ITransport &t,const DeviceIdentity &target,const Cancellation &cancel,IUpgradeObserver *o) {
    auto fes=fw.Read(fw.Require("FES_1-0000000000"));
    auto uboot=fw.Read(fw.Require("UBOOT_0000000000"));
    auto config=fw.Read(fw.Require("SYS_CONFIG_BIN00"));
    if(fes.size()<48||uboot.size()<228||config.size()>512*1024) throw std::runtime_error("Invalid FEL boot firmware");
    uint32_t run=U32(fes.data()+32),ret=U32(fes.data()+28),ubootRun=U32(uboot.data()+44);
    if(Add(run,fes.size())>0x100000000ULL||Add(ubootRun,std::max<uint64_t>(uboot.size(),4*1024*1024))>0x100000000ULL||Add(ret,136)>0x100000000ULL) throw std::runtime_error("FEL address overflow");
    Progress(o,Stage::InitDram,1,0); Log(o,QCoreApplication::translate("Allwinner", "Initializing DRAM").toStdString());
    t.FelWrite(ret,Bytes(136,0)); t.FelWrite(run,fes); t.FelExec(run);
    bool ready=false;
    for(unsigned i=0;i<60;++i) {
        Wait(1000,cancel); Bytes b;
        try { b=t.FelRead(ret,136); } catch(const std::exception &) { CheckCancel(cancel); continue; }
        uint32_t flag=U32(b.data()); if(flag==1) throw std::runtime_error("DRAM initialization failed");
        if(flag>1) { ready=true; break; }
    }
    if(!ready) throw std::runtime_error("DRAM initialization timed out");
    Progress(o,Stage::InitDram,1,1);
    auto component=[&](const char *sub,uint32_t offset,size_t limit) {
        auto *e=fw.Find(sub); if(!e) return;
        auto b=fw.Read(*e); if(b.size()>limit) throw std::runtime_error("FEL component exceeds reserved memory slot");
        t.FelWrite(ubootRun+offset,b);
    };
    Progress(o,Stage::LoadUboot,1,0);
    component("DTB_CONFIG000000",2*1024*1024,1024*1024);
    t.FelWrite(ubootRun+3*1024*1024,config);
    component("BOARD_CONFIG_BIN",3*1024*1024+512*1024,512*1024);
    Put32(uboot.data()+224,0x10); t.FelWrite(ubootRun,uboot); t.FelExec(ubootRun); t.Close();
    Progress(o,Stage::WaitFes,1,0); Log(o,QCoreApplication::translate("Allwinner", "Waiting for the FES device to reconnect on the original USB port").toStdString());
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(45);
    Wait(2000,cancel);
    do {
        try { t.Open(target,cancel); if(t.Details().mode==2) return; t.Close(); }
        catch(const std::exception &) { t.Close(); CheckCancel(cancel); }
        Wait(1000,cancel);
    } while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error("FES reconnect timed out at original USB port");
}
void Flash(Firmware &fw,const OperationRequest &request,const DeviceIdentity &target,const Cancellation &cancel,IUpgradeObserver *o,ITransport &t) {
    const auto &opts=request.allwinner; const bool raw=request.operation==Operation::AllwinnerRaw;
    QFile external; Bytes mbr; std::vector<Download> downloads;
    auto mode=raw?AllwinnerFlashMode::FullErase:opts.mode;
    bool verify=!raw&&opts.verify;
    if(raw) {
        external.setFileName(QString::fromStdString(opts.rawImage));
        if(!external.open(QIODevice::ReadOnly)||external.size()<=0) throw std::runtime_error("Cannot open non-empty raw image");
        uint64_t offset=opts.hasLogicOffset?opts.logicOffset:opts.rawStorage==RawStorage::Nor?2106:40960;
        if(offset>0x100000000ULL) throw std::runtime_error("Raw logic offset exceeds 32-bit sector range");
        Download d; d.part.name="raw"; d.part.address=opts.rawMode==RawMode::LogicOffset?0x100000000ULL-offset:0;
        d.size=d.expanded=uint64_t(external.size()); d.part.sectors=Add(d.size,511)/512;
        unsigned copies=1;
        if(opts.rawMode==RawMode::LogicOffset) {
            auto *e=fw.Find("1234567890___MBR");
            if(e) { auto b=fw.Read(*e); try { ParseMbr(b); copies=U32(b.data()+16); } catch(...) {} }
        }
        mbr=RawMbr(d.part.address,d.part.sectors,copies); downloads.push_back(d);
    } else {
        mbr=fw.Read(fw.Require("1234567890___MBR")); auto parts=fw.Partitions();
        for(const auto &selected:opts.partitions) {
            auto it=std::find_if(parts.begin(),parts.end(),[&](const Partition &p) { return p.name==selected&&!p.file.empty(); });
            if(it==parts.end()) throw std::runtime_error("Unknown or non-writable selected partition: "+selected);
        }
        for(const auto &p:parts) {
            QString lower=QString::fromStdString(p.name).toLower();
            if(mode==AllwinnerFlashMode::KeepData&&(lower=="udisk"||lower=="private"||lower=="reserve")) continue;
            if(mode==AllwinnerFlashMode::Partition&&!opts.partitions.empty()&&std::find(opts.partitions.begin(),opts.partitions.end(),p.name)==opts.partitions.end()) continue;
            if(p.file.empty()) continue;
            Download d; d.part=p; d.entry=fw.Find(Firmware::Subtype(p.file)); if(!d.entry) d.entry=fw.Find(p.file);
            if(!d.entry) throw std::runtime_error("Missing partition image: "+p.file);
            d.size=d.entry->size; d.chunks=SparseLayout(fw,*d.entry,d.expanded);
            if(d.expanded>Mul(p.sectors,512)) throw std::runtime_error("Partition payload exceeds capacity: "+p.name);
            if(Add(p.address,Add(d.expanded,511)/512)>0x100000000ULL) throw std::runtime_error("Partition exceeds FES sector range");
            downloads.push_back(d);
        }
        if(downloads.empty()) throw std::runtime_error("No partition payloads selected");
    }
    // Load FEL prerequisites and validate partition streams before any storage erase.
    fw.Require("FES_1-0000000000");
    t.Open(target,cancel); auto details=t.Details();
    if(details.mode==1) PrepareFel(fw,t,target,cancel,o);
    else if(details.mode!=2) throw std::runtime_error("Unsupported Allwinner device mode");
    Progress(o,Stage::QueryDevice,1,0);
    uint32_t secure=t.QuerySecure(),storage=t.QueryStorage(),capacity=t.QuerySize();
    Log(o,QCoreApplication::translate("Allwinner", "Storage: %1, capacity: %2 MiB, boot mode: %3")
            .arg(QString::fromStdString(StorageName(storage)))
            .arg(uint64_t(capacity)*512/1024/1024)
            .arg(secure).toStdString());
    auto boots=BootEntries(fw,secure,storage);
    // Read boot images before erasing; malformed or oversized components must fail first.
    auto boot0=fw.Read(*boots.first),boot1=fw.Read(*boots.second);
    auto *preboot=fw.Find(secure==1?"TOC0_PREBOOT0000":"1234567890PREB_0"); Bytes pre;
    if(preboot) pre=fw.Read(*preboot);
    if(raw&&Add(downloads.front().size,511)/512>capacity) throw std::runtime_error("Raw image exceeds device capacity");
    for(const auto &d:downloads) if(!raw&&Add(d.part.address,Add(d.expanded,511)/512)>capacity) throw std::runtime_error("Partition exceeds device capacity: "+d.part.name);
    if(mode!=AllwinnerFlashMode::Partition) {
        CheckCancel(cancel); Progress(o,Stage::EraseMode,1,0); Bytes flag(16,0);
        Put32(flag.data(),mode==AllwinnerFlashMode::FullErase?0x12:mode==AllwinnerFlashMode::PartitionErase?1:0);
        t.Download(0,0x7f04,flag); VerifyStatus(t,0x7f04,true,cancel);
    }
    if(!raw&&storage!=1&&storage!=6) for(const auto &d:downloads) {
        QString name=QString::fromStdString(d.part.name).toUpper();
        if(name.startsWith("UDISK")||name.startsWith("SYSRECOVERY")||name.startsWith("PRIVATE")||d.size<4) continue;
        auto b=fw.Read(*d.entry,0,4); if(U32(b.data())==0x06101831) { t.Download(0,0x7ff0,Bytes(4096)); break; }
    }
    CheckCancel(cancel); Progress(o,Stage::WriteTable,1,0); t.Download(0,0x7f01,mbr); VerifyStatus(t,0x7f01,true,cancel);
    uint64_t total=0,done=0; for(const auto &d:downloads) total=Add(total,d.expanded);
    bool access=false;
    try {
        t.FlashAccess(true); access=true;
        for(const auto &d:downloads) {
            CheckCancel(cancel); Log(o,QCoreApplication::translate("Allwinner", "Writing partition: %1").arg(QString::fromStdString(d.part.name)).toStdString());
            if(raw&&uint64_t(external.size())!=d.size) throw std::runtime_error("Raw image changed before write");
            auto write=[&](uint64_t outputOffset,Bytes b,size_t original) {
                CheckCancel(cancel); uint64_t sector=Add(d.part.address,outputOffset/512);
                if(!raw&&sector>UINT32_MAX) throw std::runtime_error("FES address overflow");
                uint32_t checksum=Sum(b); size_t padded=(b.size()+511)&~size_t(511); b.resize(padded,0);
                t.Download(uint32_t(sector),0x8000,b);
                if(verify) Verify(t,uint32_t(sector),original,checksum);
                done+=original; Progress(o,std::string(kWritePrefix)+d.part.name,total,done);
            };
            if(d.chunks.empty()) {
                for(uint64_t pos=0;pos<d.size;) {
                    CheckCancel(cancel); size_t n=size_t(std::min<uint64_t>(ChunkSize,d.size-pos)); Bytes b;
                    if(raw) { QByteArray bytes=external.read(n); if(size_t(bytes.size())!=n) throw std::runtime_error("Raw image read failed"); b.assign(bytes.begin(),bytes.end()); }
                    else b=fw.Read(*d.entry,pos,n);
                    write(pos,std::move(b),n); pos+=n;
                }
            } else for(const auto &c:d.chunks) {
                if(c.type==0xcac4) continue;
                if(c.type==0xcac3) { done+=c.size; Progress(o,std::string(kSkipPrefix)+d.part.name,total,done); continue; }
                for(uint64_t pos=0;pos<c.size;) {
                    CheckCancel(cancel); size_t n=size_t(std::min<uint64_t>(ChunkSize,c.size-pos)); Bytes b;
                    if(c.type==0xcac1) b=fw.Read(*d.entry,Add(c.offset,pos),n);
                    else { b.resize(n); for(size_t i=0;i<n;i+=4) Put32(b.data()+i,c.fill); }
                    write(Add(c.outputOffset,pos),std::move(b),n); pos+=n;
                }
            }
        }
        t.FlashAccess(false); access=false;
    } catch(...) {
        if(access) { try { t.FlashAccess(false); } catch(const std::exception &e) { Log(o,QCoreApplication::translate("Allwinner", "Failed to close storage access: %1").arg(QString::fromStdString(e.what())).toStdString()); } }
        throw;
    }
    auto boot=[&](const Bytes &b,uint32_t tag,const char *name) {
        CheckCancel(cancel); Progress(o,name,1,0); t.Download(0,tag,b); VerifyStatus(t,tag,false,cancel);
    };
    if(!pre.empty()) boot(pre,0x7f08,Stage::WritePreboot);
    boot(boot1,0x7f02,Stage::WriteBoot1); boot(boot0,0x7f03,Stage::WriteBoot0);
    CheckCancel(cancel); Progress(o,Stage::FinishAction,1,0); t.Finish(raw?PostAction::Reboot:opts.postAction);
    Progress(o,Stage::FlashDone,1,1);
}
}
OperationResult AllwinnerBackend::Execute(const OperationRequest &request,const DeviceIdentity &target,const Cancellation &cancel,IUpgradeObserver *observer) {
    OperationResult result; result.operation=request.operation;
    try {
        if(request.vendor!=Vendor::Allwinner||!Supports(request.operation)) { result.error=ErrorCode::UnsupportedDevice; throw std::runtime_error("Unsupported Allwinner operation"); }
        CheckCancel(cancel);
        if(request.operation==Operation::AllwinnerDeviceInfo) {
            auto t=factory(); t->Open(target,cancel); auto d=t->Details();
            result.items.push_back({QCoreApplication::translate("Allwinner", "Device mode").toStdString(),ModeName(d.mode)}); result.items.push_back({QCoreApplication::translate("Allwinner", "Chip ID").toStdString(),std::to_string(d.chip)});
            result.items.push_back({QCoreApplication::translate("Allwinner", "Protocol firmware").toStdString(),std::to_string(d.firmware)});
            if(d.mode==2) {
                result.items.push_back({QCoreApplication::translate("Allwinner", "Storage type").toStdString(),StorageName(t->QueryStorage())});
                result.items.push_back({QCoreApplication::translate("Allwinner", "Capacity (bytes)").toStdString(),std::to_string(uint64_t(t->QuerySize())*512)});
                result.items.push_back({QCoreApplication::translate("Allwinner", "Boot mode").toStdString(),std::to_string(t->QuerySecure())});
            }
        } else if(request.operation==Operation::AllwinnerReboot) {
            auto t=factory(); t->Open(target,cancel);
            if(t->Details().mode!=2) throw std::runtime_error("Reboot requires the device to be in FES mode");
            Log(observer,QCoreApplication::translate("Allwinner", "Rebooting device").toStdString());
            t->Finish(PostAction::Reboot);
        } else if(request.operation==Operation::AllwinnerInspect) {
            result=InspectFirmware(request.file.path);
        } else {
            Firmware fw(request.file.path);
            if(request.operation==Operation::AllwinnerExtract) {
                if(request.file.output.empty()) throw std::runtime_error("Choose an output directory");
                fw.Extract(request.file.output,cancel,[&](uint64_t total,uint64_t done) { Progress(observer,Stage::Extract,total,done); });
            } else if(request.operation==Operation::AllwinnerConvert) {
                Convert(fw,request,cancel,[&](const std::string &stage,uint64_t total,uint64_t done) { if(total) Progress(observer,stage,total,done); else Log(observer,stage); },result);
            } else {
                auto transport=factory(); Flash(fw,request,target,cancel,observer,*transport);
            }
        }
        CheckCancel(cancel); result.status=OperationStatus::Succeeded;
    } catch(const std::exception &e) {
        result.diagnostic=e.what();
        result.status=cancel&&cancel->load()?OperationStatus::Cancelled:OperationStatus::Failed;
        if(result.error==ErrorCode::None) result.error=ErrorCode::OperationFailed;
        if(observer) observer->OnOutput(result.status==OperationStatus::Cancelled?LogLevel::Info:LogLevel::Error,result.diagnostic);
    }
    if(observer) observer->OnResult(result);
    return result;
}
} }
