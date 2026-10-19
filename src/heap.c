/* scumm5-amiga - first-fit heap inside one fixed arena
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One arena instead of AllocMem per resource keeps resource churn from
 * fragmenting the 2 MB the OS and everything else share.
 */
#include "heap.h"

struct hblk {
	ULONG size;		/* whole block, header included, multiple of 8 */
	struct hblk *next;	/* free blocks only, sorted by address */
};

#define HDR 8
#define MIN_SPLIT 16
/* Small blocks come from the low end of the arena, large ones from the high
 * end, so long-lived small blocks (locked scripts) do not split the space
 * large ones (rooms, costumes, sounds) need. */
#define LARGE 16384

static struct hblk *free_list;

void heap_init(void *base, ULONG size)
{
	free_list = base;
	free_list->size = size & ~7UL;
	free_list->next = 0;
}

void *heap_alloc(ULONG size)
{
	struct hblk **prev, *b, *rest, **fit = 0;
	ULONG n = (size + HDR + 7) & ~7UL;

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
		rest = (struct hblk *)((UBYTE *)b + n);
		rest->size = b->size - n;
		rest->next = b->next;
		*fit = rest;
		b->size = n;
	} else {
		b->size -= n;
		b = (struct hblk *)((UBYTE *)b + b->size);
		b->size = n;
	}
	return (UBYTE *)b + HDR;
}

void heap_free(void *p)
{
	struct hblk *b = (struct hblk *)((UBYTE *)p - HDR);
	struct hblk **prev = &free_list, *before = 0, *after;

	while ((after = *prev) && after < b) {
		before = after;
		prev = &after->next;
	}
	b->next = after;
	*prev = b;
	if (after && (UBYTE *)b + b->size == (UBYTE *)after) {
		b->size += after->size;
		b->next = after->next;
	}
	if (before && (UBYTE *)before + before->size == (UBYTE *)b) {
		before->size += b->size;
		before->next = b->next;
	}
}

ULONG heap_free_bytes(void)
{
	struct hblk *b;
	ULONG n = 0;
	for (b = free_list; b; b = b->next)
		n += b->size;
	return n;
}

ULONG heap_largest_free(void)
{
	struct hblk *b;
	ULONG n = 0;
	for (b = free_list; b; b = b->next)
		if (b->size > n)
			n = b->size;
	return n > HDR ? n - HDR : 0;
}
