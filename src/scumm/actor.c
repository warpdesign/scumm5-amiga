/* scumm5-amiga - actors: placement, walking, directions, animation
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/actor.cpp and boxes.cpp
 * (findPathTowards), ScummVM commit
 * 96b42d42bc8533953bdd701db059cc33c839a9ff, reduced to SCUMM v5. ScummVM
 * is the legal property of its developers, whose names are too numerous
 * to list here; see the COPYRIGHT file distributed with the ScummVM source.
 */
#include "scumm.h"
#include "gfx.h"

#define CHORE_REDIRECT_INIT 56
#define CHORE_REDIRECT_WALK 57
#define CHORE_REDIRECT_STAND 58
#define CHORE_REDIRECT_START_TALK 59
#define CHORE_REDIRECT_STOP_TALK 60

#define BOX_XFLIP 0x08
#define BOX_YFLIP 0x10
#define BOX_PLAYER_ONLY 0x20
#define BOX_INVISIBLE 0x80
#define OLD_INVALID_BOX 255

static int iabs(int v) { return v < 0 ? -v : v; }

int new_dir_to_old_dir(int dir)
{
	if (dir >= 71 && dir <= 109)
		return 1;
	if (dir >= 109 && dir <= 251)
		return 2;
	if (dir >= 251 && dir <= 289)
		return 0;
	return 3;
}

int old_dir_to_new_dir(int dir)
{
	static const int new_dirs[4] = { 270, 90, 180, 0 };
	return new_dirs[dir & 3];
}

int is_valid_actor(int id)
{
	return id >= 0 && id < NUM_ACTORS && g.actors[id].number == id;
}

struct actor *deref_actor(int id)
{
	if (!is_valid_actor(id))
		fatal("invalid actor", id, 0);
	return &g.actors[id];
}

struct actor *deref_actor_safe(int id)
{
	return is_valid_actor(id) ? &g.actors[id] : 0;
}

int is_in_current_room(const struct actor *a)
{
	return a->room == g.current_room;
}

static void cost_reset(struct cost_state *c)
{
	c->anim_counter = 0;
	c->sound_counter = 0;
	c->sound_pos = 0;
	c->stopped = 0;
	memset(c->curpos, 0xFF, sizeof(c->curpos));
	memset(c->start, 0xFF, sizeof(c->start));
	memset(c->end, 0xFF, sizeof(c->end));
	memset(c->frame, 0xFF, sizeof(c->frame));
}

/* ---- boxes and scale ---- */

static void setup_actor_scale(struct actor *a)
{
	uint16_t scale;

	if (a->ignore_boxes)
		return;
	a->boxscale = (uint8_t)get_box_scale(a->walkbox);
	scale = (uint16_t)get_scale(a->walkbox, a->pos.x, a->pos.y);
	if (scale > 255)
		fatal("actor scale out of range", a->number, scale);
	a->scalex = a->scaley = (uint8_t)scale;
}

static void set_box(struct actor *a, int box)
{
	a->walkbox = (uint8_t)box;
	setup_actor_scale(a);
}

static int is_player(struct actor *a)
{
	return get_class(a->number, CLS_PLAYER);
}

static int in_box_quick_reject(const struct box_coords *b, int x, int y, int threshold)
{
	int t = x - threshold;
	if (t > b->ul.x && t > b->ur.x && t > b->lr.x && t > b->ll.x)
		return 1;
	t = x + threshold;
	if (t < b->ul.x && t < b->ur.x && t < b->lr.x && t < b->ll.x)
		return 1;
	t = y - threshold;
	if (t > b->ul.y && t > b->ur.y && t > b->lr.y && t > b->ll.y)
		return 1;
	t = y + threshold;
	if (t < b->ul.y && t < b->ur.y && t < b->lr.y && t < b->ll.y)
		return 1;
	return 0;
}

struct adjust_box_result adjust_xy_to_be_in_box(struct actor *a, int dst_x, int dst_y)
{
	static const int threshold_table[] = { 30, 80, 0 };
	struct adjust_box_result abr;
	int16_t tmp_x, tmp_y;
	int tmp_dist, best_dist, threshold, num_boxes, box, t_idx;
	uint8_t flags, best_box;

	abr.x = (int16_t)dst_x;
	abr.y = (int16_t)dst_y;
	abr.box = INVALID_BOX;

	if (a->ignore_boxes)
		return abr;

	for (t_idx = 0; t_idx < 3; t_idx++) {
		threshold = threshold_table[t_idx];
		num_boxes = get_num_boxes() - 1;
		if (num_boxes < 1)
			return abr;

		best_dist = 0xFFFF;
		best_box = INVALID_BOX;

		for (box = num_boxes; box >= 1; box--) {
			struct box_coords bc;

			flags = (uint8_t)get_box_flags(box);
			if ((flags & BOX_INVISIBLE) && !((flags & BOX_PLAYER_ONLY) && !is_player(a)))
				continue;

			bc = get_box_coordinates(box);
			if (threshold > 0 && in_box_quick_reject(&bc, dst_x, dst_y, threshold))
				continue;

			if (check_xy_in_box_bounds(box, dst_x, dst_y)) {
				a->last_valid_x = (int16_t)dst_x;
				a->last_valid_y = (int16_t)dst_y;
				abr.x = (int16_t)dst_x;
				abr.y = (int16_t)dst_y;
				abr.box = (uint8_t)box;
				return abr;
			}

			tmp_dist = get_closest_pt_on_box(&bc, dst_x, dst_y, &tmp_x, &tmp_y);
			if (tmp_dist < best_dist) {
				a->last_valid_x = tmp_x;
				a->last_valid_y = tmp_y;
				abr.x = tmp_x;
				abr.y = tmp_y;
				if (tmp_dist == 0) {
					abr.box = (uint8_t)box;
					return abr;
				}
				best_dist = tmp_dist;
				best_box = (uint8_t)box;
			}
		}

		if (threshold == 0 || threshold * threshold >= best_dist) {
			abr.box = best_box;
			return abr;
		}
	}
	return abr;
}

/* ---- costume animation ---- */

void start_anim_actor(struct actor *a, int f)
{
	switch (f) {
	case CHORE_REDIRECT_INIT: f = a->init_frame; break;
	case CHORE_REDIRECT_WALK: f = a->walk_frame; break;
	case CHORE_REDIRECT_STAND: f = a->stand_frame; break;
	case CHORE_REDIRECT_START_TALK: f = a->talk_start_frame; break;
	case CHORE_REDIRECT_STOP_TALK: f = a->talk_stop_frame; break;
	}

	if (is_in_current_room(a) && a->costume != 0) {
		a->anim_progress = 0;
		a->needs_redraw = 1;
		a->cost.anim_counter = 0;
		if (f == a->init_frame)
			cost_reset(&a->cost);
		costume_decode_data(a, f, 0xFFFFFFFFu);
		a->frame = (uint8_t)f;
	}
}

void set_direction(struct actor *a, int direction)
{
	unsigned amask;
	int i;
	uint16_t vald;

	direction = (direction + 360) % 360;
	if (a->facing == direction)
		return;
	a->facing = (int16_t)direction;

	if (a->costume == 0)
		return;
	if (!is_in_current_room(a))
		return;

	amask = 0x8000;
	for (i = 0; i < 16; i++, amask >>= 1) {
		vald = a->cost.frame[i];
		if (vald == 0xFFFF)
			continue;
		if ((vald & 3) == new_dir_to_old_dir(a->facing))
			continue;
		vald >>= 2;
		costume_decode_data(a, vald, amask);
	}
	a->needs_redraw = 1;
}

void stop_actor_moving(struct actor *a)
{
	if (a->walk_script)
		stop_script(a->walk_script);
	a->moving = 0;
}

void turn_to_direction(struct actor *a, int newdir)
{
	if (newdir == -1 || a->ignore_turns)
		return;
	a->target_facing = (int16_t)newdir;
	a->moving = MF_TURN;
}

void animate_actor(struct actor *a, int anim)
{
	int chore = anim >> 2, dir = old_dir_to_new_dir(anim & 3);

	chore = 0x3F - chore + 2;
	switch (chore) {
	case 2:
		if (is_in_current_room(a)) {
			start_anim_actor(a, a->stand_frame);
			stop_actor_moving(a);
		}
		break;
	case 3:
		if (is_in_current_room(a))
			a->moving &= (uint8_t)~MF_TURN;
		set_direction(a, dir);
		break;
	case 4:
		if (is_in_current_room(a))
			turn_to_direction(a, dir);
		break;
	default:
		start_anim_actor(a, anim);
	}
}

static void animate_costume(struct actor *a)
{
	if (a->costume == 0)
		return;
	a->anim_progress++;
	if (a->anim_progress >= a->anim_speed) {
		a->anim_progress = 0;
		if (costume_increase_anims(a))
			a->needs_redraw = 1;
	}
}

/* ---- strip usage bits ---- */

#define NUM_USAGE_STRIPS 201
static uint32_t gfx_usage[NUM_USAGE_STRIPS * 3];

void set_gfx_usage_bit(int strip, int bit)
{
	bit--;
	gfx_usage[3 * strip + bit / 32] |= 1u << (bit % 32);
}

static void clear_gfx_usage_bit(int strip, int bit)
{
	bit--;
	gfx_usage[3 * strip + bit / 32] &= ~(1u << (bit % 32));
}

static int test_gfx_usage_bit(int strip, int bit)
{
	bit--;
	return (gfx_usage[3 * strip + bit / 32] & (1u << (bit % 32))) != 0;
}

static int test_gfx_any_usage_bits(int strip)
{
	return gfx_usage[3 * strip] || gfx_usage[3 * strip + 1] || (gfx_usage[3 * strip + 2] & 0x3FFFFFFF);
}

static int test_gfx_other_usage_bits(int strip, int bit)
{
	uint32_t mask[3] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
	int i;

	bit--;
	mask[bit / 32] &= ~(1u << (bit % 32));
	for (i = 0; i < 3; i++)
		if (gfx_usage[3 * strip + i] & mask[i])
			return 1;
	return 0;
}

void clear_gfx_usage_bits(void)
{
	memset(gfx_usage, 0, sizeof(gfx_usage));
}

extern void (*prof_hook)(int phase);
#define PROF(n) do { if (prof_hook) prof_hook(n); } while (0)

int test_gfx_usage_bit_ext(int strip, int bit)
{
	return test_gfx_usage_bit(strip, bit);
}

static void set_actor_redraw_flags(void)
{
	int i, j;

	if (g.full_redraw) {
		for (j = 1; j < NUM_ACTORS; j++)
			g.actors[j].needs_redraw = 1;
		return;
	}
	for (i = 0; i < SCREEN_WIDTH / 8; i++) {
		int strip = g.screen_start_strip + i;
		if (test_gfx_any_usage_bits(strip))
			for (j = 1; j < NUM_ACTORS; j++)
				if (test_gfx_usage_bit(strip, j) && test_gfx_other_usage_bits(strip, j))
					g.actors[j].needs_redraw = 1;
	}
}

static void reset_actor_bgs(void)
{
	int i, j, run;

	for (i = 0; i < SCREEN_WIDTH / 8; i++) {
		clear_gfx_usage_bit(g.screen_start_strip + i, USAGE_BIT_DIRTY);
		clear_gfx_usage_bit(g.screen_start_strip + i, USAGE_BIT_RESTORED);
	}
	/* actor j is bit j - 1 of a strip's first usage word (NUM_ACTORS <= 32);
	 * its strips in a row are restored together */
	for (j = 1; j < NUM_ACTORS; j++) {
		struct actor *a = &g.actors[j];
		if (!((a->top != 0x7fffffff && a->needs_redraw) || a->need_bg_reset))
			continue;
		run = -1;
		for (i = 0; i <= SCREEN_WIDTH / 8; i++) {
			int strip = g.screen_start_strip + i;
			if (i < SCREEN_WIDTH / 8 && (gfx_usage[3 * strip] >> (j - 1) & 1)) {
				clear_gfx_usage_bit(strip, j);
				if (run < 0)
					run = i;
				continue;
			}
			if (run >= 0 && a->bottom - a->top >= 0)
				gfx_reset_background_run(a->top, a->bottom, run, i);
			run = -1;
		}
	}
	display_sync();
	for (i = 1; i < NUM_ACTORS; i++)
		g.actors[i].need_bg_reset = 0;
}

/* drawActorCostume(): what drawing decides besides pixels. */
static void draw_actor_costume(struct actor *a)
{
	if (a->costume == 0 || !a->needs_redraw)
		return;
	a->needs_redraw = 0;
	setup_actor_scale(a);
	if (costume_draw(a) & 1)
		a->needs_redraw = 1;
}

/* scummLoop_handleActors(): redraw flags, background reset, then actors
 * sorted by y are drawn and animated, in the original's order. */
void process_actors(void)
{
	struct actor *sorted[NUM_ACTORS], *t;
	int n = 0, i, j;

	set_actor_redraw_flags();
	PROF(9);
	reset_actor_bgs();
	PROF(10);

	for (i = 1; i < NUM_ACTORS; i++)
		if (is_in_current_room(&g.actors[i]))
			sorted[n++] = &g.actors[i];
	for (j = 0; j < n; j++)
		for (i = 0; i < n; i++)
			if (sorted[j]->pos.y < sorted[i]->pos.y) {
				t = sorted[i];
				sorted[i] = sorted[j];
				sorted[j] = t;
			}
	for (i = 0; i < n; i++)
		if (sorted[i]->costume) {
			PROF(11);
			draw_actor_costume(sorted[i]);
			PROF(13);
			animate_costume(sorted[i]);
			PROF(10);
		}
}

void set_actor_costume(struct actor *a, int c)
{
	int i;

	a->costume_needs_init = 1;
	if (a->visible) {
		hide_actor(a);
		cost_reset(&a->cost);
		a->costume = (uint16_t)c;
		show_actor(a);
	} else {
		a->costume = (uint16_t)c;
		cost_reset(&a->cost);
	}
	for (i = 0; i < 32; i++)
		a->palette[i] = 0xFF;
}

void set_actor_scale(struct actor *a, int sx, int sy)
{
	if (sx != -1)
		a->scalex = (uint8_t)sx;
	if (sy != -1)
		a->scaley = (uint8_t)sy;
	a->needs_redraw = 1;
}

void set_actor_elevation(struct actor *a, int e)
{
	if (a->elevation != e) {
		a->elevation = (int16_t)e;
		a->needs_redraw = 1;
	}
}

void set_actor_palette(struct actor *a, int idx, int val)
{
	a->palette[idx] = (uint8_t)val;
	a->needs_redraw = 1;
}

/* ---- init, placement, visibility ---- */

void set_actor_walk_speed(struct actor *a, int sx, int sy);
static int calc_movement_factor(struct actor *a, struct point next);

void init_actor(struct actor *a, int mode)
{
	if (mode == -1) {
		a->top = a->bottom = 0;
		a->needs_redraw = 0;
		a->need_bg_reset = 0;
		a->costume_needs_init = 0;
		a->visible = 0;
		a->flip = 0;
		a->speedx = 8;
		a->speedy = 2;
		a->frame = 0;
		a->walkbox = 0;
		a->anim_progress = 0;
		memset(a->palette, 0, sizeof(a->palette));
		memset(a->sound, 0, sizeof(a->sound));
		cost_reset(&a->cost);
		memset(&a->walkdata, 0, sizeof(a->walkdata));
		a->walkdata.point3.x = 32000;
		a->walk_script = 0;
	}

	if (mode == 1 || mode == -1) {
		a->costume = 0;
		a->room = 0;
		a->pos.x = 0;
		a->pos.y = 0;
		a->facing = 180;
	} else if (mode == 2) {
		a->facing = 180;
	}
	a->elevation = 0;
	a->width = 24;
	a->talk_color = 15;
	a->talk_pos_x = 0;
	a->talk_pos_y = -80;
	a->boxscale = a->scaley = a->scalex = 0xFF;
	a->charset = 0;
	memset(a->sound, 0, sizeof(a->sound));
	a->target_facing = a->facing;
	a->last_valid_x = 0;
	a->last_valid_y = 0;
	a->shadow_mode = 0;

	stop_actor_moving(a);
	set_actor_walk_speed(a, 8, 2);

	a->anim_speed = 0;
	a->ignore_boxes = 0;
	a->force_clip = 0;
	a->ignore_turns = 0;

	a->init_frame = 1;
	a->walk_frame = 2;
	a->stand_frame = 3;
	a->talk_start_frame = 4;
	a->talk_stop_frame = 5;

	a->walk_script = 0;
	a->talk_script = 0;

	g.class_data[a->number] = 0;
}

static void adjust_actor_pos(struct actor *a)
{
	struct adjust_box_result abr = adjust_xy_to_be_in_box(a, a->pos.x, a->pos.y);

	a->pos.x = abr.x;
	a->pos.y = abr.y;
	a->walkdata.destbox = abr.box;
	set_box(a, abr.box);
	a->walkdata.dest.x = -1;
	stop_actor_moving(a);
	a->cost.sound_counter = 0;
	a->cost.sound_pos = 0;

	if (a->walkbox != INVALID_BOX) {
		if (get_box_flags(a->walkbox) & 7)
			turn_to_direction(a, a->facing);
	}
}

void hide_actor(struct actor *a)
{
	if (!a->visible)
		return;
	if (a->moving) {
		stop_actor_moving(a);
		start_anim_actor(a, a->stand_frame);
	}
	a->visible = 0;
	a->cost.sound_counter = 0;
	a->cost.sound_pos = 0;
	a->needs_redraw = 0;
	a->need_bg_reset = 1;
}

void show_actor(struct actor *a)
{
	if (g.current_room == 0 || a->visible)
		return;

	adjust_actor_pos(a);
	res_get(RT_COSTUME, a->costume);

	if (a->costume_needs_init) {
		start_anim_actor(a, a->init_frame);
		a->costume_needs_init = 0;
	}
	stop_actor_moving(a);
	a->visible = 1;
	a->needs_redraw = 1;
}

void show_actors(void)
{
	int i;
	for (i = 1; i < NUM_ACTORS; i++)
		if (is_in_current_room(&g.actors[i]))
			show_actor(&g.actors[i]);
}

void put_actor(struct actor *a, int x, int y, int room)
{
	if (a->visible && g.current_room != room && get_talking_actor() == a->number)
		stop_talk();

	a->pos.x = (int16_t)x;
	a->pos.y = (int16_t)y;
	a->room = (uint8_t)room;
	a->needs_redraw = 1;

	if (VAR(VAR_EGO) == a->number)
		g.ego_positioned = 1;

	if (a->visible) {
		if (is_in_current_room(a)) {
			if (a->moving) {
				stop_actor_moving(a);
				start_anim_actor(a, a->stand_frame);
			}
			adjust_actor_pos(a);
		} else {
			hide_actor(a);
		}
	} else {
		if (is_in_current_room(a))
			show_actor(a);
	}
}

void put_actor_here(struct actor *a)
{
	put_actor(a, a->pos.x, a->pos.y, a->room);
}

void face_to_object(struct actor *a, int obj)
{
	int x2, y2;

	if (!is_in_current_room(a))
		return;
	if (get_object_or_actor_xy(obj, &x2, &y2) == -1)
		return;
	turn_to_direction(a, x2 > a->pos.x ? 90 : 270);
}

/* ---- walking ---- */

void set_actor_walk_speed(struct actor *a, int sx, int sy)
{
	if (sx == a->speedx && sy == a->speedy)
		return;
	a->speedx = (uint16_t)sx;
	a->speedy = (uint16_t)sy;
	if (a->moving)
		calc_movement_factor(a, a->walkdata.next);
}

static int remap_direction(struct actor *a, int dir, int is_walking)
{
	int specdir, flip_x, flip_y;
	uint8_t flags;

	if (!a->ignore_boxes) {
		if (a->walkbox != OLD_INVALID_BOX) {
			specdir = g.extra_box_flags[a->walkbox];
			if (specdir) {
				if (specdir & 0x8000) {
					dir = specdir & 0x3FFF;
				} else {
					specdir &= 0x3FFF;
					if (specdir - 90 < dir && dir < specdir + 90)
						dir = specdir;
					else
						dir = specdir + 180;
				}
			}
		}

		flags = (uint8_t)get_box_flags(a->walkbox);
		flip_x = a->walkdata.delta_x_factor > 0;
		flip_y = a->walkdata.delta_y_factor > 0;

		if ((flags & BOX_XFLIP) || get_class(a->number, CLS_XFLIP)) {
			dir = 360 - dir;
			flip_x = !flip_x;
		}
		if ((flags & BOX_YFLIP) || get_class(a->number, CLS_YFLIP)) {
			dir = 180 - dir;
			flip_y = !flip_y;
		}

		switch (flags & 7) {
		case 1:
			if (is_walking)
				return flip_x ? 90 : 270;
			return dir == 90 ? 90 : 270;
		case 2:
			if (is_walking)
				return flip_y ? 180 : 0;
			return dir == 0 ? 0 : 180;
		case 3:
			return 270;
		case 4:
			return 90;
		case 5:
			return 0;
		case 6:
			return 180;
		}
	}
	dir = (dir + 360) % 360;
	return dir | 0x400;
}

static int update_actor_direction(struct actor *a, int is_walking)
{
	static const uint8_t interpolate[] = { 0, 2, 2, 3, 2, 1, 2, 3, 0, 1, 2, 1, 0, 1, 0, 3 };
	int dir = remap_direction(a, a->target_facing, is_walking);

	if (dir & 0x400)
		dir = old_dir_to_new_dir(interpolate[new_dir_to_old_dir(dir & 0x3FF) | (new_dir_to_old_dir(a->facing) << 2)]);
	return dir;
}

static void start_walk_anim(struct actor *a, int cmd, int angle)
{
	if (angle == -1)
		angle = a->facing;
	if (a->walk_script) {
		int32_t args[NUM_SCRIPT_LOCAL] = { 0 };
		args[0] = a->number;
		args[1] = cmd;
		args[2] = angle;
		run_script(a->walk_script, 1, 0, args);
	} else {
		if (cmd == 3)
			turn_to_direction(a, angle);
		else
			set_direction(a, angle);
		if (cmd == 1)
			start_anim_actor(a, a->walk_frame);
		else if (cmd == 3)
			start_anim_actor(a, a->stand_frame);
	}
}

static int actor_walk_step(struct actor *a)
{
	int next_facing, dist_x, dist_y, tmp_x, tmp_y;

	a->needs_redraw = 1;

	next_facing = update_actor_direction(a, 1);
	if ((a->walk_frame != a->frame && !(a->moving & MF_IN_LEG)) || a->facing != next_facing)
		start_walk_anim(a, 1, next_facing);
	a->moving |= MF_IN_LEG;

	if (a->walkbox != a->walkdata.curbox && check_xy_in_box_bounds(a->walkdata.curbox, a->pos.x, a->pos.y))
		set_box(a, a->walkdata.curbox);

	dist_x = iabs(a->walkdata.next.x - a->walkdata.cur.x);
	dist_y = iabs(a->walkdata.next.y - a->walkdata.cur.y);

	if (iabs(a->pos.x - a->walkdata.cur.x) >= dist_x && iabs(a->pos.y - a->walkdata.cur.y) >= dist_y) {
		a->moving &= (uint8_t)~MF_IN_LEG;
		return 0;
	}

	tmp_x = a->pos.x * 0x10000 + a->walkdata.xfrac + (a->walkdata.delta_x_factor >> 8) * a->scalex;
	a->walkdata.xfrac = (uint16_t)tmp_x;
	a->pos.x = (int16_t)(tmp_x >> 16);

	tmp_y = a->pos.y * 0x10000 + a->walkdata.yfrac + (a->walkdata.delta_y_factor >> 8) * a->scaley;
	a->walkdata.yfrac = (uint16_t)tmp_y;
	a->pos.y = (int16_t)(tmp_y >> 16);

	if (iabs(a->pos.x - a->walkdata.cur.x) > dist_x)
		a->pos.x = a->walkdata.next.x;
	if (iabs(a->pos.y - a->walkdata.cur.y) > dist_y)
		a->pos.y = a->walkdata.next.y;

	if (a->pos.x == a->walkdata.next.x && a->pos.y == a->walkdata.next.y) {
		a->moving &= (uint8_t)~MF_IN_LEG;
		return 0;
	}
	return 1;
}

static int calc_movement_factor(struct actor *a, struct point next)
{
	int diff_x, diff_y;
	int32_t delta_x, delta_y;

	if (a->pos.x == next.x && a->pos.y == next.y)
		return 0;

	diff_x = next.x - a->pos.x;
	diff_y = next.y - a->pos.y;

	delta_y = (int32_t)a->speedy << 16;
	if (diff_y < 0)
		delta_y = -delta_y;

	delta_x = delta_y * diff_x;
	if (diff_y != 0)
		delta_x /= diff_y;
	else
		delta_y = 0;

	if ((unsigned)iabs(delta_x / 0x10000) > a->speedx) {
		delta_x = (int32_t)a->speedx << 16;
		if (diff_x < 0)
			delta_x = -delta_x;
		delta_y = delta_x * diff_y;
		if (diff_x != 0)
			delta_y /= diff_x;
		else
			delta_x = 0;
	}

	a->walkdata.xfrac = 0;
	a->walkdata.yfrac = 0;
	a->walkdata.cur = a->pos;
	a->walkdata.next = next;
	a->walkdata.delta_x_factor = delta_x;
	a->walkdata.delta_y_factor = delta_y;

	a->target_facing = (int16_t)((iabs(diff_y) * 3 > iabs(diff_x)) ? (delta_y > 0 ? 180 : 0) : (delta_x > 0 ? 90 : 270));

	return actor_walk_step(a);
}

#define SWAP16(x, y) do { int16_t t_ = (x); (x) = (y); (y) = t_; } while (0)

static void rotate_box(struct box_coords *b)
{
	struct point tmp = b->ul;
	b->ul = b->ur;
	b->ur = b->lr;
	b->lr = b->ll;
	b->ll = tmp;
}

static int find_path_towards(struct actor *a, uint8_t box1nr, uint8_t box2nr, uint8_t box3nr, struct point *found)
{
	struct box_coords box1 = get_box_coordinates(box1nr);
	struct box_coords box2 = get_box_coordinates(box2nr);
	int i, j, flag, q, pos;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			if (box1.ul.x == box1.ur.x && box1.ul.x == box2.ul.x && box1.ul.x == box2.ur.x) {
				flag = 0;
				if (box1.ul.y > box1.ur.y) {
					SWAP16(box1.ul.y, box1.ur.y);
					flag |= 1;
				}
				if (box2.ul.y > box2.ur.y) {
					SWAP16(box2.ul.y, box2.ur.y);
					flag |= 2;
				}
				if (box1.ul.y > box2.ur.y || box2.ul.y > box1.ur.y ||
				    ((box1.ur.y == box2.ul.y || box2.ur.y == box1.ul.y) &&
				     box1.ul.y != box1.ur.y && box2.ul.y != box2.ur.y)) {
					if (flag & 1)
						SWAP16(box1.ul.y, box1.ur.y);
					if (flag & 2)
						SWAP16(box2.ul.y, box2.ur.y);
				} else {
					pos = a->pos.y;
					if (box2nr == box3nr) {
						int diff_x = a->walkdata.dest.x - a->pos.x;
						int diff_y = a->walkdata.dest.y - a->pos.y;
						int box_diff_x = box1.ul.x - a->pos.x;
						if (diff_x != 0) {
							int t;
							diff_y *= box_diff_x;
							t = diff_y / diff_x;
							if (t == 0 && (diff_y <= 0 || diff_x <= 0) && (diff_y >= 0 || diff_x >= 0))
								t = -1;
							pos = a->pos.y + t;
						}
					}
					q = pos;
					if (q < box2.ul.y)
						q = box2.ul.y;
					if (q > box2.ur.y)
						q = box2.ur.y;
					if (q < box1.ul.y)
						q = box1.ul.y;
					if (q > box1.ur.y)
						q = box1.ur.y;
					if (q == pos && box2nr == box3nr)
						return 1;
					found->y = (int16_t)q;
					found->x = box1.ul.x;
					return 0;
				}
			}

			if (box1.ul.y == box1.ur.y && box1.ul.y == box2.ul.y && box1.ul.y == box2.ur.y) {
				flag = 0;
				if (box1.ul.x > box1.ur.x) {
					SWAP16(box1.ul.x, box1.ur.x);
					flag |= 1;
				}
				if (box2.ul.x > box2.ur.x) {
					SWAP16(box2.ul.x, box2.ur.x);
					flag |= 2;
				}
				if (box1.ul.x > box2.ur.x || box2.ul.x > box1.ur.x ||
				    ((box1.ur.x == box2.ul.x || box2.ur.x == box1.ul.x) &&
				     box1.ul.x != box1.ur.x && box2.ul.x != box2.ur.x)) {
					if (flag & 1)
						SWAP16(box1.ul.x, box1.ur.x);
					if (flag & 2)
						SWAP16(box2.ul.x, box2.ur.x);
				} else {
					if (box2nr == box3nr) {
						int diff_x = a->walkdata.dest.x - a->pos.x;
						int diff_y = a->walkdata.dest.y - a->pos.y;
						int box_diff_y = box1.ul.y - a->pos.y;
						pos = a->pos.x;
						if (diff_y != 0)
							pos += diff_x * box_diff_y / diff_y;
					} else {
						pos = a->pos.x;
					}
					q = pos;
					if (q < box2.ul.x)
						q = box2.ul.x;
					if (q > box2.ur.x)
						q = box2.ur.x;
					if (q < box1.ul.x)
						q = box1.ul.x;
					if (q > box1.ur.x)
						q = box1.ur.x;
					if (q == pos && box2nr == box3nr)
						return 1;
					found->x = (int16_t)q;
					found->y = box1.ul.y;
					return 0;
				}
			}
			rotate_box(&box1);
		}
		rotate_box(&box2);
	}
	return 0;
}

void start_walk_actor(struct actor *a, int dest_x, int dest_y, int dir)
{
	struct adjust_box_result abr = adjust_xy_to_be_in_box(a, dest_x, dest_y);

	if (!is_in_current_room(a)) {
		a->pos.x = abr.x;
		a->pos.y = abr.y;
		if (!a->ignore_turns && dir != -1)
			a->facing = (int16_t)dir;
		return;
	}

	if (a->ignore_boxes) {
		abr.box = INVALID_BOX;
		a->walkbox = INVALID_BOX;
	} else {
		if (check_xy_in_box_bounds(a->walkdata.destbox, abr.x, abr.y))
			abr.box = a->walkdata.destbox;
		else
			abr = adjust_xy_to_be_in_box(a, abr.x, abr.y);
		if (a->moving && a->walkdata.destdir == dir && a->walkdata.dest.x == abr.x && a->walkdata.dest.y == abr.y)
			return;
	}

	if (a->pos.x == abr.x && a->pos.y == abr.y) {
		if (dir != a->facing)
			turn_to_direction(a, dir);
		return;
	}

	a->walkdata.dest.x = abr.x;
	a->walkdata.dest.y = abr.y;
	a->walkdata.destbox = abr.box;
	a->walkdata.destdir = (int16_t)dir;
	a->walkdata.point3.x = 32000;
	a->walkdata.curbox = a->walkbox;
	a->moving = (uint8_t)((a->moving & MF_IN_LEG) | MF_NEW_LEG);
}

static void walk_actor(struct actor *a)
{
	int new_dir, next_box;
	struct point found;

	if (!a->moving)
		return;

	if (!(a->moving & MF_NEW_LEG)) {
		if ((a->moving & MF_IN_LEG) && actor_walk_step(a))
			return;

		if (a->moving & MF_LAST_LEG) {
			a->moving = 0;
			set_box(a, a->walkdata.destbox);
			start_anim_actor(a, a->stand_frame);
			if (a->target_facing != a->walkdata.destdir)
				turn_to_direction(a, a->walkdata.destdir);
			return;
		}

		if (a->moving & MF_TURN) {
			new_dir = update_actor_direction(a, 0);
			if (a->facing != new_dir)
				set_direction(a, new_dir);
			else
				a->moving = 0;
			return;
		}

		set_box(a, a->walkdata.curbox);
		a->moving &= MF_IN_LEG;
	}

	a->moving &= (uint8_t)~MF_NEW_LEG;
	for (;;) {
		if (a->walkbox == INVALID_BOX) {
			set_box(a, a->walkdata.destbox);
			a->walkdata.curbox = a->walkdata.destbox;
			break;
		}
		if (a->walkbox == a->walkdata.destbox)
			break;

		next_box = get_next_box(a->walkbox, a->walkdata.destbox);
		if (next_box < 0) {
			a->walkdata.destbox = a->walkbox;
			a->moving |= MF_LAST_LEG;
			return;
		}
		a->walkdata.curbox = (uint8_t)next_box;

		if (find_path_towards(a, a->walkbox, (uint8_t)next_box, a->walkdata.destbox, &found))
			break;
		if (calc_movement_factor(a, found))
			return;
		set_box(a, a->walkdata.curbox);
	}

	a->moving |= MF_LAST_LEG;
	calc_movement_factor(a, a->walkdata.dest);
}

void walk_actors(void)
{
	int i;
	for (i = 1; i < NUM_ACTORS; i++)
		if (is_in_current_room(&g.actors[i]))
			walk_actor(&g.actors[i]);
}

void walk_actor_to_actor(int actor, int to_actor, int dist)
{
	struct actor *a = deref_actor(actor);
	struct actor *to = deref_actor(to_actor);
	int x, y;

	if (dist == 0xFF) {
		dist = a->scalex * a->width / 0xFF;
		dist += (to->scalex * to->width / 0xFF) / 2;
	}
	x = to->pos.x;
	y = to->pos.y;
	if (x < a->pos.x)
		x += dist;
	else
		x -= dist;
	start_walk_actor(a, x, y, -1);
}

int get_actor_from_pos(int x, int y)
{
	int i;

	if (x < 0 || x / 8 >= NUM_USAGE_STRIPS || !test_gfx_any_usage_bits(x / 8))
		return 0;
	for (i = 1; i < NUM_ACTORS; i++)
		if (test_gfx_usage_bit(x / 8, i) && !get_class(i, CLS_UNTOUCHABLE) &&
		    y >= g.actors[i].top && y <= g.actors[i].bottom)
			return i;
	return 0;
}

void play_actor_sounds(void)
{
	int i, j;

	for (i = 1; i < NUM_ACTORS; i++) {
		if (g.actors[i].cost.sound_counter && is_in_current_room(&g.actors[i])) {
			g.current_script = 0xFF;
			sound_start(g.actors[i].sound[0]);
			for (j = 1; j < NUM_ACTORS; j++)
				g.actors[j].cost.sound_counter = 0;
			return;
		}
	}
}
