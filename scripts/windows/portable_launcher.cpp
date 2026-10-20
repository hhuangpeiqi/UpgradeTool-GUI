#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <fdi.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::wstring Wide(const std::string& text) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    Require(n > 0, "Invalid UTF-8 in package.");
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), n);
    return result;
}
struct Resource { const unsigned char* data; DWORD size; };
Resource Load(int id) {
    auto info = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    Require(info != nullptr, "Missing embedded package.");
    auto data = LockResource(LoadResource(nullptr, info));
    Require(data != nullptr, "Cannot read embedded package.");
    return {static_cast<const unsigned char*>(data), SizeofResource(nullptr, info)};
}
class Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
public:
    Hash() {
        Require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0,
                "Cannot initialize SHA256.");
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) != 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("Cannot create SHA256 hash.");
        }
    }
    ~Hash() { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); }
    void Add(const unsigned char* data, size_t size) {
        while (size) {
            auto count = static_cast<ULONG>(std::min<size_t>(size, 1024 * 1024));
            Require(BCryptHashData(hash, const_cast<PUCHAR>(data), count, 0) == 0, "SHA256 failed.");
            data += count; size -= count;
        }
    }
    std::string Finish() {
        unsigned char digest[32];
        Require(BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0, "SHA256 failed.");
        std::ostringstream out;
        for (auto c : digest) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
        return out.str();
    }
};
struct Entry { fs::path path; uintmax_t size; std::string hash; bool extracted = false; };
std::vector<Entry> Manifest(Resource resource) {
    std::istringstream lines(std::string(reinterpret_cast<const char*>(resource.data), resource.size));
    std::vector<Entry> entries;
    std::string line;
    while (std::getline(lines, line)) {
        auto first = line.find('\t'), second = line.find('\t', first + 1);
        Require(first == 64 && second != std::string::npos, "Invalid package manifest.");
        auto name = line.substr(second + 1);
        // Each archive entry has a numeric name; only this manifest supplies output paths.
        Require(!name.empty() && name.find_first_of("\\:\r\n") == std::string::npos,
                "Unsafe package path.");
        fs::path path(Wide(name));
        Require(!path.is_absolute() && !path.has_root_path(), "Unsafe package path.");
        for (const auto& part : path)
            Require(part != ".." && part != "." && !part.empty() &&
                    part.native().back() != L'.' && part.native().back() != L' ', "Unsafe package path.");
        for (const auto& entry : entries) Require(entry.path != path, "Duplicate package path.");
        entries.push_back({path, std::stoull(line.substr(first + 1, second - first - 1)), line.substr(0, first)});
    }
    Require(!entries.empty(), "Empty package manifest.");
    return entries;
}
void CheckDirectory(const fs::path& path) {
    fs::create_directories(path);
    for (auto current = path; !current.empty(); current = current.parent_path()) {
        DWORD attributes = GetFileAttributesW(current.c_str());
        Require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                "Runtime cache cannot contain links or junctions.");
        if (current == current.parent_path()) break;
    }
}
bool Valid(const fs::path& root, const std::vector<Entry>& entries, bool verifyHash) {
    try {
        for (const auto& entry : entries) {
            auto path = root / entry.path;
            for (auto current = path; current != root; current = current.parent_path()) {
                DWORD attributes = GetFileAttributesW(current.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
            }
            if (!fs::is_regular_file(path) || fs::file_size(path) != entry.size) return false;
            if (verifyHash) {
                std::ifstream file(path, std::ios::binary);
                Hash hash;
                unsigned char buffer[65536];
                while (file) {
                    file.read(reinterpret_cast<char*>(buffer), sizeof(buffer));
                    hash.Add(buffer, size_t(file.gcount()));
                }
                if (!file.eof() || hash.Finish() != entry.hash) return false;
            }
        }
        return true;
    } catch (...) { return false; }
}

struct Stream { HANDLE file = INVALID_HANDLE_VALUE; size_t offset = 0; bool input = false; };
Resource cabinet{};
FNALLOC(Allocate) { return std::malloc(cb); }
FNFREE(Free) { std::free(pv); }
FNOPEN(Open) {
    (void)oflag; (void)pmode;
    if (std::string(pszFile) != "payload.cab") return -1;
    auto stream = new (std::nothrow) Stream;
    if (!stream) return -1;
    stream->input = true;
    return reinterpret_cast<INT_PTR>(stream);
}
FNREAD(Read) {
    auto stream = reinterpret_cast<Stream*>(hf);
    if (!stream->input) return UINT(-1);
    auto size = std::min<size_t>(cb, cabinet.size - stream->offset);
    std::copy_n(cabinet.data + stream->offset, size, static_cast<unsigned char*>(pv));
    stream->offset += size;
    return UINT(size);
}
FNWRITE(Write) {
    auto stream = reinterpret_cast<Stream*>(hf);
    DWORD written = 0;
    return WriteFile(stream->file, pv, cb, &written, nullptr) ? written : UINT(-1);
}
FNCLOSE(Close) {
    std::unique_ptr<Stream> stream(reinterpret_cast<Stream*>(hf));
    return stream->input || CloseHandle(stream->file) ? 0 : -1;
}
FNSEEK(Seek) {
    auto stream = reinterpret_cast<Stream*>(hf);
    if (!stream->input) return -1;
    int64_t base = seektype == SEEK_SET ? 0 : seektype == SEEK_CUR ? stream->offset : cabinet.size;
    int64_t position = base + dist;
    if (position < 0 || position > cabinet.size) return -1;
    stream->offset = size_t(position);
    return static_cast<LONG>(position);
}
struct Extraction { fs::path root; std::vector<Entry>* entries; std::string error; };
FNFDINOTIFY(Notify) {
    auto& state = *static_cast<Extraction*>(pfdin->pv);
    try {
        if (fdint == fdintCOPY_FILE) {
            std::string name(pfdin->psz1);
            Require(!name.empty() && name.find_first_not_of("0123456789") == std::string::npos,
                    "Unexpected archive entry.");
            size_t index = std::stoull(name);
            Require(index < state.entries->size(), "Unexpected archive entry.");
            auto& entry = state.entries->at(index);
            Require(!entry.extracted && pfdin->cb >= 0 && uintmax_t(pfdin->cb) == entry.size,
                    "Invalid archive entry.");
            auto path = state.root / entry.path;
            CheckDirectory(path.parent_path());
            auto stream = std::make_unique<Stream>();
            stream->file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                       FILE_ATTRIBUTE_NORMAL, nullptr);
            Require(stream->file != INVALID_HANDLE_VALUE, "Cannot extract runtime file.");
            entry.extracted = true;
            return reinterpret_cast<INT_PTR>(stream.release());
        }
        if (fdint == fdintCLOSE_FILE_INFO) return Close(pfdin->hf) == 0 ? TRUE : FALSE;
        if (fdint == fdintNEXT_CABINET || fdint == fdintPARTIAL_FILE)
            throw std::runtime_error("Split cabinets are not supported.");
        return 0;
    } catch (const std::exception& error) { state.error = error.what(); return -1; }
}
void Extract(const fs::path& root, std::vector<Entry>& entries) {
    ERF error{};
    auto context = FDICreate(Allocate, Free, Open, Read, Write, Close, Seek, cpuUNKNOWN, &error);
    Require(context != nullptr, "Cannot initialize cabinet extraction.");
    Extraction state{root, &entries, {}};
    char name[] = "payload.cab", directory[] = "";
    BOOL success = FDICopy(context, name, directory, 0, Notify, nullptr, &state);
    FDIDestroy(context);
    Require(success, state.error.empty() ? "Cannot extract embedded cabinet." : state.error.c_str());
    Require(std::all_of(entries.begin(), entries.end(), [](const Entry& e) { return e.extracted; }) &&
            Valid(root, entries, true), "Extracted runtime failed SHA256 verification.");
}
std::wstring Quote(const std::wstring& text) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (auto c : text) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c; slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        cabinet = Load(101);
        auto entries = Manifest(Load(102));
        auto idResource = Load(103);
        std::string id(reinterpret_cast<const char*>(idResource.data), idResource.size);
        Require(id.size() == 64 && id.find_first_not_of("0123456789abcdef") == std::string::npos,
                "Invalid embedded package identity.");
        PWSTR local = nullptr;
        Require(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)),
                "Cannot locate the current user's local application data.");
        fs::path base = fs::path(local) / L"upgrade_tool" / L"runtime";
        CoTaskMemFree(local);
        CheckDirectory(base);
        // A filesystem lock is naturally scoped to this user and also coordinates sessions.
        Handle lock(CreateFileW((base / (Wide(id) + L".lock")).c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr));
        Require(lock.value != INVALID_HANDLE_VALUE, "Cannot open runtime cache lock.");
        OVERLAPPED overlap{};
        Require(LockFileEx(lock.value, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlap), "Cannot lock runtime cache.");
        const auto cache = base / Wide(id);
        auto markerValid = [&] {
            DWORD attributes = GetFileAttributesW(cache.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
            std::ifstream marker(cache / L".complete", std::ios::binary);
            std::string value((std::istreambuf_iterator<char>(marker)), {});
            return value == id && Valid(cache, entries, false);
        };
        if (!markerValid()) {
            Hash hash; hash.Add(cabinet.data, cabinet.size);
            Require(hash.Finish() == id, "Embedded package failed SHA256 verification.");
            // A shared lease is kept by every live launcher until its GUI has exited.
            if (fs::exists(cache)) {
                CheckDirectory(cache);
                Handle exclusive(CreateFileW((cache / L".in-use").c_str(), GENERIC_READ | GENERIC_WRITE,
                                            0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
                Require(exclusive.value != INVALID_HANDLE_VALUE,
                        "Runtime cache is incomplete and in use. Close other UpgradeTool windows and retry.");
            }
            if (fs::exists(cache)) fs::remove_all(cache);
            // CREATE_NEW makes the staging identity unique, including after interrupted runs.
            fs::path staging;
            for (unsigned attempt = 0; attempt < 100; ++attempt) {
                staging = base / (Wide(id) + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(attempt));
                if (CreateDirectoryW(staging.c_str(), nullptr)) break;
                Require(GetLastError() == ERROR_ALREADY_EXISTS && attempt < 99, "Cannot create runtime staging directory.");
            }
            try {
                Extract(staging, entries);
                std::ofstream marker(staging / L".complete", std::ios::binary);
                marker << id; marker.close();
                Require(bool(marker), "Cannot write runtime completion marker.");
                fs::rename(staging, cache);
            } catch (...) { std::error_code ignored; fs::remove_all(staging, ignored); throw; }
        }
        SECURITY_ATTRIBUTES leaseSecurity{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        Handle lease(CreateFileW((cache / L".in-use").c_str(), GENERIC_READ, FILE_SHARE_READ,
                                &leaseSecurity, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        Require(lease.value != INVALID_HANDLE_VALUE, "Cannot lease runtime cache.");
        auto executable = cache / L"upgrade_tool_gui.exe";
        std::wstring command = Quote(executable.native());
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        Require(argv != nullptr, "Cannot read command-line arguments.");
        for (int i = 1; i < argc; ++i) command += L" " + Quote(argv[i]);
        LocalFree(argv);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        Require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                               nullptr, nullptr, &startup, &child), "Cannot start UpgradeTool GUI from runtime cache.");
        Handle process(child.hProcess), thread(child.hThread);
        UnlockFileEx(lock.value, 0, 1, 0, &overlap);
        Require(WaitForSingleObject(process.value, INFINITE) == WAIT_OBJECT_0, "Cannot wait for UpgradeTool GUI.");
        DWORD code = 1;
        Require(GetExitCodeProcess(process.value, &code), "Cannot read GUI exit code.");
        return static_cast<int>(code);
    } catch (const std::exception& error) {
        std::string text = std::string("UpgradeTool-GUI could not start.\n\n") + error.what() + "\n";
        HANDLE stderrHandle = GetStdHandle(STD_ERROR_HANDLE);
        DWORD type = GetFileType(stderrHandle), written = 0;
        // Redirected invocations must be usable by scripts without a modal error dialog.
        if ((type != FILE_TYPE_PIPE && type != FILE_TYPE_DISK) ||
            !WriteFile(stderrHandle, text.data(), DWORD(text.size()), &written, nullptr)) {
            auto message = Wide(text);
            MessageBoxW(nullptr, message.c_str(), L"UpgradeTool-GUI", MB_OK | MB_ICONERROR);
        }
        return 1;
    }
}
