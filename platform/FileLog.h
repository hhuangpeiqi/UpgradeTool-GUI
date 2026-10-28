#pragma once
#include "domain/LogEntry.h"
#include <mutex>
#include <string>

namespace platform {
struct LogLimits { uint64_t fileBytes = 5 * 1024 * 1024; uint64_t totalBytes = 50 * 1024 * 1024; };
struct LogStorage { uint64_t bytes = 0; std::string error; };
class FileLog {
public:
    FileLog(std::string directory, std::string name);
    void write(const core::LogEntry &entry, const std::string &session);
    LogStorage manage(LogLimits limits, bool clear = false);
    LogStorage storage();
private:
    LogLimits m_limits;
    std::string m_error;
    std::string m_directory, m_name;
    std::mutex m_mutex;
};
}
