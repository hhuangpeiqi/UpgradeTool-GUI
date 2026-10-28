#include "platform/LogService.h"
#include "platform/File.h"
#include "tests/support/TestFiles.h"
#include "tests/support/Checks.h"
#include <fstream>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif

static int failures = 0;
static std::string Read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
static std::filesystem::path FirstLog(const std::filesystem::path &directory) {
    for (const auto &entry : std::filesystem::directory_iterator(directory))
        if (entry.path().extension() == ".txt") return entry.path();
    return directory / "missing.txt";
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
    const auto path = FirstLog(dir.path);
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
    std::istringstream stream(Read(FirstLog(dir.path)));
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
static void TestStorageLimits() {
    TemporaryDirectory dir;
    platform::LogService log(platform::Utf8(dir.path), "log", true);
    CHECK(log.setLimits({512, 1536}).error.empty());
    for (int i = 0; i < 30; ++i) {
        log.write(core::LogLevel::Info, "Test", {{}, std::string(140, 'a'), {}});
        CHECK(log.storage().bytes <= 1536);
    }
    int files = 0;
    for (const auto &entry : std::filesystem::directory_iterator(dir.path)) {
        if (entry.path().extension() != ".txt") continue;
        ++files;
        CHECK(entry.file_size() <= 512);
    }
    CHECK(files >= 2);
    log.write(core::LogLevel::Info, "Test", {{}, std::string(2000, 'x'), {}});
    bool truncated = false;
    for (const auto &entry : std::filesystem::directory_iterator(dir.path))
        if (entry.path().extension() == ".txt") truncated |= Read(entry.path()).find("[truncated]") != std::string::npos;
    CHECK(truncated);
    std::ofstream(dir.path / "unrelated.txt") << "keep";
    CHECK(log.setLimits({256, 512}).error.empty());
    CHECK(log.storage().bytes <= 512);
    CHECK(log.clearFiles({256, 512}).error.empty());
    CHECK(log.storage().bytes == 0);
    CHECK(Read(dir.path / "unrelated.txt") == "keep");
    log.write(core::LogLevel::Info, "Test", {{}, "after clear", {}});
    CHECK(log.storage().bytes > 0);
    CHECK(!log.setLimits({512, 256}).error.empty());
    std::error_code error;
    std::filesystem::create_symlink(dir.path / "unrelated.txt", dir.path / "log2000-01-01.txt", error);
    CHECK(log.clearFiles({256, 512}).error.empty());
    CHECK(Read(dir.path / "unrelated.txt") == "keep");
    if (!error) CHECK(std::filesystem::is_symlink(dir.path / "log2000-01-01.txt"));
    std::thread producer([&] {
        for (int i = 0; i < 100; ++i) log.write(core::LogLevel::Info, "Worker", {{}, "concurrent", {}});
    });
    for (int i = 0; i < 10; ++i) CHECK(log.clearFiles({256, 512}).error.empty());
    producer.join();
    CHECK(log.storage().bytes <= 512);
#ifndef _WIN32
    const int lock = open((dir.path / ".log.lock").c_str(), O_RDWR);
    CHECK(lock >= 0);
    CHECK(flock(lock, LOCK_EX | LOCK_NB) == 0);
    log.write(core::LogLevel::Info, "Test", {{}, "locked", {}});
    CHECK(!log.storage().error.empty());
    CHECK(!log.clearFiles({256, 512}).error.empty());
    close(lock);
    CHECK(log.clearFiles({256, 512}).error.empty());
#endif
}
int main() {
    TestBlocks(); TestConcurrentBlocks(); TestDailyFiles(); TestStorageLimits();
    return failures ? 1 : 0;
}
