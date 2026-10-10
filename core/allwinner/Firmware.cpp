// Derived from OpenixCLI (MIT); see LICENSE.OpenixCLI.
#include "core/allwinner/Firmware.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
namespace core { namespace allwinner {
uint16_t U16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t U32(const uint8_t *p) { return uint32_t(U16(p)) | uint32_t(U16(p+2)) << 16; }
uint64_t U64(const uint8_t *p) { return uint64_t(U32(p)) | uint64_t(U32(p+4)) << 32; }
void Put32(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;++i) p[i] = uint8_t(v >> (i*8)); }
void Put64(uint8_t *p, uint64_t v) { Put32(p, uint32_t(v)); Put32(p+4, uint32_t(v>>32)); }
uint32_t Crc32(const uint8_t *p, size_t n) {
    uint32_t crc=~0u;
    while (n--) { crc ^= *p++; for (int i=0;i<8;++i) crc = (crc>>1) ^ (0xedb88320u & (0u-(crc&1))); }
    return ~crc;
}
uint64_t Add(uint64_t a, uint64_t b) { if (b > uint64_t(INT64_MAX) || a > uint64_t(INT64_MAX)-b) throw std::runtime_error("Offset overflow"); return a+b; }
uint64_t Mul(uint64_t a, uint64_t b) { if (b && a > uint64_t(INT64_MAX)/b) throw std::runtime_error("Size overflow"); return a*b; }
std::string Text(const uint8_t *p, size_t n) { return std::string(reinterpret_cast<const char *>(p), std::find(p,p+n,0)-p); }
Firmware::Firmware(const std::string &path) : file(QString::fromStdString(path)) {
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(file.errorString().toStdString());
    size = uint64_t(file.size());
    QByteArray h=file.read(1024);
    if (h.size()!=1024 || std::memcmp(h.constData(),"IMAGEWTY",8)) throw std::runtime_error("Unsupported or encrypted IMAGEWTY firmware");
    auto p=reinterpret_cast<const uint8_t *>(h.constData());
    headerVersion=U32(p+8); attr=U32(p+16); version=U32(p+20);
    if (headerVersion!=0x100 && headerVersion!=0x200 && headerVersion!=0x300) throw std::runtime_error("Unsupported IMAGEWTY version");
    bool v3=headerVersion==0x300;
    uint32_t count=U32(p+(v3?60:56));
    uint64_t declared=v3?U64(p+24):U32(p+24);
    if (declared>size || Mul(Add(count,1),1024)>size) throw std::runtime_error("Firmware header exceeds input size");
    std::set<std::string> names;
    for(uint32_t i=0;i<count;++i) {
        h=file.read(1024); if(h.size()!=1024) throw std::runtime_error("Truncated entry header");
        p=reinterpret_cast<const uint8_t *>(h.constData());
        Entry e; e.mainType=Text(p+8,8); e.subType=Text(p+16,16);
        e.name=Text(p+(v3?36:52),256);
        e.storedSize=v3?U64(p+292):U32(p+36);
        e.size=v3?U64(p+300):U32(p+40);
        e.offset=v3?U64(p+308):U32(p+44);
        if (Add(e.offset,e.size)>size || Add(e.offset,e.storedSize)>size || e.size>e.storedSize) throw std::runtime_error("Invalid firmware entry range: "+e.name);
        if (e.name.empty() || !names.insert(e.name).second) throw std::runtime_error("Empty or duplicate firmware filename");
        entries.push_back(e);
    }
}
const Entry *Firmware::Find(const std::string &name, const std::string &main) const {
    for (const auto &e:entries) if ((main.empty()||main==e.mainType) && (e.name==name||e.subType==name)) return &e;
    return nullptr;
}
const Entry &Firmware::Require(const std::string &name, const std::string &main) const {
    const auto *e=Find(name,main); if(!e) throw std::runtime_error("Missing firmware component: "+name); return *e;
}
Bytes Firmware::Read(const Entry &e, uint64_t offset, uint64_t count) {
    if(offset>e.size) throw std::runtime_error("Entry offset exceeds size");
    if(count==UINT64_MAX) count=e.size-offset;
    if(Add(offset,count)>e.size || count>64*1024*1024) throw std::runtime_error("Invalid entry read size");
    if(!file.seek(qint64(Add(e.offset,offset)))) throw std::runtime_error("Firmware seek failed");
    QByteArray data=file.read(qint64(count)); if(uint64_t(data.size())!=count) throw std::runtime_error("Firmware read failed");
    return Bytes(data.begin(),data.end());
}
std::string Firmware::Subtype(const std::string &name) {
    QString s=QString::fromStdString(name).toUpper().replace('.','_')+"0000000000000000";
    return s.left(16).toStdString();
}
std::vector<Partition> ParseMbr(const Bytes &b) {
    if(b.size()<16384 || std::memcmp(b.data()+8,"softw411",8) || U32(b.data()+24)>120) throw std::runtime_error("Invalid Sunxi MBR");
    if(Crc32(b.data()+4,16380)!=U32(b.data())) throw std::runtime_error("Sunxi MBR checksum mismatch");
    std::vector<Partition> result; std::set<std::string> names;
    for(uint32_t i=0;i<U32(b.data()+24);++i) {
        const uint8_t *p=b.data()+32+i*128; Partition part;
        part.address=uint64_t(U32(p))<<32|U32(p+4); part.sectors=uint64_t(U32(p+8))<<32|U32(p+12);
        part.name=Text(p+32,16); part.readonly=U32(p+56)!=0;
        if(part.name.empty()||!names.insert(part.name).second) throw std::runtime_error("Invalid MBR partition name");
        Add(part.address,part.sectors); result.push_back(part);
    }
    return result;
}
std::vector<ConfigPartition> Firmware::ConfigPartitions() {
    const auto *e=Find("SYS_CONFIG000000"); if(!e) return {};
    auto b=Read(*e); QString text=QString::fromUtf8(reinterpret_cast<const char *>(b.data()),int(b.size()));
    std::vector<ConfigPartition> parts;
    for(QString line:text.split('\n')) {
        line=line.section(';',0,0).trimmed();
        if(line=="[partition]") { parts.push_back(ConfigPartition()); continue; }
        if(line.startsWith('[') || parts.empty() || !line.contains('=')) continue;
        QString key=line.section('=',0,0).trimmed(), value=line.section('=',1).trimmed();
        if(value.startsWith('"')&&value.endsWith('"')) value=value.mid(1,value.size()-2);
        if(key=="name") parts.back().name=value.toStdString();
        else if(key=="downloadfile") parts.back().file=value.toStdString();
        else if(key=="size") { bool ok; parts.back().sectors=value.toULongLong(&ok,value.startsWith("0x")?16:10); if(!ok) throw std::runtime_error("Invalid partition size"); }
    }
    return parts;
}
std::vector<Partition> Firmware::Partitions() {
    auto result=ParseMbr(Read(Require("1234567890___MBR")));
    auto cfg=ConfigPartitions();
    for(auto &p:result) for(const auto &c:cfg) if(c.name==p.name) p.file=c.file;
    return result;
}
FirmwareInfo Firmware::Info() {
    FirmwareInfo info; info.valid=true; info.version=std::to_string(version); info.chip="Allwinner";
    info.sign="IMAGEWTY v"+std::to_string(headerVersion>>8);
    auto partitions = ConfigPartitions();
    for(const auto &e:entries) { FirmwareEntry row; row.fileName=e.name; row.partName=e.subType; row.type=e.mainType; row.offset=e.offset; row.size=e.size;
        for(const auto &p:partitions) if(!p.file.empty() && (p.file==e.name || Subtype(p.file)==e.subType)) { row.partName=p.name; row.hasPart=true; }
        info.entries.push_back(row); }
    return info;
}
OperationResult InspectFirmware(const std::string &path) {
    OperationResult result;
    result.operation = Operation::AllwinnerInspect;
    result.kind = ResultKind::FirmwareInfo;
    Firmware fw(path);
    result.firmware = fw.Info();
    result.items = {{QCoreApplication::translate("Allwinner","Format").toStdString(), result.firmware.sign},
        {QCoreApplication::translate("Allwinner","Firmware size").toStdString(), std::to_string(fw.size)},
        {QCoreApplication::translate("Allwinner","Embedded files").toStdString(), std::to_string(fw.entries.size())},
        {QCoreApplication::translate("Allwinner","Firmware version").toStdString(), std::to_string(fw.version)},
        {QCoreApplication::translate("Allwinner","Attributes").toStdString(), std::to_string(fw.attr)},
        {QCoreApplication::translate("Allwinner","Encrypted").toStdString(), QCoreApplication::translate("Allwinner","No").toStdString()}};
    auto *mbr = fw.Find("1234567890___MBR");
    if (mbr) for (const auto &p : fw.Partitions()) result.partitions.push_back({p.name, p.address, p.sectors});
    result.status = OperationStatus::Succeeded;
    return result;
}
void Firmware::Extract(const std::string &path, const Cancellation &cancel, const std::function<void(uint64_t,uint64_t)> &progress) {
    QDir dir(QString::fromStdString(path));
    // Validate all names before creating anything. Do not follow output symlinks.
    std::set<QString> names;
    for(const auto &e:entries) {
        QString n=QString::fromStdString(e.name);
        if(n.contains('/')||n.contains('\\')||n.contains(':')||n=="."||n==".."||!names.insert(n.toCaseFolded()).second)
            throw std::runtime_error("Unsafe extraction filename: "+e.name);
        QFileInfo destination(dir.filePath(n)), input(file.fileName());
        if(destination.absoluteFilePath()==input.absoluteFilePath() || (!destination.canonicalFilePath().isEmpty() && destination.canonicalFilePath()==input.canonicalFilePath()))
            throw std::runtime_error("Extraction output must differ from input firmware");
        if(destination.isSymLink()) throw std::runtime_error("Extraction destination is a symlink");
    }
    if(!dir.mkpath(".")) throw std::runtime_error("Cannot create output directory");
    uint64_t total=0,done=0; for(const auto &e:entries) total=Add(total,e.size);
    for(const auto &e:entries) {
        QSaveFile out(dir.filePath(QString::fromStdString(e.name))); if(!out.open(QIODevice::WriteOnly)) throw std::runtime_error(out.errorString().toStdString());
        for(uint64_t pos=0;pos<e.size;) {
            if(cancel&&cancel->load()) throw std::runtime_error("Cancelled");
            auto b=Read(e,pos,std::min<uint64_t>(1024*1024,e.size-pos));
            if(out.write(reinterpret_cast<const char *>(b.data()),b.size())!=qint64(b.size())) throw std::runtime_error("Extraction write failed");
            pos+=b.size(); done+=b.size(); if(progress) progress(total,done);
        }
        if(!out.commit()) throw std::runtime_error("Extraction commit failed");
    }
}
Bytes RawMbr(uint64_t address, uint64_t sectors, unsigned copies) {
    if(address>0x100000000ULL || sectors>UINT32_MAX || !sectors || !copies || copies>4) throw std::runtime_error("Invalid raw layout");
    Bytes b(16384*copies,0);
    for(unsigned i=0;i<copies;++i) {
        auto p=b.data()+i*16384; Put32(p+4,0x200); std::memcpy(p+8,"softw411",8);
        Put32(p+16,copies); Put32(p+20,i); Put32(p+24,1); Put32(p+28,0);
        Put32(p+32,uint32_t(address>>32)); Put32(p+36,uint32_t(address));
        Put32(p+40,uint32_t(sectors>>32)); Put32(p+44,uint32_t(sectors));
        std::memcpy(p+48,"DISK",4); std::memcpy(p+64,"raw",3);
        Put32(p,Crc32(p+4,16380));
    }
    return b;
}
std::vector<SparseChunk> SparseLayout(Firmware &fw,const Entry &e,uint64_t &size) {
    size=e.size; if(e.size<4) return {};
    auto h=fw.Read(e,0,std::min<uint64_t>(28,e.size)); if(U32(h.data())!=0xed26ff3a) return {};
    if(h.size()<28||U16(h.data()+4)!=1||U16(h.data()+8)<28||U16(h.data()+10)<12) throw std::runtime_error("Invalid sparse header");
    uint32_t block=U32(h.data()+12), count=U32(h.data()+20); uint16_t fileHeader=U16(h.data()+8), chunkHeader=U16(h.data()+10);
    if(!block || block%512) throw std::runtime_error("Invalid sparse block size");
    size=Mul(block,U32(h.data()+16)); uint64_t cursor=fileHeader,expanded=0;
    if(count>e.size/chunkHeader) throw std::runtime_error("Invalid sparse chunk count");
    std::vector<SparseChunk> chunks;
    for(uint32_t i=0;i<count;++i) {
        auto c=fw.Read(e,cursor,chunkHeader); uint32_t total=U32(c.data()+8);
        if(total<chunkHeader||Add(cursor,total)>e.size) throw std::runtime_error("Invalid sparse chunk range");
        SparseChunk chunk; chunk.type=U16(c.data()); chunk.offset=Add(cursor,chunkHeader); chunk.outputOffset=expanded;
        chunk.size=Mul(U32(c.data()+4),block); chunk.fill=0; uint64_t payload=total-chunkHeader;
        switch(chunk.type) {
        case 0xcac1: if(payload!=chunk.size) throw std::runtime_error("Invalid RAW sparse payload"); break;
        case 0xcac2: if(payload!=4) throw std::runtime_error("Invalid FILL sparse payload"); chunk.fill=U32(fw.Read(e,chunk.offset,4).data()); break;
        case 0xcac3: if(payload) throw std::runtime_error("Invalid skip sparse payload"); break;
        case 0xcac4: if(payload!=4||chunk.size) throw std::runtime_error("Invalid CRC sparse payload"); break;
        default: throw std::runtime_error("Unknown sparse chunk type");
        }
        expanded=Add(expanded,chunk.size); if(expanded>size) throw std::runtime_error("Sparse expanded range exceeds size");
        chunks.push_back(chunk); cursor=Add(cursor,total);
    }
    if(expanded!=size || cursor!=e.size) throw std::runtime_error("Sparse size mismatch");
    return chunks;
}
uint64_t ParseCapacity(const std::string &text) {
    QString s=QString::fromStdString(text).trimmed().toLower(); if(s.isEmpty()||s=="auto") return 0;
    uint64_t scale=1;
    for(const auto &unit:std::vector<std::pair<QString,uint64_t>>{{"gb",1ULL<<30},{"mb",1ULL<<20},{"kb",1ULL<<10},{"g",1ULL<<30},{"m",1ULL<<20},{"k",1ULL<<10}})
        if(s.endsWith(unit.first)) { s.chop(unit.first.size()); scale=unit.second; break; }
    bool ok; auto value=s.trimmed().toULongLong(&ok); if(!ok||s.startsWith('-')||!value) throw std::runtime_error("Invalid storage capacity"); return Mul(value,scale);
}
} }
