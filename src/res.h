/* scumm5-amiga - resource pack loader and cache
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Reads packs made by tools/mkpack.py. Resources are loaded on demand into
 * a fixed arena and expired least-recently-used first when it is full,
 * after the policy of ScummVM's ResourceManager (engines/scumm/resource.cpp).
 */
#ifndef RES_H
#define RES_H

#include "platform.h"

enum { RT_ROOM, RT_SCRIPT, RT_COSTUME, RT_SOUND, RT_CHARSET, RT_COUNT };

struct res_bg {
	uint32_t offset, size, checksum;
	uint16_t width;		/* padded to 64 px */
	uint16_t height;
	uint16_t room_width;
	uint32_t rmim_offset;	/* where RMIM was in the original ROOM block */
	uint32_t rmim_size;
};

struct res_stats {
	uint32_t loads, hits, evictions, failures;
	uint32_t used, peak;	/* bytes of resources in the arena */
	uint32_t directory;	/* bytes of directory tables, outside the arena */
	uint32_t read_bytes, read_time;	/* read_time in res_clock units */
};

/* Returns 0 on success; arena_size bytes of chip RAM become the cache. */
int res_open(const char *path, uint32_t arena_size);
void res_close(void);

uint16_t res_count(uint16_t type);
/* Size in the pack, 0 if the game has no such resource. */
uint32_t res_size(uint16_t type, uint16_t idx);
/* Loads if needed; NULL if absent, unreadable or there is no room even
 * after expiry. Any call may expire unlocked resources that are not in
 * use, so pointers to them must be fetched again afterwards. */
uint8_t *res_get(uint16_t type, uint16_t idx);
/* Address if loaded, without loading or touching its age. */
uint8_t *res_peek(uint16_t type, uint16_t idx);
int res_loaded(uint16_t type, uint16_t idx);
void res_lock(uint16_t type, uint16_t idx);
void res_unlock(uint16_t type, uint16_t idx);
int res_locked(uint16_t type, uint16_t idx);
void res_nuke(uint16_t type, uint16_t idx);
/* Ages every loaded resource; the oldest unlocked one is expired first. */
void res_tick(void);
/* Marks a loaded resource as the first candidate for expiry. */
void res_make_old(uint16_t type, uint16_t idx);
/* The interpreter vetoes expiry of resources it is using (running scripts,
 * costumes of visible actors, the current room). */
extern int (*res_in_use)(uint16_t type, uint16_t idx);

/* Memory owned by the interpreter (strings, inventory copies, box data):
 * taken from the arena, expiring cached resources if needed, never expired. */
void *res_alloc(uint32_t size);
void res_free(void *p);

/* 0 if the stored checksum matches the loaded data. */
int res_verify(uint16_t type, uint16_t idx);
uint32_t res_checksum(const uint8_t *p, uint32_t n);

/* Room background: interleaved 8-bitplane rows, read straight into dest. */
const struct res_bg *res_room_bg(uint16_t room);
int res_read_bg(uint16_t room, uint8_t *dest);

/* The index blob: MAXS and DOBJ blocks of the original index file. */
const uint8_t *res_index(uint32_t *size);

/* Child block with the given tag inside a block, NULL if missing. */
const uint8_t *res_find(uint32_t tag, const uint8_t *block);

const struct res_stats *res_get_stats(void);
extern uint32_t (*res_clock)(void);	/* optional, for read_time */

#define TAG(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

static inline uint32_t rd_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline uint16_t rd_be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t rd_le32(const uint8_t *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }
static inline uint16_t rd_le16(const uint8_t *p) { return (uint16_t)(p[1] << 8 | p[0]); }

#endif
