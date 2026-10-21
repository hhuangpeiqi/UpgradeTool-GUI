// Fault injection exists only in this test launcher, never in the shipped binary.
#ifndef UNICODE
#define UNICODE
#endif
#define NOMINMAX
#include <windows.h>
BOOL WINAPI TestMoveFileExW(LPCWSTR source, LPCWSTR destination, DWORD flags);
#define MoveFileExW TestMoveFileExW
#include "../../scripts/windows/portable_launcher.cpp"
#undef MoveFileExW

BOOL WINAPI TestMoveFileExW(LPCWSTR source, LPCWSTR destination, DWORD flags) {
    wchar_t log[32768]{}, failures[32]{};
    GetEnvironmentVariableW(L"UPGRADE_TEST_RENAME_LOG", log, DWORD(std::size(log)));
    GetEnvironmentVariableW(L"UPGRADE_TEST_RENAME_FAILURES", failures, DWORD(std::size(failures)));
    if (*log) { std::ofstream file(fs::path(log), std::ios::app); file << "attempt\n"; }
    static unsigned attempts = 0;
    if (attempts++ < std::wcstoul(failures, nullptr, 10)) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return MoveFileExW(source, destination, flags);
}
