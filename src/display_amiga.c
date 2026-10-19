/* scumm5-amiga - AGA display: two 320x200 8-bitplane buffers, copper
 * palette, flip at vertical blank, VBL frame counter
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "amiga.h"
#include "scumm/gfx.h"

#define DISP_PLANE 40
#define DISP_PITCH (8 * DISP_PLANE)
#define DISP_SIZE (DISP_PITCH * SCREEN_HEIGHT)
#define FETCH_BYTES 48		/* 4x fetch, DDF $38-$d0 */
#define DIW_TOP 0x2c
#define CL_LONGS 700

extern APTR GfxBase;
void vbl_server(void);

static uint8_t *disp[2];
static ULONG *copper[2];
static uint16_t cl_palette;	/* longword index of the first palette move */
static int back;		/* buffer being drawn */
static int16_t pend_top[2][NUM_STRIPS], pend_bot[2][NUM_STRIPS];
static uint8_t pal_rgb[768];
static uint8_t pal_pending;
static APTR oldview;

static volatile ULONG vbl_count;
static struct Interrupt vbl_int;
static int vbl_rate;

static int blit_owned;

static void wait_blit(void)
{
	(void)REG(0x002);
	while (REG(0x002) & 0x4000)
		;
}

static void set_ptr(UWORD reg, const void *p)
{
	*(volatile ULONG *)(CUSTOM + reg / 2) = (ULONG)p;
}

/* Copies bytes [b0, b1) (word aligned) of n interleaved rows (8 planes of
 * 40 bytes each) from src, whose byte b0 is at src + b0, to dst. An odd
 * source address needs an 8-bit shift: one extra leading word, kept from
 * the destination through the C channel and a zero first word mask. That
 * word would overlap the end of the previous plane's line, whose D write
 * lands after the C read, so shifted copies go one plane at a time. */
static void blit_rows(uint8_t *dst, const uint8_t *src, int b0, int b1, int n)
{
	int words = (b1 - b0) / 2, p;
	const uint8_t *sp = src + b0;
	uint8_t *dp = dst + b0;

	if ((ULONG)sp & 1) {
		words++;
		for (p = 0; p < 8; p++) {
			wait_blit();
			REG(0x040) = 0x07ca;
			REG(0x042) = 0x8000;
			REG(0x044) = 0;
			REG(0x046) = 0xffff;
			REG(0x074) = 0xffff;
			REG(0x060) = (UWORD)(DISP_PITCH - words * 2);
			REG(0x062) = (UWORD)(VS_PITCH - words * 2);
			REG(0x066) = (UWORD)(DISP_PITCH - words * 2);
			set_ptr(0x048, dp - 2 + p * DISP_PLANE);
			set_ptr(0x04c, sp - 1 + p * VS_PLANE);
			set_ptr(0x054, dp - 2 + p * DISP_PLANE);
			REG(0x058) = (UWORD)((n & 1023) << 6 | words);
		}
		return;
	}
	while (n > 0) {
		int rows = n > 128 ? 128 : n;
		wait_blit();
		REG(0x040) = 0x05cc;
		REG(0x042) = 0;
		REG(0x044) = 0xffff;
		REG(0x046) = 0xffff;
		REG(0x074) = 0xffff;
		REG(0x060) = (UWORD)(DISP_PLANE - words * 2);
		REG(0x062) = (UWORD)(VS_PLANE - words * 2);
		REG(0x066) = (UWORD)(DISP_PLANE - words * 2);
		set_ptr(0x048, dp);
		set_ptr(0x04c, sp);
		set_ptr(0x054, dp);
		REG(0x058) = (UWORD)(((rows * 8) & 1023) << 6 | words);
		sp += (ULONG)rows * VS_PITCH;
		dp += (ULONG)rows * DISP_PITCH;
		n -= rows;
	}
}

static ULONG *move(ULONG *cl, UWORD reg, UWORD val)
{
	*cl++ = (ULONG)reg << 16 | val;
	return cl;
}

static void write_palette(ULONG *cl)
{
	int b, i;
	const uint8_t *c;

	cl += cl_palette;
	for (b = 0; b < 8; b++) {
		c = pal_rgb + b * 32 * 3;
		cl = move(cl, 0x106, (UWORD)(b << 13 | 0x0c00));
		for (i = 0; i < 32; i++)
			cl = move(cl, (UWORD)(0x180 + i * 2), (UWORD)((c[i * 3] & 0xf0) << 4 | (c[i * 3 + 1] & 0xf0) | c[i * 3 + 2] >> 4));
		cl = move(cl, 0x106, (UWORD)(b << 13 | 0x0e00));
		for (i = 0; i < 32; i++)
			cl = move(cl, (UWORD)(0x180 + i * 2), (UWORD)((c[i * 3] & 0x0f) << 8 | (c[i * 3 + 1] & 0x0f) << 4 | (c[i * 3 + 2] & 0x0f)));
	}
	cl = move(cl, 0x106, 0x0c00);
	*cl = 0xfffffffe;
}

static void build_copper(ULONG *cl, uint8_t *planes, UWORD *nullspr)
{
	ULONG *start = cl, p;
	int i;

	for (i = 0; i < 8; i++) {
		cl = move(cl, (UWORD)(0x120 + i * 4), (UWORD)((ULONG)nullspr >> 16));
		cl = move(cl, (UWORD)(0x122 + i * 4), (UWORD)(ULONG)nullspr);
	}
	cl = move(cl, 0x08e, DIW_TOP << 8 | 0x81);
	cl = move(cl, 0x090, (UWORD)(((DIW_TOP + SCREEN_HEIGHT) & 0xff) << 8 | 0xc1));
	cl = move(cl, 0x092, 0x0038);
	cl = move(cl, 0x094, 0x00d0);
	cl = move(cl, 0x100, 0x0211);
	cl = move(cl, 0x102, 0x0000);
	cl = move(cl, 0x104, 0x0000);
	cl = move(cl, 0x10c, 0x0011);
	cl = move(cl, 0x1fc, 0x0003);
	cl = move(cl, 0x108, DISP_PITCH - FETCH_BYTES);
	cl = move(cl, 0x10a, DISP_PITCH - FETCH_BYTES);
	for (i = 0; i < 8; i++) {
		p = (ULONG)planes + (ULONG)i * DISP_PLANE;
		cl = move(cl, (UWORD)(0x0e0 + i * 4), (UWORD)(p >> 16));
		cl = move(cl, (UWORD)(0x0e2 + i * 4), (UWORD)p);
	}
	cl_palette = (uint16_t)(cl - start);
	write_palette(start);
}

int display_open(void)
{
	int i;

	if ((REG(0x07c) & 0xff) != 0xf8)
		return -1;
	for (i = 0; i < 2; i++) {
		disp[i] = AllocMem(DISP_SIZE + 16, MEMF_CHIP | MEMF_CLEAR);
		if (disp[i])
			disp[i] += 8;	/* shifted blits touch the word before a row; FMODE 3 needs 8-byte alignment */
		copper[i] = AllocMem(CL_LONGS * 4 + 4, MEMF_CHIP | MEMF_CLEAR);
		if (!disp[i] || !copper[i])
			return -1;
		build_copper(copper[i], disp[i], (UWORD *)(copper[i] + CL_LONGS));
	}

	vbl_rate = *((signed char *)GfxBase + GFX_VBLANK);
	if (vbl_rate != 50 && vbl_rate != 60)
		vbl_rate = 50;
	vbl_int.is_Node.ln_Type = NT_INTERRUPT;
	vbl_int.is_Node.ln_Pri = 0;
	vbl_int.is_Node.ln_Name = "scumm5 vbl";
	vbl_int.is_Data = (APTR)&vbl_count;
	vbl_int.is_Code = vbl_server;
	AddIntServer(INTB_VERTB, &vbl_int);

	OwnBlitter();
	blit_owned = 1;
	oldview = *(APTR *)((UBYTE *)GfxBase + GFX_ACTIVIEW);
	LoadView(0);
	WaitTOF();
	WaitTOF();
	*(volatile ULONG *)(CUSTOM + 0x080 / 2) = (ULONG)copper[1];
	REG(0x088) = 0;
	REG(0x096) = 0x83c0;
	back = 0;
	return 0;
}

void display_close(void)
{
	int i;

	if (oldview || disp[0]) {
		*(volatile ULONG *)(CUSTOM + 0x080 / 2) = *(ULONG *)((UBYTE *)GfxBase + GFX_COPINIT);
		LoadView(oldview);
		WaitTOF();
		WaitTOF();
	}
	if (blit_owned) {
		wait_blit();
		DisownBlitter();
		blit_owned = 0;
	}
	if (vbl_int.is_Code)
		RemIntServer(INTB_VERTB, &vbl_int);
	vbl_int.is_Code = 0;
	for (i = 0; i < 2; i++) {
		if (disp[i])
			FreeMem(disp[i] - 8, DISP_SIZE + 16);
		if (copper[i])
			FreeMem(copper[i], CL_LONGS * 4 + 4);
		disp[i] = 0;
		copper[i] = 0;
	}
	oldview = 0;
}

/* Elapsed time in 1/60 s jiffies (SCUMM's clock), from vertical blanks. */
uint32_t display_jiffies(void)
{
	return (uint32_t)(vbl_count * 60 / (ULONG)vbl_rate);
}

/* Time in scanlines (64 us), for profiling. */
uint32_t display_lines(void)
{
	ULONG v1, v2, pos;
	do {
		v1 = vbl_count;
		pos = *(volatile ULONG *)(CUSTOM + 0x004 / 2);
		v2 = vbl_count;
	} while (v1 != v2);
	return v1 * 313 + ((pos >> 8) & 0x1ff);
}

void display_wait_vbl(void)
{
	WaitTOF();
}

void display_layout(int b, int h)
{
	(void)b;
	(void)h;
}

void display_set_palette(const uint8_t *rgb)
{
	memcpy(pal_rgb, rgb, sizeof(pal_rgb));
	pal_pending = 2;
}

void display_dirty(int v, int strip, int n, int top, int bottom)
{
	const struct vscreen *vs = &vscr[v];
	int y0 = vs->topline + top - g.screen_top, y1 = vs->topline + bottom - g.screen_top;
	int b, s;

	if (y0 < 0)
		y0 = 0;
	if (y1 > SCREEN_HEIGHT)
		y1 = SCREEN_HEIGHT;
	if (y0 >= y1)
		return;
	for (b = 0; b < 2; b++)
		for (s = strip; s < strip + n && s < NUM_STRIPS; s++) {
			if (pend_bot[b][s] == 0 || y0 < pend_top[b][s])
				pend_top[b][s] = (int16_t)y0;
			if (y1 > pend_bot[b][s])
				pend_bot[b][s] = (int16_t)y1;
		}
}

static const struct vscreen *screen_at(int y)
{
	int i;
	for (i = 0; i < 3; i++)
		if (y >= vscr[i].topline && y < vscr[i].topline + vscr[i].h)
			return &vscr[i];
	return 0;
}

/* Copies the pending strips of the back buffer from the virtual screens.
 * Ranges widen to whole words: the virtual screens are the truth, so the
 * extra bytes are copied correctly too. */
static void copy_pending(int b)
{
	int s, e, y, y2, p;

	for (s = 0; s < NUM_STRIPS; s = e) {
		int top = pend_top[b][s], bot = pend_bot[b][s];
		e = s + 1;
		if (!bot)
			continue;
		while (e < NUM_STRIPS && pend_top[b][e] == top && pend_bot[b][e] == bot)
			e++;
		for (y = top; y < bot; y = y2) {
			const struct vscreen *vs = screen_at(y);
			uint8_t *d = disp[b] + (uint32_t)y * DISP_PITCH;
			y2 = vs ? vs->topline + vs->h : y + 1;
			if (y2 > bot)
				y2 = bot;
			if (!vs) {
				for (p = 0; p < 8; p++)
					memset(d + p * DISP_PLANE + s, 0, (size_t)(e - s));
			} else {
				blit_rows(d, vs_pixels(vs, 0, y - vs->topline), s & ~1, (e + 1) & ~1, y2 - y);
			}
		}
		for (y = s; y < e; y++)
			pend_bot[b][y] = 0;
	}
}

void display_copy_planes(uint8_t *dst, const uint8_t *src, int b0, int b1, int n)
{
	ULONG a0 = (ULONG)dst + (ULONG)b0, a1 = (ULONG)dst + (ULONG)b1;
	ULONG w0 = a0 & ~1UL, w1 = (a1 + 1) & ~1UL;
	int words = (int)(w1 - w0) / 2;
	const uint8_t *sp = src + (w0 - (ULONG)dst);
	uint8_t *dp = (uint8_t *)w0;
	UWORD fwm = (a0 & 1) ? 0x00ff : 0xffff, lwm = (a1 & 1) ? 0xff00 : 0xffff;

	if (n <= 0 || b0 >= b1)
		return;
	if (((ULONG)dst ^ (ULONG)src) & 1) {
		display_sync();
		gfx_copy_planes_cpu(dst, src, b0, b1, n);
		return;
	}
	while (n > 0) {
		int rows = n > 128 ? 128 : n;
		wait_blit();
		REG(0x040) = 0x07ca;
		REG(0x042) = 0;
		REG(0x044) = fwm;
		REG(0x046) = lwm;
		REG(0x074) = 0xffff;
		REG(0x060) = (UWORD)(VS_PLANE - words * 2);
		REG(0x062) = (UWORD)(VS_PLANE - words * 2);
		REG(0x066) = (UWORD)(VS_PLANE - words * 2);
		set_ptr(0x048, dp);
		set_ptr(0x04c, sp);
		set_ptr(0x054, dp);
		REG(0x058) = (UWORD)(((rows * 8) & 1023) << 6 | words);
		sp += (ULONG)rows * VS_PITCH;
		dp += (ULONG)rows * VS_PITCH;
		n -= rows;
	}
}

/* One blit per plane: D = A ? B : C, A the edge masks, B the colour bit. */
void display_fill_planes(uint8_t *row0, int x0, int x1, int n, int color)
{
	ULONG a0 = (ULONG)row0 + (ULONG)(x0 >> 3), a1 = (ULONG)row0 + (ULONG)((x1 - 1) >> 3);
	ULONG w0 = a0 & ~1UL, w1 = a1 & ~1UL;
	int words = (int)(w1 - w0) / 2 + 1, p;
	int b0 = (int)((a0 - w0) * 8) + (x0 & 7), b1 = (int)((a1 - w1) * 8) + ((x1 - 1) & 7);
	UWORD fwm = (UWORD)(0xffff >> b0), lwm = (UWORD)(0xffff << (15 - b1));

	if (n <= 0 || x1 <= x0)
		return;
	if (n > 1023) {
		display_sync();
		gfx_fill_planes_cpu(row0, x0, x1, n, color);
		return;
	}
	for (p = 0; p < 8; p++) {
		uint8_t *dp = (uint8_t *)w0 + p * VS_PLANE;
		wait_blit();
		REG(0x040) = 0x03ca;
		REG(0x042) = 0;
		REG(0x044) = fwm;
		REG(0x046) = lwm;
		REG(0x074) = 0xffff;
		REG(0x072) = (color >> p) & 1 ? 0xffff : 0;
		REG(0x060) = (UWORD)(VS_PITCH - words * 2);
		REG(0x066) = (UWORD)(VS_PITCH - words * 2);
		set_ptr(0x048, dp);
		set_ptr(0x054, dp);
		REG(0x058) = (UWORD)((n & 1023) << 6 | words);
	}
}

void display_sync(void)
{
	wait_blit();
}

void display_present(void)
{
	copy_pending(back);
	if (pal_pending) {
		write_palette(copper[back]);
		pal_pending--;
	}
	*(volatile ULONG *)(CUSTOM + 0x080 / 2) = (ULONG)copper[back];
	back ^= 1;
}
