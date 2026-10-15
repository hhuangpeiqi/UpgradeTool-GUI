/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qdl.h"
#include "qdl_bridge.h"

extern struct qtq_callbacks qtq_hooks;
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); failures++; } } while (0)

struct step {
    int writing;
    int result;
    size_t length;
    unsigned char data[128];
};
struct peer {
    struct qdl_device device;
    struct step steps[80];
    int count, position, items, cancel;
};
static void append(struct peer *p, int writing, int result, const void *data, size_t length)
{
    struct step *s;
    if (p->count >= 80 || length > sizeof(p->steps[0].data)) { CHECK(0); return; }
    s = &p->steps[p->count++];
    s->writing = writing; s->result = result; s->length = length;
    if (length) memcpy(s->data, data, length);
}
static void words(struct peer *p, int writing, const uint32_t *values, size_t count)
{
    unsigned char data[128];
    size_t i;
    for (i = 0; i < count * 4; ++i) data[i] = values[i / 4] >> ((i % 4) * 8);
    append(p, writing, count * 4, data, count * 4);
}
#define PACKET(p,w,...) do { const uint32_t v[] = {__VA_ARGS__}; words(p,w,v,sizeof(v)/sizeof(*v)); } while (0)
static int receive(struct qdl_device *d, void *buf, size_t length, unsigned int timeout)
{
    struct peer *p = (struct peer *)d;
    struct step *s;
    (void)timeout;
    if (p->position == p->count) { CHECK(0); return -EIO; }
    s = &p->steps[p->position++];
    CHECK(!s->writing && s->length <= length);
    if (s->writing || s->length > length) return -EIO;
    memcpy(buf, s->data, s->length);
    return s->result;
}
static int transmit(struct qdl_device *d, const void *buf, size_t length, unsigned int timeout)
{
    struct peer *p = (struct peer *)d;
    struct step *s;
    (void)timeout;
    if (p->position == p->count) { CHECK(0); return -EIO; }
    s = &p->steps[p->position++];
    CHECK(s->writing && s->length == length && !memcmp(s->data, buf, length));
    return s->result;
}
static void item(void *context, const char *label, const char *value)
{
    struct peer *p = context;
    CHECK(label && value && *value); p->items++;
}
static int cancelled(void *context) { return ((struct peer *)context)->cancel; }
static void init(struct peer *p, enum QDL_DEVICE_TYPE type)
{
    memset(p, 0, sizeof(*p)); p->device.dev_type = type;
    p->device.read = receive; p->device.write = transmit;
    qtq_hooks = (struct qtq_callbacks){.user=p, .cancelled=cancelled, .item=item};
}
static void done(struct peer *p)
{
    CHECK(p->position == p->count);
    free(p->device.pending_buf);
    memset(&qtq_hooks, 0, sizeof(qtq_hooks));
}
static void timeout(struct peer *p) { append(p, 0, -ETIMEDOUT, NULL, 0); }
static void hello(struct peer *p, unsigned int mode)
{
    PACKET(p, 0, 1,48,2,1,4096,mode,0,0,0,0,0,0);
}
static void response(struct peer *p, unsigned int mode)
{
    PACKET(p, 1, 2,48,2,1,0,mode,0,0,0,0,0,0);
}
static void ready(struct peer *p) { PACKET(p, 0, 11,8); }
static void query(struct peer *p, unsigned int command, const uint32_t *data, size_t count)
{
    PACKET(p, 1, 13,12,command);
    PACKET(p, 0, 14,16,command,count*4);
    PACKET(p, 1, 15,12,command);
    words(p, 0, data, count);
}
static void identity(struct peer *p)
{
    const uint32_t serial[] = {0x12345678};
    const uint32_t hwid[] = {0x00010002,0x00953000};
    const uint32_t hash[] = {1,2,3,4,5,6,7,8};
    query(p, 1, serial, 1); query(p, 2, hwid, 2); query(p, 3, hash, 8);
    PACKET(p, 1, 12,12,0);
}
int main(void)
{
    struct peer p;
    int transport, scenario;
    /* Fresh HELLO and missing HELLO on both USB and Windows QUD. */
    for (transport = 0; transport < 2; ++transport) for (scenario = 0; scenario < 3; ++scenario) {
        init(&p, transport ? QDL_DEVICE_QUD : QDL_DEVICE_USB);
        if (scenario) timeout(&p); else hello(&p, 0);
        response(&p, 3);
        if (scenario == 2) {
            timeout(&p); PACKET(&p, 1, 12,12,3);
            hello(&p, 3); response(&p, 3);
        }
        ready(&p); identity(&p);
        CHECK(sahara_chipinfo(&p.device) == 0); CHECK(p.items == 4); done(&p);
    }
    /* A stale image request requires a mode switch and fresh HELLO. */
    init(&p, QDL_DEVICE_USB); PACKET(&p, 0, 3,20,13,0,512);
    PACKET(&p, 1, 12,12,3); hello(&p, 3); response(&p, 3); ready(&p);
    identity(&p); CHECK(sahara_chipinfo(&p.device) == 0); done(&p);
    /* A consumed HELLO must enter command mode before RESET, never image mode. */
    init(&p, QDL_DEVICE_USB); timeout(&p); response(&p, 3); ready(&p);
    PACKET(&p, 1, 7,8); PACKET(&p, 0, 8,8);
    append(&p, 0, -ENODEV, NULL, 0);
    CHECK(sahara_device_reset(&p.device) == 0); done(&p);
    /* A fresh HELLO can be fragmented; later packets can share one read. */
    init(&p, QDL_DEVICE_USB);
    hello(&p, 0);
    { struct step greeting = p.steps[--p.count];
      append(&p, 0, 3, greeting.data, 3);
      append(&p, 0, greeting.length-3, greeting.data+3, greeting.length-3); }
    response(&p, 3); PACKET(&p, 0, 11,8,8,8);
    PACKET(&p, 1, 7,8); append(&p, 0, -ENODEV, NULL, 0);
    CHECK(sahara_device_reset(&p.device) == 0); done(&p);
    /* Crash debug mode keeps its own handshake, then resets. */
    init(&p, QDL_DEVICE_USB); hello(&p, 2); response(&p, 2);
    PACKET(&p, 1, 7,8); append(&p, 0, -ENODEV, NULL, 0);
    CHECK(sahara_device_reset(&p.device) == 0); done(&p);
    /* No CMD_READY means no RESET is sent. */
    init(&p, QDL_DEVICE_USB); timeout(&p); response(&p, 3);
    timeout(&p); PACKET(&p, 1, 12,12,3); timeout(&p);
    CHECK(sahara_device_reset(&p.device) == -ETIMEDOUT); done(&p);
    /* A running Firehose is positively identified and its XML is preserved. */
    init(&p, QDL_DEVICE_USB); timeout(&p); response(&p, 3);
    { const char xml[] = "<?xml version=\"1.0\"?><data><response value=\"ACK\"/></data>";
      char copy[128];
      append(&p, 0, sizeof(xml)-1, xml, sizeof(xml)-1);
      CHECK(sahara_device_reset(&p.device) == 1);
      CHECK(qdl_read(&p.device, copy, sizeof(copy), 1) == sizeof(xml)-1);
      CHECK(!memcmp(copy, xml, sizeof(xml)-1)); }
    done(&p);
    init(&p, QDL_DEVICE_USB);
    { const char xml[] = "<?xml?><data/>"; append(&p,0,sizeof(xml)-1,xml,sizeof(xml)-1); }
    CHECK(sahara_chipinfo(&p.device) == -EPROTO); CHECK(p.items == 0); done(&p);
    /* Only disconnection proves a reset: timeout or generic IO error must fail. */
    for (scenario = 0; scenario < 2; ++scenario) {
        init(&p, QDL_DEVICE_USB); hello(&p,0); response(&p,3); ready(&p);
        PACKET(&p,1,7,8); PACKET(&p,0,8,8);
        append(&p,0,scenario ? -EIO : -ETIMEDOUT,NULL,0);
        CHECK(sahara_device_reset(&p.device) < 0); done(&p);
    }
    /* Reset timeout and explicit device rejection remain failures. */
    init(&p,QDL_DEVICE_USB); hello(&p,0); response(&p,3); ready(&p);
    for (scenario=0; scenario<5; ++scenario) { PACKET(&p,1,7,8); timeout(&p); }
    CHECK(sahara_device_reset(&p.device) == -ETIMEDOUT); done(&p);
    init(&p,QDL_DEVICE_USB); hello(&p,0); response(&p,3); PACKET(&p,0,4,16,0,1);
    CHECK(sahara_device_reset(&p.device) == -EPROTO); done(&p);
    /* Invalid packets, write failures, and cancellation must stop the handshake. */
    init(&p,QDL_DEVICE_USB); PACKET(&p,0,1,8);
    CHECK(sahara_chipinfo(&p.device) == -EINVAL); done(&p);
    init(&p,QDL_DEVICE_USB); hello(&p,0); response(&p,3); p.steps[1].result=-EIO;
    CHECK(sahara_device_reset(&p.device) == -EIO); done(&p);
    init(&p,QDL_DEVICE_USB); hello(&p,0); response(&p,3); p.steps[1].result=24;
    CHECK(sahara_chipinfo(&p.device) == -EIO); done(&p);
    init(&p,QDL_DEVICE_USB); PACKET(&p,0,1,48); timeout(&p);
    CHECK(sahara_chipinfo(&p.device) == -EINVAL); done(&p);
    init(&p,QDL_DEVICE_USB); hello(&p,0); response(&p,3); ready(&p);
    PACKET(&p,1,7,8); append(&p,0,-EIO,NULL,0);
    CHECK(sahara_device_reset(&p.device) == -EIO); done(&p);
    init(&p,QDL_DEVICE_USB); p.cancel=1;
    CHECK(sahara_device_reset(&p.device) == -ECANCELED); done(&p);
    return failures ? 1 : 0;
}
