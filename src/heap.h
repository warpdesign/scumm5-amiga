/* scumm5-amiga - first-fit heap inside one fixed arena
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef HEAP_H
#define HEAP_H

#include "amiga.h"

/* Blocks are 8-byte aligned (AGA 4x fetch needs it for bitplane data). */
void heap_init(void *base, ULONG size);
void *heap_alloc(ULONG size);
void heap_free(void *p);
ULONG heap_free_bytes(void);
ULONG heap_largest_free(void);

#endif
