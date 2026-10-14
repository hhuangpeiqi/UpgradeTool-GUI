/* SPDX-License-Identifier: BSD-3-Clause */
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libxml/globals.h>
#ifndef _WIN32
#include <poll.h>
#endif
#include "qdl_bridge.h"
#define QDL_EMBEDDED 1
#include "qdl.c"

struct qtq_callbacks qtq_hooks;
static const struct qtq_request *active;
static int allocation_failed;
extern void qtq_gpt_clear(void);
int qtq_cancelled(void) { return qtq_hooks.cancelled && qtq_hooks.cancelled(qtq_hooks.user); }
int qtq_simulating(void) { return active && (active->dry_run || active->digest_dir); }
int qtq_require_sahara(void) { return active && active->command == QTQ_KICKSTART; }
int qtq_failed(void) { return allocation_failed; }
void qtq_error(void) { allocation_failed = 1; }
void qtq_item(const char *label, const char *value)
{ if (qtq_hooks.item) qtq_hooks.item(qtq_hooks.user, label, value); }
void qtq_progress(const char *stage, uint64_t current, uint64_t total)
{ if (qtq_hooks.progress) qtq_hooks.progress(qtq_hooks.user, stage, current, total); }
int qtq_match_usb(unsigned int bus, const unsigned char *ports, int count)
{
    if (!active || !active->target.port_count) return 1;
    return bus == active->target.bus && count == (int)active->target.port_count &&
           !memcmp(ports, active->target.ports, (size_t)count);
}
int qtq_match_qud(const char *path)
{ return !active || !active->target.path[0] || !strcmp(path, active->target.path); }
void qtq_defaults(struct qtq_request *r)
{
    memset(r, 0, sizeof(*r));
    r->storage = QDL_STORAGE_UFS;
    r->slot = UINT_MAX;
    r->target.backend = -1;
}
void qtq_free_devices(struct qtq_device *devices) { free(devices); }
int qtq_scan(struct qtq_device **devices, size_t *count)
{
    unsigned int nu = 0, nq = 0;
    struct qdl_device_desc *u = usb_list(&nu);
    struct qud_device_desc *q = qud_list(&nq);
    struct qtq_device *rows = calloc(nu + nq + 1, sizeof(*rows));
    if (!rows) { free(u); free(q); return -ENOMEM; }
    for (unsigned int i = 0; i < nu; ++i) {
        rows[i].vid = u[i].vid; rows[i].pid = u[i].pid; rows[i].bus = u[i].bus;
        rows[i].port_count = u[i].port_count > 0 ? (unsigned int)u[i].port_count : 0;
        memcpy(rows[i].ports, u[i].ports, sizeof(rows[i].ports));
        snprintf(rows[i].serial, sizeof(rows[i].serial), "%s", u[i].serial);
        rows[i].backend = QDL_DEVICE_USB;
    }
    for (unsigned int i = 0; i < nq; ++i) {
        struct qtq_device *row = &rows[nu + i];
        row->vid = QUALCOMM_VID; row->pid = q[i].pid; row->backend = QDL_DEVICE_QUD;
        snprintf(row->serial, sizeof(row->serial), "%s", q[i].serial);
        snprintf(row->path, sizeof(row->path), "%s", q[i].path);
    }
    free(u); free(q); *devices = rows; *count = nu + nq;
    return 0;
}
static int load_images(const struct qtq_request *r, struct sahara_image *images)
{
    if (r->image_count) {
        for (size_t i = 0; i < r->image_count; ++i) {
            unsigned int id = r->images[i].id;
            if (qtq_cancelled()) return -ECANCELED;
            if (!id || id >= MAPPING_SZ || images[id].ptr || !r->images[i].file) { ux_err("Invalid or duplicate Sahara image ID\n"); return -EINVAL; }
            if (load_sahara_image(NULL, r->images[i].file, &images[id]) < 0) return -EINVAL;
        }
        return 0;
    }
    if (!r->programmer || !*r->programmer) { ux_err("Select a programmer\n"); return -EINVAL; }
    char *input = strdup(r->programmer);
    if (!input) return -ENOMEM;
    int ret = decode_programmer(input, images);
    free(input);
    return ret < 0 ? -EINVAL : 0;
}
static int load_ops(const struct qtq_request *r, struct list_head *ops,
                    struct sahara_image *images, struct ufs_provisioning *ufs)
{
    int ret = 0;
    if (r->command == QTQ_PACKAGE) {
        if (!r->package || !*r->package) return -EINVAL;
        ret = qdl_cmd_flash(ops, r->package, r->include_dir, images);
    } else {
        ret = load_images(r, images);
        if (ret < 0) return ret;
        if (r->command == QTQ_RAW) {
            if (!r->raw_count) return -EINVAL;
            for (size_t i = 0; i < r->raw_count; ++i) {
                const struct qtq_raw_op *op = &r->raw[i];
                if (qtq_cancelled()) return -ECANCELED;
                if (!op->address && op->type != QTQ_FIREHOSE_RESET) return -EINVAL;
                switch (op->type) {
                case QTQ_READ: ret = op->file ? read_cmd_add(ops, op->address, op->file) : -EINVAL; break;
                case QTQ_WRITE: ret = op->file ? program_cmd_add(ops, op->address, op->file) : -EINVAL; break;
                case QTQ_ERASE: ret = erase_cmd_add(ops, op->address); break;
                case QTQ_SHA256: ret = sha256_cmd_add(ops, op->address); break;
                case QTQ_FIREHOSE_RESET:
                    if (i + 1 != r->raw_count) return -EINVAL;
                    ret = qdl_cmd_reset(ops); break;
                default: return -EINVAL;
                }
                if (ret < 0) return ret;
            }
        } else {
            if (!r->xml_count) return -EINVAL;
            for (size_t i = 0; i < r->xml_count; ++i) {
                if (qtq_cancelled()) return -ECANCELED;
                const char *path = r->xml[i];
                if (!path || !*path) return -EINVAL;
                int type = detect_type(path);
                if ((r->command == QTQ_PROVISION) != (type == QDL_FILE_UFS)) {
                    ux_err("UFS provisioning requires a separate task containing only a UFS XML\n");
                    return -EINVAL;
                }
                xmlDoc *doc = xmlReadFile(path, NULL, XML_PARSE_NONET);
                xmlNode *root = doc ? xmlDocGetRootElement(doc) : NULL;
                if (!root) { if (doc) xmlFreeDoc(doc); return -EINVAL; }
                int mixed = 0;
                for (xmlNode *n = root->children; n; n = n->next) {
                    if (n->type != XML_ELEMENT_NODE) continue;
                    if ((type == QDL_FILE_UFS) != !xmlStrcmp(n->name, (xmlChar *)"ufs")) mixed = 1;
                }
                xmlFreeDoc(doc);
                if (mixed) { ux_err("UFS and ordinary operations cannot share an XML\n"); return -EINVAL; }
                switch (type) {
                case QDL_FILE_PROGRAM: ret = program_load(ops, path, r->storage == QDL_STORAGE_NAND, r->allow_missing, NULL, r->include_dir); break;
                case QDL_FILE_PATCH: ret = patch_load(ops, path); break;
                case QDL_FILE_READ: ret = read_op_load(ops, path, r->include_dir); break;
                case QDL_FILE_UFS:
                    if (r->storage != QDL_STORAGE_UFS) return -EINVAL;
                    ret = ufs_load(ufs, path, r->finalize); break;
                default: ux_err("Unsupported XML: %s\n", path); return -EINVAL;
                }
                if (ret < 0) return ret;
            }
        }
    }
    if (ret < 0) return ret;
    if (!r->allow_fusing && program_is_sec_partition_flashed(ops)) {
        ux_err("secdata programming requires allow-fusing\n"); return -EPERM;
    }
    return 0;
}
#ifndef _WIN32
static int node_io(struct qdl_device *d, void *buf, size_t len, unsigned int timeout, int writing)
{
    struct timespec start, now;
    if (!len) return 0;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        if (qtq_cancelled()) return -ECANCELED;
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t elapsed = (now.tv_sec - start.tv_sec) * INT64_C(1000) +
                          (now.tv_nsec - start.tv_nsec) / 1000000;
        if (timeout && elapsed >= timeout) return -ETIMEDOUT;
        int slice = !timeout || timeout - elapsed > 100 ? 100 : (int)(timeout - elapsed);
        struct pollfd fd = { .fd = d->fd, .events = writing ? POLLOUT : POLLIN };
        int ret = poll(&fd, 1, slice);
        if (ret < 0 && errno != EINTR) return -errno;
        if (ret > 0) {
            if (fd.revents & fd.events) {
                ssize_t n = writing ? write(d->fd, buf, len) : read(d->fd, buf, len);
                if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
                return n < 0 ? -errno : n == 0 ? -ENODEV : (int)n;
            }
            if (fd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -ENODEV;
        }
    }
}
static int node_read(struct qdl_device *d, void *b, size_t n, unsigned int t)
{
    /* Kickstart uses Sahara packets; serial/MHI streams may split their header. */
    unsigned char *bytes = b;
    size_t received = 0, packet_len = 8;
    struct timespec start, now;
    if (n < packet_len) return -EINVAL;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (received < packet_len) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t elapsed = (now.tv_sec - start.tv_sec) * INT64_C(1000) +
                          (now.tv_nsec - start.tv_nsec) / 1000000;
        if (t && elapsed >= t) return -ETIMEDOUT;
        int ret = node_io(d, bytes + received, packet_len - received, t ? t - elapsed : 0, 0);
        if (ret < 0) return ret;
        received += (size_t)ret;
        if (received == 8) {
            packet_len = (uint32_t)bytes[4] | ((uint32_t)bytes[5] << 8) |
                         ((uint32_t)bytes[6] << 16) | ((uint32_t)bytes[7] << 24);
            if (packet_len < 8 || packet_len > n || packet_len > INT_MAX) return -EPROTO;
        }
    }
    return (int)received;
}
struct qtq_node_device { struct qdl_device base; size_t chunk_size; };
static void node_set_chunk(struct qdl_device *d, long size)
{ ((struct qtq_node_device *)d)->chunk_size = (size_t)size; }
static int node_write(struct qdl_device *d, const void *b, size_t n, unsigned int t)
{
    size_t written = 0, chunk = ((struct qtq_node_device *)d)->chunk_size;
    struct timespec start, now;
    if (n > INT_MAX) return -EINVAL;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (written < n) {
        size_t len = n - written;
        if (chunk && len > chunk) len = chunk;
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t elapsed = (now.tv_sec - start.tv_sec) * INT64_C(1000) +
                          (now.tv_nsec - start.tv_nsec) / 1000000;
        if (t && elapsed >= t) return -ETIMEDOUT;
        int ret = node_io(d, (char *)b + written, len, t ? t - elapsed : 0, 1);
        if (ret < 0) return ret;
        written += (size_t)ret;
    }
    return (int)written;
}
#endif
static int create_archive(const struct qtq_request *r, struct sahara_image *images)
{
    if (r->package && *r->package) {
        char *selector, *filename = qdl_split_specifier(r->package, &selector);
        if (!filename) return -EINVAL;
        int ret = contents_load_programmers(filename, selector, images);
        free(filename);
        if (ret < 0) return ret;
    } else {
        int ret = load_images(r, images);
        if (ret < 0) return ret;
    }
    if (qtq_cancelled()) return -ECANCELED;
    return sahara_archive_write(r->output, images);
}
int qtq_execute(const struct qtq_request *r, const struct qtq_callbacks *callbacks)
{
    struct sahara_image images[MAPPING_SZ] = {};
    struct list_head ops = LIST_INIT(ops);
    struct ufs_provisioning ufs;
    struct qdl_device *device = NULL;
#ifndef _WIN32
    struct qtq_node_device node_storage = { .base = { .fd = -1 } };
    struct qdl_device *node = &node_storage.base;
#else
    struct qdl_device *node = NULL;
#endif
    int opened = 0, vip_initialized = 0, digests_initialized = 0;
    int ret = -EINVAL;
    if (!r || active || (r->xml_count && !r->xml) || (r->raw_count && !r->raw) ||
        (r->image_count && !r->images) || r->target.port_count > sizeof(r->target.ports)) return -EINVAL;
    qtq_hooks = callbacks ? *callbacks : (struct qtq_callbacks){0};
    active = r; allocation_failed = 0; qdl_debug = !!r->debug;
    xmlStructuredErrorFunc previous_xml_handler = xmlStructuredError;
    void *previous_xml_context = xmlStructuredErrorContext;
    ufs_provisioning_init(&ufs);
    ux_init(); qtq_gpt_clear();
    if (qtq_cancelled()) { ret = -ECANCELED; goto cleanup; }
    if (r->storage < QDL_STORAGE_EMMC || r->storage > QDL_STORAGE_SPINOR ||
        (r->vip_tables && r->digest_dir) || r->chunk_size < 0 ||
        r->command < QTQ_FLASH || r->command > QTQ_PROVISION) goto cleanup;
    if (r->command == QTQ_PROVISION && (r->storage != QDL_STORAGE_UFS || r->xml_count != 1)) {
        ux_err("UFS provisioning requires UFS storage and exactly one UFS XML\n");
        goto cleanup;
    }
    if ((r->command == QTQ_CREATE_ZIP || r->command == QTQ_CREATE_ARCHIVE || r->command == QTQ_RAMDUMP) &&
        (!r->output || !*r->output)) goto cleanup;
    if (r->command == QTQ_CREATE_ARCHIVE) { ret = create_archive(r, images); goto cleanup; }
    if (r->command == QTQ_CREATE_ZIP) {
        char *selector, *filename = r->package ? qdl_split_specifier(r->package, &selector) : NULL;
        if (!filename) goto cleanup;
        ret = contents_load(&ops, filename, selector, images, r->include_dir);
        free(filename);
        if (!ret && !qtq_cancelled()) ret = zipper_write(r->output, &ops, images);
        goto cleanup;
    }
    if (r->node && *r->node) {
        if (r->command != QTQ_KICKSTART || r->target.serial[0] || r->target.backend != -1 || r->dry_run || r->digest_dir) goto cleanup;
#ifdef _WIN32
        ret = -ENOTSUP; goto cleanup;
#else
        node->fd = qdl_open_device_node(r->node);
        if (node->fd < 0) { ret = -errno; goto cleanup; }
        int flags = fcntl(node->fd, F_GETFL);
        if (flags < 0 || fcntl(node->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            ret = -errno; close(node->fd); node->fd = -1; goto cleanup;
        }
        node->read = node_read; node->write = node_write;
        node->set_out_chunk_size = node_set_chunk; device = node;
#endif
    } else {
        enum QDL_DEVICE_TYPE backend = r->dry_run || r->digest_dir ? QDL_DEVICE_SIM :
            r->target.backend == -1 ? QDL_DEVICE_AUTO : (enum QDL_DEVICE_TYPE)r->target.backend;
        if (backend != QDL_DEVICE_USB && backend != QDL_DEVICE_QUD && backend != QDL_DEVICE_AUTO && backend != QDL_DEVICE_SIM) goto cleanup;
        device = qdl_init(backend);
        if (!device) { ret = -ENOTSUP; goto cleanup; }
    }
    device->slot = r->slot;
    device->skipblock_mode = r->skipblock ? QDL_SKIPBLOCK_SHA256 : QDL_SKIPBLOCK_NONE;
    if (r->chunk_size) {
        if (!device->set_out_chunk_size) { ret = -ENOTSUP; goto cleanup; }
        qdl_set_out_chunk_size(device, r->chunk_size);
    }
    if (r->vip_tables) {
        ret = vip_transfer_init(device, r->vip_tables);
        if (ret) goto cleanup;
        vip_initialized = 1;
    }
    if (r->digest_dir) {
        ret = vip_gen_init(device, r->digest_dir);
        if (ret) goto cleanup;
        digests_initialized = 1;
    }
    if (r->command == QTQ_FLASH || r->command == QTQ_PACKAGE || r->command == QTQ_RAW || r->command == QTQ_PROVISION) {
        ret = load_ops(r, &ops, images, &ufs);
        if (ret < 0) goto cleanup;
        ret = qdl_ensure_configured(&ops, r->storage);
        if (ret < 0) goto cleanup;
        ret = qdl_determine_bootable(&ops);
        if (ret < 0) goto cleanup;
        if (!r->skip_reset && !(r->command == QTQ_RAW && r->raw[r->raw_count-1].type == QTQ_FIREHOSE_RESET)) {
            ret = qdl_cmd_reset(&ops); if (ret < 0) goto cleanup;
        }
    } else if (r->command == QTQ_KICKSTART) {
        ret = load_images(r, images); if (ret < 0) goto cleanup;
    }
    if (qtq_cancelled()) { ret = -ECANCELED; goto cleanup; }
    qtq_progress("Sahara", 0, 0);
    if (device != node) {
        const char *serial = r->target.serial[0] ? r->target.serial : NULL;
        if (r->target.backend == QDL_DEVICE_QUD && r->target.path[0]) serial = r->target.path;
        ret = qdl_open(device, serial);
        if (ret) goto cleanup;
        opened = 1;
    }
    switch (r->command) {
    case QTQ_CHIPINFO: ret = sahara_chipinfo(device); break;
    case QTQ_RESET:
        ret = sahara_device_reset(device);
        if (ret == 1) ret = firehose_reset(device);
        break;
    case QTQ_RAMDUMP:
        ret = qdl_mkdir_p(r->output);
        if (!ret) ret = sahara_run(device, NULL, r->output, r->filter);
        break;
    case QTQ_KICKSTART: ret = sahara_run(device, images, NULL, NULL); break;
    default:
        ret = sahara_run(device, images, NULL, NULL);
        if (ret < 0 || qtq_cancelled()) break;
        ret = r->command == QTQ_PROVISION ? firehose_provision(device, &ufs, r->skip_reset) : firehose_run(device, &ops);
        if (!ret) print_sha256_results(&ops);
        break;
    }
cleanup:
    if (digests_initialized) {
        int digest_ret = vip_gen_finalize(device, ret == 0 && !qtq_cancelled());
        if (!ret && digest_ret) ret = digest_ret;
    }
    if (device && device == node) { if (node->fd >= 0) close(node->fd); free(node->pending_buf); }
    else if (device) {
        if (opened) qdl_close(device);
        if (vip_initialized) vip_transfer_deinit(device);
        qdl_deinit(device);
    }
    sahara_images_free(images, MAPPING_SZ);
    firehose_free_ops(&ops); ufs_provisioning_cleanup(&ufs); qtq_gpt_clear();
    if (qtq_cancelled()) ret = -ECANCELED;
    else if (allocation_failed) ret = -ENOMEM;
    xmlSetStructuredErrorFunc(previous_xml_context, previous_xml_handler);
    active = NULL; qdl_debug = false; memset(&qtq_hooks, 0, sizeof(qtq_hooks));
    return ret > 0 ? -EIO : ret;
}
