// Derived from Rockchip RKLog (GPL-2.0+); see backends/rockchip/legacy/license.txt.
#include "platform/FileLog.h"
#include "platform/File.h"
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <regex>
#include <vector>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace platform {
namespace {
class DirectoryLock {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
public:
    explicit DirectoryLock(const std::filesystem::path &dir) {
        const auto path = dir / ".log.lock";
#ifdef _WIN32
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        OVERLAPPED offset{};
        if (handle == INVALID_HANDLE_VALUE || !LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &offset)) {
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            throw std::runtime_error("Cannot lock log directory");
        }
#else
        handle = open(path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
        if (handle < 0 || flock(handle, LOCK_EX | LOCK_NB) != 0) {
            if (handle >= 0) close(handle);
            throw std::runtime_error("Cannot lock log directory");
        }
#endif
    }
    ~DirectoryLock() {
#ifdef _WIN32
        CloseHandle(handle);
#else
        close(handle);
#endif
    }
};
struct Item { std::filesystem::path path; uint64_t bytes; std::filesystem::file_time_type time; };
std::vector<Item> Inventory(const std::string &directory, const std::string &name) {
    std::vector<Item> files;
    for (const auto &entry : std::filesystem::directory_iterator(Path(directory))) {
        const auto filename = Utf8(entry.path().filename());
        if (!filename.starts_with(name)) continue;
        const auto tail = filename.substr(name.size());
        static const std::regex pattern(R"(\d{4}-\d{2}-\d{2}(\.[0-9]+)?\.txt)");
        if (!std::regex_match(tail, pattern) || !std::filesystem::is_regular_file(entry.symlink_status())) continue;
        files.push_back({entry.path(), entry.file_size(), entry.last_write_time()});
    }
    std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) {
        return a.time == b.time ? a.path < b.path : a.time < b.time;
    });
    return files;
}
uint64_t Trim(std::vector<Item> files, LogLimits limits, uint64_t incoming = 0) {
    uint64_t total = 0;
    for (const auto &file : files) total += file.bytes;
    for (const auto &file : files) {
        if (file.bytes <= limits.fileBytes && total + incoming <= limits.totalBytes) continue;
        if (!std::filesystem::remove(file.path)) throw std::runtime_error("Cannot remove log file");
        total -= file.bytes;
    }
    return total;
}
}
LogStorage FileLog::manage(LogLimits limits, bool clear) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!limits.fileBytes || limits.totalBytes < limits.fileBytes) return {0, "Invalid log limits"};
    m_limits = limits;
    if (m_directory.empty()) return {};
    try {
        DirectoryLock directoryLock(Path(m_directory));
        auto files = Inventory(m_directory, m_name);
        const auto bytes = Trim(files, clear ? LogLimits{0, 0} : limits);
        m_error.clear();
        return {bytes, {}};
    } catch (const std::exception &e) { m_error = e.what(); return {0, m_error}; }
}
LogStorage FileLog::storage() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_directory.empty()) return {};
    try {
        DirectoryLock directoryLock(Path(m_directory));
        uint64_t bytes = 0;
        for (const auto &file : Inventory(m_directory, m_name)) bytes += file.bytes;
        return {bytes, m_error};
    } catch (const std::exception &e) { return {0, e.what()}; }
}
FileLog::FileLog(std::string directory, std::string name)
    : m_directory(std::move(directory)), m_name(name.empty() ? "log" : std::move(name)) {}

void FileLog::write(const core::LogEntry &entry, const std::string &session) {
    if (m_directory.empty()) return;
    const auto now = std::chrono::system_clock::to_time_t(entry.time);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(entry.time.time_since_epoch()).count();
    std::ostringstream prefix;
    prefix << std::put_time(&local, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << (millis % 1000);
#ifdef _WIN32
    // The Windows CRT does not reliably format %z as a numeric UTC offset.
    auto wallClock = local;
    const auto minutes = static_cast<int>(std::difftime(_mkgmtime(&wallClock), now) / 60);
    const auto magnitude = minutes < 0 ? -minutes : minutes;
    prefix << (minutes < 0 ? '-' : '+') << std::setw(2) << magnitude / 60
           << ':' << std::setw(2) << magnitude % 60;
#else
    char zone[16]{};
    std::strftime(zone, sizeof(zone), "%z", &local);
    std::string offset(zone);
    if (offset.size() == 5) offset.insert(3, ":");
    prefix << offset;
#endif
    prefix << " [" << core::LogLevelName(entry.level) << "] [" << entry.context.source
           << "] [session=" << session;
    if (entry.context.task) prefix << " task=" << entry.context.task;
    if (entry.context.step) prefix << " step=" << entry.context.step;
    if (!entry.context.device.empty()) prefix << " device=" << entry.context.device;
    prefix << "] ";
    std::ostringstream date;
    date << std::put_time(&local, "%Y-%m-%d.txt");
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        DirectoryLock directoryLock(Path(m_directory));
        std::ostringstream block;
        std::istringstream lines(entry.message.English());
        std::string line;
        bool first = true;
        while (std::getline(lines, line)) {
            block << prefix.str() << (first ? "" : "| ") << line << '\n';
            first = false;
        }
        auto text = block.str();
        if (text.size() > m_limits.fileBytes) {
            const std::string marker = " [truncated]\n";
            size_t end = m_limits.fileBytes > marker.size() ? m_limits.fileBytes - marker.size() : 0;
            while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
            text = text.substr(0, end) + marker.substr(0, m_limits.fileBytes - end);
        }
        auto files = Inventory(m_directory, m_name);
        Trim(files, m_limits, text.size());
        const auto path = Path(m_directory) / Path(m_name + date.str());
        const auto status = std::filesystem::symlink_status(path);
        if (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))
            throw std::runtime_error("Log path is not a regular file");
        if (std::filesystem::exists(path) && std::filesystem::file_size(path) + text.size() > m_limits.fileBytes) {
            uint64_t sequence = 1;
            auto archive = path;
            do {
                archive = Path(m_directory) / Path(m_name + date.str().substr(0, 10) + "." + std::to_string(sequence++) + ".txt");
            } while (std::filesystem::exists(std::filesystem::symlink_status(archive)));
            std::filesystem::rename(path, archive);
        }
        std::ofstream out(path, std::ios::binary | std::ios::app);
        out.write(text.data(), text.size());
        out.flush();
        if (!out) throw std::runtime_error("Cannot write log file");
        m_error.clear();
    } catch (const std::exception &e) { m_error = e.what(); }
}
}
