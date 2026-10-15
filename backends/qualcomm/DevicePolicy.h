#ifndef CORE_QUALCOMM_DEVICE_POLICY_H
#define CORE_QUALCOMM_DEVICE_POLICY_H

#include <ctype.h>
#include <string.h>
#include <stdio.h>

/* Shared by discovery, the GUI and the embedded transport. Enumeration never
 * confirms a protocol session; these values describe candidates only. */
enum qtq_mode { QTQ_UNKNOWN, QTQ_DIAG, QTQ_ADB, QTQ_EDL, QTQ_DUMP };

static inline int qtq_download_identity(unsigned int vid, unsigned int pid)
{
    return vid == 0x05c6 && (pid == 0x9008 || pid == 0x900e);
}

static inline int qtq_download_interface(int cls, int sub, int protocol,
                                         int in, int out, int in_size, int out_size)
{
    return cls == 0xff && sub == 0xff &&
        (protocol == 0xff || protocol == 0x10 || protocol == 0x11 || protocol == 0x13) &&
        in > 0 && out > 0 && in_size > 0 && out_size > 0;
}

/* Windows compatible IDs describe the interface triple, but not endpoints. */
static inline int qtq_download_compatible_id(const char *id)
{
    char text[128]; size_t size = strlen(id);
    if (size >= sizeof(text)) return 0;
    for (size_t i = 0; i <= size; ++i) text[i] = toupper((unsigned char)id[i]);
    unsigned int cls=0, sub=0, protocol=0; int end=0;
    return sscanf(text, "USB\\CLASS_%x&SUBCLASS_%x&PROT_%x%n", &cls, &sub, &protocol, &end)==3 &&
        text[end]==0 && qtq_download_interface(cls,sub,protocol,0x81,1,1,1);
}

static inline int qtq_diag_name(const char *name)
{
    const char *tokens[] = {"diag", "diagnostic", "diagnostics", "qcdm"};
    for (const char *p = name; *p; ++p) {
        if (p != name && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) continue;
        for (unsigned int i = 0; i < sizeof(tokens) / sizeof(tokens[0]); ++i) {
            size_t j = 0;
            while (tokens[i][j] && p[j] && tolower((unsigned char)p[j]) == tokens[i][j]) ++j;
            if (!tokens[i][j] && !isalnum((unsigned char)p[j]) && p[j] != '_') return 1;
        }
    }
    return 0;
}

static inline int qtq_known_diag(unsigned int vid, unsigned int pid, int iface)
{
    static const struct { unsigned int vid, pid; int iface; } devices[] = {
        {0x05c6,0x9091,0}, {0x05c6,0x9092,0}, {0x05c6,0x901d,0}, {0x05c6,0x676c,0},
        {0x2c7c,0x0125,0}, {0x2c7c,0x0700,0}, {0x1199,0x9071,0}, {0x1199,0x9091,0},
        {0x0846,0x68e2,2}, {0x19d2,0x1404,2}, {0x413c,0x81d7,5},
        {0x1bc7,0x1040,0}, {0x1bc7,0x1041,0}, {0x1bc7,0x1201,0}
    };
    unsigned int i;
    for (i = 0; i < sizeof(devices) / sizeof(devices[0]); ++i)
        if (devices[i].vid == vid && devices[i].pid == pid && devices[i].iface == iface) return 1;
    return 0;
}

static inline enum qtq_mode qtq_classify(unsigned int vid, unsigned int pid,
                                        int download_interface, int adb, int diag_hint)
{
    if (adb) return QTQ_ADB;
    if (qtq_download_identity(vid, pid)) {
        if (!download_interface) return QTQ_UNKNOWN;
        return pid == 0x9008 ? QTQ_EDL : QTQ_DUMP;
    }
    return diag_hint ? QTQ_DIAG : QTQ_UNKNOWN;
}

static inline int qtq_operation_allowed(enum qtq_mode mode, int ramdump)
{
    return mode == QTQ_EDL || (mode == QTQ_DUMP && ramdump);
}
#endif
