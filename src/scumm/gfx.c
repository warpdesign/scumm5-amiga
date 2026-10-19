/* scumm5-amiga - virtual screens, room/object strips, masks (planar)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/gfx.cpp (VirtScreen handling,
 * drawBitmap, decompressBitmap and the strip codecs, decodeMask,
 * redrawBGAreas, resetBackground, restoreBackground, drawBox,
 * updateDirtyScreen) and object.cpp (drawRoomObject(s), drawObject),
 * ScummVM commit 96b42d42bc8533953bdd701db059cc33c839a9ff, reduced to the
 * DOS SCUMM v5 games. ScummVM is the legal property of its developers,
 * whose names are too numerous to list here; see the COPYRIGHT file
 * distributed with the ScummVM source.
 *
 * Pixels are kept as interleaved bitplanes instead of chunky bytes: a row
 * is 8 planes of 40 bytes, so one SCUMM strip (8 pixels) is one byte per
 * plane. ScummVM's scrolling trick (moving xstart through a linear buffer)
 * carries over unchanged, in bytes.
 */
#include "scumm.h"
#include "gfx.h"

struct vscreen vscr[3];
static uint8_t *mask_buf;
uint8_t charset_mask_used;
static uint32_t mask_itemsize;
static uint32_t img_buf_offs[8];
int num_zbuffer = 1;
int gfx_zbuffer_disabled;
static uint8_t transparent_color = 255;
static uint32_t im00_offs;

extern int16_t vs_topline[3], vs_h[3];

/* ---- helpers ---- */

static int is_light_on(void)
{
	return (VAR(VAR_CURRENT_LIGHTS) & 2) != 0;
}

/* 8x8 bit transpose (Hacker's Delight): 8 chunky pixels to 8 plane bytes. */
static void c2p8(const uint8_t *c, uint8_t *plane)
{
#ifdef __mc68000__
	uint32_t x = *(const uint32_t *)c, y = *(const uint32_t *)(c + 4);
#else
	uint32_t x = (uint32_t)c[0] << 24 | (uint32_t)c[1] << 16 | (uint32_t)c[2] << 8 | c[3];
	uint32_t y = (uint32_t)c[4] << 24 | (uint32_t)c[5] << 16 | (uint32_t)c[6] << 8 | c[7];
#endif
	uint32_t t;

	t = (x ^ (x >> 7)) & 0x00AA00AA;
	x = x ^ t ^ (t << 7);
	t = (y ^ (y >> 7)) & 0x00AA00AA;
	y = y ^ t ^ (t << 7);
	t = (x ^ (x >> 14)) & 0x0000CCCC;
	x = x ^ t ^ (t << 14);
	t = (y ^ (y >> 14)) & 0x0000CCCC;
	y = y ^ t ^ (t << 14);
	t = (x & 0xF0F0F0F0) | ((y >> 4) & 0x0F0F0F0F);
	y = ((x << 4) & 0xF0F0F0F0) | (y & 0x0F0F0F0F);
	x = t;
	/* bit row j holds bit 7-j of every pixel */
	plane[7] = (uint8_t)(x >> 24);
	plane[6] = (uint8_t)(x >> 16);
	plane[5] = (uint8_t)(x >> 8);
	plane[4] = (uint8_t)x;
	plane[3] = (uint8_t)(y >> 24);
	plane[2] = (uint8_t)(y >> 16);
	plane[1] = (uint8_t)(y >> 8);
	plane[0] = (uint8_t)y;
}

/* Writes 8 chunky pixels into the planes at dst (plane 0 byte), where m has
 * a bit set for every pixel to write. */
void planar_put8(uint8_t *dst, const uint8_t *chunky, uint8_t m)
{
	uint8_t pl[8];
	int p;

	if (!m)
		return;
	c2p8(chunky, pl);
	if (m == 0xFF) {
		for (p = 0; p < 8; p++)
			dst[p * VS_PLANE] = pl[p];
	} else {
		for (p = 0; p < 8; p++)
			dst[p * VS_PLANE] = (uint8_t)((dst[p * VS_PLANE] & ~m) | (pl[p] & m));
	}
}

int planar_get_pixel(const uint8_t *src, int bit)
{
	int p, c = 0;
	for (p = 0; p < 8; p++)
		if (src[p * VS_PLANE] & (0x80 >> bit))
			c |= 1 << p;
	return c;
}

void planar_set_pixel(uint8_t *dst, int bit, int color)
{
	uint8_t m = (uint8_t)(0x80 >> bit);
	int p;
	for (p = 0; p < 8; p++) {
		if (color & (1 << p))
			dst[p * VS_PLANE] |= m;
		else
			dst[p * VS_PLANE] &= (uint8_t)~m;
	}
}

uint8_t *vs_pixels(const struct vscreen *vs, int x, int y)
{
	return vs->pixels + (uint32_t)y * VS_PITCH + (vs->xstart + x) / 8;
}

uint8_t *vs_back_pixels(const struct vscreen *vs, int x, int y)
{
	return vs->back + (uint32_t)y * VS_PITCH + (vs->xstart + x) / 8;
}

uint8_t *gfx_mask_buffer(int x, int y, int z)
{
	return mask_buf + x + (uint32_t)y * NUM_STRIPS + img_buf_offs[z];
}

uint8_t *gfx_screen_mask_buffer(int x, int y, int z)
{
	return gfx_mask_buffer((x + vscr[0].xstart) / 8, y, z);
}

/* ---- virtual screens ---- */

static void set_dirty_range(struct vscreen *vs, int top, int bottom)
{
	int i;
	for (i = 0; i < NUM_STRIPS; i++) {
		vs->tdirty[i] = (int16_t)top;
		vs->bdirty[i] = (int16_t)bottom;
	}
}

static void init_virt_screen(int slot, int top, int height, int twobufs, int scrollable)
{
	struct vscreen *vs = &vscr[slot];
	uint32_t size;

	res_free(vs->pixels);
	res_free(vs->back);
	vs->pixels = vs->back = 0;
	vs->topline = (int16_t)top;
	vs->h = (int16_t)height;
	vs->xstart = 0;
	size = (uint32_t)VS_PITCH * (uint32_t)height;
	if (scrollable)
		size += VS_PITCH * 4;
	vs->size = size;
	if (size) {
		vs->pixels = res_alloc(size);
		if (!vs->pixels)
			fatal("out of memory for virtual screen", slot, (int)size);
		memset(vs->pixels, 0, size);
		if (twobufs) {
			vs->back = res_alloc(size);
			if (!vs->back)
				fatal("out of memory for background buffer", slot, (int)size);
			memset(vs->back, 0, size);
		}
	}
	set_dirty_range(vs, 0, height);
}

/* Verb screen images are redrawn by scripts many times a frame, each time
 * from a fresh copy of the object image. Opaque strips are unpacked once
 * into verb_shadow (laid out like the verb screen, written by nothing else)
 * and then blitted; verb_cache tells which image strip each shadow column
 * holds, by gfx_image_key (rows y, y + h). */
uint32_t gfx_image_key;
static uint8_t *verb_shadow;
static struct {
	uint32_t key;
	int16_t strip, y, h;
} verb_cache[NUM_STRIPS];

void gfx_verb_cache_clear(void)
{
	memset(verb_cache, 0, sizeof(verb_cache));
}

void gfx_init_screens(int b, int h)
{
	gfx_verb_cache_clear();
	res_free(verb_shadow);
	verb_shadow = 0;
	init_virt_screen(0, b, h - b, 1, 1);
	init_virt_screen(1, 0, b, 0, 0);
	init_virt_screen(2, h, SCREEN_HEIGHT - h, 0, 0);
	if (vscr[2].size) {
		verb_shadow = res_alloc(vscr[2].size);
		if (!verb_shadow)
			fatal("out of memory for verb images", (int)vscr[2].size, 0);
	}
	display_layout(b, h);
}

void gfx_mark_rect_dirty(int virt, int left, int right, int top, int bottom, int dirtybit)
{
	struct vscreen *vs = &vscr[virt];
	int lp, rp;

	if (left > right || top > bottom)
		return;
	if (top > vs->h || bottom < 0)
		return;
	if (top < 0)
		top = 0;
	if (bottom > vs->h)
		bottom = vs->h;

	if (virt == 0 && dirtybit) {
		lp = left / 8 + g.screen_start_strip;
		if (lp < 0)
			lp = 0;
		rp = (right + vs->xstart) / 8;
		if (rp >= 200)
			rp = 200;
		for (; lp <= rp; lp++)
			set_gfx_usage_bit(lp, dirtybit);
	}

	lp = left / 8;
	rp = right / 8;
	if (lp >= NUM_STRIPS || rp < 0)
		return;
	if (lp < 0)
		lp = 0;
	if (rp >= NUM_STRIPS)
		rp = NUM_STRIPS - 1;
	while (lp <= rp) {
		if (top < vs->tdirty[lp])
			vs->tdirty[lp] = (int16_t)top;
		if (bottom > vs->bdirty[lp])
			vs->bdirty[lp] = (int16_t)bottom;
		lp++;
	}
}

void mark_rect_as_dirty(int left, int right, int top, int bottom, int dirtybit)
{
	gfx_mark_rect_dirty(0, left, right, top, bottom, dirtybit);
}

/* ---- per-room buffers ---- */

void gfx_room_changed(void)
{
	const uint8_t *room = res_get(RT_ROOM, g.room_resource), *rmim, *rmih, *im00, *trns;
	uint32_t size;
	int i;

	rmim = res_find(TAG('R', 'M', 'I', 'M'), room);
	rmih = rmim ? res_find(TAG('R', 'M', 'I', 'H'), rmim) : 0;
	im00 = rmim ? res_find(TAG('I', 'M', '0', '0'), rmim) : 0;
	if (!rmih || !im00)
		fatal("room without RMIH/IM00", g.room_resource, 0);
	im00_offs = (uint32_t)(im00 - room);
	num_zbuffer = rd_le16(rmih + 8) + 1;
	if (num_zbuffer < 1 || num_zbuffer > 8)
		fatal("bad number of z-planes", num_zbuffer, g.room_resource);

	trns = res_find(TAG('T', 'R', 'N', 'S'), room);
	transparent_color = trns ? trns[8] : 255;

	res_free(mask_buf);
	mask_itemsize = (uint32_t)(g.room_height + 4) * NUM_STRIPS;
	size = mask_itemsize * (uint32_t)num_zbuffer;
	mask_buf = res_alloc(size);
	if (!mask_buf)
		fatal("out of memory for masks", (int)size, 0);
	memset(mask_buf, 0, size);
	charset_mask_used = 0;
	for (i = 0; i < 8; i++)
		img_buf_offs[i] = (uint32_t)(i < num_zbuffer ? i : num_zbuffer - 1) * mask_itemsize;
}

/* ---- planar strips (PSMP, converted offline by mkpack) ---- */

#define MAX_STRIP_H 512
static uint8_t strip_mask[MAX_STRIP_H];

/* Unpacks IFF ByteRun1 data into plane columns: n bytes per plane, rows
 * [0, rows) of each written to dst (plane 0 byte of the first row) through
 * the mask (0: plain store); the rest of each column is skipped. */
static void unpack_columns(uint8_t *dst, const uint8_t *src, int planes, int n, int rows,
			   const uint8_t *mask)
{
	int p = 0, y = 0, cnt, lit, fast = !mask && rows == n;
	uint8_t *d = dst, v = 0;

	while (p < planes) {
		int c = (int8_t)*src++;
		if (c == -128)
			continue;
		if (c >= 0) {
			cnt = c + 1;
			lit = 1;
		} else {
			cnt = 1 - c;
			lit = 0;
			v = *src++;
		}
		while (cnt) {
			int k = n - y;
			if (k > cnt)
				k = cnt;
			cnt -= k;
			if (fast) {
				y += k;
				if (lit) {
					while (k--) {
						*d = *src++;
						d += VS_PITCH;
					}
				} else {
					while (k--) {
						*d = v;
						d += VS_PITCH;
					}
				}
			} else {
				while (k--) {
					uint8_t b = lit ? *src++ : v;
					if (y < rows) {
						if (!mask)
							*d = b;
						else if (mask[y])
							*d = (uint8_t)((*d & ~mask[y]) | (b & mask[y]));
					}
					d += VS_PITCH;
					y++;
				}
			}
			if (y == n) {
				y = 0;
				d = dst + ++p * VS_PLANE;
			}
		}
	}
}

/* IFF ByteRun1 into a plain buffer */
static const uint8_t *unpackbits(uint8_t *dst, const uint8_t *src, uint32_t n)
{
	uint8_t *end = dst + n;

	while (dst < end) {
		int c = (int8_t)*src++;
		if (c >= 0) {
			do
				*dst++ = *src++;
			while (c--);
		} else if (c != -128) {
			uint8_t v = *src++;
			c = 1 - c;
			do
				*dst++ = v;
			while (--c);
		}
	}
	return src;
}

/* Puts rows [0, height) of one PSMP strip at dst (plane 0 byte of the first
 * row). Returns flags bit 0, the codec's transparency. */
static int put_planar_strip(uint8_t *dst, const uint8_t *strip, int stored_h, int height)
{
	int flags = strip[0];
	const uint8_t *src = strip + 1;

	if (flags & 4)
		return flags & 1;
	if (stored_h > MAX_STRIP_H)
		fatal("strip too tall", stored_h, 0);
	if (height > stored_h)
		height = stored_h;
	if (flags & 2) {
		src = unpackbits(strip_mask, src, (uint32_t)stored_h);
		unpack_columns(dst, src, 8, stored_h, height, strip_mask);
	} else {
		unpack_columns(dst, src, 8, stored_h, height, 0);
	}
	return flags & 1;
}

static void clear8col(uint8_t *dst, int height)
{
	int y, p;
	for (y = 0; y < height; y++, dst += VS_PITCH)
		for (p = 0; p < 8; p++)
			dst[p * VS_PLANE] = 0;
}

static void decompress_mask_img(uint8_t *dst, const uint8_t *src, int height, int or_mode)
{
	uint8_t b, c;

	while (height) {
		b = *src++;
		if (b & 0x80) {
			b &= 0x7F;
			c = *src++;
			do {
				if (or_mode)
					*dst |= c;
				else
					*dst = c;
				dst += NUM_STRIPS;
				--height;
			} while (--b && height);
		} else {
			do {
				if (or_mode)
					*dst |= *src;
				else
					*dst = *src;
				src++;
				dst += NUM_STRIPS;
				--height;
			} while (--b && height);
		}
	}
}

#define DB_ALLOW_MASK_OR 1
#define DB_OBJECT_MODE 2

/* Gdi::drawBitmap for the main or verb screen. ptr is the image block that
 * holds PSMP and ZPnn (IM00 of RMIM, or IMnn of an object). */
void gfx_draw_bitmap(const uint8_t *ptr, int vsn, int x, int y, int width, int height,
		     int stripnr, int numstrip, int flag)
{
	struct vscreen *vs = &vscr[vsn];
	const uint8_t *smap = res_find(TAG('P', 'S', 'M', 'P'), ptr);
	const uint8_t *zplane[8];
	int nstrips, stored_h;
	int i, k, sx, limit, lights = is_light_on();

	static const uint32_t zp_tags[5] = {
		TAG('Z', 'P', '0', '0'), TAG('Z', 'P', '0', '1'), TAG('Z', 'P', '0', '2'),
		TAG('Z', 'P', '0', '3'), TAG('Z', 'P', '0', '4')
	};

	(void)width;
	if (!smap)
		fatal("image without PSMP", g.room_resource, 0);
	nstrips = rd_be16(smap + 8);
	stored_h = rd_be16(smap + 10);
	for (i = 1; i < num_zbuffer && i < 5; i++)
		zplane[i] = gfx_zbuffer_disabled ? 0 : res_find(zp_tags[i], ptr);

	if (vs->h <= 0)
		return;
	sx = x - vs->xstart / 8;
	if (sx < 0) {
		numstrip -= -sx;
		x += -sx;
		stripnr += -sx;
		sx = 0;
	}

	limit = (g.room_width > SCREEN_WIDTH ? g.room_width : SCREEN_WIDTH) / 8 - x;
	if (limit > numstrip)
		limit = numstrip;
	if (limit > NUM_STRIPS - sx)
		limit = NUM_STRIPS - sx;

	for (k = 0; k < limit; ++k, ++stripnr, ++sx, ++x) {
		uint32_t offset;
		uint8_t *dst;
		int transp;

		if (y < vs->tdirty[sx])
			vs->tdirty[sx] = (int16_t)y;
		if (y + height > vs->bdirty[sx])
			vs->bdirty[sx] = (int16_t)(y + height);

		dst = (vs->back ? vs->back : vs->pixels) + (uint32_t)y * VS_PITCH + x;

		if (stripnr >= nstrips)
			fatal("screen strip out of range", stripnr, nstrips);
		offset = rd_be32(smap + 12 + stripnr * 4);
		if (vsn == 2 && gfx_image_key && verb_shadow && !(smap[offset] & 6)) {
			uint32_t row = (uint32_t)y * VS_PITCH;
			if (verb_cache[sx].key != gfx_image_key || verb_cache[sx].strip != stripnr ||
			    verb_cache[sx].y != y || verb_cache[sx].h != height) {
				put_planar_strip(verb_shadow + row + x, smap + offset, stored_h, height);
				verb_cache[sx].key = gfx_image_key;
				verb_cache[sx].strip = (int16_t)stripnr;
				verb_cache[sx].y = (int16_t)y;
				verb_cache[sx].h = (int16_t)height;
			}
			display_copy_planes(vs->pixels + row, verb_shadow + row, x, x + 1, height);
			transp = smap[offset] & 1;
		} else {
			transp = put_planar_strip(dst, smap + offset, stored_h, height);
		}

		if (vs->back) {
			if (lights) {
				display_copy_planes(vs->pixels + (uint32_t)y * VS_PITCH, vs->back + (uint32_t)y * VS_PITCH,
						    x, x + 1, height);
			} else {
				display_sync();
				clear8col(vs->pixels + (uint32_t)y * VS_PITCH + x, height);
			}
		}

		/* decodeMask */
		for (i = 1; i < num_zbuffer; i++) {
			uint32_t offs;
			uint8_t *mask_ptr;

			if (i >= 5 || !zplane[i])
				continue;
			offs = rd_le16(zplane[i] + stripnr * 2 + 8);
			mask_ptr = gfx_mask_buffer(x, y, i);
			if (offs) {
				decompress_mask_img(mask_ptr, zplane[i] + offs, height, transp && (flag & DB_ALLOW_MASK_OR));
			} else if (!(transp && (flag & DB_ALLOW_MASK_OR))) {
				int h;
				for (h = 0; h < height; h++)
					mask_ptr[h * NUM_STRIPS] = 0;
			}
		}
	}
	display_sync();
}

/* ---- background and objects ---- */

static void redraw_bg_strip(int start, int num)
{
	const uint8_t *room = res_get(RT_ROOM, g.room_resource);
	int s = g.screen_start_strip + start, i;

	for (i = 0; i < num; i++)
		set_gfx_usage_bit(s + i, USAGE_BIT_DIRTY);
	gfx_draw_bitmap(room + im00_offs, 0, s, 0, g.room_width, vscr[0].h, s, num, 0);
}

static const uint8_t *object_image(const struct object_data *od, int state)
{
	const uint8_t *obim;
	uint32_t tag;

	if (od->fl_object_index)
		obim = g.fl_objects[od->fl_object_index] + od->obim_offset;
	else
		obim = res_get(RT_ROOM, g.room_resource) + od->obim_offset;
	if (!od->obim_offset)
		return 0;
	if (state > 15)
		return 0;
	tag = TAG('I', 'M', '0', state < 10 ? '0' + state : 'A' + state - 10);
	return res_find(tag, obim);
}

static void draw_object(int obj, int scroll_type)
{
	struct object_data *od = &g.objs[obj];
	const uint8_t *ptr;
	int xpos, ypos, width, height, x, a, numstrip, tmp;

	if (g.bg_needs_redraw)
		scroll_type = 0;
	if (od->obj_nr == 0)
		return;

	xpos = od->x_pos / 8;
	ypos = od->y_pos;
	od->height &= 0xFFF8;
	width = od->width / 8;
	height = od->height;

	if (width == 0 || xpos > g.screen_end_strip || xpos + width < g.screen_start_strip)
		return;

	ptr = object_image(od, get_state(od->obj_nr));
	if (!ptr)
		return;

	x = 0xFFFF;
	for (a = numstrip = 0; a < width; a++) {
		tmp = xpos + a;
		if (tmp < g.screen_start_strip || g.screen_end_strip < tmp)
			continue;
		if (scroll_type > 0 && g.screen_start_strip + scroll_type <= tmp)
			continue;
		if (scroll_type < 0 && tmp <= g.screen_end_strip + scroll_type)
			continue;
		set_gfx_usage_bit(tmp, USAGE_BIT_DIRTY);
		if (tmp < x)
			x = tmp;
		numstrip++;
	}

	if (numstrip != 0 && height > 0)
		gfx_draw_bitmap(ptr, 0, x, ypos, width * 8, height, x - xpos, numstrip, od->flags | DB_OBJECT_MODE);
}

static void draw_room_object(int i, int arg)
{
	struct object_data *od = &g.objs[i];
	uint8_t a;

	if (i < 1 || od->obj_nr < 1 || !od->state)
		return;
	do {
		a = od->parentstate;
		if (!od->parent) {
			draw_object(i, arg);
			break;
		}
		od = &g.objs[od->parent];
	} while ((od->state & 0xF) == a);
}

static void draw_room_objects(int arg)
{
	int i;
	for (i = NUM_LOCAL_OBJECTS - 1; i > 0; i--)
		if (g.objs[i].obj_nr > 0 && (g.objs[i].state & 0xF))
			draw_room_object(i, arg);
}

void gfx_process_draw_queue(void)
{
	int i, j;
	for (i = 0; i < g.draw_queue_nr; i++) {
		j = g.draw_queue[i];
		if (j)
			draw_object(j, 0);
	}
	g.draw_queue_nr = 0;
}

extern uint8_t charset_has_mask;

void gfx_redraw_bg_areas(void)
{
	int i, diff, val = 0;

	if (g.camera.cur.x != g.camera.last.x && charset_has_mask)
		stop_talk();

	if (!g.full_redraw && g.bg_needs_redraw) {
		for (i = 0; i != NUM_STRIPS; i++)
			if (test_gfx_usage_bit_ext(g.screen_start_strip + i, USAGE_BIT_DIRTY))
				redraw_bg_strip(i, 1);
	}

	diff = g.camera.cur.x - g.camera.last.x;
	if (!g.full_redraw && diff == 8) {
		val = -1;
		redraw_bg_strip(NUM_STRIPS - 1, 1);
	} else if (!g.full_redraw && diff == -8) {
		val = 1;
		redraw_bg_strip(0, 1);
	} else if (g.full_redraw || diff != 0) {
		g.bg_needs_redraw = 0;
		redraw_bg_strip(0, NUM_STRIPS);
	}

	draw_room_objects(val);
	g.bg_needs_redraw = 0;
}

void gfx_set_xstart(int xstart)
{
	vscr[0].xstart = (uint16_t)xstart;
}

/* Gdi::resetBackground, keeping text (charset mask plane 0) on top. */
void gfx_reset_background(int top, int bottom, int strip)
{
	int charset_mask_used_here;
	struct vscreen *vs = &vscr[0];
	uint8_t *front, *back;
	const uint8_t *cmask;
	int y, p;

	if (top < 0)
		top = 0;
	if (bottom > vs->h)
		bottom = vs->h;
	if (top >= bottom)
		return;

	if (top < vs->tdirty[strip])
		vs->tdirty[strip] = (int16_t)top;
	if (bottom > vs->bdirty[strip])
		vs->bdirty[strip] = (int16_t)bottom;

	front = vs_pixels(vs, strip * 8, top);
	back = vs_back_pixels(vs, strip * 8, top);
	cmask = gfx_screen_mask_buffer(strip * 8, top, 0);
	charset_mask_used_here = 1;
	if (!is_light_on()) {
		display_sync();
		clear8col(front, bottom - top);
		return;
	}
	if (charset_mask_used) {
		const uint8_t *c = cmask;
		for (y = top; y < bottom && !*c; y++, c += NUM_STRIPS)
			;
		if (y == bottom)
			charset_mask_used_here = 0;
	}
	if (!charset_mask_used || !charset_mask_used_here) {
		uint8_t *f0 = front - strip, *b0 = back - strip;
		display_copy_planes(f0, b0, strip, strip + 1, bottom - top);
		return;
	}
	display_sync();
	for (y = top; y < bottom; y++, front += VS_PITCH, back += VS_PITCH, cmask += NUM_STRIPS) {
		uint8_t m = *cmask;
		if (!m) {
			for (p = 0; p < 8; p++)
				front[p * VS_PLANE] = back[p * VS_PLANE];
		} else {
			for (p = 0; p < 8; p++)
				front[p * VS_PLANE] = (uint8_t)((front[p * VS_PLANE] & m) | (back[p * VS_PLANE] & ~m));
		}
	}
}

void gfx_copy_planes_cpu(uint8_t *dst, const uint8_t *src, int b0, int b1, int n)
{
	int y, p;
	for (y = 0; y < n; y++, dst += VS_PITCH, src += VS_PITCH)
		for (p = 0; p < 8; p++)
			memcpy(dst + p * VS_PLANE + b0, src + p * VS_PLANE + b0, (size_t)(b1 - b0));
}

/* Byte-wise rectangle operations over the planes, with edge masks: lm on
 * the first byte, rm on the last, whole bytes between. */
void gfx_fill_planes_cpu(uint8_t *row0, int x0, int x1, int rows, int color)
{
	int s0 = x0 >> 3, n = ((x1 - 1) >> 3) - s0, r, p, k;
	uint8_t lm = (uint8_t)(0xFF >> (x0 & 7)), rm = (uint8_t)(0xFF << (7 - ((x1 - 1) & 7)));

	if (!n)
		lm &= rm;
	for (p = 0; p < 8; p++) {
		uint8_t *row = row0 + p * VS_PLANE + s0;
		int set = (color >> p) & 1;
		for (r = 0; r < rows; r++, row += VS_PITCH) {
			uint8_t *d = row;
			*d = (uint8_t)(set ? (*d | lm) : (*d & ~lm));
			if (!n)
				continue;
			for (k = 1, d++; k < n; k++)
				*d++ = (uint8_t)(set ? 0xFF : 0);
			*d = (uint8_t)(set ? (*d | rm) : (*d & ~rm));
		}
	}
}

static void fill_rect(struct vscreen *vs, int x, int y, int w, int h, int color)
{
	if (w <= 0 || h <= 0)
		return;
	display_fill_planes(vs->pixels + (uint32_t)y * VS_PITCH, vs->xstart + x, vs->xstart + x + w, h, color);
	display_sync();
}

static void copy_rect_from_back(struct vscreen *vs, int x, int y, int w, int h)
{
	int s0 = (vs->xstart + x) >> 3, n = ((vs->xstart + x + w - 1) >> 3) - s0, r, p, k;
	uint8_t lm = (uint8_t)(0xFF >> ((vs->xstart + x) & 7));
	uint8_t rm = (uint8_t)(0xFF << (7 - ((vs->xstart + x + w - 1) & 7)));

	if (w <= 0 || h <= 0)
		return;
	if (!n)
		lm &= rm;
	display_sync();
	for (p = 0; p < 8; p++) {
		uint32_t o = (uint32_t)y * VS_PITCH + p * VS_PLANE + s0;
		uint8_t *row = vs->pixels + o;
		const uint8_t *brow = vs->back + o;
		for (r = 0; r < h; r++, row += VS_PITCH, brow += VS_PITCH) {
			uint8_t *d = row;
			const uint8_t *b = brow;
			*d = (uint8_t)((*d & ~lm) | (*b & lm));
			if (!n)
				continue;
			for (k = 1, d++, b++; k < n; k++)
				*d++ = *b++;
			*d = (uint8_t)((*d & ~rm) | (*b & rm));
		}
	}
}

void gfx_restore_background(struct rect rect, int back_color)
{
	struct vscreen *vs;
	int v, w, h;

	if (rect.top < 0)
		rect.top = 0;
	if (rect.left >= rect.right || rect.top >= rect.bottom)
		return;
	if ((v = find_virt_screen(rect.top)) < 0)
		return;
	vs = &vscr[v];
	if (rect.left > SCREEN_WIDTH)
		return;

	rect.top -= vs->topline;
	rect.bottom -= vs->topline;
	if (rect.left < 0)
		rect.left = 0;
	if (rect.right > SCREEN_WIDTH)
		rect.right = SCREEN_WIDTH;
	if (rect.bottom > vs->h)
		rect.bottom = vs->h;
	if (rect.top > vs->h)
		rect.top = vs->h;
	h = rect.bottom - rect.top;
	w = rect.right - rect.left;

	gfx_mark_rect_dirty(v, rect.left, rect.right, rect.top, rect.bottom, USAGE_BIT_RESTORED);
	if (h <= 0 || w <= 0)
		return;

	if (vs->back && g.current_room != 0 && is_light_on())
		copy_rect_from_back(vs, rect.left, rect.top, w, h);
	else
		fill_rect(vs, rect.left, rect.top, w, h, back_color);
}

void gfx_draw_box(int x, int y, int x2, int y2, int color)
{
	struct vscreen *vs;
	int v, t;

	if ((v = find_virt_screen(y)) < 0)
		return;
	vs = &vscr[v];
	if (x > x2) {
		t = x;
		x = x2;
		x2 = t;
	}
	if (y > y2) {
		t = y;
		y = y2;
		y2 = t;
	}
	x2++;
	y2++;
	y -= vs->topline;
	y2 -= vs->topline;
	if (x < 0)
		x = 0;
	else if (x >= SCREEN_WIDTH)
		return;
	if (x2 < 0)
		return;
	else if (x2 > SCREEN_WIDTH)
		x2 = SCREEN_WIDTH;
	if (y < 0)
		y = 0;
	else if (y > vs->h)
		return;
	if (y2 < 0)
		return;
	else if (y2 > vs->h)
		y2 = vs->h;
	if (x2 - x <= 0 || y2 - y <= 0)
		return;

	gfx_mark_rect_dirty(v, x, x2, y, y2, 0);
	if (color == -1) {
		if (v != 0)
			fatal("can only copy bg to main window", v, 0);
		copy_rect_from_back(vs, x, y, x2 - x, y2 - y);
	} else {
		fill_rect(vs, x, y, x2 - x, y2 - y, color);
	}
}

/* ---- to the display ---- */

static void update_dirty_screen(int slot)
{
	struct vscreen *vs = &vscr[slot];
	int i, w = 8, start = 0;

	if (vs->h == 0)
		return;
	for (i = 0; i < NUM_STRIPS; i++) {
		if (vs->bdirty[i]) {
			int top = vs->tdirty[i], bottom = vs->bdirty[i];
			vs->tdirty[i] = vs->h;
			vs->bdirty[i] = 0;
			if (i != NUM_STRIPS - 1 && vs->bdirty[i + 1] == bottom && vs->tdirty[i + 1] == top) {
				w += 8;
				continue;
			}
			display_dirty(slot, start, w / 8, top, bottom);
			w = 8;
		}
		start = i + 1;
	}
}

void gfx_draw_dirty_screen_parts(void)
{
	update_dirty_screen(2);
	update_dirty_screen(1);
	if (g.camera.last.x != g.camera.cur.x) {
		display_dirty(0, 0, NUM_STRIPS, 0, vscr[0].h);
		set_dirty_range(&vscr[0], vscr[0].h, 0);
	} else {
		update_dirty_screen(0);
	}
}

void gfx_full_dirty(void)
{
	int i;
	for (i = 0; i < 3; i++)
		set_dirty_range(&vscr[i], 0, vscr[i].h);
}
