/* scumm5-amiga - SCUMM v5 opcodes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/script_v5.cpp, ScummVM commit
 * 96b42d42bc8533953bdd701db059cc33c839a9ff: the DOS v5 behaviour with
 * ScummVM's optional enhancements off. ScummVM is the legal property of
 * its developers, whose names are too numerous to list here; see the
 * COPYRIGHT file distributed with the ScummVM source.
 */
#include "scumm.h"

#define PARAM_1 0x80
#define PARAM_2 0x40
#define PARAM_3 0x20

#define VAR_MONKEY2_LARGO_THEME 151

static int get_var(void)
{
	return read_var(fetch_word());
}

static int get_byte_param(uint8_t mask)
{
	if (g.opcode & mask)
		return get_var();
	return fetch_byte();
}

static int get_word_param(uint8_t mask)
{
	if (g.opcode & mask)
		return get_var();
	return fetch_word_signed();
}

static void get_result_pos(void)
{
	int a;

	g.result_var = fetch_word();
	if (g.result_var & 0x2000) {
		a = fetch_word();
		if (a & 0x2000)
			g.result_var += read_var(a & ~0x2000);
		else
			g.result_var += a & 0xFFF;
		g.result_var &= ~0x2000;
	}
}

static void set_result(int value)
{
	write_var(g.result_var, value);
}

static void jump_relative(int cond)
{
	int16_t offset = (int16_t)fetch_word();
	if (!cond)
		g.script_ptr += offset;
}

int get_word_vararg(int32_t *ptr)
{
	int i;

	for (i = 0; i < NUM_SCRIPT_LOCAL; i++)
		ptr[i] = 0;
	i = 0;
	while ((g.opcode = fetch_byte()) != 0xFF)
		ptr[i++] = get_word_param(PARAM_1);
	return i;
}

static void o5_break_here(void)
{
	update_script_ptr();
	g.current_script = 0xFF;
}

/* ---- arithmetic and flow ---- */

static void o5_move(void) { get_result_pos(); set_result(get_word_param(PARAM_1)); }
static void o5_add(void) { int a; get_result_pos(); a = get_word_param(PARAM_1); set_result(read_var(g.result_var) + a); }
static void o5_subtract(void) { int a; get_result_pos(); a = get_word_param(PARAM_1); set_result(read_var(g.result_var) - a); }
static void o5_multiply(void) { int a; get_result_pos(); a = get_word_param(PARAM_1); set_result(read_var(g.result_var) * a); }
static void o5_and(void) { int a; get_result_pos(); a = get_word_param(PARAM_1); set_result(read_var(g.result_var) & a); }
static void o5_or(void) { int a; get_result_pos(); a = get_word_param(PARAM_1); set_result(read_var(g.result_var) | a); }
static void o5_increment(void) { get_result_pos(); set_result(read_var(g.result_var) + 1); }
static void o5_decrement(void) { get_result_pos(); set_result(read_var(g.result_var) - 1); }

static void o5_divide(void)
{
	int a;
	get_result_pos();
	a = get_word_param(PARAM_1);
	if (a == 0)
		fatal("divide by zero", 0, 0);
	set_result(read_var(g.result_var) / a);
}

static void o5_set_var_range(void)
{
	int a, b;

	get_result_pos();
	a = fetch_byte();
	do {
		if (g.opcode & 0x80)
			b = fetch_word_signed();
		else
			b = fetch_byte();
		set_result(b);
		g.result_var++;
	} while (--a);
}

static void o5_expression(void)
{
	int dst, i;

	g.stack_pos = 0;
	get_result_pos();
	dst = g.result_var;

	while ((g.opcode = fetch_byte()) != 0xFF) {
		switch (g.opcode & 0x1F) {
		case 1:
			g.stack[g.stack_pos++] = get_word_param(PARAM_1);
			break;
		case 2:
			i = g.stack[--g.stack_pos];
			g.stack[g.stack_pos - 1] += i;
			break;
		case 3:
			i = g.stack[--g.stack_pos];
			g.stack[g.stack_pos - 1] -= i;
			break;
		case 4:
			i = g.stack[--g.stack_pos];
			g.stack[g.stack_pos - 1] *= i;
			break;
		case 5:
			i = g.stack[--g.stack_pos];
			if (i == 0)
				fatal("divide by zero", 0, 0);
			g.stack[g.stack_pos - 1] /= i;
			break;
		case 6:
			g.opcode = fetch_byte();
			execute_opcode(g.opcode);
			g.stack[g.stack_pos++] = g.vars[0];
			break;
		}
	}

	g.result_var = (uint16_t)dst;
	set_result(g.stack[--g.stack_pos]);
}

static void o5_is_equal(void)
{
	int var = fetch_word();
	int16_t a = (int16_t)read_var(var);
	int16_t b = (int16_t)get_word_param(PARAM_1);

	/* MI2: Largo's screams only play for sound card type 5 */
	if (var == VAR_SOUNDCARD && b == 5)
		b = a;
	jump_relative(b == a);
}

static void o5_is_not_equal(void) { int16_t a = (int16_t)get_var(); int16_t b = (int16_t)get_word_param(PARAM_1); jump_relative(b != a); }
static void o5_is_greater(void) { int16_t a = (int16_t)get_var(); int16_t b = (int16_t)get_word_param(PARAM_1); jump_relative(b > a); }
static void o5_is_greater_equal(void) { int16_t a = (int16_t)get_var(); int16_t b = (int16_t)get_word_param(PARAM_1); jump_relative(b >= a); }
static void o5_is_less(void) { int16_t a = (int16_t)get_var(); int16_t b = (int16_t)get_word_param(PARAM_1); jump_relative(b < a); }
static void o5_is_less_equal(void) { int16_t a = (int16_t)get_var(); int16_t b = (int16_t)get_word_param(PARAM_1); jump_relative(b <= a); }
static void o5_equal_zero(void) { jump_relative(get_var() == 0); }
static void o5_not_equal_zero(void) { jump_relative(get_var() != 0); }
static void o5_jump_relative(void) { jump_relative(0); }

/* ---- script control ---- */

static void o5_stop_object_code(void) { stop_object_code(); }

static void o5_start_script(void)
{
	int32_t data[NUM_SCRIPT_LOCAL];
	int op = g.opcode;
	int script = get_byte_param(PARAM_1);

	get_word_vararg(data);
	run_script(script, (op & 0x20) != 0, (op & 0x40) != 0, data);
}

static void o5_chain_script(void)
{
	int32_t vars[NUM_SCRIPT_LOCAL];
	int script, cur;

	script = get_byte_param(PARAM_1);
	get_word_vararg(vars);
	cur = g.current_script;
	g.slot[cur].number = 0;
	g.slot[cur].status = SS_DEAD;
	g.current_script = 0xFF;
	run_script(script, g.slot[cur].freeze_resistant, g.slot[cur].recursive, vars);
}

static void o5_start_object(void)
{
	int32_t data[NUM_SCRIPT_LOCAL];
	int obj = get_word_param(PARAM_1);
	int script = get_byte_param(PARAM_2);

	get_word_vararg(data);
	run_object_script(obj, script, 0, 0, data);
}

static void o5_stop_script(void)
{
	int script = get_byte_param(PARAM_1);
	if (!script)
		stop_object_code();
	else
		stop_script(script);
}

static void o5_stop_object_script(void) { stop_object_script(get_word_param(PARAM_1)); }
static void o5_is_script_running(void) { get_result_pos(); set_result(is_script_running(get_byte_param(PARAM_1))); }

static void o5_freeze_scripts(void)
{
	int scr = get_byte_param(PARAM_1);
	if (scr)
		freeze_scripts(scr);
	else
		unfreeze_scripts();
}

static void o5_delay(void)
{
	int delay = fetch_byte();
	delay |= fetch_byte() << 8;
	delay |= fetch_byte() << 16;
	g.slot[g.current_script].delay = delay;
	g.slot[g.current_script].status = SS_PAUSED;
	o5_break_here();
}

static void o5_delay_variable(void)
{
	g.slot[g.current_script].delay = get_var();
	g.slot[g.current_script].status = SS_PAUSED;
	o5_break_here();
}

static void o5_cutscene(void)
{
	int32_t args[NUM_SCRIPT_LOCAL];
	get_word_vararg(args);
	begin_cutscene(args);
}

static void o5_end_cutscene(void) { end_cutscene(); }

static void o5_begin_override(void)
{
	if (fetch_byte() != 0)
		begin_override();
	else
		end_override();
}

static void o5_wait(void)
{
	const uint8_t *oldaddr = g.script_ptr - 1;
	struct actor *a;

	g.opcode = fetch_byte();
	switch (g.opcode & 0x1F) {
	case 1:
		a = deref_actor_safe(get_byte_param(PARAM_1));
		if (a && a->moving)
			break;
		return;
	case 2:
		if (VAR(VAR_HAVE_MSG))
			break;
		return;
	case 3:
		if (g.camera.cur.x / 8 != g.camera.dest.x / 8)
			break;
		return;
	case 4:
		if (g.sentence_num) {
			if (g.sentences[g.sentence_num - 1].freeze_count && !is_script_in_use(VAR(VAR_SENTENCE_SCRIPT)))
				return;
		} else if (!is_script_in_use(VAR(VAR_SENTENCE_SCRIPT))) {
			return;
		}
		break;
	default:
		fatal("o5_wait: unknown subopcode", g.opcode & 0x1F, 0);
	}
	g.script_ptr = oldaddr;
	o5_break_here();
}

static void o5_do_sentence(void)
{
	int verb = get_byte_param(PARAM_1);
	int a, b;

	if (verb == 0xFE) {
		g.sentence_num = 0;
		stop_script(VAR(VAR_SENTENCE_SCRIPT));
		clear_clicked_status();
		return;
	}
	a = get_word_param(PARAM_2);
	b = get_word_param(PARAM_3);
	do_sentence(verb, a, b);
}

static void o5_debug(void) { get_word_param(PARAM_1); }
static void o5_dummy(void) { }

static void o5_system_ops(void)
{
	uint8_t sub = fetch_byte();
	if (sub == 1 || sub == 3)
		g.quit = 1;
	else if (sub != 2)
		fatal("o5_systemOps: unknown subopcode", sub, 0);
}

static void o5_pseudo_room(void)
{
	int i = fetch_byte(), j;
	while ((j = fetch_byte()) != 0)
		if (j >= 0x80)
			g.resource_mapper[j & 0x7F] = (uint8_t)i;
}

static void o5_get_random_nr(void)
{
	get_result_pos();
	set_result((int)rnd_get((uint32_t)get_byte_param(PARAM_1)));
}

/* ---- resources ---- */

static void o5_resource_routines(void)
{
	static const uint8_t types[4] = { RT_SCRIPT, RT_SOUND, RT_COSTUME, RT_ROOM };
	int resid = 0, op;

	g.opcode = fetch_byte();
	if (g.opcode != 17)
		resid = get_byte_param(PARAM_1);
	op = g.opcode & 0x3F;

	switch (op) {
	case 1: case 2: case 3:
		res_get(types[op - 1], (uint16_t)resid);
		break;
	case 4:
		res_get(RT_ROOM, (uint16_t)resid);
		break;
	case 5: case 6: case 7: case 8:
		res_make_old(types[op - 5], (uint16_t)resid);
		break;
	case 9:
		if (resid < NUM_GLOBAL_SCRIPTS)
			res_lock(RT_SCRIPT, (uint16_t)resid);
		break;
	case 10:
		res_lock(RT_SOUND, (uint16_t)resid);
		break;
	case 11:
		res_lock(RT_COSTUME, (uint16_t)resid);
		break;
	case 12:
		if (resid > 0x7F)
			resid = g.resource_mapper[resid & 0x7F];
		res_lock(RT_ROOM, (uint16_t)resid);
		break;
	case 13:
		if (resid < NUM_GLOBAL_SCRIPTS)
			res_unlock(RT_SCRIPT, (uint16_t)resid);
		break;
	case 14:
		res_unlock(RT_SOUND, (uint16_t)resid);
		break;
	case 15:
		res_unlock(RT_COSTUME, (uint16_t)resid);
		break;
	case 16:
		if (resid > 0x7F)
			resid = g.resource_mapper[resid & 0x7F];
		res_unlock(RT_ROOM, (uint16_t)resid);
		break;
	case 17:
		break;
	case 18:
		load_charset(resid);
		break;
	case 19:
		res_make_old(RT_CHARSET, (uint16_t)resid);
		break;
	case 20:
		load_fl_object(get_word_param(PARAM_2), resid);
		break;
	case 32: case 33: case 35: case 37:
		break;
	case 36:
		get_byte_param(PARAM_2);
		fetch_byte();
		break;
	default:
		fatal("o5_resourceRoutines: default case", op, 0);
	}
}

/* ---- objects ---- */

static void o5_set_class(void)
{
	int obj = get_word_param(PARAM_1), cls;

	while ((g.opcode = fetch_byte()) != 0xFF) {
		cls = get_word_param(PARAM_1);
		if (cls == 0)
			g.class_data[obj] = 0;
		else
			put_class(obj, cls, (cls & 0x80) != 0);
	}
}

static void o5_if_class_of_is(void)
{
	int obj = get_word_param(PARAM_1), cls, b, cond = 1;

	while ((g.opcode = fetch_byte()) != 0xFF) {
		cls = get_word_param(PARAM_1);
		b = get_class(obj, cls);
		if (((cls & 0x80) && !b) || (!(cls & 0x80) && b))
			cond = 0;
	}
	jump_relative(cond);
}

static void o5_set_owner_of(void)
{
	int obj = get_word_param(PARAM_1);
	int owner = get_byte_param(PARAM_2);
	set_owner_of(obj, owner);
}

static void o5_get_object_owner(void) { get_result_pos(); set_result(get_owner(get_word_param(PARAM_1))); }
static void o5_get_object_state(void) { get_result_pos(); set_result(get_state(get_word_param(PARAM_1))); }

static void o5_set_state(void)
{
	int obj = get_word_param(PARAM_1);
	int state = get_byte_param(PARAM_2);

	put_state(obj, state);
	mark_object_rect_as_dirty(obj);
	if (g.bg_needs_redraw)
		clear_draw_object_queue();
}

static void o5_draw_object(void)
{
	int state = 1, xpos = 255, ypos = 255, obj, idx, i;
	struct object_data *od;
	int x, y, w, h;

	obj = get_word_param(PARAM_1);
	g.opcode = fetch_byte();
	switch (g.opcode & 0x1F) {
	case 1:
		xpos = get_word_param(PARAM_1);
		ypos = get_word_param(PARAM_2);
		break;
	case 2:
		state = get_word_param(PARAM_1);
		break;
	case 0x1F:
		break;
	default:
		fatal("o5_drawObject: unknown subopcode", g.opcode & 0x1F, 0);
	}

	idx = get_object_index(obj);
	if (idx == -1)
		return;

	od = &g.objs[idx];
	if (xpos != 0xFF) {
		od->walk_x += (int16_t)(xpos * 8 - od->x_pos);
		od->x_pos = (int16_t)(xpos * 8);
		od->walk_y += (int16_t)(ypos * 8 - od->y_pos);
		od->y_pos = (int16_t)(ypos * 8);
	}
	add_object_to_draw_queue(idx);

	x = od->x_pos;
	y = od->y_pos;
	w = od->width;
	h = od->height;

	i = NUM_LOCAL_OBJECTS - 1;
	do {
		if (g.objs[i].obj_nr && g.objs[i].x_pos == x && g.objs[i].y_pos == y &&
		    g.objs[i].width == w && g.objs[i].height == h)
			put_state(g.objs[i].obj_nr, 0);
	} while (--i);

	put_state(obj, state);
}

static void o5_pickup_object(void)
{
	int obj = get_word_param(PARAM_1);
	int room = get_byte_param(PARAM_2);

	if (room == 0)
		room = g.room_resource;
	add_object_to_inventory(obj, room);
	put_owner(obj, VAR(VAR_EGO));
	put_class(obj, CLS_UNTOUCHABLE, 1);
	put_state(obj, 1);
	mark_object_rect_as_dirty(obj);
	clear_draw_object_queue();
	run_inventory_script(1);
}

static void o5_find_object(void)
{
	int x, y;
	get_result_pos();
	x = get_byte_param(PARAM_1);
	y = get_byte_param(PARAM_2);
	set_result(find_object(x, y));
}

static void o5_find_inventory(void)
{
	int x, y;
	get_result_pos();
	x = get_byte_param(PARAM_1);
	y = get_byte_param(PARAM_2);
	set_result(find_inventory(x, y));
}

static void o5_get_inventory_count(void) { get_result_pos(); set_result(get_inventory_count(get_byte_param(PARAM_1))); }

static void o5_get_dist(void)
{
	int o1, o2;
	get_result_pos();
	o1 = get_word_param(PARAM_1);
	o2 = get_word_param(PARAM_2);
	set_result(get_obj_act_to_obj_act_dist(o1, o2));
}

static void o5_get_verb_entrypoint(void)
{
	int a, b;
	get_result_pos();
	a = get_word_param(PARAM_1);
	b = get_word_param(PARAM_2);
	set_result(get_verb_entrypoint(a, b));
}

static void o5_set_object_name(void) { set_object_name(get_word_param(PARAM_1)); }

/* ---- actors ---- */

static void o5_actor_ops(void)
{
	int act = get_byte_param(PARAM_1);
	struct actor *a = deref_actor(act);
	int i, j;

	while ((g.opcode = fetch_byte()) != 0xFF) {
		switch (g.opcode & 0x1F) {
		case 0:
			get_byte_param(PARAM_1);
			break;
		case 1:
			set_actor_costume(a, get_byte_param(PARAM_1));
			break;
		case 2:
			i = get_byte_param(PARAM_1);
			j = get_byte_param(PARAM_2);
			set_actor_walk_speed(a, i, j);
			break;
		case 3:
			a->sound[0] = (uint8_t)get_byte_param(PARAM_1);
			break;
		case 4:
			a->walk_frame = (uint8_t)get_byte_param(PARAM_1);
			break;
		case 5:
			a->talk_start_frame = (uint8_t)get_byte_param(PARAM_1);
			a->talk_stop_frame = (uint8_t)get_byte_param(PARAM_2);
			break;
		case 6:
			a->stand_frame = (uint8_t)get_byte_param(PARAM_1);
			break;
		case 7:
			get_byte_param(PARAM_1);
			get_byte_param(PARAM_2);
			get_byte_param(PARAM_3);
			break;
		case 8:
			init_actor(a, 0);
			break;
		case 9:
			set_actor_elevation(a, get_word_param(PARAM_1));
			break;
		case 10:
			a->init_frame = 1;
			a->walk_frame = 2;
			a->stand_frame = 3;
			a->talk_start_frame = 4;
			a->talk_stop_frame = 5;
			break;
		case 11:
			i = get_byte_param(PARAM_1);
			j = get_byte_param(PARAM_2);
			if (i > 31)
				fatal("o5_actorOps: palette slot", i, 0);
			set_actor_palette(a, i, j);
			break;
		case 12:
			a->talk_color = (uint8_t)get_byte_param(PARAM_1);
			break;
		case 13: {
			int len = res_str_len(0) + 1;
			res_free(a->name);
			a->name = res_alloc((uint32_t)len);
			if (a->name)
				memcpy(a->name, g.script_ptr, (size_t)len);
			g.script_ptr += len;
			break;
		}
		case 14:
			a->init_frame = (uint8_t)get_byte_param(PARAM_1);
			break;
		case 16:
			a->width = (uint16_t)get_byte_param(PARAM_1);
			break;
		case 17:
			i = get_byte_param(PARAM_1);
			j = get_byte_param(PARAM_2);
			a->boxscale = (uint8_t)i;
			set_actor_scale(a, i, j);
			break;
		case 18:
			a->force_clip = 0;
			break;
		case 19:
			a->force_clip = (uint8_t)get_byte_param(PARAM_1);
			break;
		case 20:
		case 21:
			a->ignore_boxes = !(g.opcode & 1);
			a->force_clip = 0;
			if (is_in_current_room(a))
				put_actor_here(a);
			break;
		case 22:
			a->anim_speed = (uint8_t)get_byte_param(PARAM_1);
			a->anim_progress = 0;
			break;
		case 23:
			a->shadow_mode = (uint8_t)get_byte_param(PARAM_1);
			break;
		default:
			fatal("o5_actorOps: default case", g.opcode & 0x1F, 0);
		}
	}
}

static void o5_put_actor(void)
{
	int act = get_byte_param(PARAM_1);
	int x = get_word_param(PARAM_2);
	int y = get_word_param(PARAM_3);
	struct actor *a = deref_actor(act);
	put_actor(a, x, y, a->room);
}

static void o5_put_actor_at_object(void)
{
	struct actor *a = deref_actor(get_byte_param(PARAM_1));
	int obj = get_word_param(PARAM_2), x, y, dir;

	if (where_is_object(obj) != WIO_NOT_FOUND) {
		get_object_xy_pos(obj, &x, &y, &dir);
	} else {
		x = 240;
		y = 120;
	}
	put_actor(a, x, y, a->room);
}

static void o5_put_actor_in_room(void)
{
	int act = get_byte_param(PARAM_1);
	int room = get_byte_param(PARAM_2);
	struct actor *a = deref_actor(act);

	if (a->visible && g.current_room != room && get_talking_actor() == a->number)
		stop_talk();
	a->room = (uint8_t)room;
	if (!room)
		put_actor(a, 0, 0, 0);
}

static void o5_walk_actor_to(void)
{
	struct actor *a = deref_actor(get_byte_param(PARAM_1));
	int x = get_word_param(PARAM_2);
	int y = get_word_param(PARAM_3);
	start_walk_actor(a, x, y, -1);
}

static void o5_walk_actor_to_actor(void)
{
	int nr = get_byte_param(PARAM_1);
	int nr2 = get_byte_param(PARAM_2);
	int dist = fetch_byte();

	if (!is_valid_actor(nr) || !is_in_current_room(deref_actor(nr)))
		return;
	if (!is_valid_actor(nr2) || !is_in_current_room(deref_actor(nr2)))
		return;
	walk_actor_to_actor(nr, nr2, dist);
}

static void o5_walk_actor_to_object(void)
{
	struct actor *a = deref_actor(get_byte_param(PARAM_1));
	int obj = get_word_param(PARAM_2), x, y, dir;

	if (where_is_object(obj) != WIO_NOT_FOUND) {
		get_object_xy_pos(obj, &x, &y, &dir);
		start_walk_actor(a, x, y, dir);
	}
}

static void o5_animate_actor(void)
{
	int act = get_byte_param(PARAM_1);
	int anim = get_byte_param(PARAM_2);
	if (!is_valid_actor(act))
		return;
	animate_actor(deref_actor(act), anim);
}

static void o5_face_actor(void)
{
	int act = get_byte_param(PARAM_1);
	int obj = get_word_param(PARAM_2);
	struct actor *a = deref_actor_safe(act);
	if (a)
		face_to_object(a, obj);
}

static void o5_actor_follow_camera(void) { actor_follow_camera(get_byte_param(PARAM_1)); }

static void o5_actor_from_pos(void)
{
	int x, y;
	get_result_pos();
	x = get_word_param(PARAM_1);
	y = get_word_param(PARAM_2);
	set_result(get_actor_from_pos(x, y));
}

static void o5_get_actor_room(void)
{
	int act;
	get_result_pos();
	act = get_byte_param(PARAM_1);
	set_result(is_valid_actor(act) ? deref_actor(act)->room : 0);
}

static void o5_get_actor_costume(void) { get_result_pos(); set_result(deref_actor(get_byte_param(PARAM_1))->costume); }
static void o5_get_actor_elevation(void) { get_result_pos(); set_result(deref_actor(get_byte_param(PARAM_1))->elevation); }
static void o5_get_actor_facing(void) { get_result_pos(); set_result(new_dir_to_old_dir(deref_actor(get_byte_param(PARAM_1))->facing)); }
static void o5_get_actor_moving(void) { get_result_pos(); set_result(deref_actor(get_byte_param(PARAM_1))->moving); }
static void o5_get_actor_scale(void) { get_result_pos(); set_result(deref_actor(get_byte_param(PARAM_1))->scalex); }
static void o5_get_actor_walk_box(void) { get_result_pos(); set_result(deref_actor(get_byte_param(PARAM_1))->walkbox); }
static void o5_get_actor_width(void) { get_result_pos(); set_result(deref_actor(get_byte_param(PARAM_1))->width); }
static void o5_get_anim_counter(void) { get_result_pos(); set_result(deref_actor(get_byte_param(PARAM_1))->cost.anim_counter); }
static void o5_get_actor_x(void) { get_result_pos(); set_result(get_obj_x(get_word_param(PARAM_1))); }
static void o5_get_actor_y(void) { get_result_pos(); set_result(get_obj_y(get_word_param(PARAM_1))); }

static void o5_is_actor_in_box(void)
{
	int act = get_byte_param(PARAM_1);
	int box = get_byte_param(PARAM_2);
	struct actor *a = deref_actor(act);
	jump_relative(check_xy_in_box_bounds(box, a->pos.x, a->pos.y));
}

static void o5_get_closest_obj_actor(void)
{
	int act, obj, dist, closest_obj = 0xFF, closest_dist = 0xFF;

	get_result_pos();
	act = get_word_param(PARAM_1);
	obj = VAR(VAR_ACTOR_RANGE_MAX);
	do {
		dist = get_obj_act_to_obj_act_dist(act, obj);
		if (dist < closest_dist) {
			closest_dist = dist;
			closest_obj = obj;
		}
	} while (--obj >= VAR(VAR_ACTOR_RANGE_MIN));
	set_result(closest_obj);
}

/* ---- rooms and camera ---- */

static void o5_load_room(void)
{
	start_scene(get_byte_param(PARAM_1), 0, 0);
	g.full_redraw = 1;
}

static void o5_load_room_with_ego(void)
{
	struct actor *a;
	int obj, room, x, y;

	obj = get_word_param(PARAM_1);
	room = get_byte_param(PARAM_2);

	a = deref_actor(VAR(VAR_EGO));
	put_actor(a, a->pos.x, a->pos.y, room);
	g.ego_positioned = 0;

	x = fetch_word_signed();
	y = fetch_word_signed();

	VAR(VAR_WALKTO_OBJ) = obj;
	start_scene(a->room, a, obj);
	VAR(VAR_WALKTO_OBJ) = 0;

	g.camera.cur.x = g.camera.dest.x = a->pos.x;
	set_camera_follows(a, 0);
	g.full_redraw = 1;

	if (x != -1)
		start_walk_actor(a, x, y, -1);
}

static void o5_set_camera_at(void) { set_camera_at_ex(get_word_param(PARAM_1)); }
static void o5_pan_camera_to(void) { pan_camera_to(get_word_param(PARAM_1), 0); }

static void o5_lights(void)
{
	int a = get_byte_param(PARAM_1);
	fetch_byte();
	if (fetch_byte() == 0)
		VAR(VAR_CURRENT_LIGHTS) = a;
	g.full_redraw = 1;
}

static void o5_room_ops(void)
{
	int a, b, c, d, e;

	g.opcode = fetch_byte();
	switch (g.opcode & 0x1F) {
	case 1:
		a = get_word_param(PARAM_1);
		b = get_word_param(PARAM_2);
		if (a < SCREEN_WIDTH / 2)
			a = SCREEN_WIDTH / 2;
		if (b < SCREEN_WIDTH / 2)
			b = SCREEN_WIDTH / 2;
		if (a > g.room_width - SCREEN_WIDTH / 2)
			a = g.room_width - SCREEN_WIDTH / 2;
		if (b > g.room_width - SCREEN_WIDTH / 2)
			b = g.room_width - SCREEN_WIDTH / 2;
		VAR(VAR_CAMERA_MIN_X) = a;
		VAR(VAR_CAMERA_MAX_X) = b;
		break;
	case 2:
		fatal("room-color is no longer a valid command", 0, 0);
		break;
	case 3:
		a = get_word_param(PARAM_1);
		b = get_word_param(PARAM_2);
		init_screens(a, b);
		break;
	case 4:
		a = get_word_param(PARAM_1);
		b = get_word_param(PARAM_2);
		c = get_word_param(PARAM_3);
		g.opcode = fetch_byte();
		d = get_byte_param(PARAM_1);
		set_pal_color(d, a, b, c);
		break;
	case 5:
	case 6:
		break;
	case 7:
		a = get_byte_param(PARAM_1);
		b = get_byte_param(PARAM_2);
		g.opcode = fetch_byte();
		c = get_byte_param(PARAM_1);
		d = get_byte_param(PARAM_2);
		g.opcode = fetch_byte();
		e = get_byte_param(PARAM_2);
		set_scale_slot(e - 1, 0, b, a, 0, d, c);
		break;
	case 8:
		a = get_byte_param(PARAM_1);
		b = get_byte_param(PARAM_2);
		c = get_byte_param(PARAM_3);
		darken_palette(a, a, a, b, c);
		break;
	case 9:
		get_byte_param(PARAM_1);
		get_byte_param(PARAM_2);
		break;
	case 10:
		a = get_word_param(PARAM_1);
		if (a) {
			g.switch_room_effect = (uint8_t)(a & 0xFF);
			g.switch_room_effect2 = (uint8_t)(a >> 8);
		} else {
			fade_in(g.new_effect);
		}
		break;
	case 11:
		a = get_word_param(PARAM_1);
		b = get_word_param(PARAM_2);
		c = get_word_param(PARAM_3);
		g.opcode = fetch_byte();
		d = get_byte_param(PARAM_1);
		e = get_byte_param(PARAM_2);
		darken_palette(a, b, c, d, e);
		break;
	case 12:
		get_word_param(PARAM_1);
		get_word_param(PARAM_2);
		get_word_param(PARAM_3);
		g.opcode = fetch_byte();
		get_byte_param(PARAM_1);
		get_byte_param(PARAM_2);
		break;
	case 13:
	case 14:
		get_byte_param(PARAM_1);
		while (fetch_byte())
			;
		break;
	case 15:
		get_byte_param(PARAM_1);
		g.opcode = fetch_byte();
		get_byte_param(PARAM_1);
		get_byte_param(PARAM_2);
		g.opcode = fetch_byte();
		get_byte_param(PARAM_1);
		break;
	case 16:
		a = get_byte_param(PARAM_1);
		b = get_byte_param(PARAM_2);
		if (a < 1 || a > 16)
			fatal("o5_roomOps: color cycle", a, 0);
		g.color_cycle[a - 1].delay = (uint16_t)(b ? 0x4000 / (b * 0x4C) : 0);
		break;
	default:
		fatal("o5_roomOps: unknown subopcode", g.opcode & 0x1F, 0);
	}
}

static void o5_matrix_ops(void)
{
	int a, b;

	g.opcode = fetch_byte();
	switch (g.opcode & 0x1F) {
	case 1:
		a = get_byte_param(PARAM_1);
		b = get_byte_param(PARAM_2);
		set_box_flags(a, b);
		break;
	case 2:
		a = get_byte_param(PARAM_1);
		b = get_byte_param(PARAM_2);
		set_box_scale(a, b);
		break;
	case 3:
		a = get_byte_param(PARAM_1);
		b = get_byte_param(PARAM_2);
		set_box_scale(a, (b - 1) | 0x8000);
		break;
	case 4:
		create_box_matrix();
		break;
	}
}

static void o5_draw_box(void)
{
	get_word_param(PARAM_1);
	get_word_param(PARAM_2);
	g.opcode = fetch_byte();
	get_word_param(PARAM_1);
	get_word_param(PARAM_2);
	get_byte_param(PARAM_3);
}

/* ---- sound ---- */

static void o5_start_music(void) { sound_start(get_byte_param(PARAM_1)); }

static void o5_start_sound(void)
{
	const uint8_t *oldaddr = g.script_ptr - 1;
	int sound = get_byte_param(PARAM_1);

	/* MI2: Largo's theme must finish before the next tune starts */
	if ((sound == 103 || sound == 110) && sound_is_running(VAR_MONKEY2_LARGO_THEME)) {
		g.script_ptr = oldaddr;
		o5_break_here();
		return;
	}
	VAR(VAR_MUSIC_TIMER) = 0;
	sound_start(sound);
}

static void o5_stop_music(void) { sound_stop_all(); }
static void o5_stop_sound(void) { sound_stop(get_byte_param(PARAM_1)); }

static void o5_is_sound_running(void)
{
	int snd;
	get_result_pos();
	snd = get_byte_param(PARAM_1);
	if (snd)
		snd = sound_is_running(snd);
	set_result(snd);
}

static void o5_sound_kludge(void)
{
	int32_t items[NUM_SCRIPT_LOCAL];
	int num = get_word_vararg(items);
	sound_kludge(items, num);
}

/* ---- cursor, verbs, text ---- */

static void o5_cursor_command(void)
{
	int32_t table[NUM_SCRIPT_LOCAL];
	int i, j, k;

	switch ((g.opcode = fetch_byte()) & 0x1F) {
	case 1:
		g.cursor_state = 1;
		verb_mouse_over(0);
		break;
	case 2:
		g.cursor_state = 0;
		verb_mouse_over(0);
		break;
	case 3:
		g.user_put = 1;
		break;
	case 4:
		g.user_put = 0;
		break;
	case 5:
		g.cursor_state++;
		verb_mouse_over(0);
		break;
	case 6:
		g.cursor_state--;
		verb_mouse_over(0);
		break;
	case 7:
		g.user_put++;
		break;
	case 8:
		g.user_put--;
		break;
	case 10:
		i = get_byte_param(PARAM_1);
		j = get_byte_param(PARAM_2);
		redefine_builtin_cursor_from_char(i, j);
		break;
	case 11:
		i = get_byte_param(PARAM_1);
		j = get_byte_param(PARAM_2);
		k = get_byte_param(PARAM_3);
		redefine_builtin_cursor_hotspot(i, j, k);
		break;
	case 12:
		i = get_byte_param(PARAM_1);
		if (i < 0 || i > 3)
			fatal("SO_CURSOR_SET: unsupported cursor id", i, 0);
		g.current_cursor = (uint8_t)i;
		break;
	case 13:
		init_charset(get_byte_param(PARAM_1));
		break;
	case 14:
		get_word_vararg(table);
		for (i = 0; i < 16; i++)
			g.charset_color_map[i] = g.charset_data[g.string[1].def.charset][i] = (uint8_t)table[i];
		break;
	}

	VAR(VAR_CURSORSTATE) = g.cursor_state;
	VAR(VAR_USERPUT) = g.user_put;
}

static void o5_verb_ops(void)
{
	int verb, slot, a, b;
	struct verb_slot *vs;

	verb = get_byte_param(PARAM_1);
	slot = get_verb_slot(verb, 0);
	if (slot >= NUM_VERBS)
		fatal("new verb slot", slot, 0);
	vs = &g.verbs[slot];
	vs->verbid = (uint16_t)verb;

	while ((g.opcode = fetch_byte()) != 0xFF) {
		switch (g.opcode & 0x1F) {
		case 1:
			a = get_word_param(PARAM_1);
			if (slot) {
				set_verb_object(g.room_resource, a, slot);
				vs->type = VERB_IMAGE;
			}
			break;
		case 2: {
			int len = res_str_len(0) + 1;
			res_free(vs->name);
			vs->name = 0;
			if (slot) {
				vs->name = res_alloc((uint32_t)len);
				if (vs->name)
					memcpy(vs->name, g.script_ptr, (size_t)len);
			}
			g.script_ptr += len;
			vs->type = VERB_TEXT;
			vs->imgindex = 0;
			break;
		}
		case 3:
			vs->color = (uint8_t)get_byte_param(PARAM_1);
			break;
		case 4:
			vs->hicolor = (uint8_t)get_byte_param(PARAM_1);
			break;
		case 5:
			vs->cur_rect.left = (int16_t)get_word_param(PARAM_1);
			vs->cur_rect.top = (int16_t)get_word_param(PARAM_2);
			vs->orig_left = vs->cur_rect.left;
			break;
		case 6:
			vs->curmode = 1;
			break;
		case 7:
			vs->curmode = 0;
			break;
		case 8:
			kill_verb(slot);
			break;
		case 9:
			slot = get_verb_slot(verb, 0);
			if (slot == 0) {
				for (slot = 1; slot < NUM_VERBS; slot++)
					if (g.verbs[slot].verbid == 0)
						break;
				if (slot == NUM_VERBS)
					fatal("too many verbs", verb, 0);
			}
			vs = &g.verbs[slot];
			vs->verbid = (uint16_t)verb;
			vs->color = 2;
			vs->hicolor = 0;
			vs->dimcolor = 8;
			vs->type = VERB_TEXT;
			vs->charset_nr = g.string[0].def.charset;
			vs->curmode = 0;
			vs->saveid = 0;
			vs->key = 0;
			vs->center = 0;
			vs->imgindex = 0;
			break;
		case 16:
			vs->dimcolor = (uint8_t)get_byte_param(PARAM_1);
			break;
		case 17:
			vs->curmode = 2;
			break;
		case 18:
			vs->key = (uint8_t)get_byte_param(PARAM_1);
			break;
		case 19:
			vs->center = 1;
			break;
		case 20: {
			int idx = get_word_param(PARAM_1);
			const uint8_t *ptr = idx >= 0 && idx < NUM_ARRAY ? g.strings[idx] : 0;
			res_free(vs->name);
			vs->name = 0;
			if (ptr && slot) {
				int len = res_str_len(ptr) + 1;
				vs->name = res_alloc((uint32_t)len);
				if (vs->name)
					memcpy(vs->name, ptr, (size_t)len);
			}
			vs->type = VERB_TEXT;
			vs->imgindex = 0;
			break;
		}
		case 22:
			a = get_word_param(PARAM_1);
			b = get_byte_param(PARAM_2);
			if (slot && vs->imgindex != a) {
				set_verb_object(b, a, slot);
				vs->type = VERB_IMAGE;
				vs->imgindex = (uint16_t)a;
			}
			break;
		case 23:
			vs->bkcolor = (uint8_t)get_byte_param(PARAM_1);
			break;
		default:
			fatal("o5_verbOps: unknown subopcode", g.opcode & 0x1F, 0);
		}
	}

	draw_verb(slot, 0);
	verb_mouse_over(0);
}

static void o5_save_restore_verbs(void)
{
	int a, b, c, slot, slot2;

	g.opcode = fetch_byte();
	a = get_byte_param(PARAM_1);
	b = get_byte_param(PARAM_2);
	c = get_byte_param(PARAM_3);

	switch (g.opcode) {
	case 1:
		while (a <= b) {
			slot = get_verb_slot(a, 0);
			if (slot && g.verbs[slot].saveid == 0) {
				g.verbs[slot].saveid = (uint16_t)c;
				draw_verb(slot, 0);
				verb_mouse_over(0);
			}
			a++;
		}
		break;
	case 2:
		while (a <= b) {
			slot = get_verb_slot(a, c);
			if (slot) {
				slot2 = get_verb_slot(a, 0);
				if (slot2)
					kill_verb(slot2);
				slot = get_verb_slot(a, c);
				g.verbs[slot].saveid = 0;
				draw_verb(slot, 0);
				verb_mouse_over(0);
			}
			a++;
		}
		break;
	case 3:
		while (a <= b) {
			slot = get_verb_slot(a, c);
			if (slot)
				kill_verb(slot);
			a++;
		}
		break;
	default:
		fatal("o5_saveRestoreVerbs: unknown subopcode", g.opcode, 0);
	}
}

static void decode_parse_string(void)
{
	int slot, len;
	struct string_tab_part *st;

	switch (g.actor_to_print_str_for) {
	case 252: slot = 3; break;
	case 253: slot = 2; break;
	case 254: slot = 1; break;
	default: slot = 0;
	}
	st = &g.string[slot].cur;
	*st = g.string[slot].def;

	while ((g.opcode = fetch_byte()) != 0xFF) {
		switch (g.opcode & 0xF) {
		case 0:
			st->xpos = (int16_t)get_word_param(PARAM_1);
			st->ypos = (int16_t)get_word_param(PARAM_2);
			st->overhead = 0;
			break;
		case 1:
			st->color = (uint8_t)get_byte_param(PARAM_1);
			break;
		case 2:
			st->right = (int16_t)get_word_param(PARAM_1);
			break;
		case 3:
			fatal("decodeParseString: unhandled case 3", 0, 0);
			break;
		case 4:
			st->center = 1;
			st->overhead = 0;
			break;
		case 6:
			st->center = 0;
			st->overhead = 0;
			break;
		case 7:
			st->overhead = 1;
			break;
		case 8:
			fatal("decodeParseString: unhandled case 8", 0, 0);
			break;
		case 15:
			len = res_str_len(g.script_ptr);
			print_string(slot, g.script_ptr);
			g.script_ptr += len + 1;
			return;
		default:
			fatal("decodeParseString: unhandled case", g.opcode & 0xF, 0);
		}
	}
	g.string[slot].def = *st;
}

static void o5_print(void)
{
	g.actor_to_print_str_for = (uint8_t)get_byte_param(PARAM_1);
	decode_parse_string();
}

static void o5_print_ego(void)
{
	g.actor_to_print_str_for = (uint8_t)VAR(VAR_EGO);
	decode_parse_string();
}

static void o5_string_ops(void)
{
	int a, b, c;

	g.opcode = fetch_byte();
	switch (g.opcode & 0x1F) {
	case 1:
		a = get_byte_param(PARAM_1);
		load_ptr_to_string(a, 0);
		break;
	case 2:
		a = get_byte_param(PARAM_1);
		b = get_byte_param(PARAM_2);
		if (a == b)
			fatal("o5_stringOps: copy to itself", a, 0);
		nuke_string(a);
		if (b >= 0 && b < NUM_ARRAY && g.strings[b])
			load_ptr_to_string(a, g.strings[b]);
		break;
	case 3:
		a = get_byte_param(PARAM_1);
		b = get_byte_param(PARAM_2);
		c = get_byte_param(PARAM_3);
		if (a < 0 || a >= NUM_ARRAY || !g.strings[a])
			fatal("string does not exist", a, 0);
		if (b >= 0 && b < g.string_size[a] + 2)
			g.strings[a][b] = (uint8_t)c;
		break;
	case 4:
		get_result_pos();
		a = get_byte_param(PARAM_1);
		b = get_byte_param(PARAM_2);
		if (a < 0 || a >= NUM_ARRAY || !g.strings[a])
			fatal("string does not exist", a, 0);
		set_result(b >= 0 && b < g.string_size[a] + 2 ? g.strings[a][b] : 0);
		break;
	case 5:
		a = get_byte_param(PARAM_1);
		b = get_byte_param(PARAM_2);
		nuke_string(a);
		if (b)
			create_string(a, b);
		break;
	}
}

static void o5_get_string_width(void)
{
	int idx;
	get_result_pos();
	idx = get_byte_param(PARAM_1);
	if (idx < 0 || idx >= NUM_ARRAY || !g.strings[idx])
		fatal("string does not exist", idx, 0);
	set_result(charset_string_width(0, g.strings[idx]));
}

/* ---- dispatch ---- */

typedef void (*opfn)(void);

struct opcode_def {
	opfn fn;
	const char *name;
};

#define OP(f, n) { f, n }
#define NOP { 0, 0 }

static const struct opcode_def opcodes[256] = {
	/* 00 */ OP(o5_stop_object_code, "o5_stopObjectCode"), OP(o5_put_actor, "o5_putActor"), OP(o5_start_music, "o5_startMusic"), OP(o5_get_actor_room, "o5_getActorRoom"),
	/* 04 */ OP(o5_is_greater_equal, "o5_isGreaterEqual"), OP(o5_draw_object, "o5_drawObject"), OP(o5_get_actor_elevation, "o5_getActorElevation"), OP(o5_set_state, "o5_setState"),
	/* 08 */ OP(o5_is_not_equal, "o5_isNotEqual"), OP(o5_face_actor, "o5_faceActor"), OP(o5_start_script, "o5_startScript"), OP(o5_get_verb_entrypoint, "o5_getVerbEntrypoint"),
	/* 0C */ OP(o5_resource_routines, "o5_resourceRoutines"), OP(o5_walk_actor_to_actor, "o5_walkActorToActor"), OP(o5_put_actor_at_object, "o5_putActorAtObject"), OP(o5_get_object_state, "o5_getObjectState"),
	/* 10 */ OP(o5_get_object_owner, "o5_getObjectOwner"), OP(o5_animate_actor, "o5_animateActor"), OP(o5_pan_camera_to, "o5_panCameraTo"), OP(o5_actor_ops, "o5_actorOps"),
	/* 14 */ OP(o5_print, "o5_print"), OP(o5_actor_from_pos, "o5_actorFromPos"), OP(o5_get_random_nr, "o5_getRandomNr"), OP(o5_and, "o5_and"),
	/* 18 */ OP(o5_jump_relative, "o5_jumpRelative"), OP(o5_do_sentence, "o5_doSentence"), OP(o5_move, "o5_move"), OP(o5_multiply, "o5_multiply"),
	/* 1C */ OP(o5_start_sound, "o5_startSound"), OP(o5_if_class_of_is, "o5_ifClassOfIs"), OP(o5_walk_actor_to, "o5_walkActorTo"), OP(o5_is_actor_in_box, "o5_isActorInBox"),
	/* 20 */ OP(o5_stop_music, "o5_stopMusic"), OP(o5_put_actor, "o5_putActor"), OP(o5_get_anim_counter, "o5_getAnimCounter"), OP(o5_get_actor_y, "o5_getActorY"),
	/* 24 */ OP(o5_load_room_with_ego, "o5_loadRoomWithEgo"), OP(o5_pickup_object, "o5_pickupObject"), OP(o5_set_var_range, "o5_setVarRange"), OP(o5_string_ops, "o5_stringOps"),
	/* 28 */ OP(o5_equal_zero, "o5_equalZero"), OP(o5_set_owner_of, "o5_setOwnerOf"), OP(o5_start_script, "o5_startScript"), OP(o5_delay_variable, "o5_delayVariable"),
	/* 2C */ OP(o5_cursor_command, "o5_cursorCommand"), OP(o5_put_actor_in_room, "o5_putActorInRoom"), OP(o5_delay, "o5_delay"), NOP,
	/* 30 */ OP(o5_matrix_ops, "o5_matrixOps"), OP(o5_get_inventory_count, "o5_getInventoryCount"), OP(o5_set_camera_at, "o5_setCameraAt"), OP(o5_room_ops, "o5_roomOps"),
	/* 34 */ OP(o5_get_dist, "o5_getDist"), OP(o5_find_object, "o5_findObject"), OP(o5_walk_actor_to_object, "o5_walkActorToObject"), OP(o5_start_object, "o5_startObject"),
	/* 38 */ OP(o5_is_less_equal, "o5_isLessEqual"), OP(o5_do_sentence, "o5_doSentence"), OP(o5_subtract, "o5_subtract"), OP(o5_get_actor_scale, "o5_getActorScale"),
	/* 3C */ OP(o5_stop_sound, "o5_stopSound"), OP(o5_find_inventory, "o5_findInventory"), OP(o5_walk_actor_to, "o5_walkActorTo"), OP(o5_draw_box, "o5_drawBox"),
	/* 40 */ OP(o5_cutscene, "o5_cutscene"), OP(o5_put_actor, "o5_putActor"), OP(o5_chain_script, "o5_chainScript"), OP(o5_get_actor_x, "o5_getActorX"),
	/* 44 */ OP(o5_is_less, "o5_isLess"), NOP, OP(o5_increment, "o5_increment"), OP(o5_set_state, "o5_setState"),
	/* 48 */ OP(o5_is_equal, "o5_isEqual"), OP(o5_face_actor, "o5_faceActor"), OP(o5_start_script, "o5_startScript"), OP(o5_get_verb_entrypoint, "o5_getVerbEntrypoint"),
	/* 4C */ OP(o5_sound_kludge, "o5_soundKludge"), OP(o5_walk_actor_to_actor, "o5_walkActorToActor"), OP(o5_put_actor_at_object, "o5_putActorAtObject"), NOP,
	/* 50 */ NOP, OP(o5_animate_actor, "o5_animateActor"), OP(o5_actor_follow_camera, "o5_actorFollowCamera"), OP(o5_actor_ops, "o5_actorOps"),
	/* 54 */ OP(o5_set_object_name, "o5_setObjectName"), OP(o5_actor_from_pos, "o5_actorFromPos"), OP(o5_get_actor_moving, "o5_getActorMoving"), OP(o5_or, "o5_or"),
	/* 58 */ OP(o5_begin_override, "o5_beginOverride"), OP(o5_do_sentence, "o5_doSentence"), OP(o5_add, "o5_add"), OP(o5_divide, "o5_divide"),
	/* 5C */ NOP, OP(o5_set_class, "o5_setClass"), OP(o5_walk_actor_to, "o5_walkActorTo"), OP(o5_is_actor_in_box, "o5_isActorInBox"),
	/* 60 */ OP(o5_freeze_scripts, "o5_freezeScripts"), OP(o5_put_actor, "o5_putActor"), OP(o5_stop_script, "o5_stopScript"), OP(o5_get_actor_facing, "o5_getActorFacing"),
	/* 64 */ OP(o5_load_room_with_ego, "o5_loadRoomWithEgo"), OP(o5_pickup_object, "o5_pickupObject"), OP(o5_get_closest_obj_actor, "o5_getClosestObjActor"), OP(o5_get_string_width, "o5_getStringWidth"),
	/* 68 */ OP(o5_is_script_running, "o5_isScriptRunning"), OP(o5_set_owner_of, "o5_setOwnerOf"), OP(o5_start_script, "o5_startScript"), OP(o5_debug, "o5_debug"),
	/* 6C */ OP(o5_get_actor_width, "o5_getActorWidth"), OP(o5_put_actor_in_room, "o5_putActorInRoom"), OP(o5_stop_object_script, "o5_stopObjectScript"), NOP,
	/* 70 */ OP(o5_lights, "o5_lights"), OP(o5_get_actor_costume, "o5_getActorCostume"), OP(o5_load_room, "o5_loadRoom"), OP(o5_room_ops, "o5_roomOps"),
	/* 74 */ OP(o5_get_dist, "o5_getDist"), OP(o5_find_object, "o5_findObject"), OP(o5_walk_actor_to_object, "o5_walkActorToObject"), OP(o5_start_object, "o5_startObject"),
	/* 78 */ OP(o5_is_greater, "o5_isGreater"), OP(o5_do_sentence, "o5_doSentence"), OP(o5_verb_ops, "o5_verbOps"), OP(o5_get_actor_walk_box, "o5_getActorWalkBox"),
	/* 7C */ OP(o5_is_sound_running, "o5_isSoundRunning"), OP(o5_find_inventory, "o5_findInventory"), OP(o5_walk_actor_to, "o5_walkActorTo"), OP(o5_draw_box, "o5_drawBox"),
	/* 80 */ OP(o5_break_here, "o5_breakHere"), OP(o5_put_actor, "o5_putActor"), OP(o5_start_music, "o5_startMusic"), OP(o5_get_actor_room, "o5_getActorRoom"),
	/* 84 */ OP(o5_is_greater_equal, "o5_isGreaterEqual"), OP(o5_draw_object, "o5_drawObject"), OP(o5_get_actor_elevation, "o5_getActorElevation"), OP(o5_set_state, "o5_setState"),
	/* 88 */ OP(o5_is_not_equal, "o5_isNotEqual"), OP(o5_face_actor, "o5_faceActor"), OP(o5_start_script, "o5_startScript"), OP(o5_get_verb_entrypoint, "o5_getVerbEntrypoint"),
	/* 8C */ OP(o5_resource_routines, "o5_resourceRoutines"), OP(o5_walk_actor_to_actor, "o5_walkActorToActor"), OP(o5_put_actor_at_object, "o5_putActorAtObject"), OP(o5_get_object_state, "o5_getObjectState"),
	/* 90 */ OP(o5_get_object_owner, "o5_getObjectOwner"), OP(o5_animate_actor, "o5_animateActor"), OP(o5_pan_camera_to, "o5_panCameraTo"), OP(o5_actor_ops, "o5_actorOps"),
	/* 94 */ OP(o5_print, "o5_print"), OP(o5_actor_from_pos, "o5_actorFromPos"), OP(o5_get_random_nr, "o5_getRandomNr"), OP(o5_and, "o5_and"),
	/* 98 */ OP(o5_system_ops, "o5_systemOps"), OP(o5_do_sentence, "o5_doSentence"), OP(o5_move, "o5_move"), OP(o5_multiply, "o5_multiply"),
	/* 9C */ OP(o5_start_sound, "o5_startSound"), OP(o5_if_class_of_is, "o5_ifClassOfIs"), OP(o5_walk_actor_to, "o5_walkActorTo"), OP(o5_is_actor_in_box, "o5_isActorInBox"),
	/* A0 */ OP(o5_stop_object_code, "o5_stopObjectCode"), OP(o5_put_actor, "o5_putActor"), OP(o5_get_anim_counter, "o5_getAnimCounter"), OP(o5_get_actor_y, "o5_getActorY"),
	/* A4 */ OP(o5_load_room_with_ego, "o5_loadRoomWithEgo"), OP(o5_pickup_object, "o5_pickupObject"), OP(o5_set_var_range, "o5_setVarRange"), OP(o5_dummy, "o5_dummy"),
	/* A8 */ OP(o5_not_equal_zero, "o5_notEqualZero"), OP(o5_set_owner_of, "o5_setOwnerOf"), OP(o5_start_script, "o5_startScript"), OP(o5_save_restore_verbs, "o5_saveRestoreVerbs"),
	/* AC */ OP(o5_expression, "o5_expression"), OP(o5_put_actor_in_room, "o5_putActorInRoom"), OP(o5_wait, "o5_wait"), NOP,
	/* B0 */ OP(o5_matrix_ops, "o5_matrixOps"), OP(o5_get_inventory_count, "o5_getInventoryCount"), OP(o5_set_camera_at, "o5_setCameraAt"), OP(o5_room_ops, "o5_roomOps"),
	/* B4 */ OP(o5_get_dist, "o5_getDist"), OP(o5_find_object, "o5_findObject"), OP(o5_walk_actor_to_object, "o5_walkActorToObject"), OP(o5_start_object, "o5_startObject"),
	/* B8 */ OP(o5_is_less_equal, "o5_isLessEqual"), OP(o5_do_sentence, "o5_doSentence"), OP(o5_subtract, "o5_subtract"), OP(o5_get_actor_scale, "o5_getActorScale"),
	/* BC */ OP(o5_stop_sound, "o5_stopSound"), OP(o5_find_inventory, "o5_findInventory"), OP(o5_walk_actor_to, "o5_walkActorTo"), OP(o5_draw_box, "o5_drawBox"),
	/* C0 */ OP(o5_end_cutscene, "o5_endCutscene"), OP(o5_put_actor, "o5_putActor"), OP(o5_chain_script, "o5_chainScript"), OP(o5_get_actor_x, "o5_getActorX"),
	/* C4 */ OP(o5_is_less, "o5_isLess"), NOP, OP(o5_decrement, "o5_decrement"), OP(o5_set_state, "o5_setState"),
	/* C8 */ OP(o5_is_equal, "o5_isEqual"), OP(o5_face_actor, "o5_faceActor"), OP(o5_start_script, "o5_startScript"), OP(o5_get_verb_entrypoint, "o5_getVerbEntrypoint"),
	/* CC */ OP(o5_pseudo_room, "o5_pseudoRoom"), OP(o5_walk_actor_to_actor, "o5_walkActorToActor"), OP(o5_put_actor_at_object, "o5_putActorAtObject"), NOP,
	/* D0 */ NOP, OP(o5_animate_actor, "o5_animateActor"), OP(o5_actor_follow_camera, "o5_actorFollowCamera"), OP(o5_actor_ops, "o5_actorOps"),
	/* D4 */ OP(o5_set_object_name, "o5_setObjectName"), OP(o5_actor_from_pos, "o5_actorFromPos"), OP(o5_get_actor_moving, "o5_getActorMoving"), OP(o5_or, "o5_or"),
	/* D8 */ OP(o5_print_ego, "o5_printEgo"), OP(o5_do_sentence, "o5_doSentence"), OP(o5_add, "o5_add"), OP(o5_divide, "o5_divide"),
	/* DC */ NOP, OP(o5_set_class, "o5_setClass"), OP(o5_walk_actor_to, "o5_walkActorTo"), OP(o5_is_actor_in_box, "o5_isActorInBox"),
	/* E0 */ OP(o5_freeze_scripts, "o5_freezeScripts"), OP(o5_put_actor, "o5_putActor"), OP(o5_stop_script, "o5_stopScript"), OP(o5_get_actor_facing, "o5_getActorFacing"),
	/* E4 */ OP(o5_load_room_with_ego, "o5_loadRoomWithEgo"), OP(o5_pickup_object, "o5_pickupObject"), OP(o5_get_closest_obj_actor, "o5_getClosestObjActor"), OP(o5_get_string_width, "o5_getStringWidth"),
	/* E8 */ OP(o5_is_script_running, "o5_isScriptRunning"), OP(o5_set_owner_of, "o5_setOwnerOf"), OP(o5_start_script, "o5_startScript"), OP(o5_debug, "o5_debug"),
	/* EC */ OP(o5_get_actor_width, "o5_getActorWidth"), OP(o5_put_actor_in_room, "o5_putActorInRoom"), OP(o5_stop_object_script, "o5_stopObjectScript"), NOP,
	/* F0 */ OP(o5_lights, "o5_lights"), OP(o5_get_actor_costume, "o5_getActorCostume"), OP(o5_load_room, "o5_loadRoom"), OP(o5_room_ops, "o5_roomOps"),
	/* F4 */ OP(o5_get_dist, "o5_getDist"), OP(o5_find_object, "o5_findObject"), OP(o5_walk_actor_to_object, "o5_walkActorToObject"), OP(o5_start_object, "o5_startObject"),
	/* F8 */ OP(o5_is_greater, "o5_isGreater"), OP(o5_do_sentence, "o5_doSentence"), OP(o5_verb_ops, "o5_verbOps"), OP(o5_get_actor_walk_box, "o5_getActorWalkBox"),
	/* FC */ OP(o5_is_sound_running, "o5_isSoundRunning"), OP(o5_find_inventory, "o5_findInventory"), OP(o5_walk_actor_to, "o5_walkActorTo"), OP(o5_draw_box, "o5_drawBox"),
};

void execute_opcode(uint8_t op)
{
	if (!opcodes[op].fn)
		fatal("invalid opcode", op, (int)(g.script_ptr - g.script_org));
	opcodes[op].fn();
}

const char *opcode_name(uint8_t op)
{
	return opcodes[op].name ? opcodes[op].name : "invalid";
}
