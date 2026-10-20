/* scumm5-amiga - first-fit heap inside one fixed arena
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One arena instead of AllocMem per resource keeps resource churn from
 * fragmenting the 2 MB the OS and everything else share.
 */
#include "heap.h"

struct hblk {
	uint32_t size;		/* whole block, header included, multiple of 8 */
	struct hblk *next;	/* free blocks only, sorted by address */
};

#define HDR 8
#define MIN_SPLIT 16
/* Small blocks come from the low end of the arena, large ones from the high
 * end, so long-lived small blocks (locked scripts) do not split the space
 * large ones (rooms, costumes, sounds) need. */
#define LARGE 16384

static struct hblk *free_list;

void heap_init(void *base, uint32_t size)
{
	free_list = base;
	free_list->size = size & ~(uint32_t)7;
	free_list->next = 0;
}

void *heap_alloc(uint32_t size)
{
	struct hblk **prev, *b, *rest, **fit = 0;
	uint32_t n = (size + HDR + 7) & ~(uint32_t)7;

	for (prev = &free_list; (b = *prev); prev = &b->next) {
		if (b->size < n)
			continue;
		fit = prev;
		if (n < LARGE)
			break;
	}
	if (!fit)
		return 0;
	b = *fit;
	if (b->size - n < MIN_SPLIT) {
		*fit = b->next;
	} else if (n < LARGE) {
		rest = (struct hblk *)((uint8_t *)b + n);
		rest->size = b->size - n;
		rest->next = b->next;
		*fit = rest;
		b->size = n;
	} else {
		b->size -= n;
		b = (struct hblk *)((uint8_t *)b + b->size);
		b->size = n;
	}
	return (uint8_t *)b + HDR;
}

void heap_free(void *p)
{
	struct hblk *b = (struct hblk *)((uint8_t *)p - HDR);
	struct hblk **prev = &free_list, *before = 0, *after;

	while ((after = *prev) && after < b) {
		before = after;
		prev = &after->next;
	}
	b->next = after;
	*prev = b;
	if (after && (uint8_t *)b + b->size == (uint8_t *)after) {
		b->size += after->size;
		b->next = after->next;
	}
	if (before && (uint8_t *)before + before->size == (uint8_t *)b) {
		before->size += b->size;
		before->next = b->next;
	}
}

uint32_t heap_block_size(const void *p)
{
	return ((const struct hblk *)((const uint8_t *)p - HDR))->size - HDR;
}

uint32_t heap_free_bytes(void)
{
	struct hblk *b;
	uint32_t n = 0;
	for (b = free_list; b; b = b->next)
		n += b->size;
	return n;
}

uint32_t heap_largest_free(void)
{
	struct hblk *b;
	uint32_t n = 0;
	for (b = free_list; b; b = b->next)
		if (b->size > n)
			n = b->size;
	return n > HDR ? n - HDR : 0;
}
