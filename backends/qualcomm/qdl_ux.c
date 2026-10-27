/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/xmlerror.h>
#include <libxml/xmlversion.h>
#include "qdl.h"
#include "qdl_bridge.h"

extern struct qtq_callbacks qtq_hooks;
static void message(int level, int with_errno, const char *fmt, va_list ap)
{
    int saved = errno;
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) return;
    char *text = malloc((size_t)n + 128);
    if (!text) { qtq_error(); return; }
    vsnprintf(text, (size_t)n + 1, fmt, ap);
    if (with_errno) snprintf(text + n, 128, ": %s", strerror(saved));
    if (qtq_hooks.log) qtq_hooks.log(qtq_hooks.user, level, text);
    free(text);
    errno = saved;
}
#if LIBXML_VERSION >= 21200
static void xml_error(void *ctx, const xmlError *error)
#else
static void xml_error(void *ctx, xmlErrorPtr error)
#endif
{
    (void)ctx;
    if (error && error->message) ux_err("%s:%d: %s", error->file ? error->file : "XML", error->line, error->message);
}
void ux_init(void) { xmlSetStructuredErrorFunc(NULL, xml_error); }
void ux_set_progname(const char *name) { (void)name; }
const char *ux_progname(void) { return "qdl"; }
#define LOGGER(name, level, with_errno, enabled) \
void name(const char *fmt, ...) { \
    if (!(enabled)) return; \
    va_list ap; va_start(ap, fmt); message(level, with_errno, fmt, ap); va_end(ap); \
}
LOGGER(ux_err, 2, 0, 1)
LOGGER(ux_warn, 1, 1, 1)
LOGGER(ux_warnx, 1, 0, 1)
LOGGER(ux_info, 0, 0, 1)
LOGGER(ux_log, 3, 0, qdl_debug)
LOGGER(ux_debug, 3, 0, qdl_debug)
void ux_progress(const char *fmt, unsigned int value, unsigned int total, ...)
{
    char stage[256];
    va_list ap; va_start(ap, total); vsnprintf(stage, sizeof(stage), fmt, ap); va_end(ap);
    qtq_progress(stage, value, total);
}
