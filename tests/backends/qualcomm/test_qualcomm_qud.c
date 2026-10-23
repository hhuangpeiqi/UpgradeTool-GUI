/* Exercise the embedded QUD transport with fake Windows devices and handles. */
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qdl_bridge.h"

static int failures, opens, closes, reports, configuring, match = 1;
static DWORD open_error, configure_error, reported_error;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static HDEVINFO devices(const GUID *g, const char *e, HWND w, DWORD flags) { return (HDEVINFO)1; }
static BOOL enumerate(HDEVINFO d, DWORD index, SP_DEVINFO_DATA *info) { return index == 0; }
static BOOL property(HDEVINFO d, SP_DEVINFO_DATA *i, DWORD key, DWORD *type, BYTE *out, DWORD size, DWORD *required) {
    const char *value = key == SPDRP_HARDWAREID ? "USB\\VID_05C6&PID_9008\0" : "USB\\Class_FF&SubClass_FF&Prot_FF\0";
    size_t n = strlen(value) + 2;
    CHECK(n <= size); if (n > size) return FALSE;
    memcpy(out, value, n); *type = REG_MULTI_SZ; return TRUE;
}
static BOOL instance(HDEVINFO d, SP_DEVINFO_DATA *i, char *out, DWORD size, DWORD *required) {
    snprintf(out, size, "USB\\VID_05C6&PID_9008\\test"); return TRUE;
}
static BOOL product(HDEVINFO d, SP_DEVINFO_DATA *i, const DEVPROPKEY *key, DEVPROPTYPE *type, BYTE *out, DWORD size, DWORD *required, DWORD flags) { return FALSE; }
static HKEY registry(HDEVINFO d, SP_DEVINFO_DATA *i, DWORD scope, DWORD profile, DWORD type, REGSAM access) { return (HKEY)2; }
static LSTATUS port(HKEY key, const char *name, DWORD *reserved, DWORD *type, BYTE *out, DWORD *size) {
    CHECK(!strcmp(name, "PortName")); CHECK(*size >= 6);
    memcpy(out, "COM52", 6); *size = 6; *type = REG_SZ; return ERROR_SUCCESS;
}
static LSTATUS close_registry(HKEY key) { return ERROR_SUCCESS; }
static BOOL close_devices(HDEVINFO d) { return TRUE; }
static HANDLE open_port(const char *path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE template) {
    ++opens; CHECK(!strcmp(path, "\\\\.\\COM52"));
    CHECK(access == (GENERIC_READ | GENERIC_WRITE) && share == 0);
    CHECK(disposition == OPEN_EXISTING && flags == FILE_FLAG_OVERLAPPED);
    if (open_error) { SetLastError(open_error); return INVALID_HANDLE_VALUE; }
    return (HANDLE)3;
}
static BOOL close_port(HANDLE handle) { CHECK(handle == (HANDLE)3); ++closes; SetLastError(999); return TRUE; }
static BOOL get_state(HANDLE handle, DCB *dcb) {
    if (configure_error) { SetLastError(configure_error); return FALSE; }
    return TRUE;
}
static BOOL set_state(HANDLE handle, DCB *dcb) { return TRUE; }
static BOOL set_timeouts(HANDLE handle, COMMTIMEOUTS *timeouts) { return TRUE; }
static BOOL setup(HANDLE handle, DWORD in, DWORD out) { return TRUE; }
static BOOL purge(HANDLE handle, DWORD flags) {
    CHECK(!(flags & (PURGE_RXCLEAR | PURGE_RXABORT))); return TRUE;
}

#define SetupDiGetClassDevsA devices
#define SetupDiEnumDeviceInfo enumerate
#define SetupDiGetDeviceRegistryPropertyA property
#define SetupDiGetDeviceInstanceIdA instance
#define SetupDiGetDevicePropertyW product
#define SetupDiOpenDevRegKey registry
#define RegQueryValueExA port
#define RegCloseKey close_registry
#define SetupDiDestroyDeviceInfoList close_devices
#define CreateFileA open_port
#define CloseHandle close_port
#define GetCommState get_state
#define SetCommState set_state
#define SetCommTimeouts set_timeouts
#define SetupComm setup
#define PurgeComm purge
#include "qud.c"

int qtq_cancelled(void) { return 0; }
int qtq_match_qud(const char *path, unsigned int pid, const char *serial, const char *id) { return match; }
void qtq_report_qud_error(const char *path, int config, unsigned long error) {
    CHECK(!strcmp(path, "\\\\.\\COM52")); ++reports; configuring = config; reported_error = error;
    SetLastError(999);
}
void ux_err(const char *fmt, ...) { }
void ux_debug(const char *fmt, ...) { }

static void reset(void) {
    opens = closes = reports = configuring = 0;
    reported_error = open_error = configure_error = 0; match = 1;
}
int main(void) {
    unsigned int count = 0;
    reset();
    struct qud_device_desc *rows = qud_list(&count);
    CHECK(count == 1 && opens == 0); free(rows);
    for (int scenario = 0; scenario < 4; ++scenario) {
        reset();
        if (scenario == 1) open_error = ERROR_ACCESS_DENIED;
        if (scenario == 2) configure_error = ERROR_GEN_FAILURE;
        if (scenario == 3) match = 0;
        struct qdl_device *device = qud_init(); CHECK(device);
        int result = device->open(device, NULL);
        CHECK((result == 0) == (scenario == 0));
        CHECK(opens == (scenario == 3 ? 0 : 1));
        if (scenario == 1) CHECK(reports == 1 && reported_error == ERROR_ACCESS_DENIED && !configuring && closes == 0);
        if (scenario == 2) CHECK(reports == 1 && reported_error == ERROR_GEN_FAILURE && configuring && closes == 1);
        device->close(device);
        CHECK(closes == (scenario == 0 || scenario == 2 ? 1 : 0));
        device->close(device); // Closing twice must not close another handle.
        CHECK(closes == (scenario == 0 || scenario == 2 ? 1 : 0));
        free(device);
    }
    printf("QUD transport: %d failures\n", failures);
    return failures ? 1 : 0;
}
