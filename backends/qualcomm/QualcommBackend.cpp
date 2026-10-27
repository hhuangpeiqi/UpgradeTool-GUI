#include "backends/DeviceIdentity.h"
#include "backends/qualcomm/QualcommBackend.h"
#include "domain/DeviceSelection.h"
#include "backends/qualcomm/OperationPolicy.h"
#include "backends/qualcomm/QudError.h"
#include "qdl_bridge.h"
#include "platform/WindowsUsb.h"
#include "platform/UsbLocation.h"
#include "platform/File.h"
#include <cerrno>
#include <climits>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <map>

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
    if (ctx.observer) ctx.observer->OnOutput(level == 2 ? LogLevel::Error : level == 1 ? LogLevel::Warn : level == 3 ? LogLevel::Debug : LogLevel::Info, text);
}
int cancelled(void *p) { const auto &c = static_cast<Context *>(p)->cancellation; return c && c->stop_requested(); }
void progress(void *p, const char *stage, uint64_t current, uint64_t total) {
    auto *observer = static_cast<Context *>(p)->observer;
    if (observer) observer->OnStageProgress(stage, total, current);
}
void item(void *p, const char *label, const char *value) {
    static_cast<Context *>(p)->result->items.push_back({label, value});
}
void qudError(void *p, const char *path, int configuring, unsigned long error, const char *description) {
    auto &result = *static_cast<Context *>(p)->result;
    result.diagnosticMessage = QudErrorMessage(path, configuring != 0, error, description);
    result.diagnostic = result.diagnosticMessage.English();
}
std::string Tr(const char *text) { return text; }
}
bool QualcommBackend::Supports(Operation op) const {
        switch(op) {
        case Operation::QualcommFlash:
        case Operation::QualcommRaw:
        case Operation::QualcommChipInfo:
        case Operation::QualcommReset:
        case Operation::QualcommRamdump:
        case Operation::QualcommKickstart:
        case Operation::QualcommCreateZip:
        case Operation::QualcommCreateArchive:
        case Operation::QualcommCreateDigests:
        case Operation::QualcommProvision:
            return true;
        default: return false;
        }
    }
std::vector<DeviceDescriptor> QualcommBackend::Scan() {
    std::lock_guard<std::mutex> lock(qdlMutex);
    qtq_device *rows = nullptr; size_t count = 0;
    if (qtq_scan(&rows, &count) < 0) throw std::runtime_error("Qualcomm enumeration failed");
#ifdef _WIN32
    // Correlate transport snapshots with the native location without opening a
    // protocol session. The location survives a driver/COM binding change.
    std::map<std::string, std::string> locations;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA info{}; info.cbSize = sizeof(info);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
            const auto paths = platform::WindowsUsbProperty(set, info, SPDRP_LOCATION_PATHS);
            if (paths.empty()) continue;
            const auto instance = platform::Utf8(std::filesystem::path(platform::WindowsUsbInstance(set, info)));
            locations[platform::FoldCase(instance)] = platform::NormalizeUsbLocation(
                platform::Utf8(std::filesystem::path(paths.front())));
        }
        SetupDiDestroyDeviceInfoList(set);
    }
#endif
    std::vector<DeviceDescriptor> devices;
    for (size_t i = 0; i < count; ++i) {
        DeviceIdentity identity;
        identity.deviceInstance = rows[i].instance;
#ifdef _WIN32
        const auto location = locations.find(platform::FoldCase(identity.deviceInstance));
        if (location != locations.end()) identity.physicalLocation = location->second;
#endif
        identity.vendor = Vendor::Qualcomm; identity.serial = rows[i].serial;
        identity.bus = rows[i].bus; identity.devicePath = rows[i].path;
        identity.transport = static_cast<QualcommTransport>(rows[i].backend);
        identity.ports.assign(rows[i].ports, rows[i].ports + rows[i].port_count);
        identity.vid = rows[i].vid; identity.pid = rows[i].pid;
        identity.usbAddress = rows[i].address;
        identity.interfaceNumber = rows[i].interface_number; identity.alternate = rows[i].alternate;
        identity.inEndpoint = rows[i].in; identity.outEndpoint = rows[i].out;
        auto d=Describe(identity); d.index=static_cast<int>(i+1);
        d.modeCode = static_cast<DeviceMode>(rows[i].mode); d.accessError = rows[i].access_error;
        d.mode = Tr(ModeLabel(static_cast<qtq_mode>(d.modeCode))) + " / " +
            (rows[i].backend == 2 ? "QDLoader (" + identity.devicePath.substr(identity.devicePath.find_last_of('\\') + 1) + ")"
                                  : "USB IF " + std::to_string(rows[i].interface_number));
        devices.push_back(d);
    }
    qtq_free_devices(rows);
    return devices;
}
OperationResult QualcommBackend::Execute(const OperationRequest &request, const DeviceRef &target,
                                         const Cancellation &cancellation, IUpgradeObserver *observer) {
    OperationResult result; result.operation = request.operation(); result.kind = ResultKind::Generic;
    auto finish = [&]() { return result; };
    auto fail = [&](ErrorCode code, const char *text) { result.error=code; result.diagnostic=Tr(text); return finish(); };
    if (!Supports(request.operation()) || request.vendor() != Vendor::Qualcomm) return fail(ErrorCode::InvalidInput, "Invalid Qualcomm operation");
    if (cancellation && cancellation->stop_requested()) { result.status=OperationStatus::Cancelled; return finish(); }
    const auto &o = request.qualcomm();
    if (o.slot < -1 || o.chunkSize < 0 || (!o.vipTables.empty() && !o.digestDir.empty()))
        return fail(ErrorCode::InvalidInput, "Invalid or conflicting Qualcomm options");
    if (!o.node.empty() && (request.operation() != Operation::QualcommKickstart || o.transport != QualcommTransport::Auto))
        return fail(ErrorCode::InvalidInput, "Device node cannot be combined with a transport selection");
    qtq_request r; qtq_defaults(&r);
    r.command = o.packageBuild ? QTQ_PACKAGE : QTQ_FLASH;
    switch (request.operation()) {
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
    if (request.operation()== Operation::QualcommProvision && (o.storage != QualcommStorage::Ufs))
        return fail(ErrorCode::InvalidInput, "UFS provisioning requires UFS storage");
    r.dry_run = IsSimulation(request);
    if (request.operation()== Operation::QualcommCreateDigests && o.digestDir.empty())
        return fail(ErrorCode::InvalidInput, "Select a digest output directory");
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
        auto devices = Scan(); std::vector<DeviceRef> identities;
        for (const auto &d:devices) identities.push_back(d.reference);
        int matched=MatchDevice(identities,target);
        if (matched < 0) return fail(matched == -2 ? ErrorCode::DeviceAmbiguous : ErrorCode::DeviceMissing, "Selected Qualcomm device is missing or ambiguous");
        const auto &identity=DeviceAccess::Identity(identities[matched]);
        const auto reason = EligibilityReason(request.operation(), static_cast<qtq_mode>(devices[matched].modeCode), devices[matched].accessError);
        if (!reason.empty()) return fail(ErrorCode::UnsupportedDevice, reason.c_str());
        if (o.transport != QualcommTransport::Auto && o.transport != identity.transport)
            return fail(ErrorCode::InvalidInput, "Selected device does not use the requested transport");
        if(identity.deviceInstance.size() >= sizeof(r.target.instance))
            return fail(ErrorCode::InvalidInput, "Qualcomm device identity is too long");
        std::snprintf(r.target.instance,sizeof(r.target.instance),"%s",identity.deviceInstance.c_str());
        r.target.backend=static_cast<int>(identity.transport);
        r.target.vid=identity.vid; r.target.pid=identity.pid; r.target.address=identity.usbAddress;
        r.target.interface_number=identity.interfaceNumber; r.target.alternate=identity.alternate;
        r.target.in=identity.inEndpoint; r.target.out=identity.outEndpoint;
        r.target.bus=identity.bus; r.target.port_count=identity.ports.size();
        if (identity.ports.size() > sizeof(r.target.ports) || identity.serial.size() >= sizeof(r.target.serial))
            return fail(ErrorCode::InvalidInput, "Qualcomm device identity is too long");
        std::copy(identity.ports.begin(),identity.ports.end(),r.target.ports);
        std::snprintf(r.target.serial,sizeof(r.target.serial),"%s",identity.serial.c_str());
        std::snprintf(r.target.path,sizeof(r.target.path),"%s",identity.devicePath.c_str());
    }
    Context context{cancellation,observer,&result};
    qtq_callbacks callbacks{&context,log,progress,cancelled,item,qudError};
    int code;
    { std::lock_guard<std::mutex> lock(qdlMutex); code=qtq_execute(&r,&callbacks); }
    if (!result.diagnosticMessage.key.empty()) result.diagnostic = result.diagnosticMessage.English();
    result.status=code == -ECANCELED ? OperationStatus::Cancelled : code == 0 ? OperationStatus::Succeeded : OperationStatus::Failed;
    result.error=code == 0 || code == -ECANCELED ? ErrorCode::None : code == -EINVAL ? ErrorCode::InvalidInput : ErrorCode::OperationFailed;
    if (code && code != -ECANCELED && result.diagnostic.empty()) result.diagnostic=Tr("Qualcomm operation failed") + ": " + std::strerror(-code);
    return finish();
}
}}
