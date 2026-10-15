#pragma once
#include "backends/allwinner/Firmware.h"
#include "tests/support/TestFiles.h"
#include <algorithm>
#include <cstring>

namespace test_support::allwinner {
using namespace core::allwinner;
struct TestEntry { std::string name,main,sub; Bytes data; };
inline void Write(const std::string &path,const Bytes &b) { platform::AtomicFile out(platform::Path(path)); out.write(b.data(),b.size()); out.commit(); }
inline Bytes Read(const std::string &path) { platform::File file(platform::Path(path)); return file.read(static_cast<size_t>(file.size())); }
inline Bytes Package(const std::vector<TestEntry> &entries,uint32_t version=0x300) {
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
inline Bytes Mbr() { auto b=RawMbr(128,4,1); std::memset(b.data()+64,0,16); std::memcpy(b.data()+64,"rootfs",6); Put32(b.data(),Crc32(b.data()+4,16380)); return b; }
inline Bytes Sparse() {
    Bytes b(28+12+512+12+4+12,0); Put32(b.data(),0xed26ff3a); b[4]=1; b[8]=28; b[10]=12;
    Put32(b.data()+12,512); Put32(b.data()+16,3); Put32(b.data()+20,3);
    size_t p=28; b[p]=0xc1; b[p+1]=0xca; Put32(b.data()+p+4,1); Put32(b.data()+p+8,524);
    std::fill(b.begin()+p+12,b.begin()+p+524,0x12); p+=524;
    b[p]=0xc2; b[p+1]=0xca; Put32(b.data()+p+4,1); Put32(b.data()+p+8,16); Put32(b.data()+p+12,0x44332211); p+=16;
    b[p]=0xc3; b[p+1]=0xca; Put32(b.data()+p+4,1); Put32(b.data()+p+8,12); return b;
}
inline std::vector<TestEntry> Entries(bool sparse=false) {
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
inline Bytes Gpt() {
    Bytes b(16384,0); auto *h=b.data()+512; std::memcpy(h,"EFI PART",8);
    Put32(h+8,0x10000); Put32(h+12,92); Put64(h+24,1); Put64(h+32,1000000);
    Put64(h+40,34); Put64(h+48,999999); Put64(h+72,2); Put32(h+80,4); Put32(h+84,128);
    Put32(h+16,Crc32(h,92)); return b;
}
inline Bytes Dtb() {
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
inline std::vector<TestEntry> AdvancedEntries() {
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
}
