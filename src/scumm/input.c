/* scumm5-amiga - mouse/keyboard state and DEMO.REC playback
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/input.cpp (processInput, parseEvent,
 * clearClickedStatus) and playback.cpp (MI2 non-interactive demo
 * playback), ScummVM commit 96b42d42bc8533953bdd701db059cc33c839a9ff.
 * ScummVM is the legal property of its developers, whose names are too
 * numerous to list here; see the COPYRIGHT file distributed with the
 * ScummVM source.
 */
#include "scumm.h"
#include "saveload.h"

#define MBS_LEFT_CLICK 0x8000
#define MBS_RIGHT_CLICK 0x4000
#define MBS_MOUSE_MASK (MBS_LEFT_CLICK | MBS_RIGHT_CLICK)
#define MS_CLICKED 1
#define MS_DOWN 2

extern int16_t vs_topline[3], vs_h[3];
void start_scene(int room, struct actor *a, int object);

static uint16_t key_pressed;

void input_key(int key)
{
	key_pressed = (uint16_t)key;
}

void input_click(int x, int y)
{
	g.mouse.x = (int16_t)x;
	g.mouse.y = (int16_t)y;
	g.left_btn_pressed |= MS_CLICKED;
}

void input_mouse(int x, int y, int left_clicks, int left_down, int right_clicks, int right_down)
{
	g.mouse.x = (int16_t)x;
	g.mouse.y = (int16_t)y;
	if (left_clicks)
		g.left_btn_pressed |= MS_CLICKED | MS_DOWN;
	if (!left_down)
		g.left_btn_pressed &= (uint8_t)~MS_DOWN;
	if (right_clicks)
		g.right_btn_pressed |= MS_CLICKED | MS_DOWN;
	if (!right_down)
		g.right_btn_pressed &= (uint8_t)~MS_DOWN;
}

int input_take_key(void)
{
	int k = key_pressed;

	key_pressed = 0;
	return k;
}

int input_take_click(void)
{
	int b = g.left_btn_pressed & MS_CLICKED ? 1 : g.right_btn_pressed & MS_CLICKED ? 2 : 0;

	g.left_btn_pressed &= (uint8_t)~MS_CLICKED;
	g.right_btn_pressed &= (uint8_t)~MS_CLICKED;
	return b;
}

void clear_clicked_status(void)
{
	key_pressed = 0;
	g.mouse_and_keyboard_stat = 0;
	g.left_btn_pressed &= (uint8_t)~MS_CLICKED;
	g.right_btn_pressed &= (uint8_t)~MS_CLICKED;
}

void process_input(void)
{
	uint16_t last_key = key_pressed;

	key_pressed = 0;

	if (g.mouse.x < 0)
		g.mouse.x = 0;
	if (g.mouse.x > SCREEN_WIDTH - 1)
		g.mouse.x = SCREEN_WIDTH - 1;
	if (g.mouse.y < 0)
		g.mouse.y = 0;
	if (g.mouse.y > SCREEN_HEIGHT - 1)
		g.mouse.y = SCREEN_HEIGHT - 1;

	g.virtual_mouse.x = (int16_t)(g.mouse.x + g.screen_start_strip * 8);
	g.virtual_mouse.y = (int16_t)(g.mouse.y - vs_topline[0]);
	if (g.virtual_mouse.y < 0)
		g.virtual_mouse.y = -1;
	if (g.virtual_mouse.y >= vs_h[0])
		g.virtual_mouse.y = -1;

	g.mouse_and_keyboard_stat = 0;
	if ((g.left_btn_pressed & MS_CLICKED) && (g.right_btn_pressed & MS_CLICKED)) {
		g.mouse_and_keyboard_stat = 0;
		last_key = 27;
	} else if (g.left_btn_pressed & MS_CLICKED) {
		g.mouse_and_keyboard_stat = MBS_LEFT_CLICK;
	} else if (g.right_btn_pressed & MS_CLICKED) {
		g.mouse_and_keyboard_stat = MBS_RIGHT_CLICK;
	}

	g.left_btn_pressed &= (uint8_t)~MS_CLICKED;
	g.right_btn_pressed &= (uint8_t)~MS_CLICKED;

	if (!last_key)
		return;

	if (VAR(VAR_MAINMENU_KEY) && last_key == VAR(VAR_MAINMENU_KEY)) {
		menu_open();
		return;
	}
	if (VAR(VAR_TALKSTOP_KEY) && last_key == '.') {
		g.talk_delay = 0;
	} else if (VAR(VAR_CUTSCENEEXIT_KEY) && last_key == 27) {
		abort_cutscene();
		g.mouse_and_keyboard_stat = (uint16_t)VAR(VAR_CUTSCENEEXIT_KEY);
	} else {
		g.mouse_and_keyboard_stat = last_key;
	}
}

/* ---- DEMO.REC playback ---- */

#define MAX_EVENTS 16384

struct frame_event {
	int16_t x, y;
	uint16_t key;
	uint16_t mbs;
	uint8_t has_pos;
};

static struct frame_event *events;
static uint32_t num_events, next_event;
static uint8_t loaded, active, started, vars_applied;
uint8_t playback_like_scummvm;
static uint16_t prev_mbs;
static uint8_t has_prev_mbs, pending_lup, pending_rup;
static int16_t cur_x, cur_y;
static uint8_t sputm_active, sputm_enters;
static char sputm_buf[33];
static uint8_t sputm_len;

int playback_open(const char *path)
{
	plat_file f = plat_open(path);
	uint8_t *buf;
	int32_t size;
	uint32_t i, stream = 0, off;
	int found = 0;

	if (!f)
		return -1;
	size = plat_size(f);
	buf = plat_alloc((uint32_t)size, 0);
	if (!buf || plat_read(f, buf, (uint32_t)size) != size) {
		plat_close(f);
		plat_free(buf, (uint32_t)size);
		return -1;
	}
	plat_close(f);

	/* the stream follows the last 0x01 0xFF marker that parses to the end */
	for (i = 0; i + 1 < (uint32_t)size; i++) {
		uint32_t count = 0;
		int ok = 1;
		if (buf[i] != 0x01 || buf[i + 1] != 0xFF)
			continue;
		off = i + 2;
		while (off < (uint32_t)size) {
			uint8_t ctrl = buf[off];
			uint32_t len = 1;
			if ((ctrl & 0x0F) || (ctrl & ~0xF0)) {
				ok = 0;
				break;
			}
			if (ctrl & 0x20)
				len += 2;
			if (ctrl & 0x10)
				len += 2;
			if (off + len > (uint32_t)size) {
				ok = 0;
				break;
			}
			off += len;
			count++;
		}
		if (ok && off == (uint32_t)size && count > 0) {
			stream = i + 2;
			found = 1;
		}
	}
	if (!found) {
		plat_free(buf, (uint32_t)size);
		return -1;
	}

	events = plat_alloc(MAX_EVENTS * sizeof(*events), PLAT_CLEAR);
	if (!events)
		fatal("out of memory for playback", 0, 0);
	num_events = 0;
	off = stream;
	while (off < (uint32_t)size && num_events < MAX_EVENTS) {
		uint8_t ctl = buf[off++];
		struct frame_event *ev = &events[num_events];
		if (ctl & 0x0F)
			break;
		if (ctl & 0x20) {
			ev->has_pos = 1;
			ev->x = (int16_t)(buf[off] << 1);
			ev->y = buf[off + 1];
			off += 2;
		}
		if (ctl & 0x10) {
			ev->key = (uint16_t)(buf[off] | buf[off + 1] << 8);
			off += 2;
		}
		if (ctl & 0x80)
			ev->mbs |= MBS_LEFT_CLICK;
		if (ctl & 0x40)
			ev->mbs |= MBS_RIGHT_CLICK;
		num_events++;
	}
	plat_free(buf, (uint32_t)size);
	loaded = num_events > 0;
	return loaded ? 0 : -1;
}

static void jump_room(int room)
{
	struct actor *ego;

	if (room <= 0)
		return;
	ego = deref_actor_safe(VAR(VAR_EGO));
	if (ego)
		ego->room = (uint8_t)room;
	start_scene(room, 0, 7);
	if (ego) {
		set_camera_follows(ego, 1);
		actor_follow_camera(ego->number);
		move_camera();
	}
	g.full_redraw = 1;
}

/* The MI2 demo recording drives the SPUTM debugger to jump between rooms. */
static int sputm_debug_key(uint16_t key)
{
	if (key == 0)
		return 0;
	if (key == 7) {
		sputm_active = 1;
		sputm_enters = 0;
		sputm_len = 0;
		return 1;
	}
	if (!sputm_active)
		return 0;
	if (key == 27) {
		sputm_active = 0;
		return 1;
	}
	if (key == 13) {
		if (sputm_enters < 2)
			sputm_enters++;
		if (sputm_enters >= 2) {
			int target = -1;
			sputm_buf[sputm_len] = 0;
			if (sputm_len == 4 && (sputm_buf[0] | 0x20) == 'v' && (sputm_buf[1] | 0x20) == 'i' &&
			    (sputm_buf[2] | 0x20) == 'l' && (sputm_buf[3] | 0x20) == 'l')
				target = 47;
			else if (sputm_len == 4 && (sputm_buf[0] | 0x20) == 'w' && (sputm_buf[1] | 0x20) == 'h' &&
				 (sputm_buf[2] | 0x20) == 'a' && (sputm_buf[3] | 0x20) == 'r')
				target = 27;
			sputm_active = 0;
			sputm_enters = 0;
			sputm_len = 0;
			if (target >= 0)
				jump_room(target);
		}
		return 1;
	}
	if (key >= 32 && key <= 126) {
		if (sputm_len < 32)
			sputm_buf[sputm_len++] = (char)key;
		return 1;
	}
	sputm_active = 0;
	sputm_enters = 0;
	sputm_len = 0;
	return 1;
}

static void apply_demo_override(void)
{
	g.vars[56] = 203;
	VAR(VAR_OVERRIDE) = 1;
}

static void start_playback(void)
{
	active = 1;
	next_event = 0;
	has_prev_mbs = 0;
	prev_mbs = 0;
	pending_lup = pending_rup = 0;
	sputm_active = 0;
	cur_x = g.mouse.x;
	cur_y = g.mouse.y;
	started = 1;
}

/* ScummVM starts the recording in room 4 instead, which the reference
 * traces need. */
static void arm_like_scummvm(void)
{
	if (!vars_applied && g.current_room == 3) {
		apply_demo_override();
		vars_applied = 1;
	}
	if (g.current_room == 4) {
		if (!active)
			start_playback();
		if (!vars_applied) {
			apply_demo_override();
			vars_applied = 1;
		}
	}
}

static void mouse_event(int16_t x, int16_t y, int down_left, int down_right)
{
	if (down_left)
		g.left_btn_pressed |= MS_CLICKED | MS_DOWN;
	if (down_right)
		g.right_btn_pressed |= MS_CLICKED | MS_DOWN;
	g.mouse.x = x;
	g.mouse.y = y;
}

void playback_pump(void)
{
	const struct frame_event *fev;
	uint16_t prev, cur;

	/* DOS replays the recording from the first frame; its early Escape
	 * skips the opening cutscene, so the demo never shows room 4. */
	if (loaded && playback_like_scummvm)
		arm_like_scummvm();
	else if (loaded && !started)
		start_playback();

	if (!active)
		return;
	if (next_event >= num_events) {
		active = 0;
		return;
	}

	if (pending_lup) {
		g.left_btn_pressed &= (uint8_t)~MS_DOWN;
		pending_lup = 0;
	}
	if (pending_rup) {
		g.right_btn_pressed &= (uint8_t)~MS_DOWN;
		pending_rup = 0;
	}

	fev = &events[next_event];
	if (fev->has_pos) {
		cur_x = fev->x;
		cur_y = fev->y;
	}
	mouse_event(cur_x, cur_y, 0, 0);

	if (playback_like_scummvm && g.current_room == 4) {
		g.virtual_mouse.x = cur_x;
		g.virtual_mouse.y = cur_y;
	} else {
		g.virtual_mouse.x = (int16_t)(cur_x + g.screen_start_strip * 8);
		g.virtual_mouse.y = (int16_t)(cur_y + g.screen_top);
	}

	prev = has_prev_mbs ? prev_mbs : 0;
	cur = fev->mbs;
	if (!(prev & MBS_LEFT_CLICK) && (cur & MBS_LEFT_CLICK)) {
		mouse_event(cur_x, cur_y, 1, 0);
		pending_lup = 1;
	}
	if (!(prev & MBS_RIGHT_CLICK) && (cur & MBS_RIGHT_CLICK)) {
		mouse_event(cur_x, cur_y, 0, 1);
		pending_rup = 1;
	}

	if (fev->key != 0) {
		if (fev->key == 27 && !sputm_active && !playback_like_scummvm)
			key_pressed = 27;	/* through process_input: aborts the cutscene */
		else if (!sputm_debug_key(fev->key))
			g.mouse_and_keyboard_stat = fev->key;
	}
	if (fev->key == 0)
		g.mouse_and_keyboard_stat = (uint16_t)((g.mouse_and_keyboard_stat & ~MBS_MOUSE_MASK) | (cur & MBS_MOUSE_MASK));

	prev_mbs = cur;
	has_prev_mbs = 1;
	next_event++;
}
