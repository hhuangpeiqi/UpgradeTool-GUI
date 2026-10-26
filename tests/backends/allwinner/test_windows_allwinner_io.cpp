#include <windows.h>
#include "tests/support/Checks.h"
#include <vector>

namespace {
enum class Scenario { Immediate, Pending, Short, Zero, Timeout, Cancel, Disconnected, CompletionFailed };
Scenario scenario;
int failures = 0, cancels = 0, drains = 0;
DWORD ioctl = 0;
ULONGLONG ticks = 0;
BOOL fakeIo(HANDLE, DWORD code, LPVOID input, DWORD inputSize, LPVOID output, DWORD size, LPDWORD done, LPOVERLAPPED pending) {
    ioctl = code;
    CHECK(!input && !inputSize && output && size == 32 && pending && pending->hEvent);
    if (scenario == Scenario::Immediate) { *done = size; return TRUE; }
    if (scenario == Scenario::Disconnected) { SetLastError(ERROR_DEVICE_NOT_CONNECTED); return FALSE; }
    SetLastError(ERROR_IO_PENDING); return FALSE;
}
DWORD fakeWait(HANDLE, DWORD milliseconds) {
    CHECK(milliseconds == 50);
    return scenario == Scenario::Timeout || scenario == Scenario::Cancel ? WAIT_TIMEOUT : WAIT_OBJECT_0;
}
BOOL fakeCancel(HANDLE, LPOVERLAPPED pending) {
    CHECK(pending && pending->hEvent); ++cancels; return TRUE;
}
BOOL fakeResult(HANDLE, LPOVERLAPPED pending, LPDWORD done, BOOL wait) {
    CHECK(pending && pending->hEvent);
    if (wait) { ++drains; CHECK(cancels == 1); SetLastError(ERROR_OPERATION_ABORTED); return FALSE; }
    if (scenario == Scenario::CompletionFailed) { SetLastError(ERROR_GEN_FAILURE); return FALSE; }
    *done = scenario == Scenario::Short ? 7 : scenario == Scenario::Zero ? 0 : 32;
    return TRUE;
}
ULONGLONG fakeTicks() { const auto result = ticks; ticks += 60001; return result; }
}
// Compile the production native implementation with only its I/O boundary
// substituted. No enumeration, device access or driver installation is performed.
#define DeviceIoControl fakeIo
#define WaitForSingleObject fakeWait
#define CancelIoEx fakeCancel
#define GetOverlappedResult fakeResult
#define GetTickCount64 fakeTicks
#include "backends/allwinner/WindowsAllwinner.cpp"
#undef DeviceIoControl
#undef WaitForSingleObject
#undef CancelIoEx
#undef GetOverlappedResult
#undef GetTickCount64

int main() {
    for (auto next : {Scenario::Immediate, Scenario::Pending, Scenario::Short, Scenario::Zero,
            Scenario::Timeout, Scenario::Cancel, Scenario::Disconnected, Scenario::CompletionFailed}) {
        scenario = next; cancels = drains = 0; ticks = 0;
        core::allwinner::NativeWire wire;
        wire.cancel = std::make_shared<std::stop_source>();
        if (scenario == Scenario::Cancel) wire.cancel->request_stop();
        char data[32]{};
        const int done = wire.Transfer(0x81, data, sizeof(data));
        CHECK(ioctl == CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_IN_DIRECT, FILE_ANY_ACCESS));
        if (scenario == Scenario::Immediate || scenario == Scenario::Pending) CHECK(done == 32);
        else if (scenario == Scenario::Short) CHECK(done == 7);
        else if (scenario == Scenario::Zero) CHECK(done == 0);
        else CHECK(done == -1 && !wire.error.empty());
        const bool cancelled = scenario == Scenario::Timeout || scenario == Scenario::Cancel;
        CHECK(cancels == int(cancelled) && drains == int(cancelled));
    }
    scenario = Scenario::Immediate;
    core::allwinner::NativeWire wire;
    char data[32]{};
    CHECK(wire.Transfer(0x01, data, sizeof(data)) == 32);
    CHECK(ioctl == CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_OUT_DIRECT, FILE_ANY_ACCESS));
    return failures ? 1 : 0;
}
