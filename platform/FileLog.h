#pragma once
#include "domain/LogEntry.h"
#include <mutex>
#include <string>

namespace platform {
class FileLog {
public:
    FileLog(std::string directory, std::string name);
    void write(const core::LogEntry &entry, const std::string &session);
private:
    std::string m_directory, m_name;
    std::mutex m_mutex;
};
}
