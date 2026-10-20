/* scumm5-amiga - saved games
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What is kept and how a game comes back follow ScummVM
 * engines/scumm/saveload.cpp (saveLoadWithSerializer, loadState), ScummVM
 * commit 96b42d42bc8533953bdd701db059cc33c839a9ff; the file format is our
 * own. ScummVM is the legal property of its developers, whose names are
 * too numerous to list here; see the COPYRIGHT file distributed with the
 * ScummVM source.
 *
 * A game is saved from the main menu, between reading the input and
 * running the scripts of a frame, and is loaded at that same point. The
 * room, costumes, scripts and sounds come back from the pack; what the
 * scripts made (variables, inventory copies, strings, names, flobjects,
 * box data) is in the file.
 */
#include "scumm.h"
#include "gfx.h"
#include "imuse.h"
#include "saveload.h"

#define SAVE_VERSION 1

extern int16_t screen_b, screen_h;
extern uint8_t screen_effect_flag, palette_dirty, cursor_anim, cursor_color;

static char base[128];
static struct ser ser;

/* ---- the serializer ---- */

static void ser_flush(struct ser *s)
{
	if (s->pos && !s->err && plat_write(s->f, s->buf, s->pos) != (int32_t)s->pos)
		s->err = 1;
	s->pos = 0;
}

void ser_bytes(struct ser *s, void *p, uint32_t n)
{
	uint8_t *d = p;

	while (n) {
		uint32_t k;
		if (s->saving) {
			if (s->pos == sizeof(s->buf))
				ser_flush(s);
			k = sizeof(s->buf) - s->pos;
			if (k > n)
				k = n;
			memcpy(s->buf + s->pos, d, k);
		} else {
			if (s->pos == s->len) {
				int32_t r = s->err ? 0 : plat_read(s->f, s->buf, sizeof(s->buf));
				s->pos = 0;
				s->len = (uint16_t)(r > 0 ? r : 0);
				if (!s->len) {
					s->err = 1;
					memset(d, 0, n);
					return;
				}
			}
			k = (uint32_t)(s->len - s->pos);
			if (k > n)
				k = n;
			memcpy(d, s->buf + s->pos, k);
		}
		s->pos = (uint16_t)(s->pos + k);
		d += k;
		n -= k;
	}
}

void ser_int(struct ser *s, void *p, int size)
{
	uint8_t b[4];
	uint32_t v = 0;
	int i;

	if (s->saving) {
		if (size == 1)
			v = *(uint8_t *)p;
		else if (size == 2)
			v = *(uint16_t *)p;
		else
			v = *(uint32_t *)p;
		for (i = 0; i < size; i++)
			b[i] = (uint8_t)(v >> (8 * (size - 1 - i)));
		ser_bytes(s, b, (uint32_t)size);
		return;
	}
	ser_bytes(s, b, (uint32_t)size);
	for (i = 0; i < size; i++)
		v = v << 8 | b[i];
	if (size == 1)
		*(uint8_t *)p = (uint8_t)v;
	else if (size == 2)
		*(uint16_t *)p = (uint16_t)v;
	else
		*(uint32_t *)p = v;
}

void ser_ints(struct ser *s, void *p, int size, int n)
{
	uint8_t *q = p;

	for (; n > 0; n--, q += size)
		ser_int(s, q, size);
}

void ser_blob(struct ser *s, uint8_t **p)
{
	uint32_t n = s->saving && *p ? res_alloc_size(*p) : 0;

	SV(s, n);
	if (!s->saving) {
		res_free(*p);
		*p = 0;
		if (!n || s->err)
			return;
		*p = n < 0x100000 ? res_alloc(n) : 0;
		if (!*p) {
			s->err = 1;
			return;
		}
	}
	ser_bytes(s, *p, n);
}

/* ---- the state ---- */

static void sync_point(struct ser *s, struct point *p)
{
	SV(s, p->x);
	SV(s, p->y);
}

static void sync_rect(struct ser *s, struct rect *r)
{
	SV(s, r->left);
	SV(s, r->top);
	SV(s, r->right);
	SV(s, r->bottom);
}

static void sync_slot(struct ser *s, struct script_slot *ss)
{
	SV(s, ss->offs);
	SV(s, ss->delay);
	SV(s, ss->number);
	SV(s, ss->delay_frame_count);
	SV(s, ss->status);
	SV(s, ss->where);
	SV(s, ss->freeze_resistant);
	SV(s, ss->recursive);
	SV(s, ss->freeze_count);
	SV(s, ss->didexec);
	SV(s, ss->cutscene_override);
	SV(s, ss->cycle);
}

static void sync_object(struct ser *s, struct object_data *od)
{
	SV(s, od->obim_offset);
	SV(s, od->obcd_offset);
	SV(s, od->walk_x);
	SV(s, od->walk_y);
	SV(s, od->obj_nr);
	SV(s, od->x_pos);
	SV(s, od->y_pos);
	SV(s, od->width);
	SV(s, od->height);
	SV(s, od->actordir);
	SV(s, od->parent);
	SV(s, od->parentstate);
	SV(s, od->state);
	SV(s, od->fl_object_index);
	SV(s, od->flags);
}

static void sync_actor(struct ser *s, struct actor *a)
{
	struct walk_data *w = &a->walkdata;
	struct cost_state *c = &a->cost;

	SV(s, a->number);
	sync_point(s, &a->pos);
	SV(s, a->top);
	SV(s, a->bottom);
	SV(s, a->width);
	SV(s, a->room);
	SV(s, a->talk_color);
	SV(s, a->talk_pos_x);
	SV(s, a->talk_pos_y);
	SV(s, a->talk_script);
	SV(s, a->walk_script);
	SV(s, a->ignore_boxes);
	SV(s, a->force_clip);
	SV(s, a->init_frame);
	SV(s, a->walk_frame);
	SV(s, a->stand_frame);
	SV(s, a->talk_start_frame);
	SV(s, a->talk_stop_frame);
	SV(s, a->needs_redraw);
	SV(s, a->need_bg_reset);
	SV(s, a->costume_needs_init);
	SV(s, a->visible);
	SV(s, a->shadow_mode);
	SV(s, a->flip);
	SV(s, a->frame);
	SV(s, a->walkbox);
	SV(s, a->elevation);
	SV(s, a->costume);
	SV(s, a->moving);
	SV(s, a->ignore_turns);
	SV(s, a->boxscale);
	SV(s, a->scalex);
	SV(s, a->scaley);
	SV(s, a->charset);
	SV(s, a->anim_speed);
	SV(s, a->anim_progress);
	SV(s, a->facing);
	SV(s, a->target_facing);
	SV(s, a->speedx);
	SV(s, a->speedy);
	SV(s, a->last_valid_x);
	SV(s, a->last_valid_y);
	SVA(s, a->sound);
	SVA(s, a->palette);
	sync_point(s, &w->dest);
	SV(s, w->destbox);
	SV(s, w->destdir);
	sync_point(s, &w->cur);
	sync_point(s, &w->next);
	sync_point(s, &w->point3);
	SV(s, w->delta_x_factor);
	SV(s, w->delta_y_factor);
	SV(s, w->xfrac);
	SV(s, w->yfrac);
	SV(s, w->curbox);
	SV(s, c->anim_counter);
	SV(s, c->sound_counter);
	SV(s, c->sound_pos);
	SV(s, c->stopped);
	SVA(s, c->curpos);
	SVA(s, c->start);
	SVA(s, c->end);
	SVA(s, c->frame);
	ser_blob(s, &a->name);
}

static void sync_verb(struct ser *s, struct verb_slot *v)
{
	sync_rect(s, &v->cur_rect);
	sync_rect(s, &v->old_rect);
	SV(s, v->verbid);
	SV(s, v->color);
	SV(s, v->hicolor);
	SV(s, v->dimcolor);
	SV(s, v->bkcolor);
	SV(s, v->type);
	SV(s, v->charset_nr);
	SV(s, v->curmode);
	SV(s, v->saveid);
	SV(s, v->key);
	SV(s, v->center);
	SV(s, v->prep);
	SV(s, v->imgindex);
	SV(s, v->orig_left);
	SV(s, v->image_key);
	ser_blob(s, &v->name);
}

static void sync_string_part(struct ser *s, struct string_tab_part *p)
{
	SV(s, p->xpos);
	SV(s, p->ypos);
	SV(s, p->right);
	SV(s, p->height);
	SV(s, p->color);
	SV(s, p->charset);
	SV(s, p->center);
	SV(s, p->overhead);
	SV(s, p->no_talk_anim);
	SV(s, p->wrapping);
}

static int16_t saved_b, saved_h;

static void sync_state(struct ser *s)
{
	int i;
	uint8_t type;
	uint16_t idx;

	/* scripts */
	for (i = 0; i < NUM_SCRIPT_SLOT; i++)
		sync_slot(s, &g.slot[i]);
	ser_ints(s, g.localvar, 4, NUM_SCRIPT_SLOT * (NUM_SCRIPT_LOCAL + 1));
	for (i = 0; i < MAX_NESTING; i++) {
		SV(s, g.nest[i].number);
		SV(s, g.nest[i].where);
		SV(s, g.nest[i].slot);
	}
	SV(s, g.num_nested);
	SV(s, g.cutscene_sp);
	SV(s, g.cutscene_script_index);
	SVA(s, g.cutscene_ptr);
	SVA(s, g.cutscene_script);
	SVA(s, g.cutscene_data);
	for (i = 0; i < NUM_SENTENCE; i++) {
		SV(s, g.sentences[i].verb);
		SV(s, g.sentences[i].preposition);
		SV(s, g.sentences[i].object_a);
		SV(s, g.sentences[i].object_b);
		SV(s, g.sentences[i].freeze_count);
	}
	SV(s, g.sentence_num);
	ser_ints(s, g.vars, 4, g.num_vars);
	ser_bytes(s, g.bit_vars, (uint32_t)(g.num_bit_vars + 7) / 8);

	/* objects */
	ser_bytes(s, g.object_owner, g.num_global_objects);
	ser_bytes(s, g.object_state, g.num_global_objects);
	ser_ints(s, g.class_data, 4, g.num_global_objects);
	for (i = 0; i < NUM_LOCAL_OBJECTS; i++)
		sync_object(s, &g.objs[i]);
	SVA(s, g.inventory);
	for (i = 0; i < NUM_INVENTORY; i++)
		ser_blob(s, &g.inventory_data[i]);
	for (i = 0; i < NUM_FL_OBJECTS; i++)
		ser_blob(s, &g.fl_objects[i]);
	SVA(s, g.new_names);
	for (i = 0; i < NUM_NEW_NAMES; i++)
		ser_blob(s, &g.new_name_data[i]);

	/* room */
	SV(s, g.current_room);
	SV(s, g.room_resource);
	SVA(s, g.resource_mapper);
	SV(s, g.num_objects_in_room);
	ser_blob(s, &g.box_data);
	SV(s, g.box_data_size);
	ser_blob(s, &g.box_matrix);
	SV(s, g.box_matrix_size);
	SVA(s, g.extra_box_flags);
	for (i = 0; i < NUM_SCALE_SLOTS; i++) {
		SV(s, g.scale_slots[i].x1);
		SV(s, g.scale_slots[i].y1);
		SV(s, g.scale_slots[i].scale1);
		SV(s, g.scale_slots[i].x2);
		SV(s, g.scale_slots[i].y2);
		SV(s, g.scale_slots[i].scale2);
	}
	SVA(s, g.room_palette);
	SVA(s, g.shadow_palette);
	SVA(s, g.current_palette);
	for (i = 0; i < NUM_COLOR_CYCLES; i++) {
		SV(s, g.color_cycle[i].delay);
		SV(s, g.color_cycle[i].counter);
		SV(s, g.color_cycle[i].flags);
		SV(s, g.color_cycle[i].start);
		SV(s, g.color_cycle[i].end);
	}
	SV(s, g.switch_room_effect);
	SV(s, g.switch_room_effect2);
	SV(s, g.new_effect);
	SV(s, g.ego_positioned);
	SV(s, g.screen_start_strip);
	SV(s, g.screen_end_strip);
	SV(s, g.screen_top);
	saved_b = screen_b;
	saved_h = screen_h;
	SV(s, saved_b);
	SV(s, saved_h);
	SV(s, screen_effect_flag);
	SV(s, gfx_zbuffer_disabled);

	/* actors and camera */
	for (i = 0; i < NUM_ACTORS; i++)
		sync_actor(s, &g.actors[i]);
	actor_usage_sync(s);
	sync_point(s, &g.camera.cur);
	sync_point(s, &g.camera.dest);
	sync_point(s, &g.camera.accel);
	sync_point(s, &g.camera.last);
	SV(s, g.camera.left_trigger);
	SV(s, g.camera.right_trigger);
	SV(s, g.camera.follows);
	SV(s, g.camera.mode);
	SV(s, g.camera.moving_to_actor);
	SV(s, g.talk_delay_pending);
	SV(s, g.talk_delay);
	SV(s, g.actor_to_print_str_for);
	SV(s, g.have_msg);
	SV(s, g.have_actor_speech_msg);
	SV(s, g.keep_text);
	SV(s, g.talking_actor);

	/* verbs and cursor */
	for (i = 0; i < NUM_VERBS; i++)
		sync_verb(s, &g.verbs[i]);
	SV(s, g.cursor_state);
	SV(s, g.user_put);
	SV(s, g.current_cursor);
	SV(s, cursor_anim);
	SV(s, cursor_color);

	/* text */
	for (i = 0; i < NUM_STRING_SLOTS; i++) {
		sync_string_part(s, &g.string[i].cur);
		sync_string_part(s, &g.string[i].def);
	}
	for (i = 0; i < NUM_ARRAY; i++)
		ser_blob(s, &g.strings[i]);
	SVA(s, g.string_size);
	SVA(s, g.charset_color_map);
	ser_bytes(s, g.charset_data, sizeof(g.charset_data));
	SV(s, g.cur_charset);
	SVA(s, g.charset_buf);
	SV(s, g.charset_buf_pos);
	text_sync(s);

	/* sound */
	SVA(s, g.sound_queue);
	SV(s, g.sound_queue_pos);
	sound_sync(s);

	SV(s, g.rnd_seed);
	SV(s, g.frame);

	/* resources the scripts locked */
	if (s->saving) {
		for (type = 0; type < RT_COUNT; type++)
			for (idx = 0; idx < res_count(type); idx++)
				if (res_locked(type, idx)) {
					SV(s, type);
					SV(s, idx);
				}
		type = 0xFF;
		SV(s, type);
	} else {
		for (;;) {
			SV(s, type);
			if (type == 0xFF || s->err)
				break;
			SV(s, idx);
			if (type < RT_COUNT && idx < res_count(type))
				res_lock(type, idx);
		}
	}
}

/* ---- files ---- */

void saveload_set_base(const char *pack)
{
	int n = (int)strlen(pack), dot = n, i;

	for (i = n - 1; i >= 0 && pack[i] != '/' && pack[i] != ':'; i--)
		if (pack[i] == '.') {
			dot = i;
			break;
		}
	if (dot > (int)sizeof(base) - 5)
		dot = (int)sizeof(base) - 5;
	memcpy(base, pack, (size_t)dot);
	base[dot] = 0;
}

static void slot_path(int slot, char *path)
{
	int n = (int)strlen(base);

	memcpy(path, base, (size_t)n);
	path[n++] = '.';
	path[n++] = 's';
	path[n++] = (char)('0' + slot / 10 % 10);
	path[n++] = (char)('0' + slot % 10);
	path[n] = 0;
}

static const uint8_t magic[4] = { 'S', '5', 'S', 'V' };

struct header {
	uint8_t magic[4];
	uint16_t version, num_vars, num_bit_vars, num_global_objects;
	char name[SAVE_NAME_LEN];
};

static void sync_header(struct ser *s, struct header *h)
{
	ser_bytes(s, h->magic, 4);
	SV(s, h->version);
	SV(s, h->num_vars);
	SV(s, h->num_bit_vars);
	SV(s, h->num_global_objects);
	ser_bytes(s, h->name, SAVE_NAME_LEN);
}

/* Opens the slot and reads its header; the file stays open if it is ours. */
static int open_slot(int slot, struct header *h)
{
	char path[sizeof(base) + 4];

	slot_path(slot, path);
	memset(&ser, 0, sizeof(ser));
	ser.f = plat_open(path);
	if (!ser.f)
		return 0;
	sync_header(&ser, h);
	if (ser.err || memcmp(h->magic, magic, 4) || h->version != SAVE_VERSION || h->num_vars != g.num_vars ||
	    h->num_bit_vars != g.num_bit_vars || h->num_global_objects != g.num_global_objects) {
		plat_close(ser.f);
		return -1;
	}
	return 1;
}

int save_slot_name(int slot, char *name)
{
	struct header h;
	int r = open_slot(slot, &h);

	name[0] = 0;
	if (r <= 0)
		return r;
	plat_close(ser.f);
	memcpy(name, h.name, SAVE_NAME_LEN);
	name[SAVE_NAME_LEN] = 0;
	return 1;
}

int save_state(int slot, const char *name)
{
	char path[sizeof(base) + 4];
	struct header h;
	int n = (int)strlen(name);

	slot_path(slot, path);
	memset(&ser, 0, sizeof(ser));
	ser.f = plat_create(path);
	if (!ser.f)
		return 0;
	ser.saving = 1;
	memset(&h, 0, sizeof(h));
	memcpy(h.magic, magic, 4);
	h.version = SAVE_VERSION;
	h.num_vars = g.num_vars;
	h.num_bit_vars = g.num_bit_vars;
	h.num_global_objects = g.num_global_objects;
	memcpy(h.name, name, (size_t)(n < SAVE_NAME_LEN ? n : SAVE_NAME_LEN));
	sync_header(&ser, &h);
	sync_state(&ser);
	ser_flush(&ser);
	plat_close(ser.f);
	return !ser.err;
}

static void after_load(void)
{
	int i;

	if (saved_b != screen_b || saved_h != screen_h) {
		init_screens(saved_b, saved_h);
	} else {
		for (i = 0; i < 3; i++) {
			if (vscr[i].pixels)
				memset(vscr[i].pixels, 0, vscr[i].size);
			if (vscr[i].back)
				memset(vscr[i].back, 0, vscr[i].size);
		}
		gfx_verb_cache_clear();
		gfx_full_dirty();
	}
	room_after_load();
	g.camera.last.x = g.camera.cur.x;
	if (g.current_room)
		camera_moved();
	palette_dirty = 1;
	text_after_load();
	for (i = 1; i < NUM_ACTORS; i++)
		g.actors[i].needs_redraw = 1;
	g.draw_queue_nr = 0;
	g.verb_mouse_over = 0;
	g.bg_needs_redraw = 0;
	g.full_redraw = 1;
	redraw_verbs();
	VAR(VAR_ROOM_FLAG) = 1;
	sound_after_load();
	clear_clicked_status();
}

int load_state(int slot)
{
	struct header h;
	uint8_t type;
	uint16_t idx;

	if (open_slot(slot, &h) <= 0)
		return 0;
	sound_stop_all();
	for (type = 0; type < RT_COUNT; type++)
		for (idx = 0; idx < res_count(type); idx++)
			if (res_locked(type, idx))
				res_unlock(type, idx);
	sync_state(&ser);
	plat_close(ser.f);
	if (ser.err)
		fatal("the saved game is damaged", slot, 0);
	after_load();
	return 1;
}
