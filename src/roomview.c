/* scumm5-amiga - show a room from a resource pack on AGA, 8 bitplanes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage: roomview PACK ROOM [FRAMES [X]]
 * Shows the room until the left mouse button is pressed, or for FRAMES
 * frames if FRAMES is non-zero. Rooms wider than the screen scroll back and
 * forth with the hardware, or stay at column X if given.
 *
 * Scrolling: bitplane pointers move in 64 px steps (4x fetch granularity)
 * and BPLCON1 delays the picture by 0-63 px. Fetch starts one 64 px block
 * early so the delayed left edge has data.
 */
#include "amiga.h"
#include "res.h"
#include "util.h"

APTR GfxBase;

#define PLANES 8
#define ARENA_SIZE (128 * 1024)
#define SCREEN_W 320
#define DDF_START 0x18		/* $38 minus one 64 px block */
#define FETCH_BYTES 56		/* 4x fetch, DDF $18-$d0: 7 blocks of 64 px */
#define BLOCK_BYTES 8
#define PAD_FRONT BLOCK_BYTES	/* x < 64 fetches the block before plane 0 */
#define PAD_BACK 16		/* the last fetch can run up to 127 px past a row */
#define DIW_TOP 0x2c

static UWORD cl_bplpt, cl_bplcon1;	/* word index of the patched copper values */

static ULONG *move(ULONG *cl, UWORD reg, UWORD val)
{
	*cl++ = (ULONG)reg << 16 | val;
	return cl;
}

static void build_copper(ULONG *cl, UWORD rowbytes, UWORD height, const UBYTE *pal, UWORD *nullspr)
{
	ULONG *start = cl;
	UWORD i, b;

	for (i = 0; i < 8; i++) {
		cl = move(cl, 0x120 + i * 4, (UWORD)((ULONG)nullspr >> 16));
		cl = move(cl, 0x122 + i * 4, (UWORD)(ULONG)nullspr);
	}

	cl = move(cl, 0x08e, DIW_TOP << 8 | 0x81);			/* DIWSTRT */
	cl = move(cl, 0x090, (UWORD)(((DIW_TOP + height) & 0xff) << 8 | 0xc1)); /* DIWSTOP */
	cl = move(cl, 0x092, DDF_START);				/* DDFSTRT */
	cl = move(cl, 0x094, 0x00d0);					/* DDFSTOP */
	cl = move(cl, 0x100, 0x0211);			/* BPLCON0: 8 planes, COLOR, ECSENA */
	cl_bplcon1 = (UWORD)((cl - start) * 2 + 1);
	cl = move(cl, 0x102, 0x0000);					/* BPLCON1 */
	cl = move(cl, 0x104, 0x0000);					/* BPLCON2 */
	cl = move(cl, 0x10c, 0x0011);					/* BPLCON4 */
	cl = move(cl, 0x1fc, 0x0003);			/* FMODE: 4x bitplane fetch */
	cl = move(cl, 0x108, (UWORD)(rowbytes * PLANES - FETCH_BYTES)); /* BPL1MOD */
	cl = move(cl, 0x10a, (UWORD)(rowbytes * PLANES - FETCH_BYTES)); /* BPL2MOD */

	cl_bplpt = (UWORD)((cl - start) * 2 + 1);
	for (i = 0; i < PLANES * 2; i++)
		cl = move(cl, 0x0e0 + i * 2, 0);

	for (b = 0; b < 8; b++) {
		const UBYTE *c = pal + b * 32 * 3;
		cl = move(cl, 0x106, (UWORD)(b << 13 | 0x0c00));
		for (i = 0; i < 32; i++)
			cl = move(cl, 0x180 + i * 2, (UWORD)((c[i * 3] & 0xf0) << 4 | (c[i * 3 + 1] & 0xf0) | c[i * 3 + 2] >> 4));
		cl = move(cl, 0x106, (UWORD)(b << 13 | 0x0e00));
		for (i = 0; i < 32; i++)
			cl = move(cl, 0x180 + i * 2, (UWORD)((c[i * 3] & 0x0f) << 8 | (c[i * 3 + 1] & 0x0f) << 4 | (c[i * 3 + 2] & 0x0f)));
	}
	cl = move(cl, 0x106, 0x0c00);

	*cl = 0xfffffffe;
}

/* Show room column x at the left screen edge. */
static void set_scroll(ULONG *cl, const UBYTE *planes, UWORD rowbytes, UWORD x)
{
	UWORD *w = (UWORD *)cl;
	UWORD block = (UWORD)((x + 63) >> 6);
	UWORD delay = (UWORD)((block << 6) - x);
	ULONG p = (ULONG)planes + (ULONG)(block - 1) * BLOCK_BYTES;
	UWORD i;

	w[cl_bplcon1] = (UWORD)((delay & 15) * 0x11 | (delay >> 4) * 0x4400);
	for (i = 0; i < PLANES; i++, p += rowbytes) {
		w[cl_bplpt + i * 4] = (UWORD)(p >> 16);
		w[cl_bplpt + i * 4 + 2] = (UWORD)p;
	}
}

#define CL_LONGS 768		/* one list is ~590 moves */
#define COPPER_SIZE (2 * CL_LONGS * 4)	/* two lists, then an empty sprite */

int app_main(int argc, char **argv)
{
	const char *p;
	const struct res_bg *bg;
	const UBYTE *room, *clut;
	ULONG frames = 0, n = 0, plane_size = 0, alloc_size = 0;
	UWORD room_no, height, rowbytes, x = 0, max_x;
	WORD dx = 1;
	int fixed = 0, cur = 0;
	UBYTE *alloc = 0, *planes;
	ULONG *copper = 0, *cl[2];
	APTR oldview;
	int rc = 20;

	if (argc < 3) {
		report("usage: roomview PACK ROOM [FRAMES [X]]");
		return 20;
	}
	p = argv[2];
	room_no = (UWORD)parse_uint(&p);
	if (argc > 3) {
		p = argv[3];
		frames = parse_uint(&p);
	}
	if (argc > 4) {
		p = argv[4];
		x = (UWORD)parse_uint(&p);
		fixed = 1;
	}

	GfxBase = OpenLibrary("graphics.library", 39);
	if (!GfxBase) {
		report("roomview: graphics.library V39 (AGA) required");
		goto out;
	}
	if ((REG(0x07c) & 0xff) != 0xf8) {
		report("roomview: AGA chipset required");
		goto out;
	}

	if (res_open(argv[1], ARENA_SIZE)) {
		report("roomview: cannot open pack");
		goto out;
	}
	room = res_get(RT_ROOM, room_no);
	bg = res_room_bg(room_no);
	clut = room ? res_find(TAG('C', 'L', 'U', 'T'), room) : 0;
	if (!room || !bg || !clut) {
		report("roomview: no such room in pack");
		goto out;
	}
	height = bg->height;
	rowbytes = bg->width / 8;
	plane_size = bg->size;
	max_x = bg->room_width > SCREEN_W ? (UWORD)(bg->room_width - SCREEN_W) : 0;
	if (x > max_x)
		x = max_x;

	alloc_size = PAD_FRONT + plane_size + PAD_BACK;
	alloc = AllocMem(alloc_size, MEMF_CHIP | MEMF_CLEAR);
	copper = AllocMem(COPPER_SIZE + 4, MEMF_CHIP | MEMF_CLEAR);
	if (!alloc || !copper) {
		report("roomview: out of chip memory");
		goto out;
	}
	planes = alloc + PAD_FRONT;
	if (res_read_bg(room_no, planes)) {
		report("roomview: cannot read background");
		goto out;
	}

	cl[0] = copper;
	cl[1] = copper + CL_LONGS;
	for (n = 0; n < 2; n++) {
		build_copper(cl[n], rowbytes, height, clut + 8, (UWORD *)((UBYTE *)copper + COPPER_SIZE));
		set_scroll(cl[n], planes, rowbytes, x);
	}
	report("roomview: room loaded, display on");

	oldview = *(APTR *)((UBYTE *)GfxBase + GFX_ACTIVIEW);
	LoadView(0);
	WaitTOF();
	WaitTOF();
	*(volatile ULONG *)(CUSTOM + 0x080 / 2) = (ULONG)cl[0];	/* COP1LC */
	REG(0x088) = 0;							/* COPJMP1 */
	REG(0x096) = 0x8380;	/* DMACON: LoadView(NULL) may leave bitplane DMA off */

	for (n = 0; frames == 0 || n < frames; n++) {
		WaitTOF();
		if (!(CIAA_PRA & 0x40))
			break;
		if (fixed || !max_x)
			continue;
		/* the copper restarts from COP1LC at the next vblank; the list
		 * running now is never touched */
		if ((dx > 0 && x >= max_x) || (dx < 0 && x == 0))
			dx = (WORD)-dx;
		x = (UWORD)(x + dx);
		cur ^= 1;
		set_scroll(cl[cur], planes, rowbytes, x);
		*(volatile ULONG *)(CUSTOM + 0x080 / 2) = (ULONG)cl[cur];
	}

	*(volatile ULONG *)(CUSTOM + 0x080 / 2) = *(ULONG *)((UBYTE *)GfxBase + GFX_COPINIT);
	LoadView(oldview);
	WaitTOF();
	WaitTOF();
	report("roomview: done");
	rc = 0;

out:
	if (copper)
		FreeMem(copper, COPPER_SIZE + 4);
	if (alloc)
		FreeMem(alloc, alloc_size);
	res_close();
	if (GfxBase)
		CloseLibrary(GfxBase);
	return rc;
}
