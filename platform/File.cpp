#include "platform/File.h"
#include <atomic>
#include <cerrno>
#include <limits>
#include <random>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif
namespace platform {
namespace {
[[noreturn]] void Fail(const char *what) { throw std::system_error(errno, std::generic_category(), what); }
std::FILE *Open(const std::filesystem::path &p, bool exclusive) {
#ifdef _WIN32
    if (!exclusive) return _wfopen(p.c_str(), L"rb");
    const int fd = _wopen(p.c_str(), _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                         _S_IREAD | _S_IWRITE);
    if (fd < 0) return nullptr;
    std::FILE *file = _wfdopen(fd, L"w+b");
    if (!file) {
        const int error = errno;
        _close(fd);
        _wremove(p.c_str());
        errno = error;
    }
    return file;
#else
    return std::fopen(p.c_str(), exclusive ? "w+bx" : "rb");
#endif
}
}
std::filesystem::path Path(const std::string &utf8) { return std::filesystem::u8path(utf8); }
std::string Utf8(const std::filesystem::path &path) {
    const auto s = path.u8string();
    return std::string(s.begin(), s.end());
}
bool SameFile(const std::filesystem::path &a, const std::filesystem::path &b) {
    if (std::filesystem::absolute(a).lexically_normal() == std::filesystem::absolute(b).lexically_normal()) return true;
    std::error_code error;
    return std::filesystem::equivalent(a, b, error) && !error;
}
File::File(const std::filesystem::path &p) : File(p, false) {}
File::File(const std::filesystem::path &p, bool exclusive) : handle(Open(p, exclusive)) {
    if (!handle) Fail("Cannot open file");
}
File::~File() { if (handle) std::fclose(handle); }
uint64_t File::size() const {
#ifdef _WIN32
    struct _stat64 info{};
    if (_fstat64(_fileno(handle), &info)) Fail("Cannot stat file");
#else
    struct stat info{};
    if (fstat(fileno(handle), &info)) Fail("Cannot stat file");
#endif
    if (info.st_size < 0) throw std::runtime_error("Invalid file size");
    return static_cast<uint64_t>(info.st_size);
}
void File::seek(uint64_t offset) {
    if (offset > static_cast<uint64_t>(INT64_MAX)) throw std::runtime_error("File offset overflow");
#ifdef _WIN32
    if (_fseeki64(handle, static_cast<int64_t>(offset), SEEK_SET)) Fail("Cannot seek file");
#else
    if (fseeko(handle, static_cast<off_t>(offset), SEEK_SET)) Fail("Cannot seek file");
#endif
}
std::vector<uint8_t> File::read(size_t count) {
    std::vector<uint8_t> bytes(count);
    const size_t n = std::fread(bytes.data(), 1, count, handle);
    if (n != count) throw std::runtime_error("Truncated file or read failure");
    return bytes;
}
AtomicFile::Paths AtomicFile::MakePaths(const std::filesystem::path &dest) {
    static std::atomic<uint64_t> sequence{0};
    static const uint64_t nonce = (uint64_t(std::random_device{}()) << 32) | std::random_device{}();
    auto temp = dest;
    temp += ".tmp-" + std::to_string(nonce) + "-" + std::to_string(sequence++);
    return {dest, temp};
}
AtomicFile::AtomicFile(const std::filesystem::path &dest) : AtomicFile(MakePaths(dest)) {}
AtomicFile::AtomicFile(Paths p) : File(p.temporary, true), paths(std::move(p)) {}
AtomicFile::~AtomicFile() {
    if (handle) { std::fclose(handle); handle = nullptr; }
    if (!committed) { std::error_code error; std::filesystem::remove(paths.temporary, error); }
}
void AtomicFile::write(const void *data, size_t size) {
    if (std::fwrite(data, 1, size, handle) != size) Fail("Cannot write file");
}
void AtomicFile::resize(uint64_t size) {
    if (size > static_cast<uint64_t>(INT64_MAX)) throw std::runtime_error("File size overflow");
    if (std::fflush(handle)) Fail("Cannot flush file");
#ifdef _WIN32
    if (_chsize_s(_fileno(handle), size)) Fail("Cannot resize file");
#else
    if (ftruncate(fileno(handle), static_cast<off_t>(size))) Fail("Cannot resize file");
#endif
}
void AtomicFile::commit() {
    if (committed) throw std::logic_error("File already committed");
    if (std::fflush(handle)) Fail("Cannot flush file");
    const int closed = std::fclose(handle); handle = nullptr;
    if (closed) Fail("Cannot close file");
#ifdef _WIN32
    if (!MoveFileExW(paths.temporary.c_str(), paths.destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::system_error(GetLastError(), std::system_category(), "Cannot replace output file");
#else
    std::filesystem::rename(paths.temporary, paths.destination);
#endif
    committed = true;
}
}
