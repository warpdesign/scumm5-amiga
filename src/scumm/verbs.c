/* scumm5-amiga - verbs and verb input
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/verbs.cpp, ScummVM commit
 * 96b42d42bc8533953bdd701db059cc33c839a9ff, reduced to SCUMM v5. Verb
 * extents are computed exactly; pixels are not drawn yet. ScummVM is the
 * legal property of its developers, whose names are too numerous to list
 * here; see the COPYRIGHT file distributed with the ScummVM source.
 */
#include "scumm.h"
#include "gfx.h"

#define MBS_LEFT_CLICK 0x8000
#define MBS_RIGHT_CLICK 0x4000
#define MBS_MOUSE_MASK (MBS_LEFT_CLICK | MBS_RIGHT_CLICK)
#define MBS_MAX_KEY 0x0200

enum { CLICK_VERB = 1, CLICK_SCENE = 2, CLICK_INVENTORY = 3, CLICK_KEY = 4 };

void layout_verb_text(int slot, struct rect *r);
int find_virt_screen(int y);

int get_verb_slot(int id, int mode)
{
	int i;
	for (i = 1; i < NUM_VERBS; i++)
		if (g.verbs[i].verbid == id && g.verbs[i].saveid == mode)
			return i;
	return 0;
}

static void restore_verb_bg(int verb)
{
	struct verb_slot *vs = &g.verbs[verb];

	if (vs->old_rect.left != -1) {
		gfx_restore_background(vs->old_rect, vs->bkcolor);
		vs->old_rect.left = -1;
	}
}

static void draw_verb_bitmap(int verb)
{
	struct verb_slot *vs = &g.verbs[verb];
	const uint8_t *imhd, *imptr;
	int v, imgw, imgh, i, xstrip, ydiff;

	if ((v = find_virt_screen(vs->cur_rect.top)) < 0 || !vs->name)
		return;
	imhd = res_find(TAG('I', 'M', 'H', 'D'), vs->name);
	if (!imhd)
		fatal("verb image without IMHD", verb, 0);
	imgw = rd_le16(imhd + 8 + 12) / 8;
	imgh = rd_le16(imhd + 8 + 14) / 8;
	imptr = res_find(TAG('I', 'M', '0', '1'), vs->name);
	xstrip = vs->cur_rect.left / 8;
	ydiff = vs->cur_rect.top - vscr[v].topline;
	if (imptr) {
		gfx_zbuffer_disabled = 1;
		for (i = 0; i < imgw; i++)
			gfx_draw_bitmap(imptr, v, xstrip + i, ydiff, imgw * 8, imgh * 8, i, 1, 3);
		gfx_zbuffer_disabled = 0;
	}
	vs->cur_rect.right = (int16_t)(vs->cur_rect.left + rd_le16(imhd + 8 + 12) / 8 * 8);
	vs->cur_rect.bottom = (int16_t)(vs->cur_rect.top + rd_le16(imhd + 8 + 14) / 8 * 8);
	vs->old_rect = vs->cur_rect;
}

void draw_verb(int verb, int mode)
{
	struct verb_slot *vs;
	struct rect r;

	(void)mode;
	if (!verb)
		return;
	vs = &g.verbs[verb];

	if (!vs->saveid && vs->curmode && vs->verbid) {
		if (vs->type == VERB_IMAGE) {
			draw_verb_bitmap(verb);
			return;
		}
		restore_verb_bg(verb);
		if (!vs->name)
			return;
		layout_verb_text(verb, &r);
		vs->cur_rect.right = r.right;
		vs->cur_rect.bottom = r.bottom;
		vs->old_rect = r;
	} else {
		restore_verb_bg(verb);
	}
}

void kill_verb(int slot)
{
	struct verb_slot *vs;

	if (slot == 0)
		return;
	vs = &g.verbs[slot];
	vs->verbid = 0;
	vs->curmode = 0;
	res_free(vs->name);
	vs->name = 0;
	if (vs->saveid == 0) {
		draw_verb(slot, 0);
		verb_mouse_over(0);
	}
	vs->saveid = 0;
}

void verb_mouse_over(int verb)
{
	if (g.verb_mouse_over != verb) {
		if (g.verbs[g.verb_mouse_over].type != VERB_IMAGE) {
			draw_verb(g.verb_mouse_over, 0);
			g.verb_mouse_over = (uint8_t)verb;
		}
		if (g.verbs[verb].type != VERB_IMAGE && g.verbs[verb].hicolor) {
			draw_verb(verb, 1);
			g.verb_mouse_over = (uint8_t)verb;
		}
	}
}

static int find_verb_at_pos(int x, int y)
{
	int i = NUM_VERBS - 1;
	const struct verb_slot *vs = &g.verbs[i];

	do {
		if (vs->curmode != 1 || !vs->verbid || vs->saveid || y < vs->cur_rect.top || y >= vs->cur_rect.bottom)
			continue;
		if (vs->center) {
			if (x < -(vs->cur_rect.right - 2 * vs->cur_rect.left) || x >= vs->cur_rect.right)
				continue;
		} else {
			if (x < vs->cur_rect.left || x >= vs->cur_rect.right)
				continue;
		}
		return i;
	} while (--vs, --i);
	return 0;
}

void redraw_verbs(void)
{
	int i, verb = 0;

	if (g.cursor_state > 0)
		verb = find_verb_at_pos(g.mouse.x, g.mouse.y);
	for (i = 0; i < NUM_VERBS; i++)
		draw_verb(i, i == verb && g.verbs[verb].hicolor);
	g.verb_mouse_over = (uint8_t)verb;
}

void handle_mouse_over(int update_inventory)
{
	(void)update_inventory;
	if (g.cursor_state > 0)
		verb_mouse_over(find_verb_at_pos(g.mouse.x, g.mouse.y));
}

void check_exec_verbs(void)
{
	int i, over, zone;
	struct verb_slot *vs;

	if (g.user_put <= 0 || g.mouse_and_keyboard_stat == 0)
		return;

	if (g.mouse_and_keyboard_stat < MBS_MAX_KEY) {
		vs = &g.verbs[1];
		for (i = 1; i < NUM_VERBS; i++, vs++) {
			if (vs->verbid && vs->saveid == 0 && vs->curmode == 1 &&
			    g.mouse_and_keyboard_stat == vs->key) {
				run_input_script(CLICK_VERB, vs->verbid, 1);
				return;
			}
		}
		run_input_script(CLICK_KEY, g.mouse_and_keyboard_stat, 1);
	} else if (g.mouse_and_keyboard_stat & MBS_MOUSE_MASK) {
		int code = (g.mouse_and_keyboard_stat & MBS_LEFT_CLICK) ? 1 : 2;

		zone = find_virt_screen(g.mouse.y);
		if (zone < 0)
			return;
		over = find_verb_at_pos(g.mouse.x, g.mouse.y);
		if (over != 0)
			run_input_script(CLICK_VERB, g.verbs[over].verbid, code);
		else
			run_input_script(zone == 0 ? CLICK_SCENE : CLICK_VERB, 0, code);
	}
}

void set_verb_object(int room, int object, int verb)
{
	const uint8_t *roomptr, *p, *end;
	int numobj, n;
	uint32_t size;

	if (where_is_object(object) == WIO_FLOBJECT)
		fatal("can't grab verb image from flobject", object, 0);

	roomptr = res_get(RT_ROOM, (uint16_t)room);
	if (!roomptr)
		fatal("setVerbObject: room not found", room, object);
	numobj = rd_le16(res_find(TAG('R', 'M', 'H', 'D'), roomptr) + 12);
	end = roomptr + rd_be32(roomptr + 4);
	n = 0;
	for (p = roomptr + 8; p < end && n < numobj; p += rd_be32(p + 4)) {
		const uint8_t *imhd;
		if (rd_be32(p) != TAG('O', 'B', 'I', 'M'))
			continue;
		n++;
		imhd = res_find(TAG('I', 'M', 'H', 'D'), p);
		if (rd_le16(imhd + 8) != object)
			continue;
		size = rd_be32(p + 4);
		res_free(g.verbs[verb].name);
		g.verbs[verb].name = res_alloc(size);
		if (!g.verbs[verb].name)
			fatal("out of memory for verb image", verb, (int)size);
		roomptr = res_get(RT_ROOM, (uint16_t)room);
		memcpy(g.verbs[verb].name, roomptr + (p - roomptr), size);
		return;
	}
	fatal("setVerbObject: object image not found", object, room);
}
