/* SPDX-License-Identifier: BSD-3-Clause */
#include "PathCompat.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

extern "C" void ux_err(const char *format, ...);
extern "C" void ux_info(const char *format, ...);

namespace {
namespace fs = std::filesystem;
std::string Utf8(const fs::path &path) {
    const auto bytes = path.u8string();
    return {bytes.begin(), bytes.end()};
}
fs::path Path(const char *path) {
    if (!path) throw std::invalid_argument("Null path");
#ifdef _WIN32
    // Reject invalid UTF-8 rather than silently replacing bytes in a filename.
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0))
        throw std::invalid_argument("Invalid UTF-8 path");
#endif
    return fs::path(reinterpret_cast<const char8_t *>(path));
}
template<class Result, class Function>
Result Guard(Result failure, Function function) noexcept {
    try { return function(); }
    catch (const std::bad_alloc &) { errno = ENOMEM; }
    catch (const fs::filesystem_error &error) {
        errno = error.code().default_error_condition().value();
    }
    catch (const std::exception &) { errno = EINVAL; }
    return failure;
}
bool EqualAsciiCase(std::string a, std::string b) {
    auto fold = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (fold(a[i]) != fold(b[i])) return false;
    return true;
}
// Resolve only missing components. Existing components always retain priority.
int ResolveCase(const fs::path &candidate, fs::path &result) {
    result = candidate.root_path();
    for (const auto &component : candidate.relative_path()) {
        const auto exact = result / component;
        std::error_code error;
        if (fs::exists(exact, error)) { result = exact; continue; }
        if (error && error != std::errc::no_such_file_or_directory &&
            error != std::errc::not_a_directory)
            throw fs::filesystem_error("Cannot inspect image path", exact, error);
        const auto folder = result.empty() ? fs::path(".") : result;
        fs::directory_iterator entries(folder, error);
        if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory)
            return 0;
        if (error) throw fs::filesystem_error("Cannot search image directory", folder, error);
        std::vector<fs::path> matches;
        for (const auto &entry : entries)
            if (EqualAsciiCase(Utf8(entry.path().filename()), Utf8(component)))
                matches.push_back(entry.path());
        if (matches.empty()) return 0;
        if (matches.size() > 1) {
            ux_err("Ambiguous image path \"%s\": multiple case-insensitive matches in \"%s\"\n",
                   Utf8(candidate).c_str(), Utf8(folder).c_str());
            for (const auto &match : matches) ux_err("Candidate: %s\n", Utf8(match).c_str());
            return -EINVAL;
        }
        result = matches.front();
    }
    return fs::exists(result) ? 1 : 0;
}
}

extern "C" int qtq_path_open(const char *path, int flags, int mode) {
    return Guard(-1, [&] {
        const auto native = Path(path);
#ifdef _WIN32
        return _wopen(native.c_str(), flags, mode);
#else
        return open(native.c_str(), flags, mode);
#endif
    });
}
extern "C" FILE *qtq_path_fopen(const char *path, const char *mode) {
    return Guard<FILE *>(nullptr, [&] {
        if (!mode) throw std::invalid_argument("Null mode");
        const auto native = Path(path);
#ifdef _WIN32
        std::wstring wideMode;
        for (auto c = mode; *c; ++c) wideMode += static_cast<unsigned char>(*c);
        return _wfopen(native.c_str(), wideMode.c_str());
#else
        return fopen(native.c_str(), mode);
#endif
    });
}
extern "C" int qtq_path_access(const char *path, int mode) {
    return Guard(-1, [&] {
        const auto native = Path(path);
#ifdef _WIN32
        return _waccess(native.c_str(), mode);
#else
        return access(native.c_str(), mode);
#endif
    });
}
extern "C" int qtq_path_is_directory(const char *path) {
    return Guard(0, [&] { return fs::is_directory(Path(path)) ? 1 : 0; });
}
extern "C" int qtq_path_mkdir(const char *path) {
    return Guard(-1, [&] {
        const auto native = Path(path);
#ifdef _WIN32
        return _wmkdir(native.c_str());
#else
        return mkdir(native.c_str(), 0755);
#endif
    });
}
extern "C" int qtq_path_rename(const char *from, const char *to) {
    return Guard(-1, [&] {
        const auto source = Path(from), destination = Path(to);
#ifdef _WIN32
        return _wrename(source.c_str(), destination.c_str());
#else
        return rename(source.c_str(), destination.c_str());
#endif
    });
}
extern "C" int qtq_path_unlink(const char *path) {
    return Guard(-1, [&] {
        const auto native = Path(path);
#ifdef _WIN32
        return _wunlink(native.c_str());
#else
        return unlink(native.c_str());
#endif
    });
}
extern "C" int qtq_resolve_image(const char *filename, const char *xml,
                                 const char *contents_path, const char *include_dir,
                                 char **resolved) {
    if (!resolved) return -EINVAL;
    *resolved = nullptr;
    const int result = Guard(-1, [&] {
        const auto name = Path(filename);
        const auto definition = Path(xml);
        std::vector<fs::path> candidates;
        auto add = [&](fs::path path) {
            for (const auto &existing : candidates) if (existing == path) return;
            candidates.push_back(std::move(path));
        };
        if (contents_path && *contents_path) add(Path(contents_path));
        if (include_dir && *include_dir) add(Path(include_dir) / name);
        if (!definition.parent_path().empty()) add(definition.parent_path() / name);
        add(name);
        auto save = [&](const fs::path &path) {
            const auto bytes = Utf8(path);
            auto copy = static_cast<char *>(std::malloc(bytes.size() + 1));
            if (!copy) throw std::bad_alloc();
            std::memcpy(copy, bytes.c_str(), bytes.size() + 1);
            *resolved = copy;
            return 1;
        };
        for (const auto &candidate : candidates)
            if (fs::exists(candidate)) return save(candidate);
        for (const auto &candidate : candidates) {
            fs::path actual;
            const int found = ResolveCase(candidate, actual);
            if (found < 0) {
                ux_err("Ambiguous image \"%s\" referenced by XML \"%s\"\n", filename, xml);
                return found;
            }
            if (found) {
                ux_info("Image reference \"%s\" resolved to \"%s\" (case-insensitive match)\n",
                        filename, Utf8(actual).c_str());
                return save(actual);
            }
        }
        ux_info("Image \"%s\" referenced by XML \"%s\" was not found. Searched:\n", filename, xml);
        for (const auto &candidate : candidates) ux_info("  %s\n", Utf8(candidate).c_str());
        return 0;
    });
    if (result == -1) {
        const int error = errno;
        ux_err("Cannot resolve image \"%s\" referenced by XML \"%s\": %s\n",
               filename ? filename : "", xml ? xml : "", std::strerror(error));
        return -error;
    }
    return result;
}
