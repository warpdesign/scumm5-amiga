/* scumm5-amiga - resource loader test
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage: restest PACK [ARENA_KB [ACCESSES]]
 * Loads every resource, then makes ACCESSES random accesses with the
 * current room and a few scripts locked, verifying checksums throughout; then reads every room
 * background. A small arena forces expiry. Prints "restest: PASS" or FAIL.
 */
#include "res.h"
#include "heap.h"
#include "util.h"

#define MAX_LOCKS 4

struct ref {
	uint16_t type, idx;
};

static const char *const type_name[RT_COUNT] = { "rooms", "scripts", "costumes", "sounds", "charsets" };
static uint32_t errors;

static void fail(const char *what, uint16_t type, uint16_t idx)
{
	errors++;
	out_str("restest: ");
	out_str(what);
	out_str(" ");
	out_str(type_name[type]);
	out_str(" ");
	out_num(idx);
	report("");
}

static int is_locked(const struct ref *locks, uint16_t n, const struct ref *r)
{
	while (n--)
		if (locks[n].type == r->type && locks[n].idx == r->idx)
			return 1;
	return 0;
}

static void check(uint16_t type, uint16_t idx)
{
	if (!res_get(type, idx))
		fail("load failed", type, idx);
	else if (res_verify(type, idx))
		fail("bad checksum", type, idx);
}

static void report_time(const char *what, uint32_t bytes, uint32_t t)
{
	out_str("restest: ");
	out_str(what);
	out_str(" ");
	out_num(bytes);
	out_str(" bytes in ");
	out_num(t * 20);
	out_str(" ms");
	if (t) {
		out_str(", ");
		out_num(bytes / t * 50 / 1024);
		out_str(" KB/s");
	}
	report("");
}

static void report_stats(const char *when)
{
	const struct res_stats *st = res_get_stats();
	out_str("restest: ");
	out_str(when);
	out_str(": loads ");
	out_num(st->loads);
	out_str(" hits ");
	out_num(st->hits);
	out_str(" expired ");
	out_num(st->evictions);
	out_str(" failures ");
	out_num(st->failures);
	out_str(" in use ");
	out_num(st->used);
	out_str(" peak ");
	out_num(st->peak);
	out_str(" largest free ");
	out_num(heap_largest_free());
	report("");
}

int app_main(int argc, char **argv)
{
	const char *p;
	uint32_t arena = 256, accesses = 2000, nrefs = 0, bytes, t0, seed = 12345, i;
	struct ref *refs = 0, locks[MAX_LOCKS];
	uint16_t type, idx, nlocks = 0, maxbg = 0, cur_room = 0, nlocks_room = 0;
	uint32_t bgmax = 0;
	uint8_t *bgbuf = 0;
	int rc = 20;

	if (argc < 2) {
		report("usage: restest PACK [ARENA_KB [ACCESSES]]");
		return 20;
	}
	if (argc > 2) {
		p = argv[2];
		arena = parse_uint(&p);
	}
	if (argc > 3) {
		p = argv[3];
		accesses = parse_uint(&p);
	}
	if (!arena)
		arena = 256;
	if (!accesses)
		accesses = 2000;

	if (res_open(argv[1], arena * 1024)) {
		report("restest: cannot open pack (or out of memory)");
		goto out;
	}

	out_str("restest: arena ");
	out_num(arena);
	out_str(" KB, directory ");
	out_num(res_get_stats()->directory);
	out_str(" bytes;");
	for (type = 0; type < RT_COUNT; type++) {
		uint32_t present = 0;
		for (idx = 0; idx < res_count(type); idx++)
			if (res_size(type, idx))
				present++;
		nrefs += present;
		out_str(" ");
		out_str(type_name[type]);
		out_str(" ");
		out_num(present);
		out_str("/");
		out_num(res_count(type));
	}
	report("");

	refs = plat_alloc(nrefs * sizeof(struct ref), 0);
	if (!refs) {
		report("restest: out of memory");
		goto out;
	}
	nrefs = 0;
	for (type = 0; type < RT_COUNT; type++)
		for (idx = 0; idx < res_count(type); idx++)
			if (res_size(type, idx)) {
				refs[nrefs].type = type;
				refs[nrefs].idx = idx;
				nrefs++;
			}

	/* every resource once, in order */
	bytes = 0;
	t0 = plat_ticks();
	for (i = 0; i < nrefs; i++) {
		check(refs[i].type, refs[i].idx);
		bytes += res_size(refs[i].type, refs[i].idx);
		res_tick();
	}
	report_time("sequential load+verify:", bytes, plat_ticks() - t0);
	report_stats("sequential");

	/* random accesses, locking like the interpreter does: the current room
	 * and a few running scripts; locked resources must survive expiry */
	for (i = 0; i < accesses; i++) {
		struct ref *r;
		seed = seed * 1103515245 + 12345;
		r = &refs[(seed >> 16) % nrefs];
		if (i % 300 == 0 && r->type == RT_ROOM) {
			if (nlocks_room)
				res_unlock(RT_ROOM, cur_room);
			cur_room = r->idx;
			nlocks_room = 1;
			res_lock(RT_ROOM, cur_room);
		}
		check(r->type, r->idx);
		if (i % 10 == 9)
			res_tick();
		if (r->type == RT_SCRIPT && i % 7 == 0 && res_loaded(r->type, r->idx) && !is_locked(locks, nlocks, r)) {
			if (nlocks == MAX_LOCKS) {
				struct ref *old = &locks[0];
				if (!res_loaded(old->type, old->idx) || res_verify(old->type, old->idx))
					fail("locked resource lost", old->type, old->idx);
				res_unlock(old->type, old->idx);
				locks[0] = locks[1];
				locks[1] = locks[2];
				locks[2] = locks[3];
				nlocks--;
			}
			locks[nlocks++] = *r;
			res_lock(r->type, r->idx);
		}
		if (nlocks_room && (!res_loaded(RT_ROOM, cur_room) || (i % 100 == 0 && res_verify(RT_ROOM, cur_room))))
			fail("locked room lost", RT_ROOM, cur_room);
	}
	report_stats("random");

	for (i = 0; i < nlocks; i++)
		if (!res_loaded(locks[i].type, locks[i].idx) || res_verify(locks[i].type, locks[i].idx))
			fail("locked resource lost", locks[i].type, locks[i].idx);
	for (i = 0; i < nrefs; i++)
		if (res_loaded(refs[i].type, refs[i].idx) && res_verify(refs[i].type, refs[i].idx))
			fail("cached resource corrupted", refs[i].type, refs[i].idx);
	for (i = 0; i < nrefs; i++) {
		res_unlock(refs[i].type, refs[i].idx);
		res_nuke(refs[i].type, refs[i].idx);
	}
	if (heap_largest_free() + 8 < (arena * 1024 & ~(uint32_t)7) || res_get_stats()->used) {
		errors++;
		report("restest: heap not fully coalesced after freeing everything");
	}

	/* backgrounds */
	for (idx = 0; idx < res_count(RT_ROOM); idx++) {
		const struct res_bg *bg = res_room_bg(idx);
		if (bg && bg->size > bgmax) {
			bgmax = bg->size;
			maxbg = idx;
		}
	}
	bgbuf = plat_alloc(bgmax, PLAT_CHIP);
	if (!bgbuf) {
		report("restest: out of chip memory for backgrounds");
		goto out;
	}
	bytes = 0;
	t0 = plat_ticks();
	for (idx = 0; idx < res_count(RT_ROOM); idx++) {
		const struct res_bg *bg = res_room_bg(idx);
		if (!bg)
			continue;
		if (res_read_bg(idx, bgbuf))
			fail("background read failed", RT_ROOM, idx);
		else if (res_checksum(bgbuf, bg->size) != bg->checksum)
			fail("bad background checksum", RT_ROOM, idx);
		bytes += bg->size;
	}
	report_time("backgrounds:", bytes, plat_ticks() - t0);
	out_str("restest: largest background: room ");
	out_num(maxbg);
	out_str(", ");
	out_num(bgmax);
	report(" bytes");

	if (errors) {
		out_num(errors);
		report(" errors; restest: FAIL");
	} else {
		report("restest: PASS");
		rc = 0;
	}

out:
	plat_free(bgbuf, bgmax);
	plat_free(refs, nrefs * sizeof(struct ref));
	res_close();
	return rc;
}
