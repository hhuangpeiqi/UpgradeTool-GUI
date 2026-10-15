#include "core/qualcomm/QualcommBackend.h"
#include "core/DeviceSelection.h"
#include "qdl_bridge.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <thread>
#include <chrono>
#include <atomic>
class CRKLog;
CRKLog *g_pLogObject = nullptr;
using namespace core;
static int failures;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(0)
static void Write(const QString &path,const QByteArray &data) { QFile f(path); CHECK(f.open(QIODevice::WriteOnly)); CHECK(f.write(data)==data.size()); }
struct Capture {
    int errors=0, progress=0; bool cancel=false;
    std::string text, cancelOnLog;
};
static void Log(void *p,int level,const char *s) { auto &c=*static_cast<Capture *>(p); if(level==2) { ++c.errors; std::fprintf(stderr,"qdl: %s",s); } c.text+=s; if (!c.cancelOnLog.empty() && std::string(s).find(c.cancelOnLog)!=std::string::npos) c.cancel=true; }
static void Progress(void *p,const char *,uint64_t,uint64_t) { ++static_cast<Capture *>(p)->progress; }
static int Cancel(void *p) { return static_cast<Capture *>(p)->cancel; }
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QTemporaryDir temporary;
    QString dir=temporary.path();
    auto path=[&](const char *name) { return QDir(dir).filePath(name); };
    Write(path("loader.mbn"),QByteArray(512,'L')); Write(path("boot.bin"),QByteArray(1024,'B'));
    Write(path("raw.xml"),"<data><program SECTOR_SIZE_IN_BYTES=\"512\" filename=\"boot.bin\" label=\"boot\" sparse=\"false\" file_sector_offset=\"0\" num_partition_sectors=\"2\" physical_partition_number=\"0\" start_sector=\"34\"/></data>");
    Write(path("patch.xml"),"<patches><patch SECTOR_SIZE_IN_BYTES=\"512\" byte_offset=\"0\" filename=\"DISK\" physical_partition_number=\"0\" size_in_bytes=\"4\" start_sector=\"34\" value=\"1\" what=\"test\"/></patches>");
    std::string loader=path("loader.mbn").toStdString(), raw=path("raw.xml").toStdString(), patch=path("patch.xml").toStdString();
    const char *xml[]={raw.c_str(),patch.c_str()};
    qtq_request r; qtq_defaults(&r); r.programmer=loader.c_str(); r.xml=xml; r.xml_count=2; r.dry_run=1;
    Capture capture; qtq_callbacks callbacks{&capture,Log,Progress,Cancel,nullptr};
    CHECK(qtq_execute(&r,&callbacks)==0); CHECK(capture.progress>0); CHECK(capture.text.find("boot")!=std::string::npos);
    // Repeated runs must not leave parser, transport or simulation state behind.
    CHECK(qtq_execute(&r,&callbacks)==0);
    r.debug=1; capture.cancelOnLog="FIREHOSE: READ:";
    CHECK(qtq_execute(&r,&callbacks)==-ECANCELED); CHECK(capture.cancel);
    capture.cancel=false; capture.cancelOnLog.clear(); r.debug=0;
    CHECK(qtq_execute(&r,&callbacks)==0);
    capture.cancel=true; CHECK(qtq_execute(&r,&callbacks)==-ECANCELED); capture.cancel=false;
    Write(path("broken.xml"),"<data><program"); std::string broken=path("broken.xml").toStdString(); const char *bad[]={broken.c_str()};
    r.xml=bad; r.xml_count=1; CHECK(qtq_execute(&r,&callbacks)!=0); CHECK(capture.errors>0);
    r.xml=xml; r.xml_count=2; CHECK(qtq_execute(&r,&callbacks)==0);
    std::string malformed=path("malformed.bin").toStdString(); QByteArray archive(120,'0'); archive.replace(0,6,"070701"); archive.replace(54,8,"ZZZZZZZZ"); Write(QString::fromStdString(malformed),archive);
    r.programmer=malformed.c_str(); CHECK(qtq_execute(&r,&callbacks)!=0); r.programmer=loader.c_str(); CHECK(qtq_execute(&r,&callbacks)==0);
    Write(path("sahara.xml"),"<?xml version=\"1.0\"?><sahara_config><images><image image_id=\"13\" image_path=\"loader.mbn\"/></images></sahara_config>");
    std::string config=path("sahara.xml").toStdString(); r.programmer=config.c_str(); CHECK(qtq_execute(&r,&callbacks)==0);
    Write(path("sahara.xml"),"<?xml version=\"1.0\"?><sahara_config><"); CHECK(qtq_execute(&r,&callbacks)!=0); r.programmer=loader.c_str();
    std::string missing=path("missing.xml").toStdString(); Write(QString::fromStdString(missing),"<data><program SECTOR_SIZE_IN_BYTES=\"512\" filename=\"absent.bin\" label=\"missing\" sparse=\"false\" file_sector_offset=\"0\" num_partition_sectors=\"1\" physical_partition_number=\"0\" start_sector=\"34\"/></data>");
    const char *miss[]={missing.c_str()}; r.xml=miss; r.xml_count=1; CHECK(qtq_execute(&r,&callbacks)!=0);
    r.allow_missing=1; CHECK(qtq_execute(&r,&callbacks)==0); r.allow_missing=0; r.xml=xml; r.xml_count=2;
    std::string output=path("programmer.bin").toStdString(); r.command=QTQ_CREATE_ARCHIVE; r.output=output.c_str(); CHECK(qtq_execute(&r,&callbacks)==0);
    r.command=QTQ_FLASH; r.programmer=output.c_str(); CHECK(qtq_execute(&r,&callbacks)==0); r.programmer=loader.c_str();
    std::string digest=path("digests").toStdString(); QDir().mkpath(QString::fromStdString(digest)); r.digest_dir=digest.c_str(); CHECK(qtq_execute(&r,&callbacks)==0); CHECK(QFile::exists(QString::fromStdString(digest)+"/DigestsToSign.bin"));
    CHECK(QFile::remove(QString::fromStdString(digest)+"/DigestsToSign.bin"));
    CHECK(QDir().mkpath(QString::fromStdString(digest)+"/DigestsToSign.bin"));
    CHECK(qtq_execute(&r,&callbacks)!=0);
    CHECK(!QFileInfo::exists(QString::fromStdString(digest)+"/DigestsToSign.bin"));
    r.vip_tables=digest.c_str(); CHECK(qtq_execute(&r,&callbacks)==-EINVAL); r.vip_tables=nullptr; r.digest_dir=nullptr;
    std::string binary=path("read.bin").toStdString(); qtq_raw_op read{QTQ_READ,"0/34+2",binary.c_str()}; r.command=QTQ_RAW; r.raw=&read; r.raw_count=1; CHECK(qtq_execute(&r,&callbacks)==0); CHECK(!QFileInfo::exists(QString::fromStdString(binary)));
    Write(QString::fromStdString(binary), "original backup");
    CHECK(qtq_execute(&r,&callbacks)==0);
    { QFile f(QString::fromStdString(binary)); CHECK(f.open(QIODevice::ReadOnly)); CHECK(f.readAll()=="original backup"); }
    Write(QString::fromStdString(binary),QByteArray(8192,'B'));
    qtq_raw_op erase{QTQ_ERASE,"0/34+2",nullptr}; r.raw=&erase; CHECK(qtq_execute(&r,&callbacks)==0);
    qtq_raw_op hash{QTQ_SHA256,"0/34+2",nullptr}; r.raw=&hash; CHECK(qtq_execute(&r,&callbacks)!=0); // Upstream simulator does not supply a device digest.
    qtq_raw_op write{QTQ_WRITE,"0/34+2",binary.c_str()}; r.raw=&write; CHECK(qtq_execute(&r,&callbacks)==0);
    Write(path("contents.xml"),"<contents><builds_flat><build><windows_root_path>./</windows_root_path><linux_root_path>./</linux_root_path><device_programmer storage_type=\"ufs\"><file_name>loader.mbn</file_name><file_path>./</file_path></device_programmer><partition_file storage_type=\"ufs\"><file_name>raw.xml</file_name><file_path>./</file_path></partition_file><partition_patch_file storage_type=\"ufs\"><file_name>patch.xml</file_name><file_path>./</file_path></partition_patch_file></build></builds_flat></contents>");
    std::string contents=path("contents.xml").toStdString(), zipped=path("installer.zip").toStdString();
    r.command=QTQ_CREATE_ZIP; r.package=contents.c_str(); r.output=zipped.c_str(); CHECK(qtq_execute(&r,&callbacks)==0);
    r.command=QTQ_PACKAGE; r.package=zipped.c_str(); CHECK(qtq_execute(&r,&callbacks)==0);
    r.package=contents.c_str(); CHECK(qtq_execute(&r,&callbacks)==0);
    r.command=QTQ_CREATE_ARCHIVE; r.output=output.c_str(); CHECK(qtq_execute(&r,&callbacks)==0);
    r.package=nullptr; r.command=QTQ_FLASH;
    qtq_image mappings[]={{13,loader.c_str()},{42,loader.c_str()}}; r.images=mappings; r.image_count=2;
    CHECK(qtq_execute(&r,&callbacks)==0); r.command=QTQ_KICKSTART; CHECK(qtq_execute(&r,&callbacks)==0);
    mappings[1].id=13; CHECK(qtq_execute(&r,&callbacks)==-EINVAL); r.image_count=0; r.images=nullptr;
    r.command=QTQ_RESET; r.debug=1;
    capture.text.clear(); CHECK(qtq_execute(&r,&callbacks)==0);
    const auto configurePosition=capture.text.find("<configure ");
    const auto resetPosition=capture.text.find("<power ");
    CHECK(configurePosition != std::string::npos && resetPosition != std::string::npos);
    CHECK(configurePosition < resetPosition);
    CHECK(capture.text.find("<program ") == std::string::npos);
    CHECK(capture.text.find("<erase ") == std::string::npos);
    r.debug=0; r.programmer=nullptr; CHECK(qtq_execute(&r,&callbacks)==0);
    const std::string absentProgrammer=path("absent-programmer.mbn").toStdString();
    r.programmer=absentProgrammer.c_str(); CHECK(qtq_execute(&r,&callbacks)!=0);
    r.programmer=nullptr; r.images=mappings; r.image_count=1;
    CHECK(qtq_execute(&r,&callbacks)==0);
    r.image_count=0; r.images=nullptr; r.programmer=loader.c_str();
    Write(path("ufs.xml"),"<data><ufs bNumberLU=\"1\" bBootEnable=\"1\" bDescrAccessEn=\"1\" bInitPowerMode=\"1\" bHighPriorityLUN=\"0\" bSecureRemovalType=\"0\" bInitActiveICCLevel=\"0\" wPeriodicRTCUpdate=\"0\" bConfigDescrLock=\"0\"/><ufs LUNum=\"0\" bLUEnable=\"1\" bBootLunID=\"1\" size_in_kb=\"1024\" bDataReliability=\"1\" bLUWriteProtect=\"0\" bMemoryType=\"0\" bLogicalBlockSize=\"12\" bProvisioningType=\"0\" wContextCapabilities=\"0\" desc=\"system\"/><ufs commit=\"1\" LUNtoGrow=\"0\"/></data>");
    std::string ufs=path("ufs.xml").toStdString(); const char *ufsXml[]={ufs.c_str()};
    r.command=QTQ_FLASH; r.xml=ufsXml; r.xml_count=1;
    CHECK(qtq_execute(&r,&callbacks)==-EINVAL);
    const char *mixedXml[]={raw.c_str(),ufs.c_str()}; r.xml=mixedXml; r.xml_count=2;
    CHECK(qtq_execute(&r,&callbacks)==-EINVAL);
    r.command=QTQ_PROVISION; CHECK(qtq_execute(&r,&callbacks)==-EINVAL);
    r.xml=xml; r.xml_count=1; CHECK(qtq_execute(&r,&callbacks)==-EINVAL);
    r.xml=ufsXml; r.xml_count=1;
    CHECK(qtq_execute(&r,&callbacks)==0);
    r.finalize=1; CHECK(qtq_execute(&r,&callbacks)!=0); r.finalize=0;
    r.xml=nullptr; CHECK(qtq_execute(&r,&callbacks)==-EINVAL); r.xml=xml; r.xml_count=2; r.command=QTQ_FLASH;
    // XML reads in validation must neither truncate backups nor create new output files.
    QString readXmlPath=path("read.xml");
    Write(readXmlPath,QString("<data><read SECTOR_SIZE_IN_BYTES=\"512\" filename=\"%1\" num_partition_sectors=\"1\" physical_partition_number=\"0\" start_sector=\"34\"/></data>").arg(QString::fromStdString(binary)).toUtf8());
    std::string readXml=readXmlPath.toStdString(); const char *readFiles[]={readXml.c_str()};
    Write(QString::fromStdString(binary),"original backup"); r.xml=readFiles; r.xml_count=1;
    CHECK(qtq_execute(&r,&callbacks)==0);
    { QFile f(QString::fromStdString(binary)); CHECK(f.open(QIODevice::ReadOnly)); CHECK(f.readAll()=="original backup"); }
    CHECK(QFile::remove(QString::fromStdString(binary))); CHECK(qtq_execute(&r,&callbacks)==0);
    CHECK(!QFileInfo::exists(QString::fromStdString(binary)));
    qualcomm::QualcommBackend backend;
    OperationRequest validation; validation.vendor=Vendor::Qualcomm; validation.operation=Operation::QualcommFlash;
    validation.qualcomm.dryRun=true;
    validation.qualcomm.programmer=loader; validation.qualcomm.xml={readXml};
    CHECK(backend.Execute(validation,DeviceIdentity(),Cancellation(),nullptr).ok());
    CHECK(!QFileInfo::exists(QString::fromStdString(binary)));
    validation.qualcomm.xml={raw,ufs};
    CHECK(backend.Execute(validation,DeviceIdentity(),Cancellation(),nullptr).error==ErrorCode::InvalidInput);
    validation.operation=Operation::QualcommProvision; validation.qualcomm.dryRun=true;
    CHECK(backend.Execute(validation,DeviceIdentity(),Cancellation(),nullptr).error==ErrorCode::InvalidInput);
    validation.qualcomm.xml={ufs}; CHECK(backend.Execute(validation,DeviceIdentity(),Cancellation(),nullptr).ok());
    CHECK(backend.Supports(Operation::QualcommFlash)); CHECK(!backend.Supports(Operation::AllwinnerFlash));
    BackendRegistry registry; registry.Add(backend); TaskJob job; OperationRequest request;
    request.vendor=Vendor::Qualcomm; request.operation=Operation::QualcommFlash; job.steps.push_back(request); CHECK(!registry.Validate(job));
    job.steps[0].qualcomm.dryRun=true; CHECK(registry.Validate(job));
    job.steps[0].qualcomm.dryRun=false; job.steps[0].operation=Operation::QualcommKickstart; job.steps[0].qualcomm.node="/dev/test"; CHECK(registry.Validate(job));
    DeviceIdentity a,b; a.vendor=b.vendor=Vendor::Qualcomm; a.transport=b.transport=QualcommTransport::Usb; a.bus=b.bus=1; a.ports={2}; b.ports={3};
    CHECK(MatchDevice({a,b},a)==0); DeviceIdentity ambiguous; ambiguous.vendor=Vendor::Qualcomm; CHECK(MatchDevice({a,b},ambiguous)==-2);
    // Caller-owned cancellation works while waiting for an absent target.
    qtq_defaults(&r); r.command=QTQ_CHIPINFO; r.target.backend=0; r.target.bus=255; r.target.ports[0]=255; r.target.port_count=1;
    std::atomic<bool> abort(false); qtq_callbacks waitHooks{}; waitHooks.user=&abort; waitHooks.cancelled=[](void *p) { return static_cast<std::atomic<bool> *>(p)->load() ? 1 : 0; };
    std::thread stop([&] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); abort.store(true); });
    CHECK(qtq_execute(&r,&waitHooks)==-ECANCELED); stop.join();
    if(argc>1) {
        OperationRequest real; real.vendor=Vendor::Qualcomm; real.operation=Operation::QualcommFlash;
        real.qualcomm.dryRun=true;
        QDir firmware(QString::fromLocal8Bit(argv[1])); real.qualcomm.programmer=firmware.filePath("prog_emmc_firehose_8953_ddr.mbn").toStdString();
        real.qualcomm.xml={firmware.filePath("rawprogram_unsparse_without_nonhlos.xml").toStdString(),firmware.filePath("patch0.xml").toStdString()};
        qtq_defaults(&r); r.dry_run=1; r.programmer=real.qualcomm.programmer.c_str();
        const char *realXml[]={real.qualcomm.xml[0].c_str(),real.qualcomm.xml[1].c_str()}; r.xml=realXml; r.xml_count=2;
        capture.text.clear(); CHECK(qtq_execute(&r,&callbacks)==0);
        CHECK(capture.text.find("13 patches applied")!=std::string::npos);
        auto result=backend.Execute(real,DeviceIdentity(),Cancellation(),nullptr); CHECK(result.ok()); if(!result.ok()) std::fprintf(stderr,"%s\n",result.diagnostic.c_str());
        std::printf("APOLLO firmware: embedded qdl dry-run passed, 13 patches applied\n");
    }
    return failures ? 1 : 0;
}
