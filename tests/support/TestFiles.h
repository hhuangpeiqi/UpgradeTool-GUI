#ifndef TEST_FILES_H
#define TEST_FILES_H
#include "platform/File.h"
#include <atomic>
#include <chrono>
#include <stdexcept>
struct Directory {
    std::filesystem::path path;
    explicit Directory(const std::string &p) : path(platform::Path(p)) {}
    std::string filePath(const std::string &name) const { return platform::Utf8(path/platform::Path(name)); }
};
struct TemporaryDirectory : Directory {
    TemporaryDirectory() : Directory(Create()) {}
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path,error); }
    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
private:
    static std::string Create() {
        static std::atomic<unsigned> sequence{0};
        for(unsigned n=0;n<100;++n) {
            auto p=std::filesystem::temp_directory_path()/("upgrade-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++));
            if(std::filesystem::create_directory(p)) return platform::Utf8(p);
        }
        throw std::runtime_error("Cannot create test directory");
    }
};
#endif
