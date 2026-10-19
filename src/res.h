/* scumm5-amiga - resource pack loader and cache
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Reads packs made by tools/mkpack.py. Resources are loaded on demand into
 * a fixed arena and expired least-recently-used first when it is full,
 * after the policy of ScummVM's ResourceManager (engines/scumm/resource.cpp).
 */
#ifndef RES_H
#define RES_H

#include "amiga.h"

enum { RT_ROOM, RT_SCRIPT, RT_COSTUME, RT_SOUND, RT_CHARSET, RT_COUNT };

struct res_bg {
	ULONG offset, size, checksum;
	UWORD width;		/* padded to 64 px */
	UWORD height;
	UWORD room_width;
	UWORD pad;
};

struct res_stats {
	ULONG loads, hits, evictions, failures;
	ULONG used, peak;	/* bytes of resources in the arena */
	ULONG directory;	/* bytes of directory tables, outside the arena */
};

/* Returns 0 on success; arena_size bytes of chip RAM become the cache. */
int res_open(const char *path, ULONG arena_size);
void res_close(void);

UWORD res_count(UWORD type);
/* Size in the pack, 0 if the game has no such resource. */
ULONG res_size(UWORD type, UWORD idx);
/* Loads if needed; NULL if absent, unreadable or there is no room even
 * after expiry. Any call may expire unlocked resources that are not in
 * use, so pointers to them must be fetched again afterwards. */
UBYTE *res_get(UWORD type, UWORD idx);
int res_loaded(UWORD type, UWORD idx);
void res_lock(UWORD type, UWORD idx);
void res_unlock(UWORD type, UWORD idx);
void res_nuke(UWORD type, UWORD idx);
/* Ages every loaded resource; the oldest unlocked one is expired first. */
void res_tick(void);
/* The interpreter vetoes expiry of resources it is using (running scripts,
 * costumes of visible actors, the current room). */
extern int (*res_in_use)(UWORD type, UWORD idx);

/* 0 if the stored checksum matches the loaded data. */
int res_verify(UWORD type, UWORD idx);
ULONG res_checksum(const UBYTE *p, ULONG n);

/* Room background: interleaved 8-bitplane rows, read straight into dest. */
const struct res_bg *res_room_bg(UWORD room);
int res_read_bg(UWORD room, UBYTE *dest);

/* Child block with the given tag inside a block, NULL if missing. */
const UBYTE *res_find(ULONG tag, const UBYTE *block);

const struct res_stats *res_get_stats(void);

#define TAG(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))

#endif
