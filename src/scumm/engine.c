/* scumm5-amiga - engine start-up and main loop
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/scumm.cpp (resetScumm, scummLoop,
 * go, runBootscript), vars.cpp (resetScummVars) and resource.cpp
 * (readMAXS, isResourceInUse), ScummVM commit
 * 96b42d42bc8533953bdd701db059cc33c839a9ff, reduced to the DOS SCUMM v5
 * games. ScummVM is the legal property of its developers, whose names are
 * too numerous to list here; see the COPYRIGHT file distributed with the
 * ScummVM source.
 */
#include "scumm.h"

#define LIGHTMODE_ACTOR_USE_BASE_PALETTE 1
#define LIGHTMODE_ROOM_LIGHTS_ON 2
#define LIGHTMODE_ACTOR_USE_COLORS 8

extern uint8_t do_effect;
extern uint8_t charset_has_mask;
int charset_cur_id(void);

static int is_costume_in_use(int cost)
{
	int i;
	for (i = 1; i < NUM_ACTORS; i++)
		if (is_in_current_room(&g.actors[i]) && g.actors[i].costume == cost)
			return 1;
	return 0;
}

static int resource_in_use(uint16_t type, uint16_t idx)
{
	switch (type) {
	case RT_ROOM:
		return g.room_resource == idx;
	case RT_SCRIPT:
		return is_script_in_use(idx);
	case RT_COSTUME:
		return is_costume_in_use(idx);
	case RT_SOUND:
		return sound_is_running(idx);
	case RT_CHARSET:
		return charset_cur_id() == idx;
	}
	return 0;
}

static void read_index(void)
{
	uint32_t size;
	const uint8_t *idx = res_index(&size), *p, *end = idx + size;
	const uint8_t *dobj = 0;

	for (p = idx; p + 8 <= end; p += rd_be32(p + 4)) {
		if (rd_be32(p) == TAG('M', 'A', 'X', 'S')) {
			g.num_vars = rd_le16(p + 8);
			g.num_bit_vars = rd_le16(p + 12);
		} else if (rd_be32(p) == TAG('D', 'O', 'B', 'J')) {
			dobj = p;
		}
	}
	if (!g.num_vars || !dobj)
		fatal("pack index lacks MAXS or DOBJ", 0, 0);

	g.vars = res_alloc((uint32_t)g.num_vars * 4);
	g.bit_vars = res_alloc((uint32_t)(g.num_bit_vars + 7) / 8);
	if (!g.vars || !g.bit_vars)
		fatal("out of memory for variables", g.num_vars, g.num_bit_vars);
	memset(g.vars, 0, (size_t)g.num_vars * 4);
	memset(g.bit_vars, 0, (size_t)(g.num_bit_vars + 7) / 8);
	read_global_objects(dobj);
}

static void reset_scumm(void)
{
	int i;

	init_screens(16, 144);
	for (i = 0; i < 256; i++)
		g.room_palette[i] = (uint8_t)i;
	load_charset(1);

	for (i = 0; i < NUM_ACTORS; i++) {
		g.actors[i].number = (uint8_t)i;
		init_actor(&g.actors[i], -1);
	}

	vm_init();

	for (i = 0; i < NUM_VERBS; i++) {
		g.verbs[i].verbid = 0;
		g.verbs[i].cur_rect.right = SCREEN_WIDTH - 1;
		g.verbs[i].old_rect.left = -1;
		g.verbs[i].type = 0;
		g.verbs[i].color = 2;
		g.verbs[i].hicolor = 0;
		g.verbs[i].charset_nr = 1;
		g.verbs[i].curmode = 0;
		g.verbs[i].saveid = 0;
		g.verbs[i].center = 0;
		g.verbs[i].key = 0;
	}

	g.camera.left_trigger = 10;
	g.camera.right_trigger = 30;
	g.camera.mode = 0;
	g.camera.follows = 0;

	g.mouse.x = 104;
	g.mouse.y = 56;
	g.current_room = 0;
	g.num_objects_in_room = 0;
	g.actor_to_print_str_for = 0;
	g.charset_buf_pos = 0;
	g.have_msg = 0;
	g.screen_start_strip = 0;
	g.talk_delay = 0;
	g.keep_text = 0;
	g.current_cursor = 0;
	g.cursor_state = 0;
	g.user_put = 0;
	g.new_effect = 129;
	g.full_redraw = 1;
	clear_draw_object_queue();

	for (i = 0; i < NUM_STRING_SLOTS; i++) {
		g.string[i].def.xpos = 2;
		g.string[i].def.ypos = 5;
		g.string[i].def.right = SCREEN_WIDTH - 1;
		g.string[i].def.height = 0;
		g.string[i].def.color = 0xF;
		g.string[i].def.center = 0;
		g.string[i].def.charset = 0;
	}
}

static void reset_scumm_vars(void)
{
	VAR(VAR_SOUNDCARD) = 0;		/* as ScummVM with no music device */
	VAR(VAR_VIDEOMODE) = 19;
	VAR(VAR_HEAPSPACE) = 1400;
	VAR(VAR_FIXEDDISK) = 1;
	VAR(VAR_INPUTMODE) = 3;
	VAR(VAR_DEBUGMODE) = 0;
	VAR(VAR_FADE_DELAY) = 3;
	VAR(VAR_CHARINC) = 4;
	set_talking_actor(0);
	VAR(VAR_V5_TALK_STRING_Y) = -0x50;
	VAR(VAR_CURRENT_LIGHTS) = LIGHTMODE_ACTOR_USE_BASE_PALETTE | LIGHTMODE_ACTOR_USE_COLORS | LIGHTMODE_ROOM_LIGHTS_ON;
}

int engine_init(const char *pack, uint32_t arena)
{
	if (res_open(pack, arena))
		return -1;
	res_in_use = resource_in_use;
	g.rnd_seed = 1;
	read_index();
	reset_scumm();
	reset_scumm_vars();
	return 0;
}

void engine_boot(void)
{
	int32_t args[NUM_SCRIPT_LOCAL] = { 0 };
	run_script(1, 0, 0, args);
}

void scumm_loop(int delta)
{
	int old_ego;

	VAR(VAR_TIMER) = delta;
	VAR(VAR_TIMER_TOTAL) += delta;
	VAR(VAR_TMR_1) += delta;
	VAR(VAR_TMR_2) += delta;
	VAR(VAR_TMR_3) += delta;

	if (delta > 15)
		delta = 15;

	decrease_script_delay(delta);

	g.talk_delay -= delta;
	if (g.talk_delay < 0)
		g.talk_delay = 0;

	old_ego = VAR(VAR_EGO);

	process_input();

	playback_pump();
	VAR(VAR_CAMERA_POS_X) = g.camera.cur.x;
	VAR(VAR_HAVE_MSG) = g.have_msg;
	VAR(VAR_VIRT_MOUSE_X) = g.virtual_mouse.x;
	VAR(VAR_VIRT_MOUSE_Y) = g.virtual_mouse.y;
	VAR(VAR_MOUSE_X) = g.mouse.x;
	VAR(VAR_MOUSE_Y) = g.mouse.y;
	VAR(VAR_DEBUGMODE) = 0;

	sound_update_music_timer();

	run_all_scripts();
	check_exec_verbs();
	check_and_run_sentence_script();

	if (g.quit)
		return;

	if (g.current_room == 0) {
		display_dialog();
	} else {
		walk_actors();
		move_camera();
		update_object_states();
		display_dialog();

		if (g.camera.cur.x != g.camera.last.x || g.camera.cur.y != g.camera.last.y ||
		    g.bg_needs_redraw || g.full_redraw) {
			/* redrawBGAreas(): text is drawn over the room, so scrolling removes it */
			if (g.camera.cur.x != g.camera.last.x && charset_has_mask)
				stop_talk();
			g.bg_needs_redraw = 0;
		}
		g.draw_queue_nr = 0;

		process_actors();

		g.full_redraw = 0;

		if (do_effect) {
			do_effect = 0;
			fade_in(g.new_effect);
			clear_clicked_status();
		}

		handle_mouse_over(old_ego != VAR(VAR_EGO));
		play_actor_sounds();
	}

	sound_process();
	g.camera.last = g.camera.cur;

	if ((++g.frame & 0xFF) == 0)
		res_tick();
}

void engine_frame(void)
{
	int delta = VAR(VAR_TIMER_NEXT);
	if (delta < 1)
		delta = 1;
	scumm_loop(delta);
}
