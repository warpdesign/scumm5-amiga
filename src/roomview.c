/* scumm5-amiga - milestone 1: show a converted room on AGA, 8 bitplanes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage: roomview FILE.pln [FRAMES [X]]
 * Shows the room until the left mouse button is pressed, or for FRAMES
 * frames if FRAMES is non-zero. Rooms wider than the screen scroll back and
 * forth with the hardware, or stay at column X if given.
 *
 * Scrolling: bitplane pointers move in 64 px steps (4x fetch granularity)
 * and BPLCON1 delays the picture by 0-63 px. Fetch starts one 64 px block
 * early so the delayed left edge has data.
 */
#include "amiga.h"

APTR DOSBase;
APTR GfxBase;

#define PLANES 8
#define HDR_SIZE (12 + 768)
#define SCREEN_W 320
#define DDF_START 0x18		/* $38 minus one 64 px block */
#define FETCH_BYTES 56		/* 4x fetch, DDF $18-$d0: 7 blocks of 64 px */
#define BLOCK_BYTES 8
#define PAD_FRONT BLOCK_BYTES	/* x < 64 fetches the block before plane 0 */
#define PAD_BACK 16		/* the last fetch can run up to 127 px past a row */
#define DIW_TOP 0x2c

static UWORD cl_bplpt, cl_bplcon1;	/* word index of the patched copper values */

static UWORD hdr_word(const UBYTE *p) { return (UWORD)(p[0] << 8 | p[1]); }

static void uae_log(const char *s)
{
	typedef LONG (*uaelib_fn)(LONG, const char *);
	uaelib_fn uae = (uaelib_fn)0xf0ff60;
	UWORD op = *(volatile UWORD *)0xf0ff60;
	if (op == 0x4eb9 || op == 0xa00e)
		uae(86, s);
}

static void report(const char *s)
{
	PutStr(s);
	PutStr("\n");
	uae_log(s);
}

static ULONG parse_uint(const char **pp)
{
	const char *p = *pp;
	ULONG v = 0;
	while (*p >= '0' && *p <= '9')
		v = v * 10 + (ULONG)(*p++ - '0');
	*pp = p;
	return v;
}

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

int amiga_main(const char *args, ULONG arglen)
{
	char name[108];
	UBYTE hdr[HDR_SIZE];
	const char *p = args;
	ULONG frames = 0, n = 0, plane_size, alloc_size = 0;
	UWORD width, height, room_w, rowbytes, x = 0, max_x;
	WORD dx = 1;
	int fixed = 0, cur = 0;
	UBYTE *alloc = 0, *planes;
	ULONG *copper = 0, *cl[2];
	APTR oldview;
	LONG fh;
	int rc = 20;

	(void)arglen;
	while (*p == ' ')
		p++;
	while (*p > ' ' && n < sizeof(name) - 1)
		name[n++] = *p++;
	name[n] = 0;
	while (*p == ' ')
		p++;
	frames = parse_uint(&p);
	while (*p == ' ')
		p++;
	if (*p >= '0' && *p <= '9') {
		x = (UWORD)parse_uint(&p);
		fixed = 1;
	}

	DOSBase = OpenLibrary("dos.library", 36);
	if (!DOSBase)
		return 20;
	GfxBase = OpenLibrary("graphics.library", 39);
	if (!GfxBase) {
		report("roomview: graphics.library V39 (AGA) required");
		goto out;
	}
	if ((REG(0x07c) & 0xff) != 0xf8) {
		report("roomview: AGA chipset required");
		goto out;
	}

	fh = Open(name, MODE_OLDFILE);
	if (!fh) {
		report("roomview: cannot open file");
		goto out;
	}
	if (Read(fh, hdr, HDR_SIZE) != HDR_SIZE || hdr[0] != 'P' || hdr[1] != 'L' || hdr[2] != 'N' || hdr[3] != '8') {
		report("roomview: not a PLN8 file");
		Close(fh);
		goto out;
	}
	width = hdr_word(hdr + 4);
	height = hdr_word(hdr + 6);
	room_w = hdr_word(hdr + 8);
	rowbytes = width / 8;
	plane_size = (ULONG)rowbytes * PLANES * height;
	max_x = room_w > SCREEN_W ? (UWORD)(room_w - SCREEN_W) : 0;
	if (x > max_x)
		x = max_x;

	alloc_size = PAD_FRONT + plane_size + PAD_BACK;
	alloc = AllocMem(alloc_size, MEMF_CHIP | MEMF_CLEAR);
	copper = AllocMem(COPPER_SIZE + 4, MEMF_CHIP | MEMF_CLEAR);
	if (!alloc || !copper) {
		report("roomview: out of chip memory");
		Close(fh);
		goto out;
	}
	planes = alloc + PAD_FRONT;
	if (Read(fh, planes, (LONG)plane_size) != (LONG)plane_size) {
		report("roomview: short read");
		Close(fh);
		goto out;
	}
	Close(fh);

	cl[0] = copper;
	cl[1] = copper + CL_LONGS;
	for (n = 0; n < 2; n++) {
		build_copper(cl[n], rowbytes, height, hdr + 12, (UWORD *)((UBYTE *)copper + COPPER_SIZE));
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
	if (GfxBase)
		CloseLibrary(GfxBase);
	CloseLibrary(DOSBase);
	return rc;
}
