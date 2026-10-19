/* scumm5-amiga - platform interface (AmigaOS, or a host OS for testing)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#define PLAT_CHIP  1	/* Amiga chip RAM (blitter/bitplane/audio data) */
#define PLAT_CLEAR 2

typedef void *plat_file;

plat_file plat_open(const char *path);
plat_file plat_create(const char *path);
void plat_close(plat_file f);
int32_t plat_read(plat_file f, void *buf, uint32_t n);
int32_t plat_write(plat_file f, const void *buf, uint32_t n);
int plat_seek(plat_file f, uint32_t pos);
int32_t plat_size(plat_file f);

void *plat_alloc(uint32_t n, int flags);
void plat_free(void *p, uint32_t n);

/* One line to the console and the debug log; no newline in s. */
void plat_log(const char *s);

/* Ends the program from anywhere (fatal errors). */
void plat_exit(int rc);

/* 50 Hz ticks since an arbitrary origin. */
uint32_t plat_ticks(void);

#ifdef AMIGA
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
#else
#include <string.h>
#endif

#endif
