/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef UPGRADE_QDL_BRIDGE_H
#define UPGRADE_QDL_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum qtq_command { QTQ_FLASH, QTQ_PACKAGE, QTQ_RAW, QTQ_CHIPINFO, QTQ_RESET,
    QTQ_RAMDUMP, QTQ_KICKSTART, QTQ_CREATE_ZIP, QTQ_CREATE_ARCHIVE, QTQ_PROVISION };
enum qtq_raw_type { QTQ_READ, QTQ_WRITE, QTQ_ERASE, QTQ_SHA256, QTQ_FIREHOSE_RESET };
struct qtq_raw_op { enum qtq_raw_type type; const char *address; const char *file; };
struct qtq_image { unsigned int id; const char *file; };
struct qtq_device {
    unsigned int vid, pid, bus;
    unsigned char ports[8];
    unsigned int port_count;
    char serial[128], path[128];
    int backend; /* 0 USB, 2 QUD; -1 automatic selection */
};
struct qtq_request {
    enum qtq_command command;
    const char *programmer, *package, *include_dir, *output, *filter, *node;
    const char *vip_tables, *digest_dir;
    const char *const *xml;
    size_t xml_count;
    const struct qtq_raw_op *raw;
    size_t raw_count;
    const struct qtq_image *images;
    size_t image_count;
    struct qtq_device target;
    int storage; /* qdl_storage_type: 1 emmc, 2 nand, 3 ufs, 4 nvme, 5 spinor */
    unsigned int slot;
    long chunk_size;
    int debug, dry_run, allow_missing, allow_fusing, finalize, skip_reset, skipblock;
};
struct qtq_callbacks {
    void *user;
    void (*log)(void *, int level, const char *text);
    void (*progress)(void *, const char *stage, uint64_t current, uint64_t total);
    int (*cancelled)(void *);
    void (*item)(void *, const char *label, const char *value);
};
/* All entry points are serialized by the caller. Requests are borrowed for the call. */
void qtq_defaults(struct qtq_request *request);
int qtq_scan(struct qtq_device **devices, size_t *count);
void qtq_free_devices(struct qtq_device *devices);
/* 0 success, negative errno on failure, -ECANCELED on cancellation. */
int qtq_execute(const struct qtq_request *request, const struct qtq_callbacks *callbacks);

/* Internal hooks used by the patched upstream sources. */
int qtq_cancelled(void);
int qtq_simulating(void);
int qtq_require_sahara(void);
int qtq_failed(void);
void qtq_error(void);
void qtq_item(const char *label, const char *value);
int qtq_match_usb(unsigned int bus, const unsigned char *ports, int count);
int qtq_match_qud(const char *path);
void qtq_progress(const char *stage, uint64_t current, uint64_t total);
#ifdef __cplusplus
}
#endif
#endif
