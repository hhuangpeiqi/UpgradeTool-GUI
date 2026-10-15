#ifndef PLATFORM_FILE_LOG_H
#define PLATFORM_FILE_LOG_H
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
namespace platform {
class FileLog {
public:
    FileLog(std::string directory,std::string name,bool enabled=false);
    std::string GetLogSavePath() const { return m_path; }
    bool GetEnableLog() const { return m_enabled.load(); }
    void SetEnableLog(bool enabled) { m_enabled.store(enabled); }
    void Record(const char *format,...);
    bool SaveBuffer(std::string fileName,unsigned char *bytes,uint32_t size);
    void PrintBuffer(std::string &,unsigned char *,uint32_t size,unsigned lineCount=16);
private:
    std::string m_path,m_name;
    std::atomic<bool> m_enabled;
    std::mutex m_mutex;
};
}
#endif
