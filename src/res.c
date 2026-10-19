/* scumm5-amiga - resource pack loader and cache
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Expiry policy adapted from ScummVM engines/scumm/resource.cpp
 * (ResourceManager::expireResources, increaseResourceCounters), ScummVM
 * commit 96b42d42bc8533953bdd701db059cc33c839a9ff. ScummVM is the legal
 * property of its developers, whose names are too numerous to list here;
 * see the COPYRIGHT file distributed with the ScummVM source.
 */
#include "res.h"
#include "heap.h"

#define HEADER_SIZE 60
#define AGE_MAX 127
#define F_LOCK 0x80

struct dirent {
	ULONG offset, size, checksum;
};

struct slot {
	UBYTE *ptr;
	UBYTE age;		/* 0: not loaded, 1: used since the last tick */
	UBYTE flags;
};

struct restype {
	UWORD count;
	struct dirent *dir;
	struct slot *slot;
};

int (*res_in_use)(UWORD type, UWORD idx);

static LONG fh;
static struct restype types[RT_COUNT];
static struct res_bg *bgs;
static UBYTE *arena;
static ULONG arena_len;
static struct res_stats stats;

static void *os_alloc(ULONG n)
{
	void *p = AllocMem(n, MEMF_CLEAR);
	if (p)
		stats.directory += n;
	return p;
}

static int read_at(ULONG offset, void *dest, ULONG n)
{
	if (Seek(fh, (LONG)offset, OFFSET_BEGINNING) < 0)
		return -1;
	return Read(fh, dest, (LONG)n) == (LONG)n ? 0 : -1;
}

static ULONG be32(const UBYTE *p) { return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3]; }
static UWORD be16(const UBYTE *p) { return (UWORD)(p[0] << 8 | p[1]); }

int res_open(const char *path, ULONG arena_size)
{
	UBYTE hdr[HEADER_SIZE];
	UWORD t;

	fh = Open(path, MODE_OLDFILE);
	if (!fh)
		return -1;
	if (read_at(0, hdr, HEADER_SIZE) || be32(hdr) != TAG('S', '5', 'P', 'K') ||
	    be16(hdr + 4) != 1 || be16(hdr + 6) != RT_COUNT)
		goto fail;

	stats.directory = 0;
	for (t = 0; t < RT_COUNT; t++) {
		struct restype *rt = &types[t];
		const UBYTE *e = hdr + 8 + t * 8;
		rt->count = be16(e);
		rt->dir = os_alloc((ULONG)rt->count * sizeof(struct dirent));
		rt->slot = os_alloc((ULONG)rt->count * sizeof(struct slot));
		if (!rt->dir || !rt->slot || read_at(be32(e + 4), rt->dir, (ULONG)rt->count * sizeof(struct dirent)))
			goto fail;
	}
	bgs = os_alloc((ULONG)types[RT_ROOM].count * sizeof(struct res_bg));
	if (!bgs || read_at(be32(hdr + 48), bgs, (ULONG)types[RT_ROOM].count * sizeof(struct res_bg)))
		goto fail;

	/* chip RAM: converted graphics resources will be blitted from here */
	arena = AllocMem(arena_size, MEMF_CHIP);
	if (!arena)
		goto fail;
	arena_len = arena_size;
	heap_init(arena, arena_size);
	return 0;

fail:
	res_close();
	return -1;
}

void res_close(void)
{
	UWORD t;

	for (t = 0; t < RT_COUNT; t++) {
		struct restype *rt = &types[t];
		if (rt->dir)
			FreeMem(rt->dir, (ULONG)rt->count * sizeof(struct dirent));
		if (rt->slot)
			FreeMem(rt->slot, (ULONG)rt->count * sizeof(struct slot));
		rt->dir = 0;
		rt->slot = 0;
	}
	if (bgs)
		FreeMem(bgs, (ULONG)types[RT_ROOM].count * sizeof(struct res_bg));
	bgs = 0;
	if (arena)
		FreeMem(arena, arena_len);
	arena = 0;
	if (fh)
		Close(fh);
	fh = 0;
}

UWORD res_count(UWORD type)
{
	return type < RT_COUNT ? types[type].count : 0;
}

ULONG res_size(UWORD type, UWORD idx)
{
	return type < RT_COUNT && idx < types[type].count ? types[type].dir[idx].size : 0;
}

int res_loaded(UWORD type, UWORD idx)
{
	return res_size(type, idx) && types[type].slot[idx].ptr;
}

void res_nuke(UWORD type, UWORD idx)
{
	struct slot *s;

	if (!res_loaded(type, idx))
		return;
	s = &types[type].slot[idx];
	heap_free(s->ptr);
	stats.used -= types[type].dir[idx].size;
	s->ptr = 0;
	s->age = 0;
	s->flags = 0;
}

void res_tick(void)
{
	UWORD t, i;

	for (t = 0; t < RT_COUNT; t++)
		for (i = 0; i < types[t].count; i++) {
			struct slot *s = &types[t].slot[i];
			if (s->age && s->age < AGE_MAX)
				s->age++;
		}
}

/* Expire the least recently used resource that is neither locked nor in use. */
static int expire_one(void)
{
	UWORD t, i, best_t = 0, best_i = 0;
	UBYTE best_age = 0;

	for (t = 0; t < RT_COUNT; t++)
		for (i = 0; i < types[t].count; i++) {
			struct slot *s = &types[t].slot[i];
			if (s->ptr && !(s->flags & F_LOCK) && s->age > best_age &&
			    !(res_in_use && res_in_use(t, i))) {
				best_age = s->age;
				best_t = t;
				best_i = i;
			}
		}
	if (!best_age)
		return 0;
	res_nuke(best_t, best_i);
	stats.evictions++;
	return 1;
}

UBYTE *res_get(UWORD type, UWORD idx)
{
	struct slot *s;
	const struct dirent *d;
	UBYTE *p;

	if (!res_size(type, idx))
		return 0;
	s = &types[type].slot[idx];
	d = &types[type].dir[idx];
	if (s->ptr) {
		s->age = 1;
		stats.hits++;
		return s->ptr;
	}
	while (!(p = heap_alloc(d->size)))
		if (!expire_one())
			break;
	if (!p || read_at(d->offset, p, d->size)) {
		if (p)
			heap_free(p);
		stats.failures++;
		return 0;
	}
	s->ptr = p;
	s->age = 1;
	stats.loads++;
	stats.used += d->size;
	if (stats.used > stats.peak)
		stats.peak = stats.used;
	return p;
}

void res_lock(UWORD type, UWORD idx)
{
	if (res_size(type, idx))
		types[type].slot[idx].flags |= F_LOCK;
}

void res_unlock(UWORD type, UWORD idx)
{
	if (res_size(type, idx))
		types[type].slot[idx].flags &= (UBYTE)~F_LOCK;
}

ULONG res_checksum(const UBYTE *p, ULONG n)
{
	ULONG a = 1, c = 0;
	while (n--) {
		a += *p++;
		c += a;
	}
	return c << 16 ^ a;
}

int res_verify(UWORD type, UWORD idx)
{
	if (!res_loaded(type, idx))
		return -1;
	return res_checksum(types[type].slot[idx].ptr, types[type].dir[idx].size) == types[type].dir[idx].checksum ? 0 : -1;
}

const struct res_bg *res_room_bg(UWORD room)
{
	if (room >= types[RT_ROOM].count || !bgs[room].size)
		return 0;
	return &bgs[room];
}

int res_read_bg(UWORD room, UBYTE *dest)
{
	const struct res_bg *bg = res_room_bg(room);
	if (!bg)
		return -1;
	return read_at(bg->offset, dest, bg->size);
}

const UBYTE *res_find(ULONG tag, const UBYTE *block)
{
	const UBYTE *p = block + 8, *end = block + be32(block + 4);
	ULONG size;

	while (p + 8 <= end) {
		size = be32(p + 4);
		if (size < 8)
			return 0;
		if (be32(p) == tag)
			return p;
		p += size;
	}
	return 0;
}

const struct res_stats *res_get_stats(void)
{
	return &stats;
}
