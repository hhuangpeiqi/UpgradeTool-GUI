#pragma once
#include "platform/FileLog.h"
#include <atomic>
#include <functional>
#include <mutex>

namespace platform {
class LogService {
public:
    using Consumer = std::function<void(const core::LogEntry &)>;
    explicit LogService(std::string directory = {}, std::string name = "log", bool enabled = false);
    void setConsumer(Consumer consumer);
    void setEnabled(bool enabled) { m_enabled.store(enabled); }
    void write(core::LogEntry entry);
    void write(core::LogLevel level, std::string source, core::Message message,
               bool visible = true, std::string displayText = {});
    // Formatted low-level diagnostics stay on disk; user-facing events use write().
    void writef(core::LogLevel level, const char *source, const char *format, ...);
    static uint64_t nextTaskId();
    LogStorage setLimits(LogLimits limits) { return m_file.manage(limits); }
    LogStorage storage() { return m_file.storage(); }
    LogStorage clearFiles(LogLimits limits) { return m_file.manage(limits, true); }
private:
    FileLog m_file;
    std::string m_session;
    std::atomic<bool> m_enabled;
    std::mutex m_mutex;
    Consumer m_consumer;
};
}
