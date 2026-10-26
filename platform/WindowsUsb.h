#pragma once
#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <string>
#include <vector>

namespace platform {
inline std::vector<std::wstring> WindowsUsbProperty(HDEVINFO set, SP_DEVINFO_DATA &info, DWORD key) {
    DWORD bytes = 0, type = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &info, key, &type, nullptr, 0, &bytes);
    if (!bytes || bytes > 65536) return {};
    std::vector<wchar_t> data(bytes / sizeof(wchar_t) + 2, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, &info, key, &type,
        reinterpret_cast<BYTE *>(data.data()), bytes, nullptr)) return {};
    if (type != REG_SZ && type != REG_MULTI_SZ && type != REG_EXPAND_SZ) return {};
    std::vector<std::wstring> result;
    for (const wchar_t *p = data.data(); *p; p += wcslen(p) + 1) {
        result.emplace_back(p);
        if (type != REG_MULTI_SZ) break;
    }
    return result;
}
inline std::wstring WindowsUsbInstance(HDEVINFO set, SP_DEVINFO_DATA &info) {
    wchar_t id[MAX_DEVICE_ID_LEN] = {};
    return SetupDiGetDeviceInstanceIdW(set, &info, id, MAX_DEVICE_ID_LEN, nullptr) ? id : L"";
}
inline std::wstring WindowsUsbSerial(DEVINST dev) {
    for (int depth = 0; depth < 8; ++depth) {
        wchar_t name[MAX_DEVICE_ID_LEN] = {};
        if (CM_Get_Device_IDW(dev, name, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) break;
        std::wstring id(name);
        if (id.rfind(L"USB\\", 0) == 0 && id.find(L"&MI_") == std::wstring::npos) {
            const auto serial = id.substr(id.find_last_of(L'\\') + 1);
            return serial.find(L'&') == std::wstring::npos ? serial : L"";
        }
        DEVINST parent;
        if (CM_Get_Parent(&parent, dev, 0) != CR_SUCCESS) break;
        dev = parent;
    }
    return {};
}
}
#endif
