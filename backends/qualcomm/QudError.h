#ifndef CORE_QUALCOMM_QUD_ERROR_H
#define CORE_QUALCOMM_QUD_ERROR_H
#include "domain/Message.h"
namespace core::qualcomm {
inline Message QudErrorMessage(const char *path, bool configuring, unsigned long error, const char *description) {
    const char *key;
    if (configuring)
        key = "QDLoader (COM) %1: port configuration failed (Windows error %2: %3). Reconnect the device and enter EDL (9008) again.";
    else if (error == 5 || error == 32) // ERROR_ACCESS_DENIED / ERROR_SHARING_VIOLATION
        key = "QDLoader (COM) %1: opening failed (Windows error %2: %3). Access denied. The port may be in use or access permissions may be restricted. Check port usage and access permissions.";
    else if (error == 2 || error == 3 || error == 433 || error == 1167) // Missing path/device
        key = "QDLoader (COM) %1: opening failed (Windows error %2: %3). The device path is unavailable. Reconnect the device, enter EDL (9008) mode, and retry.";
    else
        key = "QDLoader (COM) %1: opening failed (Windows error %2: %3). Check the device connection and driver, reconnect the device, and re-enter EDL (9008) mode.";
    std::string port = path;
    if (port.compare(0, 4, "\\\\.\\") == 0) port.erase(0, 4);
    return {"QualcommBackend", key, {port, std::to_string(error), description}};
}
}
#endif
