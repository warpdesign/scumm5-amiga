/* scumm5-amiga - camera
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/camera.cpp, ScummVM commit
 * 96b42d42bc8533953bdd701db059cc33c839a9ff, reduced to SCUMM v5. ScummVM
 * is the legal property of its developers, whose names are too numerous
 * to list here; see the COPYRIGHT file distributed with the ScummVM source.
 */
#include "scumm.h"
#include "gfx.h"


extern uint8_t charset_has_mask;

void set_camera_at(int pos_x, int pos_y)
{
	(void)pos_y;
	if (g.camera.mode != CAMERA_FOLLOW_ACTOR || (pos_x - g.camera.cur.x > SCREEN_WIDTH / 2) ||
	    (g.camera.cur.x - pos_x > SCREEN_WIDTH / 2))
		g.camera.cur.x = (int16_t)pos_x;
	g.camera.dest.x = (int16_t)pos_x;

	if (g.camera.cur.x < VAR(VAR_CAMERA_MIN_X))
		g.camera.cur.x = (int16_t)VAR(VAR_CAMERA_MIN_X);
	if (g.camera.cur.x > VAR(VAR_CAMERA_MAX_X))
		g.camera.cur.x = (int16_t)VAR(VAR_CAMERA_MAX_X);

	if (VAR(VAR_SCROLL_SCRIPT)) {
		VAR(VAR_CAMERA_POS_X) = g.camera.cur.x;
		run_script(VAR(VAR_SCROLL_SCRIPT), 0, 0, 0);
	}

	if (g.camera.cur.x != g.camera.last.x && charset_has_mask)
		stop_talk();
}

void set_camera_at_ex(int at)
{
	g.camera.mode = CAMERA_NORMAL;
	g.camera.cur.x = (int16_t)at;
	set_camera_at(at, 0);
	g.camera.moving_to_actor = 0;
}

void set_camera_follows(struct actor *a, int set_camera)
{
	int t, i;

	g.camera.mode = CAMERA_FOLLOW_ACTOR;
	g.camera.follows = a->number;

	if (!is_in_current_room(a)) {
		start_scene(a->room, 0, 0);
		g.camera.mode = CAMERA_FOLLOW_ACTOR;
		g.camera.cur.x = a->pos.x;
		set_camera_at(g.camera.cur.x, 0);
	}

	t = a->pos.x / 8 - g.screen_start_strip;
	if (t < g.camera.left_trigger || t > g.camera.right_trigger || set_camera)
		set_camera_at(a->pos.x, 0);

	for (i = 1; i < NUM_ACTORS; i++)
		if (is_in_current_room(&g.actors[i]))
			g.actors[i].needs_redraw = 1;
	run_inventory_script(0);
}

void camera_moved(void)
{
	if (g.camera.cur.x < SCREEN_WIDTH / 2)
		g.camera.cur.x = SCREEN_WIDTH / 2;
	else if (g.camera.cur.x > g.room_width - SCREEN_WIDTH / 2)
		g.camera.cur.x = (int16_t)(g.room_width - SCREEN_WIDTH / 2);

	g.screen_start_strip = (uint8_t)(g.camera.cur.x / 8 - NUM_STRIPS / 2);
	g.screen_end_strip = (uint8_t)(g.screen_start_strip + NUM_STRIPS - 1);
	g.screen_top = (int16_t)(g.camera.cur.y - SCREEN_HEIGHT / 2);
	gfx_set_xstart(g.screen_start_strip * 8);
}

void move_camera(void)
{
	int pos = g.camera.cur.x, t;
	int snap = VAR(VAR_CAMERA_FAST_X) != 0;
	struct actor *a = 0;

	g.camera.cur.x &= (int16_t)0xFFF8;

	if (g.camera.cur.x < VAR(VAR_CAMERA_MIN_X)) {
		if (snap)
			g.camera.cur.x = (int16_t)VAR(VAR_CAMERA_MIN_X);
		else
			g.camera.cur.x += 8;
		camera_moved();
		return;
	}
	if (g.camera.cur.x > VAR(VAR_CAMERA_MAX_X)) {
		if (snap)
			g.camera.cur.x = (int16_t)VAR(VAR_CAMERA_MAX_X);
		else
			g.camera.cur.x -= 8;
		camera_moved();
		return;
	}

	if (g.camera.mode == CAMERA_FOLLOW_ACTOR) {
		int actorx;
		a = deref_actor(g.camera.follows);
		actorx = a->pos.x;
		t = actorx / 8 - g.screen_start_strip;
		if (t < g.camera.left_trigger || t > g.camera.right_trigger) {
			if (snap) {
				if (t > 40 - 5)
					g.camera.dest.x = (int16_t)(actorx + 80);
				if (t < 5)
					g.camera.dest.x = (int16_t)(actorx - 80);
			} else {
				g.camera.moving_to_actor = 1;
			}
		}
	}

	if (g.camera.moving_to_actor) {
		a = deref_actor(g.camera.follows);
		g.camera.dest.x = a->pos.x;
	}

	if (g.camera.dest.x < VAR(VAR_CAMERA_MIN_X))
		g.camera.dest.x = (int16_t)VAR(VAR_CAMERA_MIN_X);
	if (g.camera.dest.x > VAR(VAR_CAMERA_MAX_X))
		g.camera.dest.x = (int16_t)VAR(VAR_CAMERA_MAX_X);

	if (snap) {
		g.camera.cur.x = g.camera.dest.x;
	} else {
		if (g.camera.cur.x < g.camera.dest.x)
			g.camera.cur.x += 8;
		if (g.camera.cur.x > g.camera.dest.x)
			g.camera.cur.x -= 8;
	}

	if (g.camera.moving_to_actor && g.camera.cur.x / 8 == a->pos.x / 8)
		g.camera.moving_to_actor = 0;

	camera_moved();

	if (VAR(VAR_SCROLL_SCRIPT) && pos != g.camera.cur.x) {
		VAR(VAR_CAMERA_POS_X) = g.camera.cur.x;
		run_script(VAR(VAR_SCROLL_SCRIPT), 0, 0, 0);
	}
}

void pan_camera_to(int x, int y)
{
	(void)y;
	g.camera.dest.x = (int16_t)x;
	g.camera.mode = CAMERA_PANNING;
	g.camera.moving_to_actor = 0;
}

void actor_follow_camera(int act)
{
	int old = g.camera.follows;

	set_camera_follows(deref_actor(act), 0);
	if (g.camera.follows != old)
		run_inventory_script(0);
	g.camera.moving_to_actor = 0;
}
