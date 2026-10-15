#ifndef PLATFORM_FILE_H
#define PLATFORM_FILE_H
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace platform {
std::filesystem::path Path(const std::string &utf8);
std::string Utf8(const std::filesystem::path &path);
bool SameFile(const std::filesystem::path &, const std::filesystem::path &);
class File {
public:
    explicit File(const std::filesystem::path &path);
    ~File();
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    uint64_t size() const;
    void seek(uint64_t offset);
    std::vector<uint8_t> read(size_t count);
protected:
    File(const std::filesystem::path &, bool exclusive);
    std::FILE *handle = nullptr;
};
// Writes beside the destination; only commit replaces it. Destruction removes
// an incomplete temporary file and never changes the previous destination.
class AtomicFile : public File {
public:
    explicit AtomicFile(const std::filesystem::path &destination);
    ~AtomicFile();
    void write(const void *, size_t);
    void resize(uint64_t);
    void commit();
private:
    struct Paths { std::filesystem::path destination, temporary; };
    static Paths MakePaths(const std::filesystem::path &);
    explicit AtomicFile(Paths);
    Paths paths;
    bool committed = false;
};
}
#endif
