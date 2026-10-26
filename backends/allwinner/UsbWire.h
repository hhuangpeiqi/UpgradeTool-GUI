#pragma once
#include "backends/allwinner/Transport.h"
#include <libefex.h>
#include <algorithm>

namespace core::allwinner {
// Protocol callbacks use the same exact-length transfers on both platforms.
class UsbWire {
public:
    Cancellation cancel;
    bool cleanup = false;
    std::chrono::steady_clock::time_point deadline = (std::chrono::steady_clock::time_point::max)();
    std::string error;
    virtual ~UsbWire() = default;
    virtual void Open(const DeviceIdentity &, int &in, int &out) = 0;
    virtual void Close() = 0;
    virtual int Transfer(int endpoint, char *data, int size) = 0;
    bool Stopped() const { return !cleanup && ((cancel && cancel->stop_requested()) ||
        std::chrono::steady_clock::now() >= deadline); }
    unsigned TimeoutMs() const {
        if (cleanup || deadline == (std::chrono::steady_clock::time_point::max)()) return 60000;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        return unsigned(std::clamp<int64_t>(left, 1, 60000));
    }
    int Bulk(int endpoint, char *data, ssize_t length) {
        if (length < 0 || (!data && length)) return EFEX_ERR_INVALID_PARAM;
        for (ssize_t pos = 0; pos < length;) {
            if (Stopped()) { error = cancel && cancel->stop_requested() ? "Cancelled" : "USB handshake timed out"; return EFEX_ERR_USB_TRANSFER; }
            const int amount = int(std::min<ssize_t>(length - pos, 65536));
            const int done = Transfer(endpoint, data + pos, amount);
            if (done <= 0 || done > amount) {
                if (error.empty()) error = "Invalid USB transfer length: " + std::to_string(done);
                return EFEX_ERR_USB_TRANSFER;
            }
            pos += done;
        }
        return 0;
    }
};
std::unique_ptr<UsbWire> CreateUsbWire(std::shared_ptr<platform::UsbContext>);
}
