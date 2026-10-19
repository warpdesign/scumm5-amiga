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

#define VERSION 2
#define HEADER_SIZE 60
#define DIRENT_SIZE 12
#define BGENT_SIZE 28
#define AGE_MAX 127
#define F_LOCK 0x80

struct dirent {
	uint32_t offset, size, checksum;
};

struct slot {
	uint8_t *ptr;
	uint8_t age;		/* 0: not loaded, 1: used since the last tick */
	uint8_t flags;
};

struct restype {
	uint16_t count;
	struct dirent *dir;
	struct slot *slot;
};

int (*res_in_use)(uint16_t type, uint16_t idx);

static plat_file fh;
static struct restype types[RT_COUNT];
static struct res_bg *bgs;
static uint8_t *index_blob;
static uint32_t index_size;
static uint8_t *arena;
static uint32_t arena_len;
static struct res_stats stats;

static void *os_alloc(uint32_t n)
{
	void *p = plat_alloc(n ? n : 1, PLAT_CLEAR);
	if (p)
		stats.directory += n;
	return p;
}

static int read_at(uint32_t offset, void *dest, uint32_t n)
{
	if (plat_seek(fh, offset))
		return -1;
	return plat_read(fh, dest, n) == (int32_t)n ? 0 : -1;
}

static int read_dir(uint16_t t, uint32_t offset)
{
	struct restype *rt = &types[t];
	uint8_t buf[DIRENT_SIZE * 16];
	uint16_t i, n;

	for (i = 0; i < rt->count; i += n) {
		n = rt->count - i < 16 ? rt->count - i : 16;
		if (read_at(offset + (uint32_t)i * DIRENT_SIZE, buf, (uint32_t)n * DIRENT_SIZE))
			return -1;
		for (uint16_t j = 0; j < n; j++) {
			rt->dir[i + j].offset = rd_be32(buf + j * DIRENT_SIZE);
			rt->dir[i + j].size = rd_be32(buf + j * DIRENT_SIZE + 4);
			rt->dir[i + j].checksum = rd_be32(buf + j * DIRENT_SIZE + 8);
		}
	}
	return 0;
}

static int read_bgs(uint32_t offset)
{
	uint8_t e[BGENT_SIZE];
	uint16_t i;

	for (i = 0; i < types[RT_ROOM].count; i++) {
		if (read_at(offset + (uint32_t)i * BGENT_SIZE, e, BGENT_SIZE))
			return -1;
		bgs[i].offset = rd_be32(e);
		bgs[i].size = rd_be32(e + 4);
		bgs[i].checksum = rd_be32(e + 8);
		bgs[i].width = rd_be16(e + 12);
		bgs[i].height = rd_be16(e + 14);
		bgs[i].room_width = rd_be16(e + 16);
		bgs[i].rmim_offset = rd_be32(e + 20);
		bgs[i].rmim_size = rd_be32(e + 24);
	}
	return 0;
}

int res_open(const char *path, uint32_t arena_size)
{
	uint8_t hdr[HEADER_SIZE];
	uint16_t t;

	fh = plat_open(path);
	if (!fh)
		return -1;
	if (read_at(0, hdr, HEADER_SIZE) || rd_be32(hdr) != TAG('S', '5', 'P', 'K') ||
	    rd_be16(hdr + 4) != VERSION || rd_be16(hdr + 6) != RT_COUNT)
		goto fail;

	stats.directory = 0;
	for (t = 0; t < RT_COUNT; t++) {
		struct restype *rt = &types[t];
		const uint8_t *e = hdr + 8 + t * 8;
		rt->count = rd_be16(e);
		rt->dir = os_alloc((uint32_t)rt->count * sizeof(struct dirent));
		rt->slot = os_alloc((uint32_t)rt->count * sizeof(struct slot));
		if (!rt->dir || !rt->slot || read_dir(t, rd_be32(e + 4)))
			goto fail;
	}
	bgs = os_alloc((uint32_t)types[RT_ROOM].count * sizeof(struct res_bg));
	if (!bgs || read_bgs(rd_be32(hdr + 48)))
		goto fail;
	index_size = rd_be32(hdr + 56);
	index_blob = os_alloc(index_size);
	if (!index_blob || read_at(rd_be32(hdr + 52), index_blob, index_size))
		goto fail;

	/* chip RAM: converted graphics resources will be blitted from here */
	arena = plat_alloc(arena_size, PLAT_CHIP);
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
	uint16_t t;

	for (t = 0; t < RT_COUNT; t++) {
		struct restype *rt = &types[t];
		plat_free(rt->dir, (uint32_t)rt->count * sizeof(struct dirent));
		plat_free(rt->slot, (uint32_t)rt->count * sizeof(struct slot));
		rt->dir = 0;
		rt->slot = 0;
	}
	plat_free(bgs, (uint32_t)types[RT_ROOM].count * sizeof(struct res_bg));
	bgs = 0;
	plat_free(index_blob, index_size);
	index_blob = 0;
	plat_free(arena, arena_len);
	arena = 0;
	if (fh)
		plat_close(fh);
	fh = 0;
}

uint16_t res_count(uint16_t type)
{
	return type < RT_COUNT ? types[type].count : 0;
}

uint32_t res_size(uint16_t type, uint16_t idx)
{
	return type < RT_COUNT && idx < types[type].count ? types[type].dir[idx].size : 0;
}

int res_loaded(uint16_t type, uint16_t idx)
{
	return res_size(type, idx) && types[type].slot[idx].ptr;
}

uint8_t *res_peek(uint16_t type, uint16_t idx)
{
	return res_size(type, idx) ? types[type].slot[idx].ptr : 0;
}

void res_nuke(uint16_t type, uint16_t idx)
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
	uint16_t t, i;

	for (t = 0; t < RT_COUNT; t++)
		for (i = 0; i < types[t].count; i++) {
			struct slot *s = &types[t].slot[i];
			if (s->age && s->age < AGE_MAX)
				s->age++;
		}
}

void res_make_old(uint16_t type, uint16_t idx)
{
	if (res_loaded(type, idx))
		types[type].slot[idx].age = AGE_MAX;
}

/* Expire the least recently used resource that is neither locked nor in use. */
static int expire_one(void)
{
	uint16_t t, i, best_t = 0, best_i = 0;
	uint8_t best_age = 0;

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

static void *alloc_expiring(uint32_t size)
{
	void *p;

	while (!(p = heap_alloc(size)))
		if (!expire_one())
			return 0;
	return p;
}

uint8_t *res_get(uint16_t type, uint16_t idx)
{
	struct slot *s;
	const struct dirent *d;
	uint8_t *p;

	if (!res_size(type, idx))
		return 0;
	s = &types[type].slot[idx];
	d = &types[type].dir[idx];
	if (s->ptr) {
		s->age = 1;
		stats.hits++;
		return s->ptr;
	}
	p = alloc_expiring(d->size);
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

void *res_alloc(uint32_t size)
{
	void *p = alloc_expiring(size);
	if (!p)
		stats.failures++;
	return p;
}

void res_free(void *p)
{
	if (p)
		heap_free(p);
}

void res_lock(uint16_t type, uint16_t idx)
{
	if (res_size(type, idx))
		types[type].slot[idx].flags |= F_LOCK;
}

void res_unlock(uint16_t type, uint16_t idx)
{
	if (res_size(type, idx))
		types[type].slot[idx].flags &= (uint8_t)~F_LOCK;
}

int res_locked(uint16_t type, uint16_t idx)
{
	return res_size(type, idx) && (types[type].slot[idx].flags & F_LOCK);
}

uint32_t res_checksum(const uint8_t *p, uint32_t n)
{
	uint32_t a = 1, c = 0;
	while (n--) {
		a += *p++;
		c += a;
	}
	return c << 16 ^ a;
}

int res_verify(uint16_t type, uint16_t idx)
{
	if (!res_loaded(type, idx))
		return -1;
	return res_checksum(types[type].slot[idx].ptr, types[type].dir[idx].size) == types[type].dir[idx].checksum ? 0 : -1;
}

const struct res_bg *res_room_bg(uint16_t room)
{
	if (room >= types[RT_ROOM].count || !bgs[room].size)
		return 0;
	return &bgs[room];
}

int res_read_bg(uint16_t room, uint8_t *dest)
{
	const struct res_bg *bg = res_room_bg(room);
	if (!bg)
		return -1;
	return read_at(bg->offset, dest, bg->size);
}

const uint8_t *res_index(uint32_t *size)
{
	*size = index_size;
	return index_blob;
}

const uint8_t *res_find(uint32_t tag, const uint8_t *block)
{
	const uint8_t *p = block + 8, *end = block + rd_be32(block + 4);
	uint32_t size;

	while (p + 8 <= end) {
		size = rd_be32(p + 4);
		if (size < 8)
			return 0;
		if (rd_be32(p) == tag)
			return p;
		p += size;
	}
	return 0;
}

const struct res_stats *res_get_stats(void)
{
	return &stats;
}
