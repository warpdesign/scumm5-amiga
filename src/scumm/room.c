/* scumm5-amiga - entering rooms, room data, palette and transitions
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/room.cpp (startScene,
 * setupRoomSubBlocks, resetRoomSubBlocks), palette.cpp (setPalColor,
 * darkenPalette) and gfx.cpp (fadeIn, fadeOut, dissolveEffect), ScummVM
 * commit 96b42d42bc8533953bdd701db059cc33c839a9ff, reduced to SCUMM v5.
 * ScummVM is the legal property of its developers, whose names are too
 * numerous to list here; see the COPYRIGHT file distributed with the
 * ScummVM source.
 */
#include "scumm.h"
#include "gfx.h"
#include "saveload.h"

extern int16_t vs_h[3];

uint8_t screen_effect_flag;
uint8_t do_effect;

/* The dissolve effects draw a random permutation; consume the same random
 * numbers ScummVM does so later script results stay identical. */
static void dissolve(int width, int height)
{
	int w = SCREEN_WIDTH / width, h = vs_h[0] / height, i;

	if (SCREEN_WIDTH % width)
		w++;
	if (vs_h[0] % height)
		h++;
	for (i = 1; i < w * h; i++)
		rnd_get((uint32_t)(i - 1));
}

void fade_in(int effect)
{
	switch (effect) {
	case 128:
		dissolve(8, 4);
		break;
	case 134:
		dissolve(1, 1);
		break;
	case 135:
		dissolve(1, vs_h[0]);
		break;
	}
	screen_effect_flag = 1;
}

static void fade_out(int effect)
{
	g.camera.last.x = g.camera.cur.x;
	if (screen_effect_flag && effect != 0) {
		switch (effect) {
		case 128:
			dissolve(8, 4);
			break;
		case 134:
			dissolve(1, 1);
			break;
		case 135:
			dissolve(1, vs_h[0]);
			break;
		}
	}
	screen_effect_flag = 0;
}

/* ---- palette ---- */

uint8_t palette_dirty = 1;

void set_pal_color(int idx, int r, int gr, int b)
{
	palette_dirty = 1;
	g.current_palette[idx * 3 + 0] = (uint8_t)r;
	g.current_palette[idx * 3 + 1] = (uint8_t)gr;
	g.current_palette[idx * 3 + 2] = (uint8_t)b;
}

/* ---- colour cycling (ScummVM palette.cpp initCycl, cyclePalette) ---- */

static void init_cycles(const uint8_t *ptr)
{
	int j;

	memset(g.color_cycle, 0, sizeof(g.color_cycle));
	while ((j = *ptr++) != 0) {
		struct color_cycle *c;
		int freq;
		if (j > NUM_COLOR_CYCLES)
			fatal("init_cycles: cycle", j, 0);
		c = &g.color_cycle[j - 1];
		ptr += 2;
		freq = ptr[0] << 8 | ptr[1];
		c->delay = (uint16_t)(freq ? 16384 / freq : 0);
		c->flags = (uint16_t)(ptr[2] << 8 | ptr[3]);
		c->start = ptr[4];
		c->end = ptr[5];
		ptr += 6;
	}
}

static void rotate(uint8_t *pal, int start, int end, int size, int forward)
{
	uint8_t tmp[3];
	uint8_t *first = pal + start * size, *last = pal + end * size;

	if (forward) {
		memcpy(tmp, last, (size_t)size);
		memmove(first + size, first, (size_t)((end - start) * size));
		memcpy(first, tmp, (size_t)size);
	} else {
		memcpy(tmp, first, (size_t)size);
		memmove(first, first + size, (size_t)((end - start) * size));
		memcpy(last, tmp, (size_t)size);
	}
}

void cycle_palette(void)
{
	struct color_cycle *c;
	int add = VAR(VAR_TIMER), i, k;

	if (add < VAR(VAR_TIMER_NEXT))
		add = VAR(VAR_TIMER_NEXT);
	for (i = 0, c = g.color_cycle; i < NUM_COLOR_CYCLES; i++, c++) {
		int forward, num;
		if (!c->delay || c->start > c->end)
			continue;
		c->counter = (uint16_t)(c->counter + add);
		if (c->counter < c->delay)
			continue;
		c->counter %= c->delay;
		forward = !(c->flags & 2);
		rotate(g.current_palette, c->start, c->end, 3, forward);
		num = c->end - c->start + 1;
		for (k = 0; k < 256; k++)
			if (g.shadow_palette[k] >= c->start && g.shadow_palette[k] <= c->end)
				g.shadow_palette[k] = (uint8_t)((g.shadow_palette[k] - c->start + (forward ? 1 : num - 1)) % num + c->start);
		rotate(g.shadow_palette, c->start, c->end, 1, forward);
		palette_dirty = 1;
	}
}

void darken_palette(int red, int green, int blue, int start, int end)
{
	const uint8_t *room, *cptr;
	int i, j;

	if (start > end)
		return;
	room = res_get(RT_ROOM, g.room_resource);
	if (!room || !g.clut_offs)
		return;
	palette_dirty = 1;
	cptr = room + g.clut_offs + start * 3;
	for (j = start; j <= end; j++) {
		i = *cptr++ * red / 0xFF;
		g.current_palette[j * 3 + 0] = (uint8_t)(i > 0xFF ? 0xFF : i);
		i = *cptr++ * green / 0xFF;
		g.current_palette[j * 3 + 1] = (uint8_t)(i > 0xFF ? 0xFF : i);
		i = *cptr++ * blue / 0xFF;
		g.current_palette[j * 3 + 2] = (uint8_t)(i > 0xFF ? 0xFF : i);
	}
}

/* ---- room data ---- */

static void setup_room_sub_blocks(void)
{
	const uint8_t *room = res_get(RT_ROOM, g.room_resource), *ptr, *end;

	if (!room)
		fatal("room data not found", g.room_resource, 0);

	g.encd_offs = g.excd_offs = g.clut_offs = 0;

	ptr = res_find(TAG('R', 'M', 'H', 'D'), room);
	g.room_width = rd_le16(ptr + 8);
	g.room_height = rd_le16(ptr + 10);
	g.num_objects_in_room = (uint8_t)rd_le16(ptr + 12);

	ptr = res_find(TAG('E', 'X', 'C', 'D'), room);
	if (ptr)
		g.excd_offs = (uint32_t)(ptr + 8 - room);
	ptr = res_find(TAG('E', 'N', 'C', 'D'), room);
	if (ptr)
		g.encd_offs = (uint32_t)(ptr + 8 - room);

	memset(g.local_script_offsets, 0, sizeof(g.local_script_offsets));
	end = room + rd_be32(room + 4);
	for (ptr = room + 8; ptr < end; ptr += rd_be32(ptr + 4)) {
		if (rd_be32(ptr) == TAG('L', 'S', 'C', 'R')) {
			int id = ptr[8];
			g.local_script_offsets[id - NUM_GLOBAL_SCRIPTS] = (uint32_t)(ptr + 9 - room);
		}
	}

	ptr = res_find(TAG('C', 'L', 'U', 'T'), room);
	if (ptr)
		g.clut_offs = (uint32_t)(ptr + 8 - room);
}

static uint8_t *copy_block_data(uint32_t tag, uint32_t *size)
{
	const uint8_t *room = res_get(RT_ROOM, g.room_resource);
	const uint8_t *ptr = res_find(tag, room);
	uint8_t *copy;

	*size = 0;
	if (!ptr)
		return 0;
	*size = rd_be32(ptr + 4) - 8;
	copy = res_alloc(*size);
	if (!copy)
		fatal("out of memory for room data", (int)tag, (int)*size);
	/* the allocation may have expired the room; the current room is in use, but be safe */
	room = res_get(RT_ROOM, g.room_resource);
	ptr = res_find(tag, room);
	memcpy(copy, ptr + 8, *size);
	return copy;
}

static void reset_room_sub_blocks(void)
{
	const uint8_t *room, *ptr;
	int i;

	memset(g.extra_box_flags, 0, sizeof(g.extra_box_flags));

	res_free(g.box_matrix);
	g.box_matrix = 0;
	res_free(g.box_data);
	g.box_data = 0;

	g.box_data = copy_block_data(TAG('B', 'O', 'X', 'D'), &g.box_data_size);
	g.box_matrix = copy_block_data(TAG('B', 'O', 'X', 'M'), &g.box_matrix_size);

	room = res_get(RT_ROOM, g.room_resource);
	ptr = res_find(TAG('S', 'C', 'A', 'L'), room);
	if (ptr) {
		ptr += 8;
		for (i = 1; i < NUM_SCALE_SLOTS; i++, ptr += 8) {
			int s1 = rd_le16(ptr), y1 = rd_le16(ptr + 2), s2 = rd_le16(ptr + 4), y2 = rd_le16(ptr + 6);
			if (i > 4)
				break;
			if (s1 || y1 || s2 || y2)
				set_scale_slot(i, 0, y1, s1, 0, y2, s2);
		}
	}

	if (g.clut_offs) {
		memcpy(g.current_palette, room + g.clut_offs, 768);
		palette_dirty = 1;
	}

	ptr = res_find(TAG('C', 'Y', 'C', 'L'), room);
	if (ptr)
		init_cycles(ptr + 8);
}

/* ---- startScene ---- */

void start_scene(int room, struct actor *a, int object_nr)
{
	int i;

	stop_talk();
	fade_out(g.switch_room_effect2);
	g.new_effect = g.switch_room_effect;

	if (g.current_script != 0xFF) {
		struct script_slot *ss = &g.slot[g.current_script];
		if (ss->where == WIO_ROOM || ss->where == WIO_FLOBJECT || ss->where == WIO_LOCAL) {
			if (ss->cutscene_override)
				fatal("script stopped with active cutscene/override in exit", ss->number, 0);
			g.current_script = 0xFF;
		}
	}

	VAR(VAR_NEW_ROOM) = room;
	run_exit_script();
	kill_scripts_and_resources();
	for (i = 0; i < NUM_COLOR_CYCLES; i++)
		g.color_cycle[i].delay = 0;
	clear_draw_object_queue();

	for (i = 1; i < NUM_ACTORS; i++)
		hide_actor(&g.actors[i]);

	for (i = 0; i < 256; i++) {
		g.room_palette[i] = (uint8_t)i;
		g.shadow_palette[i] = (uint8_t)i;
	}

	VAR(VAR_ROOM) = room;
	g.full_redraw = 1;
	res_tick();

	g.current_room = (uint8_t)room;
	VAR(VAR_ROOM) = room;
	g.room_resource = room >= 0x80 ? g.resource_mapper[room & 0x7F] : (uint8_t)room;
	VAR(VAR_ROOM_RESOURCE) = g.room_resource;

	if (room != 0)
		res_get(RT_ROOM, g.room_resource);

	clear_room_objects();

	if (g.current_room == 0) {
		g.encd_offs = g.excd_offs = 0;
		g.num_objects_in_room = 0;
		return;
	}

	setup_room_sub_blocks();
	reset_room_sub_blocks();
	gfx_room_changed();
	reset_room_objects();

	VAR(VAR_CAMERA_MIN_X) = SCREEN_WIDTH / 2;
	VAR(VAR_CAMERA_MAX_X) = g.room_width - SCREEN_WIDTH / 2;

	g.camera.mode = CAMERA_NORMAL;
	g.camera.cur.x = g.camera.dest.x = SCREEN_WIDTH / 2;
	g.camera.cur.y = g.camera.dest.y = SCREEN_HEIGHT / 2;

	if (g.room_resource == 0)
		return;

	clear_gfx_usage_bits();

	if (a) {
		int where = where_is_object(object_nr), x, y, dir;
		if (where != WIO_ROOM && where != WIO_FLOBJECT)
			fatal("startScene: object not in room", object_nr, g.current_room);
		get_object_xy_pos(object_nr, &x, &y, &dir);
		put_actor(a, x, y, g.current_room);
		set_direction(a, dir + 180);
		stop_actor_moving(a);
	}

	show_actors();

	g.ego_positioned = 0;
	run_entry_script();
	if (a && !g.ego_positioned) {
		int x, y, dir;
		get_object_xy_pos(object_nr, &x, &y, &dir);
		put_actor(a, x, y, g.current_room);
		a->moving = 0;
	}

	do_effect = 1;
}

/* The room data a loaded game needs that is not in the saved game. */
void room_after_load(void)
{
	if (g.current_room == 0) {
		g.encd_offs = g.excd_offs = 0;
		return;
	}
	setup_room_sub_blocks();
	gfx_room_changed();
}
