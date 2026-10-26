#ifndef GUI_ADB_TIMING_H
#define GUI_ADB_TIMING_H

namespace gui {

// Read-only ADB commands can implicitly start the shared server and wait for its ACK.
inline constexpr int kAdbReadTimeoutMs = 30000;

struct AdbPollTiming {
    int pollIntervalMs = 1000;
    int slowQueryMs = 5000;
    int hardTimeoutMs = kAdbReadTimeoutMs;
    int initialRetryMs = 2000;
    int maximumRetryMs = 15000;
};

} // namespace gui

#endif
