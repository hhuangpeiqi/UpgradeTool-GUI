#include "platform/LogService.h"
#include <cstdarg>
#include <cstdio>
#include <random>
#include <sstream>
#include <vector>

namespace platform {
namespace {
std::string Normalize(std::string text) {
    std::string result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            result += '\n';
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else if (text[i] == '\t') result += "    ";
        else result += text[i];
    }
    while (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}
}
LogService::LogService(std::string directory, std::string name, bool enabled)
    : m_file(std::move(directory), std::move(name)), m_enabled(enabled) {
    std::ostringstream session;
    session << std::hex << std::chrono::system_clock::now().time_since_epoch().count() << '-' << std::random_device{}();
    m_session = session.str();
}
void LogService::setConsumer(Consumer consumer) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_consumer = std::move(consumer);
}
uint64_t LogService::nextTaskId() {
    static std::atomic<uint64_t> next{0};
    return ++next;
}
void LogService::write(core::LogEntry entry) {
    if (entry.context.source.empty()) entry.context.source = core::activeLogContext.source.empty() ? "Application" : core::activeLogContext.source;
    if (!entry.context.task) entry.context.task = core::activeLogContext.task;
    if (!entry.context.step) entry.context.step = core::activeLogContext.step;
    if (entry.context.device.empty()) entry.context.device = core::activeLogContext.device;
    // Normalize after interpolation so argument newlines receive the same treatment.
    const auto text = Normalize(entry.message.English());
    if (text.find_first_not_of(" \n") == std::string::npos) return;
    // Preserve the translation key/arguments for the UI; the sink uses normalized English.
    core::LogEntry disk = entry;
    disk.message = {{}, text, {}};
    Consumer consumer;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_enabled.load()) m_file.write(disk, m_session);
        if (entry.visible && entry.level != core::LogLevel::Debug) consumer = m_consumer;
    }
    // Consumers may emit signals or log another event; never invoke them under the writer lock.
    if (consumer) consumer(entry);
}
void LogService::write(core::LogLevel level, std::string source, core::Message message, bool visible, std::string displayText) {
    core::LogEntry entry;
    entry.level = level;
    entry.context.source = std::move(source);
    entry.message = std::move(message);
    entry.visible = visible;
    entry.displayText = std::move(displayText);
    write(std::move(entry));
}
void LogService::writef(core::LogLevel level, const char *source, const char *format, ...) {
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    const int size = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (size < 0) { va_end(args); return; }
    std::vector<char> buffer(static_cast<size_t>(size) + 1);
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    write(level, source, {{}, std::string(buffer.data(), size), {}}, false);
}
}
