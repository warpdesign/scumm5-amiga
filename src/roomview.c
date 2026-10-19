/* scumm5-amiga - milestone 1: show a converted room on AGA, 8 bitplanes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage: roomview FILE.pln [FRAMES]
 * Shows the room until the left mouse button is pressed, or for FRAMES
 * frames if given.
 */
#include "amiga.h"

APTR DOSBase;
APTR GfxBase;

#define PLANES 8
#define HDR_SIZE (12 + 768)
#define FETCH_BYTES 48		/* 4x fetch, DDFSTOP $d0: 6 blocks of 64 px */
#define DIW_TOP 0x2c

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

static ULONG *build_copper(ULONG *cl, UBYTE *planes, UWORD rowbytes, UWORD height,
			   const UBYTE *pal, UWORD *nullspr)
{
	UWORD i, b;
	ULONG p;

	for (i = 0; i < 8; i++) {
		cl = move(cl, 0x120 + i * 4, (UWORD)((ULONG)nullspr >> 16));
		cl = move(cl, 0x122 + i * 4, (UWORD)(ULONG)nullspr);
	}

	cl = move(cl, 0x08e, DIW_TOP << 8 | 0x81);			/* DIWSTRT */
	cl = move(cl, 0x090, (UWORD)(((DIW_TOP + height) & 0xff) << 8 | 0xc1)); /* DIWSTOP */
	cl = move(cl, 0x092, 0x0038);					/* DDFSTRT */
	cl = move(cl, 0x094, 0x00d0);					/* DDFSTOP */
	cl = move(cl, 0x100, 0x0211);			/* BPLCON0: 8 planes, COLOR, ECSENA */
	cl = move(cl, 0x102, 0x0000);					/* BPLCON1 */
	cl = move(cl, 0x104, 0x0000);					/* BPLCON2 */
	cl = move(cl, 0x10c, 0x0011);					/* BPLCON4 */
	cl = move(cl, 0x1fc, 0x0003);			/* FMODE: 4x bitplane fetch */
	cl = move(cl, 0x108, (UWORD)(rowbytes * PLANES - FETCH_BYTES)); /* BPL1MOD */
	cl = move(cl, 0x10a, (UWORD)(rowbytes * PLANES - FETCH_BYTES)); /* BPL2MOD */

	for (i = 0; i < PLANES; i++) {
		p = (ULONG)planes + (ULONG)i * rowbytes;
		cl = move(cl, 0x0e0 + i * 4, (UWORD)(p >> 16));
		cl = move(cl, 0x0e2 + i * 4, (UWORD)p);
	}

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

	*cl++ = 0xfffffffe;
	return cl;
}

#define COPPER_SIZE 4096

int amiga_main(const char *args, ULONG arglen)
{
	char name[108];
	UBYTE hdr[HDR_SIZE];
	const char *p = args;
	ULONG frames = 0, n = 0, plane_size;
	UWORD width, height, rowbytes;
	UBYTE *planes = 0;
	ULONG *copper = 0;
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
	rowbytes = width / 8;
	plane_size = (ULONG)rowbytes * PLANES * height;

	planes = AllocMem(plane_size, MEMF_CHIP);
	copper = AllocMem(COPPER_SIZE + 4, MEMF_CHIP | MEMF_CLEAR);
	if (!planes || !copper) {
		report("roomview: out of chip memory");
		Close(fh);
		goto out;
	}
	if (Read(fh, planes, (LONG)plane_size) != (LONG)plane_size) {
		report("roomview: short read");
		Close(fh);
		goto out;
	}
	Close(fh);

	/* last 4 bytes of the copper buffer double as an empty sprite */
	build_copper(copper, planes, rowbytes, height, hdr + 12, (UWORD *)((UBYTE *)copper + COPPER_SIZE));
	report("roomview: room loaded, display on");

	oldview = *(APTR *)((UBYTE *)GfxBase + GFX_ACTIVIEW);
	LoadView(0);
	WaitTOF();
	WaitTOF();
	*(volatile ULONG *)(CUSTOM + 0x080 / 2) = (ULONG)copper;	/* COP1LC */
	REG(0x088) = 0;							/* COPJMP1 */
	REG(0x096) = 0x8380;	/* DMACON: LoadView(NULL) may leave bitplane DMA off */

	for (n = 0; frames == 0 || n < frames; n++) {
		WaitTOF();
		if (!(CIAA_PRA & 0x40))
			break;
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
	if (planes)
		FreeMem(planes, plane_size);
	if (GfxBase)
		CloseLibrary(GfxBase);
	CloseLibrary(DOSBase);
	return rc;
}
