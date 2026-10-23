/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef QUALCOMM_PATH_COMPAT_H
#define QUALCOMM_PATH_COMPAT_H
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Filesystem paths are UTF-8, including on Windows. */
int qtq_path_open(const char *path, int flags, int mode);
FILE *qtq_path_fopen(const char *path, const char *mode);
int qtq_path_access(const char *path, int mode);
int qtq_path_is_directory(const char *path);
int qtq_path_mkdir(const char *path);
int qtq_path_rename(const char *from, const char *to);
int qtq_path_unlink(const char *path);

/* Returns 1 with a malloc-owned path, 0 if missing, or negative errno.
 * Exact matches precede ASCII case-insensitive matches across all candidates.
 * ZIP members and output paths must not use this resolver. */
int qtq_resolve_image(const char *filename, const char *xml,
                      const char *contents_path, const char *include_dir,
                      char **resolved);
#ifdef __cplusplus
}
#endif
#endif
