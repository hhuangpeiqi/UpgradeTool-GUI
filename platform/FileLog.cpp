// Derived from Rockchip RKLog (GPL-2.0+); see backends/rockchip/legacy/license.txt.
#include "platform/FileLog.h"
#include "platform/File.h"
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace platform {
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
    std::ofstream out(Path(m_directory) / Path(m_name + date.str()), std::ios::binary | std::ios::app);
    if (!out) return;
    std::istringstream lines(entry.message.English());
    std::string line;
    bool first = true;
    while (std::getline(lines, line)) {
        out << prefix.str() << (first ? "" : "| ") << line << '\n';
        first = false;
    }
}
}
