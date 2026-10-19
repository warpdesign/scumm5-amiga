/* scumm5-amiga - classic (v5) costume animation commands
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/costume.cpp (ClassicCostumeLoader:
 * loadCostume, costumeDecodeData, increaseAnims, increaseAnim), ScummVM
 * commit 96b42d42bc8533953bdd701db059cc33c839a9ff, reduced to SCUMM v5.
 * ScummVM is the legal property of its developers, whose names are too
 * numerous to list here; see the COPYRIGHT file distributed with the
 * ScummVM source.
 */
#include "scumm.h"

struct costume_view {
	const uint8_t *baseptr;
	const uint8_t *data_offsets;
	const uint8_t *anim_cmds;
	int num_anim;
};

static int load_costume(int id, struct costume_view *cv)
{
	const uint8_t *ptr = res_get(RT_COSTUME, (uint16_t)id);
	int format, num_colors = 0;

	if (!ptr)
		fatal("costume not found", id, 0);
	ptr += 2;
	cv->baseptr = ptr;
	cv->num_anim = ptr[6];
	format = ptr[7] & 0x7F;
	if (format == 0x58)
		num_colors = 16;
	else if (format == 0x59)
		num_colors = 32;
	else
		fatal("invalid costume format", id, format);
	ptr += 8 + num_colors;
	cv->data_offsets = ptr + 34;
	cv->anim_cmds = cv->baseptr + rd_le16(ptr);
	return 0;
}

void costume_decode_data(struct actor *a, int frame, unsigned usemask)
{
	struct costume_view cv;
	const uint8_t *r;
	unsigned mask, j;
	int i, anim;
	uint8_t extra, cmd;

	load_costume(a->costume, &cv);

	anim = new_dir_to_old_dir(a->facing) + frame * 4;
	if (anim > cv.num_anim)
		return;

	r = cv.baseptr + rd_le16(cv.data_offsets + anim * 2);
	if (r == cv.baseptr)
		return;

	mask = rd_le16(r);
	r += 2;
	i = 0;
	do {
		if (mask & 0x8000) {
			j = rd_le16(r);
			r += 2;
			if (usemask & 0x8000) {
				if (j == 0xFFFF) {
					a->cost.curpos[i] = 0xFFFF;
					a->cost.start[i] = 0;
					a->cost.frame[i] = (uint16_t)anim;
				} else {
					extra = *r++;
					cmd = cv.anim_cmds[j];
					if (cmd == 0x7A) {
						a->cost.stopped &= (uint16_t)~(1 << i);
					} else if (cmd == 0x79) {
						a->cost.stopped |= (uint16_t)(1 << i);
					} else {
						a->cost.curpos[i] = a->cost.start[i] = (uint16_t)j;
						a->cost.end[i] = (uint16_t)(j + (extra & 0x7F));
						if (extra & 0x80)
							a->cost.curpos[i] |= 0x8000;
						a->cost.frame[i] = (uint16_t)anim;
					}
				}
			} else {
				if (j != 0xFFFF)
					r++;
			}
		}
		i++;
		usemask <<= 1;
		mask <<= 1;
	} while (mask & 0xFFFF);
}

static int increase_anim(struct actor *a, const struct costume_view *cv, int slot)
{
	int highflag, i, end;
	uint8_t code, nc;

	if (a->cost.curpos[slot] == 0xFFFF)
		return 0;

	highflag = a->cost.curpos[slot] & 0x8000;
	i = a->cost.curpos[slot] & 0x7FFF;
	end = a->cost.end[slot];
	code = cv->anim_cmds[i] & 0x7F;

	for (;;) {
		if (!highflag) {
			if (i++ >= end)
				i = a->cost.start[slot];
		} else {
			if (i != end)
				i++;
		}
		nc = cv->anim_cmds[i];

		if (nc == 0x7C) {
			a->cost.anim_counter++;
			if (a->cost.start[slot] != end)
				continue;
		} else if (nc == 0x78) {
			a->cost.sound_counter++;
			if (a->cost.start[slot] != end)
				continue;
		}

		a->cost.curpos[slot] = (uint16_t)(i | highflag);
		return (cv->anim_cmds[i] & 0x7F) != code;
	}
}

int costume_increase_anims(struct actor *a)
{
	struct costume_view cv;
	int i, r = 0;

	load_costume(a->costume, &cv);
	for (i = 0; i != 16; i++)
		if (a->cost.curpos[i] != 0xFFFF)
			r |= increase_anim(a, &cv, i);
	return r;
}

/* ---- draw extents ---- */

static const uint8_t small_costume_scale_table[256] = {
	0xFF, 0xFD, 0x7D, 0xBD, 0x3D, 0xDD, 0x5D, 0x9D, 0x1D, 0xED, 0x6D, 0xAD, 0x2D, 0xCD, 0x4D, 0x8D,
	0x0D, 0xF5, 0x75, 0xB5, 0x35, 0xD5, 0x55, 0x95, 0x15, 0xE5, 0x65, 0xA5, 0x25, 0xC5, 0x45, 0x85,
	0x05, 0xF9, 0x79, 0xB9, 0x39, 0xD9, 0x59, 0x99, 0x19, 0xE9, 0x69, 0xA9, 0x29, 0xC9, 0x49, 0x89,
	0x09, 0xF1, 0x71, 0xB1, 0x31, 0xD1, 0x51, 0x91, 0x11, 0xE1, 0x61, 0xA1, 0x21, 0xC1, 0x41, 0x81,
	0x01, 0xFB, 0x7B, 0xBB, 0x3B, 0xDB, 0x5B, 0x9B, 0x1B, 0xEB, 0x6B, 0xAB, 0x2B, 0xCB, 0x4B, 0x8B,
	0x0B, 0xF3, 0x73, 0xB3, 0x33, 0xD3, 0x53, 0x93, 0x13, 0xE3, 0x63, 0xA3, 0x23, 0xC3, 0x43, 0x83,
	0x03, 0xF7, 0x77, 0xB7, 0x37, 0xD7, 0x57, 0x97, 0x17, 0xE7, 0x67, 0xA7, 0x27, 0xC7, 0x47, 0x87,
	0x07, 0xEF, 0x6F, 0xAF, 0x2F, 0xCF, 0x4F, 0x8F, 0x0F, 0xDF, 0x5F, 0x9F, 0x1F, 0xBF, 0x3F, 0x7F,
	0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
	0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
	0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
	0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
	0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
	0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
	0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
	0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE
};

#include "gfx.h"

extern int16_t vs_h[3];

#define LIGHTMODE_ACTOR_USE_COLORS 8

static uint8_t *scratch;	/* chunky main screen: SCREEN_WIDTH x vs h */
static uint8_t *scratch_wr;	/* bit per scratch pixel: written */
static int scratch_h;

struct cel_state {
	int actor_x, actor_y, scale_x, scale_y, to_right, out_h;
	int x_move, y_move;
	int32_t draw_top, draw_bottom;
	int actor_id;
	int shr, mask;
	int zbuf, shadow_mode;
	uint8_t palette[32];
	int has_shadow_color;		/* a non-transparent colour maps to 13 */
	int min_x, max_x, min_y, max_y;
	int draw;			/* 0: extents only */
};

struct rle {
	const uint8_t *src;
	int x, y, skip_width, scale_x_index, scale_y_index, scale_x_step;
	int rep_len, rep_color;
};

static void skip_cel_lines(struct rle *r, int height, int shr, int mask, int num)
{
	num *= height;
	for (;;) {
		r->rep_len = *r->src++;
		r->rep_color = r->rep_len >> shr;
		r->rep_len &= mask;
		if (!r->rep_len)
			r->rep_len = *r->src++;
		if ((num -= r->rep_len) <= 0) {
			r->rep_len = 1 - num;
			return;
		}
	}
}

static int dst_pixel(int x, int y)
{
	uint32_t i = (uint32_t)y * SCREEN_WIDTH + (uint32_t)x;
	if (scratch_wr[i >> 3] & (0x80 >> (i & 7)))
		return scratch[i];
	return planar_get_pixel(vs_pixels(&vscr[0], x, y), (vscr[0].xstart + x) & 7);
}

/* BaseCostumeRenderer::byleRLEDecode into the chunky scratch. Runs of
 * transparent pixels are skipped in one step when the actor is not scaled
 * vertically; this changes nothing visible, only the work done. */
static void rle_decode(struct cel_state *st, struct rle *r, int width, int height)
{
	const uint8_t *table = small_costume_scale_table;
	const uint8_t *src = r->src, *zmask, *cmask;
	int len = r->rep_len, color = r->rep_color;
	int y = r->y, h = height, scale_index_y = r->scale_y_index;
	int bounds_bottom = st->out_h, unscaled_y = st->scale_y == 255;
	int col_min_y = 0x7fff, col_max_y = -1;
	uint8_t maskbit = (uint8_t)(0x80 >> (r->x & 7));

	(void)width;
	zmask = gfx_screen_mask_buffer(0, 0, st->zbuf);
	cmask = gfx_screen_mask_buffer(0, 0, 0);

	if (len)
		goto start;
	for (;;) {
		len = *src++;
		color = len >> st->shr;
		len &= st->mask;
		if (!len)
			len = *src++;
		if (!color && unscaled_y) {
			/* transparent run: advance down the columns */
			for (;;) {
				int n = len < h ? len : h;
				y += n;
				h -= n;
				len -= n;
				if (h)
					break;
				if (!--r->skip_width)
					goto done;
				h = height;
				y = r->y;
				if (st->scale_x == 255 || table[r->scale_x_index] < st->scale_x) {
					if (col_max_y >= 0) {
						if (r->x < st->min_x) st->min_x = r->x;
						if (r->x > st->max_x) st->max_x = r->x;
					}
					r->x += r->scale_x_step;
					if (r->x < 0 || r->x >= SCREEN_WIDTH)
						goto done;
					maskbit = (uint8_t)(0x80 >> (r->x & 7));
				}
				r->scale_x_index = (r->scale_x_index + r->scale_x_step) & 0xFF;
				if (!len)
					break;
			}
			continue;
		}
		do {
			if (unscaled_y || table[scale_index_y++ & 0xFF] < st->scale_y) {
				if (color && y >= 0 && y < bounds_bottom) {
					uint32_t mo = (uint32_t)y * NUM_STRIPS + (uint32_t)(r->x >> 3);
					if (!((zmask[mo] | cmask[mo]) & maskbit)) {
						int pcolor;
						uint32_t i = (uint32_t)y * SCREEN_WIDTH + (uint32_t)r->x;
						if (st->shadow_mode & 0x20) {
							pcolor = g.shadow_palette[dst_pixel(r->x, y)];
						} else {
							pcolor = st->palette[color];
							if (pcolor == 13)
								pcolor = g.shadow_palette[dst_pixel(r->x, y)];
						}
						scratch[i] = (uint8_t)pcolor;
						scratch_wr[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
						if (y < col_min_y)
							col_min_y = y;
						if (y > col_max_y)
							col_max_y = y;
					}
				}
				y++;
			}
			if (!--h) {
				if (col_max_y >= 0) {
					if (r->x < st->min_x) st->min_x = r->x;
					if (r->x > st->max_x) st->max_x = r->x;
				}
				if (!--r->skip_width)
					goto done;
				h = height;
				y = r->y;
				scale_index_y = r->scale_y_index;
				if (st->scale_x == 255 || table[r->scale_x_index] < st->scale_x) {
					r->x += r->scale_x_step;
					if (r->x < 0 || r->x >= SCREEN_WIDTH)
						goto done;
					maskbit = (uint8_t)(0x80 >> (r->x & 7));
				}
				r->scale_x_index = (r->scale_x_index + r->scale_x_step) & 0xFF;
			}
start:;
		} while (--len);
	}
done:
	if (col_max_y >= 0) {
		if (r->x < st->min_x) st->min_x = r->x;
		if (r->x > st->max_x) st->max_x = r->x;
	}
	if (col_min_y < st->min_y)
		st->min_y = col_min_y;
	if (col_max_y > st->max_y)
		st->max_y = col_max_y;
}

/* rle_decode by whole runs: a run of source rows [cy, cy + n) of one
 * colour covers clipped output rows [yclip[cy], yclip[cy + n]), yclip[]
 * advancing on the rows vertical scaling keeps. Extents are conservative
 * (masked pixels count); the flush skips them. */
#define MAX_CEL_H 511
static void rle_decode_runs(struct cel_state *st, struct rle *r, int height)
{
	static int16_t yclip[MAX_CEL_H + 1];
	static uint32_t moff[MAX_CEL_H + 1];
	const uint8_t *table = small_costume_scale_table;
	const uint8_t *src = r->src, *pal = st->palette;
	const uint8_t *zmask = gfx_screen_mask_buffer(0, 0, st->zbuf);
	const uint8_t *cmask = gfx_screen_mask_buffer(0, 0, 0);
	const uint8_t *zc, *cc;
	uint8_t *wc, *sc;
	int len = r->rep_len ? r->rep_len - 1 : 0, color = r->rep_color;
	int h = height, x = r->x, bottom = st->out_h;
	int shr = st->shr, msk = st->mask, scale_x = st->scale_x, scale_y = st->scale_y;
	int skip_width = r->skip_width, sxi = r->scale_x_index, sx_step = r->scale_x_step;
	int min_x = st->min_x, max_x = st->max_x, col_written = 0, any_written = 0;
	int shadow_all = (st->shadow_mode & 0x20) != 0;
	/* 0: no masks, 1: z-plane or charset mask only (zc), 2: both */
	int mode = zmask != cmask ? (charset_mask_used ? 2 : 1) : (charset_mask_used ? 1 : 0);
	uint8_t maskbit = (uint8_t)(0x80 >> (x & 7));
	int k, oy = r->y;

	/* source row k starts at clipped output row yclip[k] */
	for (k = 0; k <= height; k++) {
		yclip[k] = (int16_t)(oy < 0 ? 0 : oy > bottom ? bottom : oy);
		moff[k] = (uint32_t)yclip[k] * NUM_STRIPS;
		if (k < height && (scale_y == 255 || table[(r->scale_y_index + k) & 0xFF] < scale_y))
			oy++;
	}
	zc = zmask + (x >> 3);
	cc = cmask + (x >> 3);
	wc = scratch_wr + (x >> 3);
	sc = scratch + x;
	for (;;) {
		while (len) {
			int n = len < h ? len : h, cy = height - h;
			if (color) {
				int ya = yclip[cy], yb = yclip[cy + n];
				if (ya < yb) {
					uint32_t off = moff[cy];
					const uint8_t *zm = zc + off, *cm = cc + off;
					uint8_t *wp = wc + off, *sp = sc + (off << 3);
					int pc = pal[color], yy = yb - ya;
					if (shadow_all || pc == 13) {
						for (yy = ya; yy < yb; yy++, zm += NUM_STRIPS, cm += NUM_STRIPS, wp += NUM_STRIPS, sp += SCREEN_WIDTH)
							if (!((*zm | *cm) & maskbit)) {
								*sp = g.shadow_palette[dst_pixel(x, yy)];
								*wp |= maskbit;
							}
					} else if (mode == 0) {
						do {
							*sp = (uint8_t)pc;
							*wp |= maskbit;
							wp += NUM_STRIPS;
							sp += SCREEN_WIDTH;
						} while (--yy);
					} else if (mode == 1) {
						do {
							if (!(*zm & maskbit)) {
								*sp = (uint8_t)pc;
								*wp |= maskbit;
							}
							zm += NUM_STRIPS;
							wp += NUM_STRIPS;
							sp += SCREEN_WIDTH;
						} while (--yy);
					} else {
						do {
							if (!((*zm | *cm) & maskbit)) {
								*sp = (uint8_t)pc;
								*wp |= maskbit;
							}
							zm += NUM_STRIPS;
							cm += NUM_STRIPS;
							wp += NUM_STRIPS;
							sp += SCREEN_WIDTH;
						} while (--yy);
					}
					col_written = 1;
				}
			}
			h -= n;
			len -= n;
			if (!h) {
				if (col_written) {
					if (x < min_x) min_x = x;
					if (x > max_x) max_x = x;
					col_written = 0;
					any_written = 1;
				}
				if (!--skip_width)
					goto done;
				h = height;
				if (scale_x == 255 || table[sxi] < scale_x) {
					x += sx_step;
					if (x < 0 || x >= SCREEN_WIDTH)
						goto done;
					maskbit = (uint8_t)(0x80 >> (x & 7));
					zc = zmask + (x >> 3);
					cc = cmask + (x >> 3);
					wc = scratch_wr + (x >> 3);
					sc = scratch + x;
				}
				sxi = (sxi + sx_step) & 0xFF;
			}
		}
		len = *src++;
		color = len >> shr;
		len &= msk;
		if (!len)
			len = *src++;
	}
done:
	if (col_written) {
		if (x < min_x) min_x = x;
		if (x > max_x) max_x = x;
		any_written = 1;
	}
	st->min_x = min_x;
	st->max_x = max_x;
	if (any_written) {
		if (yclip[0] < st->min_y)
			st->min_y = yclip[0];
		if (yclip[height] - 1 > st->max_y)
			st->max_y = yclip[height] - 1;
	}
}

/* Fast path for unscaled, unshadowed cels that fit vertically: one run
 * table entry gives a byte's colour (palette applied), its length (0: the
 * next byte holds it) and, in bit 7, transparency. Field order and widths
 * are shared with costume68k.s. */
struct cost_runs {
	const uint8_t *src;
	uint8_t *sp, *wp;
	const uint8_t *zm, *cm, *tab;
	int32_t len, pc, transparent;
	int32_t height;			/* bit 31 set: columns go left */
	int32_t columns, mode, maskbit;
};

#define RUN_TABS 4
static struct {
	uint8_t tab[512], pal[32];
	int shr;
	uint32_t used;
} run_tabs[RUN_TABS];
static uint32_t run_tab_clock;

static const uint8_t *get_run_tab(const struct cel_state *st)
{
	int b, i, lru = 0;

	for (i = 0; i < RUN_TABS; i++) {
		if (run_tabs[i].used && run_tabs[i].shr == st->shr && !memcmp(run_tabs[i].pal, st->palette, 32)) {
			run_tabs[i].used = ++run_tab_clock;
			return run_tabs[i].tab;
		}
		if (run_tabs[i].used < run_tabs[lru].used)
			lru = i;
	}
	for (b = 0; b < 256; b++) {
		int c = b >> st->shr;
		run_tabs[lru].tab[b] = st->palette[c];
		run_tabs[lru].tab[256 + b] = (uint8_t)((b & st->mask) | (c ? 0 : 0x80));
	}
	memcpy(run_tabs[lru].pal, st->palette, 32);
	run_tabs[lru].shr = st->shr;
	run_tabs[lru].used = ++run_tab_clock;
	return run_tabs[lru].tab;
}

#ifdef __mc68000__
void cost_runs(const struct cost_runs *a);
void cost_flush_row(uint8_t *wr, const uint8_t *c, uint8_t *d, int n);
#else
/* C model of costume68k.s cost_runs */
static void cost_runs(const struct cost_runs *a)
{
	const uint8_t *src = a->src, *zm = a->zm, *cm = a->cm, *tab = a->tab;
	uint8_t *sp = a->sp, *wp = a->wp;
	int len = a->len, pc = a->pc, transparent = a->transparent;
	int height = a->height & 0xffff, left = a->height < 0, h = height;
	int columns = a->columns, mode = a->mode;
	uint8_t maskbit = (uint8_t)a->maskbit;

	for (;;) {
		while (len) {
			int n = len < h ? len : h, k;
			len -= n;
			h -= n;
			for (k = 0; k < n; k++) {
				if (!transparent) {
					uint8_t m = mode == 0 ? 0 : mode == 1 ? *zm : (uint8_t)(*zm | *cm);
					if (!(m & maskbit)) {
						*sp = (uint8_t)pc;
						*wp |= maskbit;
					}
				}
				sp += SCREEN_WIDTH;
				wp += NUM_STRIPS;
				zm += NUM_STRIPS;
				cm += NUM_STRIPS;
			}
			if (!h) {
				if (!--columns)
					return;
				h = height;
				sp -= (uint32_t)height * SCREEN_WIDTH;
				wp -= (uint32_t)height * NUM_STRIPS;
				zm -= (uint32_t)height * NUM_STRIPS;
				cm -= (uint32_t)height * NUM_STRIPS;
				if (left) {
					sp--;
					if (maskbit == 0x80) {
						maskbit = 1;
						wp--, zm--, cm--;
					} else {
						maskbit <<= 1;
					}
				} else {
					sp++;
					if (maskbit == 1) {
						maskbit = 0x80;
						wp++, zm++, cm++;
					} else {
						maskbit >>= 1;
					}
				}
			}
		}
		{
			int b = *src++;
			pc = tab[b];
			len = tab[256 + b] & 0x7f;
			transparent = tab[256 + b] >> 7;
			if (!len)
				len = *src++;
		}
	}
}
#endif

/* Draws a cel through cost_runs when it qualifies. Extents are the whole
 * cel rectangle: the flush skips bytes nothing was written to. */
static int rle_decode_fast(struct cel_state *st, struct rle *r, int height)
{
	struct cost_runs a;
	int x = r->x, y = r->y, columns, last;
	const uint8_t *zmask, *cmask;

	if (st->scale_x != 255 || st->scale_y != 255 || st->shadow_mode & 0x20 || st->has_shadow_color)
		return 0;
	if (y < 0 || y + height > st->out_h || height <= 0 || height > 0xffff || x < 0 || x >= SCREEN_WIDTH)
		return 0;
	columns = r->scale_x_step > 0 ? SCREEN_WIDTH - x : x + 1;
	if (columns > r->skip_width)
		columns = r->skip_width;
	if (columns <= 0)
		return 1;

	zmask = gfx_screen_mask_buffer(0, 0, st->zbuf);
	cmask = gfx_screen_mask_buffer(0, 0, 0);
	a.src = r->src;
	a.sp = scratch + (uint32_t)y * SCREEN_WIDTH + (uint32_t)x;
	a.wp = scratch_wr + (uint32_t)y * NUM_STRIPS + (uint32_t)(x >> 3);
	a.mode = zmask != cmask ? (charset_mask_used ? 2 : 1) : (charset_mask_used ? 1 : 0);
	a.zm = (a.mode ? zmask : cmask) + (uint32_t)y * NUM_STRIPS + (uint32_t)(x >> 3);
	a.cm = cmask + (uint32_t)y * NUM_STRIPS + (uint32_t)(x >> 3);
	a.tab = get_run_tab(st);
	a.len = r->rep_len ? r->rep_len - 1 : 0;
	a.pc = st->palette[r->rep_color];
	a.transparent = r->rep_color == 0;
	a.height = (int32_t)((uint32_t)height | (r->scale_x_step < 0 ? 0x80000000u : 0));
	a.columns = columns;
	a.maskbit = 0x80 >> (x & 7);
	cost_runs(&a);

	last = x + r->scale_x_step * (columns - 1);
	if ((x < last ? x : last) < st->min_x)
		st->min_x = x < last ? x : last;
	if ((x > last ? x : last) > st->max_x)
		st->max_x = x > last ? x : last;
	if (y < st->min_y)
		st->min_y = y;
	if (y + height - 1 > st->max_y)
		st->max_y = y + height - 1;
	return 1;
}

/* BaseCostumeRenderer::paintCelByleRLECommon and ClassicCostumeRenderer::
 * paintCelByleRLE: cel rectangle, strip usage, extent, then pixels. */
static int cel_paint(struct cel_state *st, const uint8_t *src, int x_move_cur, int y_move_cur, int width, int height)
{
	const uint8_t *table = small_costume_scale_table;
	int scaled = st->scale_x != 0xFF || st->scale_y != 0xFF;
	int i, j, step, lines_to_skip = 0, draw_flag = 1;
	int start_x = 128, start_y = 128;
	int left, right, top, bottom;
	int bounds_right = SCREEN_WIDTH, bounds_bottom = st->out_h;
	struct rle r;

	r.src = src;
	r.x = st->actor_x;
	r.y = st->actor_y;

	if (scaled) {
		int scale_x_step = -1;
		if (x_move_cur < 0) {
			x_move_cur = -x_move_cur;
			scale_x_step = 1;
		}
		if (st->to_right) {
			start_x = j = (128 - x_move_cur) & 0xFF;
			for (i = 0; i < x_move_cur; i++)
				if (table[j++ & 0xFF] < st->scale_x)
					r.x -= scale_x_step;
			left = right = r.x;
			j = start_x;
			for (i = 0; i < width; i++) {
				if (right < 0) {
					lines_to_skip++;
					start_x = j;
				}
				if (table[j++ & 0xFF] < st->scale_x)
					right++;
			}
		} else {
			start_x = j = (128 + x_move_cur) & 0xFF;
			for (i = 0; i < x_move_cur; i++)
				if (table[j-- & 0xFF] < st->scale_x)
					r.x += scale_x_step;
			left = right = r.x;
			j = start_x;
			for (i = 0; i < width; i++) {
				if (left >= bounds_right) {
					start_x = j;
					lines_to_skip++;
				}
				if (table[j-- & 0xFF] < st->scale_x)
					left--;
			}
		}
		if (lines_to_skip)
			lines_to_skip--;

		step = -1;
		if (y_move_cur < 0) {
			y_move_cur = -y_move_cur;
			step = -step;
		}
		start_y = j = (128 - y_move_cur) & 0xFF;
		for (i = 0; i < y_move_cur; i++)
			if (table[j++ & 0xFF] < st->scale_y)
				r.y -= step;
		top = bottom = r.y;
		j = start_y;
		for (i = 0; i < height; i++)
			if (table[j++ & 0xFF] < st->scale_y)
				bottom++;
	} else {
		if (!st->to_right)
			x_move_cur = -x_move_cur;
		r.x += x_move_cur;
		r.y += y_move_cur;
		if (st->to_right) {
			left = r.x;
			right = r.x + width;
		} else {
			left = r.x - width;
			right = r.x;
		}
		top = r.y;
		bottom = top + height;
	}

	r.scale_x_index = start_x;
	r.scale_y_index = start_y;
	r.skip_width = width;
	r.scale_x_step = st->to_right ? 1 : -1;
	r.rep_len = 0;
	r.rep_color = 0;

	mark_rect_as_dirty(left, right + 1, top, bottom, st->actor_id);

	if (top >= bounds_bottom || bottom <= 0)
		return 0;
	if (left >= bounds_right || right <= 0)
		return 0;

	if (st->to_right) {
		if (!scaled)
			lines_to_skip = 0 - r.x;
		if (lines_to_skip > 0) {
			r.skip_width -= lines_to_skip;
			skip_cel_lines(&r, height, st->shr, st->mask, lines_to_skip);
			r.x = 0;
		} else {
			lines_to_skip = right - bounds_right;
			if (lines_to_skip <= 0)
				draw_flag = 2;
			else
				r.skip_width -= lines_to_skip;
		}
	} else {
		if (!scaled)
			lines_to_skip = right - bounds_right;
		if (lines_to_skip > 0) {
			r.skip_width -= lines_to_skip;
			skip_cel_lines(&r, height, st->shr, st->mask, lines_to_skip);
			r.x = bounds_right - 1;
		} else {
			lines_to_skip = (0 - 1) - left;
			if (lines_to_skip <= 0)
				draw_flag = 2;
			else
				r.skip_width -= lines_to_skip;
		}
	}
	if (r.skip_width <= 0)
		return 0;

	if (top < 0)
		top = 0;
	if (top > bounds_bottom)
		top = bounds_bottom;
	if (bottom > bounds_bottom)
		bottom = bounds_bottom;
	if (st->draw_top > top)
		st->draw_top = top;
	if (st->draw_bottom < bottom)
		st->draw_bottom = bottom;
	if (height + top >= 256)
		return 2;

	if (st->draw && !rle_decode_fast(st, &r, height)) {
		if (height <= MAX_CEL_H)
			rle_decode_runs(st, &r, height);
		else
			rle_decode(st, &r, width, height);
	}
	return draw_flag;
}

static void flush_scratch(struct cel_state *st)
{
	int y, s, s0 = st->min_x / 8, s1 = st->max_x / 8;

	if (st->min_x > st->max_x)
		return;
	for (y = st->min_y; y <= st->max_y; y++) {
		uint8_t *wr = scratch_wr + (uint32_t)y * NUM_STRIPS + s0;
		const uint8_t *c = scratch + (uint32_t)y * SCREEN_WIDTH + (uint32_t)s0 * 8;
		uint8_t *d = vs_pixels(&vscr[0], s0 * 8, y);
#ifdef __mc68000__
		(void)s;
		cost_flush_row(wr, c, d, s1 - s0 + 1);
#else
		for (s = s0; s <= s1; s++, wr++, c += 8, d++)
			if (*wr) {
				planar_put8(d, c, *wr);
				*wr = 0;
			}
#endif
	}
}

/* Actor::drawActorCostume: draws the actor into the main screen, records
 * its strips and vertical extent. Returns the costume renderer's flags. */
int costume_draw(struct actor *a)
{
	const uint8_t *ptr = res_get(RT_COSTUME, a->costume), *base, *frame_offsets, *anim_cmds;
	struct cel_state st;
	int limb, result = 0, num_colors, mirror, i;

	if (!ptr)
		fatal("costume not found", a->costume, 0);

	if (!scratch || scratch_h < vscr[0].h) {
		plat_free(scratch, (uint32_t)SCREEN_WIDTH * (uint32_t)scratch_h);
		plat_free(scratch_wr, (uint32_t)SCREEN_WIDTH / 8 * (uint32_t)scratch_h);
		scratch_h = vscr[0].h;
		scratch = plat_alloc((uint32_t)SCREEN_WIDTH * (uint32_t)scratch_h, 0);
		scratch_wr = plat_alloc((uint32_t)SCREEN_WIDTH / 8 * (uint32_t)scratch_h, PLAT_CLEAR);
		if (!scratch || !scratch_wr)
			fatal("out of memory for actor scratch", scratch_h, 0);
		ptr = res_get(RT_COSTUME, a->costume);
	}

	base = ptr + 2;
	num_colors = (base[7] & 0x7F) == 0x58 ? 16 : 32;
	mirror = (base[7] & 0x80) != 0;
	frame_offsets = base + 8 + num_colors + 2;
	anim_cmds = base + rd_le16(base + 8 + num_colors);

	st.actor_id = a->number;
	st.actor_x = a->pos.x - vscr[0].xstart;
	st.actor_y = a->pos.y - a->elevation;
	st.scale_x = a->scalex;
	st.scale_y = a->scaley;
	st.to_right = new_dir_to_old_dir(a->facing) != 0 || mirror;
	st.out_h = vs_h[0];
	st.x_move = st.y_move = 0;
	st.draw_top = 0x7fffffff;
	st.draw_bottom = 0;
	st.shr = num_colors == 32 ? 3 : 4;
	st.mask = num_colors == 32 ? 7 : 15;
	st.shadow_mode = a->shadow_mode;
	st.min_x = st.min_y = 0x7fff;
	st.max_x = st.max_y = -1;
	st.draw = 1;

	if (a->force_clip)
		st.zbuf = a->force_clip;
	else if (get_class(a->number, CLS_NEVER_CLIP))
		st.zbuf = 0;
	else {
		st.zbuf = get_mask_from_box(a->walkbox);
		if (st.zbuf > num_zbuffer - 1)
			st.zbuf = num_zbuffer - 1;
	}

	memset(st.palette, 0, sizeof(st.palette));
	if (VAR(VAR_CURRENT_LIGHTS) & LIGHTMODE_ACTOR_USE_COLORS) {
		for (i = 0; i < num_colors; i++) {
			uint8_t c = a->palette[i];
			st.palette[i] = c == 255 ? base[8 + i] : c;
		}
	} else {
		memset(st.palette, 8, (size_t)num_colors);
		st.palette[12] = 0;
	}

	st.has_shadow_color = 0;
	for (i = 1; i < num_colors; i++)
		if (st.palette[i] == 13)
			st.has_shadow_color = 1;

	for (limb = 0; limb < 16; limb++) {
		const uint8_t *frameptr, *src;
		int code;

		if (a->cost.curpos[limb] == 0xFFFF || (a->cost.stopped & (1 << limb)))
			continue;
		i = a->cost.curpos[limb] & 0x7FFF;
		frameptr = base + rd_le16(frame_offsets + limb * 2);
		code = anim_cmds[i] & 0x7F;
		if (code == 0x7B)
			continue;
		src = base + rd_le16(frameptr + code * 2);
		{
			int width = rd_le16(src), height = rd_le16(src + 2);
			int x_move_cur = st.x_move + (int16_t)rd_le16(src + 4);
			int y_move_cur = st.y_move + (int16_t)rd_le16(src + 6);
			st.x_move += (int16_t)rd_le16(src + 8);
			st.y_move -= (int16_t)rd_le16(src + 10);
			result |= cel_paint(&st, src + 12, x_move_cur, y_move_cur, width, height);
		}
	}
	{ extern void (*prof_hook)(int); if (prof_hook) prof_hook(12); }
	flush_scratch(&st);
	a->top = st.draw_top;
	a->bottom = st.draw_bottom;
	return result;
}
