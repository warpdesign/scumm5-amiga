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

extern int16_t vs_h[3];

struct cel_state {
	int actor_x, actor_y, scale_x, scale_y, to_right, out_h;
	int x_move, y_move;
	int32_t draw_top, draw_bottom;
	int actor_id;
};

/* BaseCostumeRenderer::paintCelByleRLECommon up to the point where pixels
 * would be decoded: the cel rectangle, strip usage and vertical extent. */
static int cel_extent(struct cel_state *st, int x_move_cur, int y_move_cur, int width, int height)
{
	const uint8_t *table = small_costume_scale_table;
	int scaled = st->scale_x != 0xFF || st->scale_y != 0xFF;
	int x = st->actor_x, y = st->actor_y, i, j, step, lines_to_skip = 0, skip_width, draw_flag = 1;
	int left, right, top, bottom;
	int bounds_right = SCREEN_WIDTH, bounds_bottom = st->out_h;

	if (scaled) {
		int scale_x_step = -1;
		if (x_move_cur < 0) {
			x_move_cur = -x_move_cur;
			scale_x_step = 1;
		}
		if (st->to_right) {
			j = (128 - x_move_cur) & 0xFF;
			for (i = 0; i < x_move_cur; i++)
				if (table[j++ & 0xFF] < st->scale_x)
					x -= scale_x_step;
			left = right = x;
			j = (128 - x_move_cur) & 0xFF;
			for (i = 0; i < width; i++) {
				if (right < 0)
					lines_to_skip++;
				if (table[j++ & 0xFF] < st->scale_x)
					right++;
			}
		} else {
			j = (128 + x_move_cur) & 0xFF;
			for (i = 0; i < x_move_cur; i++)
				if (table[j-- & 0xFF] < st->scale_x)
					x += scale_x_step;
			left = right = x;
			j = (128 + x_move_cur) & 0xFF;
			for (i = 0; i < width; i++) {
				if (left >= bounds_right)
					lines_to_skip++;
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
		j = (128 - y_move_cur) & 0xFF;
		for (i = 0; i < y_move_cur; i++)
			if (table[j++ & 0xFF] < st->scale_y)
				y -= step;
		top = bottom = y;
		j = (128 - y_move_cur) & 0xFF;
		for (i = 0; i < height; i++)
			if (table[j++ & 0xFF] < st->scale_y)
				bottom++;
	} else {
		if (!st->to_right)
			x_move_cur = -x_move_cur;
		x += x_move_cur;
		y += y_move_cur;
		if (st->to_right) {
			left = x;
			right = x + width;
		} else {
			left = x - width;
			right = x;
		}
		top = y;
		bottom = top + height;
	}

	skip_width = width;
	mark_rect_as_dirty(left, right + 1, top, bottom, st->actor_id);

	if (top >= bounds_bottom || bottom <= 0)
		return 0;
	if (left >= bounds_right || right <= 0)
		return 0;

	if (st->to_right) {
		if (!scaled)
			lines_to_skip = 0 - x;
		if (lines_to_skip > 0) {
			skip_width -= lines_to_skip;
		} else {
			lines_to_skip = right - bounds_right;
			if (lines_to_skip <= 0)
				draw_flag = 2;
			else
				skip_width -= lines_to_skip;
		}
	} else {
		if (!scaled)
			lines_to_skip = right - bounds_right;
		if (lines_to_skip > 0) {
			skip_width -= lines_to_skip;
		} else {
			lines_to_skip = (0 - 1) - left;
			if (lines_to_skip <= 0)
				draw_flag = 2;
			else
				skip_width -= lines_to_skip;
		}
	}
	if (skip_width <= 0)
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
	return draw_flag;
}

int costume_draw_extents(struct actor *a)
{
	const uint8_t *ptr = res_get(RT_COSTUME, a->costume), *base, *frame_offsets, *anim_cmds;
	struct cel_state st;
	int limb, result = 0, num_colors, mirror;

	if (!ptr)
		fatal("costume not found", a->costume, 0);
	base = ptr + 2;
	num_colors = (base[7] & 0x7F) == 0x58 ? 16 : 32;
	mirror = (base[7] & 0x80) != 0;
	frame_offsets = base + 8 + num_colors + 2;
	anim_cmds = base + rd_le16(base + 8 + num_colors);

	st.actor_id = a->number;
	st.actor_x = a->pos.x - g.screen_start_strip * 8;
	st.actor_y = a->pos.y - a->elevation;
	st.scale_x = a->scalex;
	st.scale_y = a->scaley;
	st.to_right = new_dir_to_old_dir(a->facing) != 0 || mirror;
	st.out_h = vs_h[0];
	st.x_move = st.y_move = 0;
	st.draw_top = 0x7fffffff;
	st.draw_bottom = 0;

	for (limb = 0; limb < 16; limb++) {
		const uint8_t *frameptr, *src;
		int i, code;

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
			result |= cel_extent(&st, x_move_cur, y_move_cur, width, height);
		}
	}
	a->top = st.draw_top;
	a->bottom = st.draw_bottom;
	return result;
}
