#include "platform/WindowsUsb.h"
#include "platform/UsbLocation.h"
#include "UsbTransport.h"
#include "RockusbProtocol.h"
#include "platform/LogService.h"
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <usbioctl.h>
#include <array>
#include <map>
#include <mutex>

namespace core::rockchip {
namespace {
// DriverAssistant's rockusb.sys registers the USB device interface GUID, not
// the Rockusb setup class GUID from rockusb.inf. Always also check the service.
constexpr GUID usbDeviceGuid{0xa5dcbf10,0x6530,0x11d2,{0x90,0x1f,0x00,0xc0,0x4f,0xb9,0x51,0xed}};
constexpr GUID usbHubGuid{0xf18a0e88,0xc30c,0x11d0,{0x88,0x15,0x00,0xa0,0xc9,0x06,0xbe,0xd8}};
// ABI verified against DriverAssistant's x64/win10/rockusb.sys.
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE v) : value(v) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};
std::string utf8(const std::wstring &value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    std::string text(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), text.data(), size, nullptr, nullptr);
    return text;
}
std::wstring wide(const std::string &value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    std::wstring text(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), text.data(), size);
    return text;
}
std::wstring property(HDEVINFO set, SP_DEVINFO_DATA &info, DWORD key) {
    const auto values = platform::WindowsUsbProperty(set, info, key);
    return values.empty() ? std::wstring{} : values.front();
}
// Session-stable, collision-free IDs retain legacy engine location semantics.
uint32_t locationId(const std::string &path) {
    static std::mutex mutex;
    static std::map<std::string, uint32_t> ids;
    std::lock_guard lock(mutex);
    auto [it, inserted] = ids.try_emplace(path, uint32_t(ids.size() + 1));
    return it->second;
}
bool descriptorAtHub(DEVINST child, USB_DEVICE_DESCRIPTOR &descriptor) {
    for (int depth = 0; depth < 16; ++depth) {
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, child, 0) != CR_SUCCESS) return false;
        wchar_t id[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Device_IDW(parent, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) return false;
        ULONG size = 0;
        if (CM_Get_Device_Interface_List_SizeW(&size, const_cast<GUID *>(&usbHubGuid), id,
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS && size > 1 && size < 65536) {
            std::vector<wchar_t> paths(size, 0);
            if (CM_Get_Device_Interface_ListW(const_cast<GUID *>(&usbHubGuid), id, paths.data(), size,
                    CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS) return false;
            ULONG port = 0, bytes = sizeof(port), type = 0;
            if (CM_Get_DevNode_Registry_PropertyW(child, CM_DRP_ADDRESS, &type, &port, &bytes, 0) != CR_SUCCESS || !port) return false;
            Handle hub(CreateFileW(paths.data(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, 0, nullptr));
            if (hub.value == INVALID_HANDLE_VALUE) return false;
            std::vector<BYTE> buffer(sizeof(USB_NODE_CONNECTION_INFORMATION_EX) + 32 * sizeof(USB_PIPE_INFO), 0);
            auto *info = reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX *>(buffer.data());
            info->ConnectionIndex = port;
            DWORD done = 0;
            if (!DeviceIoControl(hub.value, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                    info, DWORD(buffer.size()), info, DWORD(buffer.size()), &done, nullptr) ||
                info->ConnectionStatus != DeviceConnected || info->DeviceDescriptor.bLength != sizeof(USB_DEVICE_DESCRIPTOR)) return false;
            descriptor = info->DeviceDescriptor;
            return true;
        }
        child = parent;
    }
    return false;
}
class RockusbTransport final : public UsbTransport {
public:
    Handle device, input, output;
    platform::LogService *log = nullptr;
    std::string serial;
    // A separate OVERLAPPED/event per operation remains alive through cancellation.
    int Transfer(HANDLE handle, DWORD code, unsigned char *data, DWORD size, unsigned timeout, int kind) {
        OVERLAPPED pending{};
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.value) return -int(GetLastError());
        pending.hEvent = event.value;
        DWORD done = 0;
        const ULONGLONG start = GetTickCount64();
        BOOL ok = kind == 1 ? ReadFile(handle, data, size, &done, &pending) :
                  kind == 2 ? WriteFile(handle, data, size, &done, &pending) :
                  DeviceIoControl(handle, code, kind == 3 ? data : nullptr, kind == 3 ? size : 0,
                                  kind == 4 ? data : nullptr, kind == 4 ? size : 0, &done, &pending);
        DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && error == ERROR_IO_PENDING) {
            const DWORD wait = WaitForSingleObject(event.value, timeout);
            if (wait != WAIT_OBJECT_0) {
                const DWORD waitError = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
                CancelIoEx(handle, &pending);
                // Cancellation is only a request. Drain completion before releasing data/event.
                GetOverlappedResult(handle, &pending, &done, TRUE);
                error = waitError;
            } else {
                ok = GetOverlappedResult(handle, &pending, &done, FALSE);
                error = ok ? ERROR_SUCCESS : GetLastError();
            }
        }
        if (log) log->writef(error ? core::LogLevel::Error : core::LogLevel::Debug, "Rockchip", "Rockusb I/O: kind=%d ioctl=%08lx requested=%lu completed=%lu elapsed_ms=%llu error=%lu",
                             kind, code, size, done, GetTickCount64() - start, error);
        return error ? -int(error) : int(done);
    }
    int Bulk(uint8_t endpoint, unsigned char *data, int size, int &done, unsigned timeout) override {
        done = 0;
        if (endpoint != bulkIn && endpoint != bulkOut) return -ERROR_INVALID_PARAMETER;
        int result = Transfer(endpoint == bulkIn ? input.value : output.value, 0, data, size, timeout, endpoint == bulkIn ? 1 : 2);
        if (result < 0) return result;
        done = result; return 0;
    }
    int Control(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                unsigned char *data, uint16_t size, unsigned timeout) override {
        if (type == 0x40 && request == 0x0c && value == 0 && (index == 0x471 || index == 0x472))
            return Transfer(device.value, rockusb::BootRequest(index), data, size, timeout, 3);
        if (type == 0x02 && request == 1 && !value && !size && (index == bulkIn || index == bulkOut))
            return Transfer(index == bulkIn ? input.value : output.value, rockusb::ResetPipe, nullptr, 0, timeout, 0);
        return -ERROR_NOT_SUPPORTED;
    }
    int Reset() override { return Transfer(device.value, rockusb::ResetDevice, nullptr, 0, 5000, 0); }
    std::string Serial() override { return serial; }
};
}
std::vector<std::shared_ptr<const UsbDevice>> EnumerateUsb(libusb_context *, platform::LogService *log) {
    std::vector<std::shared_ptr<const UsbDevice>> result;
    HDEVINFO set = SetupDiGetClassDevsW(&usbDeviceGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        if (log) log->writef(core::LogLevel::Error, "Rockchip", "Rockusb enumeration failed: error=%lu", GetLastError());
        return result;
    }
    SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof(iface);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &usbDeviceGuid, i, &iface); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &size, nullptr);
        if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || size > 65536) continue;
        std::vector<BYTE> buffer(size, 0);
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data()); detail->cbSize = sizeof(*detail);
        SP_DEVINFO_DATA info{}; info.cbSize = sizeof(info);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, size, nullptr, &info) ||
            _wcsicmp(property(set, info, SPDRP_SERVICE).c_str(), L"rockusb")) continue;
        USB_DEVICE_DESCRIPTOR descriptor{};
        if (!descriptorAtHub(info.DevInst, descriptor)) {
            if (log) log->writef(core::LogLevel::Debug, "Rockchip", "Rockusb device descriptor unavailable: path=%s", utf8(detail->DevicePath).c_str());
            continue;
        }
        wchar_t instance[MAX_DEVICE_ID_LEN]{};
        if (!SetupDiGetDeviceInstanceIdW(set, &info, instance, MAX_DEVICE_ID_LEN, nullptr)) continue;
        auto device = std::make_shared<UsbDevice>();
        device->vid = descriptor.idVendor; device->pid = descriptor.idProduct; device->bcdUsb = descriptor.bcdUSB;
        device->path = utf8(detail->DevicePath); device->instance = utf8(instance);
        DWORD capabilities = 0, propertyType = 0;
        if (SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_CAPABILITIES, &propertyType,
                reinterpret_cast<BYTE *>(&capabilities), sizeof(capabilities), nullptr) && (capabilities & CM_DEVCAP_UNIQUEID)) {
            const auto slash = device->instance.rfind('\\');
            if (slash != std::string::npos) device->serial = device->instance.substr(slash + 1);
        }
        device->physicalLocation = utf8(property(set, info, SPDRP_LOCATION_PATHS));
        device->physicalLocation = platform::NormalizeUsbLocation(device->physicalLocation);
        if (device->physicalLocation.empty()) continue; // Never guess identity across mode transitions.
        device->location = locationId(device->physicalLocation);
        result.push_back(std::move(device));
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}
std::unique_ptr<UsbTransport> OpenUsb(const UsbDevice &device, bool massStorage, int &error, platform::LogService *log) {
    error = -ERROR_NOT_SUPPORTED;
    if (massStorage || device.path.empty()) return {};
    auto usb = std::make_unique<RockusbTransport>(); usb->log = log; usb->serial = device.serial;
    const auto path = wide(device.path);
    usb->device.value = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (usb->device.value == INVALID_HANDLE_VALUE) { error = -int(GetLastError()); return {}; }
    std::vector<unsigned char> config(65535, 0);
    int bytes = usb->Transfer(usb->device.value, rockusb::GetConfiguration, config.data(), DWORD(config.size()), 5000, 4);
    if (bytes < 0) { error = bytes; return {}; }
    if (bytes > int(config.size())) { error = -ERROR_INVALID_DATA; return {}; }
    const auto endpoints = rockusb::ConfigurationEndpoints({config.data(), size_t(bytes)});
    if (!endpoints) { error = -ERROR_INVALID_DATA; return {}; }
    usb->bulkIn = endpoints->in; usb->bulkOut = endpoints->out;
    // rockusb.sys resolves PIPE00 to the first IN pipe, PIPE01 to the first OUT pipe.
    usb->input.value = CreateFileW((path + L"\\PIPE00").c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (usb->input.value == INVALID_HANDLE_VALUE) { error = -int(GetLastError()); return {}; }
    usb->output.value = CreateFileW((path + L"\\PIPE01").c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (usb->output.value == INVALID_HANDLE_VALUE) { error = -int(GetLastError()); return {}; }
    error = 0;
    if (log) log->writef(core::LogLevel::Debug, "Rockchip", "USB opened: transport=Rockusb path=%s vid=%04x pid=%04x bcdUSB=%04x bulk_in=%02x bulk_out=%02x",
        device.path.c_str(), device.vid, device.pid, device.bcdUsb, usb->bulkIn, usb->bulkOut);
    return usb;
}
int ProbeRockusb(const std::string &instance, const std::string &path) {
    for (const auto &device : EnumerateUsb(nullptr)) {
        if (_stricmp(device->instance.c_str(), instance.c_str()) || (!path.empty() && _stricmp(device->path.c_str(), path.c_str()))) continue;
        int error = 0;
        auto usb = OpenUsb(*device, false, error);
        return usb ? 0 : error;
    }
    return -ERROR_NOT_FOUND;
}
std::string UsbError(int error) { return "Windows error " + std::to_string(error < 0 ? -int64_t(error) : error); }
}
