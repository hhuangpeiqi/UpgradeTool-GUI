#pragma once
#include "domain/Message.h"
#include "domain/Vendor.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

namespace core {
// Keep the existing values stable for observer implementations.
enum class LogLevel { Info, Warn, Error, Debug };
inline const char *LogLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}
inline const char *VendorLogSource(Vendor vendor) {
    switch (vendor) {
    case Vendor::Rockchip: return "Rockchip";
    case Vendor::Allwinner: return "Allwinner";
    case Vendor::Qualcomm: return "Qualcomm";
    }
    return "Application";
}
struct LogContext {
    std::string source;
    uint64_t task = 0;
    int step = 0;
    std::string device;
};
const LogContext &ActiveLogContext();
class ScopedLogContext {
public:
    explicit ScopedLogContext(LogContext context);
    ~ScopedLogContext();
    LogContext &context() { return current; }
    ScopedLogContext(const ScopedLogContext &) = delete;
    ScopedLogContext &operator=(const ScopedLogContext &) = delete;
private:
    LogContext current;
    LogContext *previous;
};
struct LogEntry {
    std::chrono::system_clock::time_point time = std::chrono::system_clock::now();
    LogLevel level = LogLevel::Info;
    LogContext context;
    Message message;
    // Only used for external UI status signals whose diagnostic is recorded separately.
    std::string displayText;
    bool visible = true;
};
}
