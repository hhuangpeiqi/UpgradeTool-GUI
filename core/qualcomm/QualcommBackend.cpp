#include "core/qualcomm/QualcommBackend.h"
#include "core/DeviceSelection.h"
#include "core/qualcomm/OperationPolicy.h"
#include "qdl_bridge.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace core { namespace qualcomm {
namespace {
std::mutex qdlMutex;
struct Context {
    Cancellation cancellation;
    IUpgradeObserver *observer;
    OperationResult *result;
};
const char *optional(const std::string &s) { return s.empty() ? nullptr : s.c_str(); }
void log(void *p, int level, const char *text) {
    auto &ctx = *static_cast<Context *>(p);
    if (level == 2) ctx.result->diagnostic = text;
    if (ctx.observer) ctx.observer->OnOutput(level == 2 ? LogLevel::Error : level == 1 ? LogLevel::Warn : LogLevel::Info, text);
}
int cancelled(void *p) { const auto &c = static_cast<Context *>(p)->cancellation; return c && c->load(); }
void progress(void *p, const char *stage, uint64_t current, uint64_t total) {
    auto *observer = static_cast<Context *>(p)->observer;
    if (observer) observer->OnStageProgress(stage, total, current);
}
void item(void *p, const char *label, const char *value) {
    static_cast<Context *>(p)->result->items.push_back({label, value});
}
std::string Tr(const char *text) { return QCoreApplication::translate("QualcommBackend", text).toStdString(); }
}
bool QualcommBackend::Supports(Operation op) const { return op >= Operation::QualcommFlash && op <= Operation::QualcommProvision; }
std::vector<DeviceDescriptor> QualcommBackend::Scan() {
    std::lock_guard<std::mutex> lock(qdlMutex);
    qtq_device *rows = nullptr; size_t count = 0;
    if (qtq_scan(&rows, &count) < 0) throw std::runtime_error("Qualcomm enumeration failed");
    std::vector<DeviceDescriptor> devices;
    for (size_t i = 0; i < count; ++i) {
        DeviceDescriptor d;
        d.identity.deviceInstance = rows[i].instance;
        d.identity.vendor = Vendor::Qualcomm; d.identity.serial = rows[i].serial;
        d.identity.bus = rows[i].bus; d.identity.devicePath = rows[i].path;
        d.identity.transport = static_cast<QualcommTransport>(rows[i].backend);
        d.identity.ports.assign(rows[i].ports, rows[i].ports + rows[i].port_count);
        d.vid = rows[i].vid; d.pid = rows[i].pid; d.index = static_cast<int>(i + 1);
        d.identity.vid = d.vid; d.identity.pid = d.pid; d.identity.usbAddress = rows[i].address;
        d.identity.interfaceNumber = rows[i].interface_number; d.identity.alternate = rows[i].alternate;
        d.identity.inEndpoint = rows[i].in; d.identity.outEndpoint = rows[i].out;
        d.qualcommMode = rows[i].mode; d.accessError = rows[i].access_error;
        d.mode = Tr(ModeLabel(d.qualcommMode)) + " / " +
            (rows[i].backend == 2 ? rows[i].path : "USB IF " + std::to_string(rows[i].interface_number));
        devices.push_back(d);
    }
    qtq_free_devices(rows);
    return devices;
}
OperationResult QualcommBackend::Execute(const OperationRequest &request, const DeviceIdentity &target,
                                         const Cancellation &cancellation, IUpgradeObserver *observer) {
    OperationResult result; result.operation = request.operation; result.kind = ResultKind::Generic;
    auto finish = [&]() { if (observer) observer->OnResult(result); return result; };
    auto fail = [&](ErrorCode code, const char *text) { result.error=code; result.diagnostic=Tr(text); return finish(); };
    if (!Supports(request.operation) || request.vendor != Vendor::Qualcomm) return fail(ErrorCode::InvalidInput, QT_TRANSLATE_NOOP("QualcommBackend", "Invalid Qualcomm operation"));
    if (cancellation && cancellation->load()) { result.status=OperationStatus::Cancelled; return finish(); }
    const auto &o = request.qualcomm;
    if (o.slot < -1 || o.chunkSize < 0 || (!o.vipTables.empty() && !o.digestDir.empty()))
        return fail(ErrorCode::InvalidInput, QT_TRANSLATE_NOOP("QualcommBackend", "Invalid or conflicting Qualcomm options"));
    if (!o.node.empty() && (request.operation != Operation::QualcommKickstart || o.transport != QualcommTransport::Auto))
        return fail(ErrorCode::InvalidInput, QT_TRANSLATE_NOOP("QualcommBackend", "Device node cannot be combined with a transport selection"));
    qtq_request r; qtq_defaults(&r);
    r.command = o.packageBuild ? QTQ_PACKAGE : QTQ_FLASH;
    switch (request.operation) {
    case Operation::QualcommRaw: r.command=QTQ_RAW; break;
    case Operation::QualcommChipInfo: r.command=QTQ_CHIPINFO; break;
    case Operation::QualcommReset: r.command=QTQ_RESET; break;
    case Operation::QualcommRamdump: r.command=QTQ_RAMDUMP; break;
    case Operation::QualcommKickstart: r.command=QTQ_KICKSTART; break;
    case Operation::QualcommCreateZip: r.command=QTQ_CREATE_ZIP; break;
    case Operation::QualcommCreateArchive: r.command=QTQ_CREATE_ARCHIVE; break;
    case Operation::QualcommProvision: r.command=QTQ_PROVISION; break;
    default: break;
    }
    if (request.operation == Operation::QualcommProvision && (o.storage != QualcommStorage::Ufs))
        return fail(ErrorCode::InvalidInput, QT_TRANSLATE_NOOP("QualcommBackend", "UFS provisioning requires UFS storage"));
    r.dry_run = o.dryRun || request.operation == Operation::QualcommCreateDigests;
    if (request.operation == Operation::QualcommCreateDigests && o.digestDir.empty())
        return fail(ErrorCode::InvalidInput, QT_TRANSLATE_NOOP("QualcommBackend", "Select a digest output directory"));
    std::string package = o.package + (o.selector.empty() ? "" : "::" + o.selector);
    r.programmer=optional(o.programmer); r.package=optional(package); r.include_dir=optional(o.includeDir);
    r.output=optional(o.output); r.filter=optional(o.filter); r.node=optional(o.node);
    r.vip_tables=optional(o.vipTables); r.digest_dir=optional(o.digestDir);
    r.storage=static_cast<int>(o.storage); r.slot=o.slot < 0 ? UINT_MAX : static_cast<unsigned int>(o.slot);
    r.chunk_size=o.chunkSize; r.debug=o.debug; r.allow_missing=o.allowMissing; r.allow_fusing=o.allowFusing;
    r.finalize=o.finalize; r.skip_reset=o.skipReset; r.skipblock=o.skipblock;
    std::vector<const char *> xml;
    for (const auto &path:o.xml) xml.push_back(path.c_str());
    std::vector<qtq_raw_op> raw;
    for (const auto &op:o.raw) raw.push_back({static_cast<qtq_raw_type>(op.type), optional(op.address), optional(op.file)});
    std::vector<qtq_image> images;
    for (const auto &image:o.images) images.push_back({image.id, image.file.c_str()});
    r.xml=xml.data(); r.xml_count=xml.size(); r.raw=raw.data(); r.raw_count=raw.size();
    r.images=images.data(); r.image_count=images.size();
    if (RequiresEnumeratedDevice(request)) {
        auto devices = Scan(); std::vector<DeviceIdentity> identities;
        for (const auto &d:devices) identities.push_back(d.identity);
        int matched=MatchDevice(identities,target);
        if (matched < 0) return fail(matched == -2 ? ErrorCode::DeviceAmbiguous : ErrorCode::DeviceMissing, QT_TRANSLATE_NOOP("QualcommBackend", "Selected Qualcomm device is missing or ambiguous"));
        const auto &identity=identities[matched];
        const auto reason = EligibilityReason(request.operation, devices[matched].qualcommMode, devices[matched].accessError);
        if (!reason.isEmpty()) return fail(ErrorCode::UnsupportedDevice, reason.toUtf8().constData());
        if (o.transport != QualcommTransport::Auto && o.transport != identity.transport)
            return fail(ErrorCode::InvalidInput, QT_TRANSLATE_NOOP("QualcommBackend", "Selected device does not use the requested transport"));
        if(identity.deviceInstance.size() >= sizeof(r.target.instance))
            return fail(ErrorCode::InvalidInput, QT_TRANSLATE_NOOP("QualcommBackend", "Qualcomm device identity is too long"));
        std::snprintf(r.target.instance,sizeof(r.target.instance),"%s",identity.deviceInstance.c_str());
        r.target.backend=static_cast<int>(identity.transport);
        r.target.vid=identity.vid; r.target.pid=identity.pid; r.target.address=identity.usbAddress;
        r.target.interface_number=identity.interfaceNumber; r.target.alternate=identity.alternate;
        r.target.in=identity.inEndpoint; r.target.out=identity.outEndpoint;
        r.target.bus=identity.bus; r.target.port_count=identity.ports.size();
        if (identity.ports.size() > sizeof(r.target.ports) || identity.serial.size() >= sizeof(r.target.serial))
            return fail(ErrorCode::InvalidInput, QT_TRANSLATE_NOOP("QualcommBackend", "Qualcomm device identity is too long"));
        std::copy(identity.ports.begin(),identity.ports.end(),r.target.ports);
        std::snprintf(r.target.serial,sizeof(r.target.serial),"%s",identity.serial.c_str());
        std::snprintf(r.target.path,sizeof(r.target.path),"%s",identity.devicePath.c_str());
    }
    Context context{cancellation,observer,&result};
    qtq_callbacks callbacks{&context,log,progress,cancelled,item};
    int code;
    { std::lock_guard<std::mutex> lock(qdlMutex); code=qtq_execute(&r,&callbacks); }
    result.status=code == -ECANCELED ? OperationStatus::Cancelled : code == 0 ? OperationStatus::Succeeded : OperationStatus::Failed;
    result.error=code == 0 || code == -ECANCELED ? ErrorCode::None : code == -EINVAL ? ErrorCode::InvalidInput : ErrorCode::OperationFailed;
    if (code && code != -ECANCELED && result.diagnostic.empty()) result.diagnostic=Tr(QT_TRANSLATE_NOOP("QualcommBackend", "Qualcomm operation failed")) + ": " + std::strerror(-code);
    return finish();
}
}}
