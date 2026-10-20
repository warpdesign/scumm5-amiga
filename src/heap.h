/* scumm5-amiga - first-fit heap inside one fixed arena
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef HEAP_H
#define HEAP_H

#include "platform.h"

/* Blocks are 8-byte aligned (AGA 4x fetch needs it for bitplane data). */
void heap_init(void *base, uint32_t size);
void *heap_alloc(uint32_t size);
void heap_free(void *p);
/* Usable bytes of an allocated block (at least what was asked for). */
uint32_t heap_block_size(const void *p);
uint32_t heap_free_bytes(void);
uint32_t heap_largest_free(void);

#endif
