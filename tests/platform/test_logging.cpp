#include "platform/LogService.h"
#include "platform/File.h"
#include "tests/support/TestFiles.h"
#include "tests/support/Checks.h"
#include <fstream>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

static int failures = 0;
static std::string Read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
static void TestBlocks() {
    TemporaryDirectory dir;
    platform::LogService log(platform::Utf8(dir.path), "log", true);
    core::LogEntry captured;
    int visible = 0;
    log.setConsumer([&](const auto &entry) { captured = entry; ++visible; });
    {
        core::ScopedLogContext context({"Allwinner", 12, 2, "usb:1-5.3"});
        {
            core::ScopedLogContext nested({"Rockchip", 99, 4, "usb:2-1"});
            CHECK(core::ActiveLogContext().task == 99);
        }
        CHECK(core::ActiveLogContext().task == 12);
        log.write(core::LogLevel::Warn, {}, {"Context", "Hello %1\r\n\tsecond\rthird\n\n", {"board"}});
    }
    CHECK(core::ActiveLogContext().source.empty());
    CHECK(visible == 1 && captured.context.task == 12 && captured.context.step == 2);
    CHECK(captured.message.context == "Context");
    log.write(core::LogLevel::Debug, "ADB", {{}, "details", {}});
    log.writef(core::LogLevel::Error, "Driver", "raw details");
    log.write(core::LogLevel::Info, "Application", {{}, "\r\n \t\n", {}});
    CHECK(visible == 1);
    const auto path = std::filesystem::directory_iterator(dir.path)->path();
    auto text = Read(path);
    CHECK(text.find(" task=12 step=2 device=usb:1-5.3] Hello board\n") != std::string::npos);
    CHECK(text.find("|     second\n") != std::string::npos);
    CHECK(text.find("| third\n") != std::string::npos);
    CHECK(text.find("\n\n") == std::string::npos && text.find('\r') == std::string::npos);
    CHECK(std::regex_search(text, std::regex(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}[+-]\d{2}:\d{2} \[WARN\] \[Allwinner\])")));
    log.setEnabled(false);
    log.write(core::LogLevel::Info, "Application", {{}, "Visible with file disabled", {}});
    CHECK(visible == 2);
    CHECK(Read(path) == text);
}
static void TestConcurrentBlocks() {
    TemporaryDirectory dir;
    platform::LogService log(platform::Utf8(dir.path), "log", true);
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&, producer] {
            core::ScopedLogContext context({"Worker", uint64_t(producer + 1), producer + 1, "usb:" + std::to_string(producer)});
            for (int i = 0; i < 40; ++i) {
                const auto id = std::to_string(producer) + ":" + std::to_string(i);
                log.write(core::LogLevel::Debug, {}, {{}, id + " start\n" + id + " end", {}});
            }
        });
    }
    for (auto &producer : producers) producer.join();
    std::istringstream stream(Read(std::filesystem::directory_iterator(dir.path)->path()));
    std::string first, second;
    int count = 0;
    while (std::getline(stream, first)) {
        CHECK(bool(std::getline(stream, second)));
        const auto marker = first.rfind("] ");
        CHECK(marker != std::string::npos);
        if (marker == std::string::npos) break;
        const auto prefix = first.substr(0, marker + 2);
        const auto id = first.substr(marker + 2, first.size() - marker - 8);
        const auto producer = std::stoi(id);
        CHECK(prefix.find("[Worker] [session=") != std::string::npos);
        CHECK(prefix.find(" task=" + std::to_string(producer + 1) +
                          " step=" + std::to_string(producer + 1) +
                          " device=usb:" + std::to_string(producer) + "] ") != std::string::npos);
        CHECK(second == prefix + "| " + id + " end");
        ++count;
    }
    CHECK(count == 160);
}
static void TestDailyFiles() {
    TemporaryDirectory dir;
    platform::LogService log(platform::Utf8(dir.path), "log", true);
    std::tm calendar{};
    calendar.tm_year = 126; calendar.tm_mon = 9; calendar.tm_mday = 27;
    calendar.tm_hour = 23; calendar.tm_min = 59; calendar.tm_sec = 59; calendar.tm_isdst = -1;
    core::LogEntry entry;
    entry.time = std::chrono::system_clock::from_time_t(std::mktime(&calendar));
    entry.message = {{}, "before midnight", {}};
    log.write(entry);
    entry.time += std::chrono::seconds(2);
    entry.message.key = "after midnight";
    log.write(entry);
    CHECK(Read(dir.path / "log2026-10-27.txt").find("before midnight") != std::string::npos);
    CHECK(Read(dir.path / "log2026-10-28.txt").find("after midnight") != std::string::npos);
}
int main() {
    TestBlocks(); TestConcurrentBlocks(); TestDailyFiles();
    return failures ? 1 : 0;
}
