#include "platform/WindowsUsb.h"
#include "backends/allwinner/UsbWire.h"
#include "backends/allwinner/WindowsAllwinner.h"
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <array>
#include <cwctype>
#include <stdexcept>
#include <mutex>
#include <map>

namespace core::allwinner {
namespace {
std::mutex sessionMutex;
std::map<std::string,unsigned> openSessions;
constexpr GUID usbDeviceGuid{0xa5dcbf10, 0x6530, 0x11d2, {0x90, 0x1f, 0x00, 0xc0, 0x4f, 0xb9, 0x51, 0xed}};
// usbUDisc ABI, also used by the pinned libefex upstream Windows backend.
constexpr DWORD sendCode = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_OUT_DIRECT, FILE_ANY_ACCESS);
constexpr DWORD receiveCode = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_IN_DIRECT, FILE_ANY_ACCESS);
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE v = INVALID_HANDLE_VALUE) : value(v) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};
struct DeviceSet {
    HDEVINFO value;
    explicit DeviceSet(HDEVINFO v) : value(v) {}
    ~DeviceSet() { if (value != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value); }
};
std::string utf8(const std::wstring &s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string text(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), text.data(), n, nullptr, nullptr); return text;
}
std::wstring wide(const std::string &s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    std::wstring text(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), text.data(), n); return text;
}
std::wstring upper(std::wstring text) {
    for (auto &c : text) c = std::towupper(c);
    return text;
}
std::vector<std::wstring> property(HDEVINFO set, SP_DEVINFO_DATA &info, DWORD key) {
    return platform::WindowsUsbProperty(set, info, key);
}
struct NativeDevice { DeviceIdentity identity; DWORD error = ERROR_NOT_READY; };
std::vector<NativeDevice> enumerate() {
    DeviceSet set(SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT));
    if (set.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Allwinner SetupDiGetClassDevs: " + std::to_string(GetLastError()));
    std::vector<NativeDevice> devices;
    SP_DEVINFO_DATA info{}; info.cbSize = sizeof(info);
    for (DWORD i = 0;; ++i) {
        if (!SetupDiEnumDeviceInfo(set.value, i, &info)) {
            const auto error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) throw std::runtime_error("Allwinner enumeration: " + std::to_string(error));
            break;
        }
        bool match = false;
        for (const auto &id : property(set.value, info, SPDRP_HARDWAREID)) {
            const auto text = upper(id);
            match |= text == L"USB\\VID_1F3A&PID_EFE8" || text.rfind(L"USB\\VID_1F3A&PID_EFE8&REV_", 0) == 0;
        }
        if (!match) continue;
        bool vendorInterface = false, auxiliary = false;
        for (const auto &id : property(set.value, info, SPDRP_COMPATIBLEIDS)) {
            const auto text = upper(id);
            vendorInterface |= text.find(L"USB\\CLASS_FF") != std::wstring::npos;
            auxiliary |= text.find(L"SUBCLASS_42") != std::wstring::npos || text == L"USB\\COMPOSITE";
        }
        if (!vendorInterface || auxiliary) continue;
        NativeDevice device;
        auto &d = device.identity; d.vendor = Vendor::Allwinner; d.vid = 0x1f3a; d.pid = 0xefe8;
        std::array<wchar_t, MAX_DEVICE_ID_LEN> instance{};
        if (!SetupDiGetDeviceInstanceIdW(set.value, &info, instance.data(), DWORD(instance.size()), nullptr)) continue;
        d.deviceInstance = utf8(upper(instance.data()));
        auto locations = property(set.value, info, SPDRP_LOCATION_PATHS);
        // Use the complete root/controller + hub chain. Interface enumeration
        // indexes and the final port alone are not physical device identities.
        if (locations.empty() || locations.front().empty()) continue;
        d.physicalLocation = utf8(upper(locations.front()));
        ULONG status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) != CR_SUCCESS) device.error = ERROR_GEN_FAILURE;
        else if (status & DN_HAS_PROBLEM) device.error = problem == CM_PROB_NEED_RESTART ? ERROR_SUCCESS_REBOOT_REQUIRED : ERROR_NOT_READY;
        else {
            const auto services = property(set.value, info, SPDRP_SERVICE);
            device.error = (status & DN_STARTED) && !services.empty() && upper(services.front()) == L"USBUDISC" ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
        }
        devices.push_back(std::move(device));
    }
    DeviceSet interfaces(SetupDiGetClassDevsW(&usbDeviceGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (interfaces.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Allwinner interface enumeration: " + std::to_string(GetLastError()));
    SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof(iface);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(interfaces.value, nullptr, &usbDeviceGuid, i, &iface); ++i) {
        DWORD bytes = 0;
        SetupDiGetDeviceInterfaceDetailW(interfaces.value, &iface, nullptr, 0, &bytes, nullptr);
        if (bytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || bytes > 65536) continue;
        std::vector<BYTE> buffer(bytes);
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data()); detail->cbSize = sizeof(*detail);
        SP_DEVINFO_DATA node{}; node.cbSize = sizeof(node);
        if (!SetupDiGetDeviceInterfaceDetailW(interfaces.value, &iface, detail, bytes, nullptr, &node)) continue;
        std::array<wchar_t, MAX_DEVICE_ID_LEN> id{};
        if (!SetupDiGetDeviceInstanceIdW(interfaces.value, &node, id.data(), DWORD(id.size()), nullptr)) continue;
        const auto instance = utf8(upper(id.data()));
        for (auto &device : devices) if (device.identity.deviceInstance == instance) {
            if (!device.identity.devicePath.empty()) device.error = ERROR_DUP_NAME;
            else device.identity.devicePath = utf8(detail->DevicePath);
        }
    }
    return devices;
}
HANDLE openNative(const NativeDevice &device) {
    if (device.error || device.identity.devicePath.empty()) {
        SetLastError(device.error ? device.error : ERROR_NOT_FOUND); return INVALID_HANDLE_VALUE;
    }
    const auto path = wide(device.identity.devicePath);
    return CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
}
class NativeWire final : public UsbWire {
    Handle device;
    std::string instance;
public:
    ~NativeWire() override { Close(); }
    void Close() override {
        if (device.value != INVALID_HANDLE_VALUE) CloseHandle(device.value);
        device.value = INVALID_HANDLE_VALUE;
        if(!instance.empty()) {
            std::lock_guard<std::mutex> lock(sessionMutex);
            auto it=openSessions.find(instance);
            if(it!=openSessions.end() && --it->second==0) openSessions.erase(it);
            instance.clear();
        }
    }
    void Open(const DeviceIdentity &target, int &in, int &out) override {
        if (target.vendor != Vendor::Allwinner || target.physicalLocation.empty()) throw std::runtime_error("Missing full Allwinner physical device identity");
        const auto devices = enumerate(); const NativeDevice *selected = nullptr;
        for (const auto &candidate : devices) if (candidate.identity.physicalLocation == target.physicalLocation) {
            if (selected) throw std::runtime_error("Ambiguous Allwinner physical device identity");
            selected = &candidate;
        }
        if (!selected) throw std::runtime_error("Target Allwinner device disconnected");
        device.value = openNative(*selected);
        if (device.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open official Allwinner driver: " + std::to_string(GetLastError()));
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            instance=selected->identity.deviceInstance; ++openSessions[instance];
        }
        // The official driver's IOCTLs select the pipes; these values carry
        // direction for libefex callbacks, rather than selecting USB endpoints.
        in = 0x81; out = 0x01;
    }
    int Transfer(int endpoint, char *data, int size) override {
        OVERLAPPED pending{};
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.value) { error = "Allwinner CreateEvent: " + std::to_string(GetLastError()); return -1; }
        pending.hEvent = event.value;
        DWORD done = 0;
        const auto started = GetTickCount64();
        const auto timeoutMs = TimeoutMs();
        const BOOL ok = DeviceIoControl(device.value, endpoint & 0x80 ? receiveCode : sendCode,
            nullptr, 0, data, DWORD(size), &done, &pending);
        DWORD failure = ok ? ERROR_SUCCESS : GetLastError();
        if (failure == ERROR_IO_PENDING) {
            for (;;) {
                const DWORD wait = WaitForSingleObject(event.value, 50);
                if (wait == WAIT_OBJECT_0) {
                    failure = GetOverlappedResult(device.value, &pending, &done, FALSE) ? ERROR_SUCCESS : GetLastError();
                    break;
                }
                if (wait == WAIT_FAILED || Stopped() || GetTickCount64() - started >= timeoutMs) {
                    failure = wait == WAIT_FAILED ? GetLastError() : Stopped() ? ERROR_OPERATION_ABORTED : ERROR_TIMEOUT;
                    CancelIoEx(device.value, &pending);
                    // Cancellation is a request: data and OVERLAPPED must survive completion.
                    GetOverlappedResult(device.value, &pending, &done, TRUE);
                    break;
                }
            }
        }
        if (failure) { error = "Allwinner USB I/O: " + std::to_string(failure); return -1; }
        return int(done);
    }
};
}
std::unique_ptr<UsbWire> CreateUsbWire(std::shared_ptr<platform::UsbContext>) { return std::make_unique<NativeWire>(); }
int ProbeAllwinner(const std::string &instance) {
    try {
        for (const auto &d : enumerate()) if (d.identity.deviceInstance == utf8(upper(wide(instance)))) {
            if(d.error) return int(d.error);
            {
                std::lock_guard<std::mutex> lock(sessionMutex);
                if(openSessions.count(d.identity.deviceInstance)) return 0;
            }
            Handle handle(openNative(d)); return handle.value == INVALID_HANDLE_VALUE ? int(GetLastError()) : 0;
        }
        return ERROR_NOT_FOUND;
    } catch (...) { return ERROR_GEN_FAILURE; }
}
std::vector<DeviceDescriptor> ScanUsb(libusb_context *) {
    std::vector<DeviceDescriptor> result;
    for (const auto &device : enumerate()) {
        auto d = Describe(device.identity); d.index = int(result.size() + 1); d.mode = "FEL/FES";
        // Enumeration reports driver readiness without opening a second handle.
        d.accessError = int(device.error ? device.error :
            device.identity.devicePath.empty() ? ERROR_NOT_FOUND : ERROR_SUCCESS);
        result.push_back(std::move(d));
    }
    return result;
}
}
