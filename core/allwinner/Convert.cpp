// Derived from OpenixCLI (MIT); see LICENSE.OpenixCLI.
#include "core/allwinner/Firmware.h"
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QStringList>
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace core { namespace allwinner {
namespace {
uint32_t BE32(const uint8_t *p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
struct Offsets { uint64_t logic=40960,norLogic=1024,uboot=48; };
bool ParseDtb(const Bytes &b,Offsets &result) {
    if(b.size()<40||BE32(b.data())!=0xd00dfeed) return false;
    uint32_t total=BE32(b.data()+4), nodes=BE32(b.data()+8), strings=BE32(b.data()+12), stringSize=BE32(b.data()+32), nodeSize=BE32(b.data()+36);
    if(total>b.size()||uint64_t(strings)+stringSize>total||uint64_t(nodes)+nodeSize>total) return false;
    QStringList path; uint64_t p=nodes,end=uint64_t(nodes)+nodeSize; bool found=false;
    while(p+4<=end) {
        uint32_t token=BE32(b.data()+p); p+=4;
        if(token==1) {
            uint64_t start=p; while(p<end&&b[p]) ++p; if(p==end) return false;
            path<<QString::fromStdString(Text(b.data()+start,size_t(p-start))); p=(p+4)&~uint64_t(3);
        } else if(token==2) { if(path.isEmpty()) return false; path.removeLast(); }
        else if(token==3) {
            if(p+8>end) return false;
            uint32_t n=BE32(b.data()+p), name=BE32(b.data()+p+4); p+=8;
            if(p+n>end||name>=stringSize) return false;
            std::string key=Text(b.data()+strings+name,stringSize-name);
            QString full=path.join('/');
            if(n>=4&&full=="/soc/sunxi_flashmap/sdmmc_map"&&key=="logic_offset") { result.logic=BE32(b.data()+p); found=true; }
            if(n>=4&&full=="/soc/sunxi_flashmap/nor_map") {
                if(key=="logic_offset") { result.norLogic=BE32(b.data()+p); found=true; }
                if(key=="uboot_start") { result.uboot=BE32(b.data()+p); found=true; }
            }
            p=(p+n+3)&~uint64_t(3);
        } else if(token==9) return found;
        else if(token!=4) return false;
    }
    return false;
}
Offsets DetectOffsets(Firmware &fw) {
    Offsets result; auto *dtb=fw.Find("DTB_CONFIG000000");
    if(dtb && ParseDtb(fw.Read(*dtb),result)) return result;
    auto *uboot=fw.Find("UBOOT_0000000000"); if(!uboot) return result;
    auto b=fw.Read(*uboot);
    for(size_t pos=b.size()&~size_t(3);pos>=4;) {
        pos-=4;
        if(pos+40>b.size()||BE32(b.data()+pos)!=0xd00dfeed) continue;
        uint32_t n=BE32(b.data()+pos+4); if(n>=40&&n<=b.size()-pos&&ParseDtb(Bytes(b.begin()+pos,b.begin()+pos+n),result)) break;
    }
    return result;
}
void RewriteGpt(Bytes &b,uint64_t target,bool backup) {
    if(b.size()<604||std::memcmp(b.data()+512,"EFI PART",8)||U32(b.data()+524)!=92) throw std::runtime_error("Invalid GPT header");
    auto *h=b.data()+512;
    uint64_t entries=(Mul(U32(h+80),U32(h+84))+511)/512, sectors=target/512;
    if(sectors<entries+3) throw std::runtime_error("GPT target too small");
    uint64_t last=sectors-entries-2;
    if(last<U64(h+48)||backup) {
        Put64(h+48,last); Put64(h+32,backup?1:sectors-1);
        if(backup) { Put64(h+24,sectors-1); Put64(h+72,sectors-1-entries); }
        Put32(h+16,0); Put32(h+16,Crc32(h,92));
    }
}
}
void Convert(Firmware &fw,const OperationRequest &request,const Cancellation &cancel,
             const std::function<void(const std::string &,uint64_t,uint64_t)> &progress,OperationResult &result) {
    const auto &o=request.allwinner; bool nor=o.convertTarget==ConvertTarget::Spinor;
    QFileInfo input(QString::fromStdString(request.file.path));
    QString path=QString::fromStdString(request.file.output);
    if(path.isEmpty()) path=input.dir().filePath(input.completeBaseName()+(nor?"_full_img.bin":"_programmer.bin"));
    QFileInfo output(path);
    if(output.absoluteFilePath()==input.absoluteFilePath() || (!output.canonicalFilePath().isEmpty() && output.canonicalFilePath()==input.canonicalFilePath())) throw std::runtime_error("Output path must differ from input");
    auto offsets=DetectOffsets(fw); auto cfg=fw.ConfigPartitions();
    uint64_t logic=Mul(o.hasLogicOffset?o.logicOffset:(nor?offsets.norLogic:offsets.logic),512);
    uint64_t ubootOffset=Mul(o.hasUbootOffset?o.ubootOffset:offsets.uboot,512);
    uint64_t position=Add(logic,nor?16384:65536), size=position;
    for(const auto &p:cfg) size=Add(size,Mul(p.sectors,512));
    size=Mul(Add(size,511)/512,512);
    uint64_t norSize=Mul(o.norMiB,1024*1024),capacity=ParseCapacity(o.storageSize);
    if(nor) { if(!norSize||size>norSize) throw std::runtime_error("Partitions exceed NOR capacity"); size=norSize; }
    uint64_t gptSize=std::max(capacity,size);
    QSaveFile out(path); if(!out.open(QIODevice::WriteOnly)) throw std::runtime_error(out.errorString().toStdString());
    if(!out.resize(qint64(size))) throw std::runtime_error("Cannot allocate output image");
    auto check=[&]() { if(cancel&&cancel->load()) throw std::runtime_error("Cancelled"); };
    auto write=[&](uint64_t offset,const Bytes &b) {
        check(); uint64_t end=Add(offset,b.size()); if(nor&&end>size) throw std::runtime_error("Component exceeds NOR size");
        if(!out.seek(qint64(offset))||out.write(reinterpret_cast<const char *>(b.data()),b.size())!=qint64(b.size())) throw std::runtime_error("Output image write failed");
        if(!nor) size=std::max(size,end);
    };
    if(nor) {
        Bytes fill(1024*1024,0xff);
        for(uint64_t off=0;off<size;) { check(); size_t n=size_t(std::min<uint64_t>(fill.size(),size-off)); if(out.write(reinterpret_cast<char *>(fill.data()),n)!=qint64(n)) throw std::runtime_error("NOR output write failed"); off+=n; if(progress) progress("初始化 NOR 镜像",size,off); }
    }
    auto copy=[&](const Entry &entry,uint64_t dest,bool sparse,uint64_t limit) {
        uint64_t expanded=entry.size; auto chunks=sparse?SparseLayout(fw,entry,expanded):std::vector<SparseChunk>();
        if(expanded>limit) throw std::runtime_error("Payload exceeds partition: "+entry.name);
        if(chunks.empty()) {
            for(uint64_t pos=0;pos<entry.size;) { auto b=fw.Read(entry,pos,std::min<uint64_t>(1024*1024,entry.size-pos)); write(Add(dest,pos),b); pos+=b.size(); if(progress) progress(entry.name,entry.size,pos); }
        } else for(const auto &c:chunks) {
            if(c.type==0xcac4) continue;
            for(uint64_t pos=0;pos<c.size;) {
                check(); uint64_t n=std::min<uint64_t>(1024*1024,c.size-pos); Bytes b;
                if(c.type==0xcac1) b=fw.Read(entry,Add(c.offset,pos),n);
                else { b.resize(size_t(n),0); if(c.type==0xcac2) for(size_t i=0;i<b.size();i+=4) Put32(b.data()+i,c.fill); }
                write(Add(dest,Add(c.outputOffset,pos)),b); pos+=n; if(progress) progress(entry.name,expanded,c.outputOffset+pos);
            }
        }
    };
    auto component=[&](const char *sub,uint64_t dest,bool twice,uint64_t second) {
        auto *e=fw.Find(sub);
        if(!e) { if(progress) progress(std::string("缺少可选组件：")+sub,0,0); return; }
        copy(*e,dest,!nor,nor?size-dest:UINT64_MAX); if(twice) copy(*e,second,true,UINT64_MAX);
    };
    if(nor) {
        component("1234567890BNOR_0",0,false,0); component("BOOTPKG-NOR00000",ubootOffset,false,0);
        component("1234567890___GPT",logic,false,0);
    } else {
        bool secure=o.secure==SecureMode::Enabled;
        if(o.secure==SecureMode::Auto) { auto *e=fw.Find("UBOOT_0000000000"); if(e&&e->size>1286) secure=fw.Read(*e,1286,1)[0]!=0; }
        bool ufs=o.convertTarget==ConvertTarget::Ufs;
        component(secure?(ufs?"TOC0_UFS00000000":"TOC0_SDCARD00000"):(ufs?"1234567890BUFS_0":"1234567890BOOT_0"),16*512,true,256*512);
        component(secure?"TOC1_00000000000":"BOOTPKG-00000000",24576*512,true,32800*512);
        auto *gpt=fw.Find("1234567890___GPT");
        if(gpt) { auto b=fw.Read(*gpt,0,std::min<uint64_t>(16384,gpt->size)); RewriteGpt(b,gptSize,false); write(0,b); RewriteGpt(b,gptSize,true); write(logic,b); }
        else { auto *mbr=fw.Find("1234567890___MBR"); if(mbr) write(logic,fw.Read(*mbr,0,std::min<uint64_t>(65536,mbr->size))); }
    }
    for(const auto &p:cfg) {
        uint64_t length=Mul(p.sectors,512);
        if(!p.file.empty()) {
            auto *e=fw.Find(Firmware::Subtype(p.file)); if(!e) e=fw.Find(p.file);
            if(!e) throw std::runtime_error("Missing partition image: "+p.file);
            copy(*e,position,!nor,length);
        }
        position=Add(position,length);
    }
    check(); size=Mul(Add(size,511)/512,512);
    if(!out.resize(qint64(size))||!out.commit()) throw std::runtime_error("Cannot finalize output image");
    result.items.push_back({"输出文件",path.toStdString()}); result.items.push_back({"字节数",std::to_string(size)});
}
} }
