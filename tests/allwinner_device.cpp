// Manual integration harness. Never registered with CTest; writes require --flash.
#include "core/allwinner/AllwinnerBackend.h"
#include <QCoreApplication>
#include <libusb.h>
#include <cstdio>
#include <chrono>
class Observer : public core::IUpgradeObserver {
    std::chrono::steady_clock::time_point last;
public:
    void OnOutput(core::LogLevel,const std::string &text) override { std::printf("%s\n",text.c_str()); std::fflush(stdout); }
    void OnProgress(ENUM_PROGRESS_PROMPT,long long,long long,ENUM_CALL_STEP) override {}
    void OnTransferProgress(core::TransferStage,long long,long long) override {}
    void OnUpgradePrompt(ENUM_UPGRADE_PROMPT,unsigned) override {}
    int OnSelectStorage(const std::vector<std::string> &,int) override { return 0; }
    void OnStageProgress(const std::string &text,uint64_t total,uint64_t done,int) override {
        auto now=std::chrono::steady_clock::now();
        if(done==0||done==total||now-last>std::chrono::seconds(2)) { std::printf("%s: %llu/%llu\n",text.c_str(),(unsigned long long)done,(unsigned long long)total); std::fflush(stdout); last=now; }
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); libusb_init(nullptr); Observer observer; core::allwinner::AllwinnerBackend backend;
    core::OperationRequest r; r.vendor=core::Vendor::Allwinner; r.operation=core::Operation::AllwinnerDeviceInfo;
    auto args=app.arguments();
    if(args.size()>2) {
        r.file.path=args[2].toStdString();
        if(args[1]=="--inspect") r.operation=core::Operation::AllwinnerInspect;
        else if(args[1]=="--flash") r.operation=core::Operation::AllwinnerFlash;
        else if(args[1]=="--convert"&&args.size()>3) { r.operation=core::Operation::AllwinnerConvert; r.file.output=args[3].toStdString();
            if(args.size()>4) {
                const QStringList targets={"spinor","sdcard","emmc","sdnand","ufs"};
                int index=targets.indexOf(args[4]); if(index<0) return 2;
                r.allwinner.convertTarget=static_cast<core::ConvertTarget>(index);
            } }
        else return 2;
    }
    core::DeviceIdentity target;
    if(!core::IsLocalOperation(r.operation)) {
        auto devices=backend.Scan(); if(devices.size()!=1) { std::fprintf(stderr,"Expected exactly one Allwinner device; found %zu\n",devices.size()); libusb_exit(nullptr); return 2; }
        target=devices[0].identity;
    }
    auto result=backend.Execute(r,target,{},&observer);
    for(const auto &item:result.items) std::printf("%s: %s\n",item.label.c_str(),item.value.c_str());
    for(const auto &p:result.partitions) std::printf("%s: %llu + %llu sectors\n",p.name.c_str(),(unsigned long long)p.begin,(unsigned long long)p.count);
    libusb_exit(nullptr); return result.ok()?0:1;
}
