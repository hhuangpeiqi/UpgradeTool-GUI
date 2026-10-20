#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static void Write(std::ofstream& file, const std::wstring& text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), utf8.data(), size, nullptr, nullptr);
    file.write(utf8.data(), utf8.size()); file.put('\0');
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 4) return 99;
    std::ofstream file(std::filesystem::path(argv[1]), std::ios::binary);
    Write(file, std::filesystem::current_path().native());
    std::vector<wchar_t> executable(32768);
    GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
    Write(file, executable.data());
    for (int i = 4; i < argc; ++i) Write(file, argv[i]);
    file.close();
    Sleep(std::stoul(argv[2]));
    return std::stoi(argv[3]);
}
