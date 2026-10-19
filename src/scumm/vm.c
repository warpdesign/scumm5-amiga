/* scumm5-amiga - SCUMM v5 script machinery
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/script.cpp and common/random.cpp,
 * ScummVM commit 96b42d42bc8533953bdd701db059cc33c839a9ff, reduced to
 * SCUMM v5. ScummVM is the legal property of its developers, whose names
 * are too numerous to list here; see the COPYRIGHT file distributed with
 * the ScummVM source.
 */
#include "scumm.h"

struct scumm g;

uint32_t rnd_get(uint32_t max)
{
	g.rnd_seed ^= g.rnd_seed >> 13;
	g.rnd_seed ^= g.rnd_seed << 21;
	g.rnd_seed ^= g.rnd_seed >> 11;
	return (g.rnd_seed * 0xDEADBF03u) % (max + 1);
}

/* ---- script pointer ---- */

uint8_t *script_base(const struct script_slot *ss)
{
	int idx;

	switch (ss->where) {
	case WIO_INVENTORY:
		for (idx = 0; idx < NUM_INVENTORY; idx++)
			if (g.inventory[idx] == ss->number)
				return g.inventory_data[idx];
		fatal("inventory script not found", ss->number, 0);
		return 0;
	case WIO_LOCAL:
	case WIO_ROOM:
		return res_get(RT_ROOM, g.room_resource);
	case WIO_GLOBAL:
		return res_get(RT_SCRIPT, ss->number);
	case WIO_FLOBJECT:
		idx = get_object_index(ss->number);
		if (idx < 0)
			fatal("flobject script not found", ss->number, 0);
		return g.fl_objects[g.objs[idx].fl_object_index];
	}
	fatal("bad script type", ss->where, ss->number);
	return 0;
}

static void get_script_base_address(void)
{
	if (g.current_script == 0xFF)
		return;
	g.script_org = script_base(&g.slot[g.current_script]);
}

static void reset_script_pointer(void)
{
	if (g.current_script == 0xFF)
		return;
	g.script_ptr = g.script_org + g.slot[g.current_script].offs;
}

void update_script_ptr(void)
{
	if (g.current_script == 0xFF)
		return;
	g.slot[g.current_script].offs = (uint32_t)(g.script_ptr - g.script_org);
}

void refresh_script_ptr(void)
{
}

uint8_t fetch_byte(void)
{
	return *g.script_ptr++;
}

uint16_t fetch_word(void)
{
	uint16_t a = rd_le16(g.script_ptr);
	g.script_ptr += 2;
	return a;
}

int16_t fetch_word_signed(void)
{
	return (int16_t)fetch_word();
}

/* ---- variables ---- */

int32_t read_var(uint32_t var)
{
	int a;

	if (var & 0x2000) {
		a = fetch_word();
		if (a & 0x2000)
			var += read_var(a & ~0x2000);
		else
			var += a & 0xFFF;
		var &= ~0x2000;
	}

	if (!(var & 0xF000)) {
		/* MI2 with copy protection disabled, as ScummVM does by default */
		if (var == 490)
			var = 518;
		if (var == VAR_NOSUBTITLES)
			return 0;
		if (var >= g.num_vars)
			fatal("variable out of range (read)", var, 0);
		return g.vars[var];
	}

	if (var & 0x8000) {
		var &= 0x7FFF;
		if (var >= g.num_bit_vars)
			fatal("bit variable out of range (read)", var, 0);
		return (g.bit_vars[var >> 3] & (1 << (var & 7))) ? 1 : 0;
	}

	if (var & 0x4000) {
		var &= 0xFFF;
		if (var > 20)
			fatal("local variable out of range (read)", var, 0);
		return g.localvar[g.current_script][var];
	}

	fatal("illegal varbits (read)", var, 0);
	return -1;
}

void write_var(uint32_t var, int32_t value)
{
	if (!(var & 0xF000)) {
		if (var >= g.num_vars)
			fatal("variable out of range (write)", var, 0);
		g.vars[var] = value;
		return;
	}

	if (var & 0x8000) {
		var &= 0x7FFF;
		if (var >= g.num_bit_vars)
			fatal("bit variable out of range (write)", var, 0);
		if (value)
			g.bit_vars[var >> 3] |= (uint8_t)(1 << (var & 7));
		else
			g.bit_vars[var >> 3] &= (uint8_t)~(1 << (var & 7));
		return;
	}

	if (var & 0x4000) {
		var &= 0xFFF;
		if (var > 20)
			fatal("local variable out of range (write)", var, 0);
		g.localvar[g.current_script][var] = value;
		return;
	}

	fatal("illegal varbits (write)", var, 0);
}

/* ---- running scripts ---- */

static void initialize_locals(int slot, const int32_t *vars)
{
	int i;
	for (i = 0; i < NUM_SCRIPT_LOCAL; i++)
		g.localvar[slot][i] = vars ? vars[i] : 0;
}

int get_script_slot(void)
{
	int i;
	for (i = 1; i < NUM_SCRIPT_SLOT; i++)
		if (g.slot[i].status == SS_DEAD)
			return i;
	fatal("too many scripts running", NUM_SCRIPT_SLOT, 0);
	return -1;
}

static void execute_script(void)
{
	while (g.current_script != 0xFF) {
		g.opcode = fetch_byte();
		g.slot[g.current_script].didexec = 1;
		if (trace_on)
			trace_opcode();
		execute_opcode(g.opcode);
	}
}

void run_script_nested(int script)
{
	struct nested_script *nest;
	struct script_slot *slot;

	update_script_ptr();

	if (g.num_nested >= MAX_NESTING)
		fatal("too many nested scripts", g.num_nested, 0);

	nest = &g.nest[g.num_nested];
	if (g.current_script == 0xFF) {
		nest->number = 0;
		nest->where = 0xFF;
	} else {
		slot = &g.slot[g.current_script];
		nest->number = slot->number;
		nest->where = slot->where;
		nest->slot = g.current_script;
	}
	g.num_nested++;

	g.current_script = (uint8_t)script;
	get_script_base_address();
	reset_script_pointer();
	execute_script();

	if (g.num_nested)
		g.num_nested--;

	if (nest->number) {
		slot = &g.slot[nest->slot];
		if (slot->number == nest->number && slot->where == nest->where &&
		    slot->status != SS_DEAD && slot->freeze_count == 0) {
			g.current_script = nest->slot;
			get_script_base_address();
			reset_script_pointer();
			return;
		}
	}
	g.current_script = 0xFF;
}

void run_script(int script, int freeze_resistant, int recursive, const int32_t *lvars)
{
	struct script_slot *s;
	uint32_t offs;
	uint8_t where;
	int slot;

	if (!script)
		return;
	if (!recursive)
		stop_script(script);

	if (script < NUM_GLOBAL_SCRIPTS) {
		if (!res_get(RT_SCRIPT, script))
			fatal("global script missing", script, 0);
		offs = 8;
		where = WIO_GLOBAL;
		if (trace_on)
			trace_run_script(script, 0);
	} else {
		offs = g.local_script_offsets[script - NUM_GLOBAL_SCRIPTS];
		if (!offs)
			fatal("local script not in room", script, g.room_resource);
		where = WIO_LOCAL;
		if (trace_on)
			trace_run_script(script, 1);
	}

	slot = get_script_slot();
	s = &g.slot[slot];
	s->number = (uint16_t)script;
	s->offs = offs;
	s->status = SS_RUNNING;
	s->where = where;
	s->freeze_resistant = (uint8_t)freeze_resistant;
	s->recursive = (uint8_t)recursive;
	s->freeze_count = 0;
	s->delay_frame_count = 0;
	s->cycle = 1;

	initialize_locals(slot, lvars);
	run_script_nested(slot);
}

void run_object_script(int object, int entry, int freeze_resistant, int recursive, const int32_t *vars)
{
	struct script_slot *s;
	uint32_t obcd;
	int where, offs, slot;

	if (!object)
		return;
	if (!recursive)
		stop_object_script(object);

	where = where_is_object(object);
	if (where == WIO_NOT_FOUND)
		return;

	obcd = get_obcd_offs(object);
	slot = get_script_slot();

	offs = get_verb_entrypoint(object, entry);
	if (offs == 0)
		return;

	s = &g.slot[slot];
	s->number = (uint16_t)object;
	s->offs = obcd + (uint32_t)offs;
	s->status = SS_RUNNING;
	s->where = (uint8_t)where;
	s->freeze_resistant = (uint8_t)freeze_resistant;
	s->recursive = (uint8_t)recursive;
	s->freeze_count = 0;
	s->delay_frame_count = 0;
	s->cycle = 1;

	initialize_locals(slot, vars);
	run_script_nested(slot);
}

int get_verb_entrypoint(int obj, int entry)
{
	const uint8_t *objptr, *verbptr;
	int verboffs;

	if (where_is_object(obj) == WIO_NOT_FOUND)
		return 0;

	objptr = get_obcd_from_object(obj);
	if (!objptr)
		fatal("object code missing", obj, 0);
	verbptr = res_find(TAG('V', 'E', 'R', 'B'), objptr);
	if (!verbptr)
		fatal("object has no VERB block", obj, 0);
	verboffs = (int)(verbptr - objptr);
	verbptr += 8;

	for (;;) {
		if (!*verbptr)
			return 0;
		if (*verbptr == entry || *verbptr == 0xFF)
			break;
		verbptr += 3;
	}
	return verboffs + rd_le16(verbptr + 1);
}

static void kill_slot(int i)
{
	struct script_slot *ss = &g.slot[i];

	if (ss->cutscene_override)
		fatal("script stopped with active cutscene/override", ss->number, 0);
	ss->number = 0;
	ss->status = SS_DEAD;
	if (g.current_script == i)
		g.current_script = 0xFF;
}

void stop_script(int script)
{
	int i;

	if (!script)
		return;
	for (i = 0; i < NUM_SCRIPT_SLOT; i++) {
		struct script_slot *ss = &g.slot[i];
		if (script == ss->number && ss->status != SS_DEAD &&
		    (ss->where == WIO_GLOBAL || ss->where == WIO_LOCAL))
			kill_slot(i);
	}
	for (i = 0; i < g.num_nested; i++) {
		if (g.nest[i].number == script &&
		    (g.nest[i].where == WIO_GLOBAL || g.nest[i].where == WIO_LOCAL)) {
			g.nest[i].number = 0;
			g.nest[i].slot = 0xFF;
			g.nest[i].where = 0xFF;
		}
	}
}

void stop_object_script(int script)
{
	int i;

	if (!script)
		return;
	for (i = 0; i < NUM_SCRIPT_SLOT; i++) {
		struct script_slot *ss = &g.slot[i];
		if (script == ss->number && ss->status != SS_DEAD &&
		    (ss->where == WIO_ROOM || ss->where == WIO_INVENTORY || ss->where == WIO_FLOBJECT))
			kill_slot(i);
	}
	for (i = 0; i < g.num_nested; i++) {
		if (g.nest[i].number == script &&
		    (g.nest[i].where == WIO_ROOM || g.nest[i].where == WIO_INVENTORY || g.nest[i].where == WIO_FLOBJECT)) {
			g.nest[i].number = 0;
			g.nest[i].slot = 0xFF;
			g.nest[i].where = 0xFF;
		}
	}
}

void stop_object_code(void)
{
	struct script_slot *ss = &g.slot[g.current_script];

	if (ss->where != WIO_GLOBAL && ss->where != WIO_LOCAL) {
		stop_object_script(ss->number);
	} else {
		if (ss->cutscene_override)
			fatal("script ending with active cutscene/override", ss->number, ss->cutscene_override);
		ss->number = 0;
		ss->status = SS_DEAD;
	}
	g.current_script = 0xFF;
}

void run_inventory_script(int i)
{
	int32_t args[NUM_SCRIPT_LOCAL] = { 0 };

	if (VAR(VAR_INVENTORY_SCRIPT)) {
		args[0] = i;
		run_script(VAR(VAR_INVENTORY_SCRIPT), 0, 0, args);
	}
}

void freeze_scripts(int flag)
{
	int i;

	for (i = 0; i < NUM_SCRIPT_SLOT; i++) {
		if (g.current_script != i && g.slot[i].status != SS_DEAD &&
		    (!g.slot[i].freeze_resistant || flag >= 0x80)) {
			g.slot[i].status |= 0x80;
			g.slot[i].freeze_count++;
		}
	}
	for (i = 0; i < NUM_SENTENCE; i++)
		g.sentences[i].freeze_count++;

	if (g.cutscene_script_index != 0xFF) {
		g.slot[g.cutscene_script_index].status &= 0x7F;
		g.slot[g.cutscene_script_index].freeze_count = 0;
	}
}

void unfreeze_scripts(void)
{
	int i;

	for (i = 0; i < NUM_SCRIPT_SLOT; i++) {
		if (g.slot[i].status & 0x80) {
			if (!--g.slot[i].freeze_count)
				g.slot[i].status &= 0x7F;
		}
	}
	for (i = 0; i < NUM_SENTENCE; i++)
		if (g.sentences[i].freeze_count > 0)
			g.sentences[i].freeze_count--;
}

void run_all_scripts(void)
{
	int i;

	for (i = 0; i < NUM_SCRIPT_SLOT; i++)
		g.slot[i].didexec = 0;

	g.current_script = 0xFF;
	for (i = 0; i < NUM_SCRIPT_SLOT; i++) {
		if (g.slot[i].cycle == 1 && g.slot[i].status == SS_RUNNING && !g.slot[i].didexec) {
			g.current_script = (uint8_t)i;
			get_script_base_address();
			reset_script_pointer();
			execute_script();
		}
	}
}

static void run_room_code(uint16_t number, uint32_t offs)
{
	int slot = get_script_slot();
	struct script_slot *s = &g.slot[slot];

	s->status = SS_RUNNING;
	s->number = number;
	s->where = WIO_ROOM;
	s->offs = offs;
	s->freeze_resistant = 0;
	s->recursive = 0;
	s->freeze_count = 0;
	s->delay_frame_count = 0;
	s->cycle = 1;
	initialize_locals(slot, 0);
	run_script_nested(slot);
}

void run_exit_script(void)
{
	if (VAR(VAR_EXIT_SCRIPT))
		run_script(VAR(VAR_EXIT_SCRIPT), 0, 0, 0);
	if (g.excd_offs)
		run_room_code(SCRIPT_EXCD, g.excd_offs);
	if (VAR(VAR_EXIT_SCRIPT2))
		run_script(VAR(VAR_EXIT_SCRIPT2), 0, 0, 0);
}

void run_entry_script(void)
{
	if (VAR(VAR_ENTRY_SCRIPT))
		run_script(VAR(VAR_ENTRY_SCRIPT), 0, 0, 0);
	if (g.encd_offs)
		run_room_code(SCRIPT_ENCD, g.encd_offs);
	if (VAR(VAR_ENTRY_SCRIPT2))
		run_script(VAR(VAR_ENTRY_SCRIPT2), 0, 0, 0);
}

void kill_scripts_and_resources(void)
{
	int i;

	for (i = 0; i < NUM_SCRIPT_SLOT; i++) {
		struct script_slot *ss = &g.slot[i];
		if (ss->where == WIO_ROOM || ss->where == WIO_FLOBJECT || ss->where == WIO_LOCAL) {
			ss->cutscene_override = 0;
			ss->status = SS_DEAD;
		}
	}

	for (i = 0; i < NUM_NEW_NAMES; i++) {
		int obj = g.new_names[i];
		if (obj) {
			int owner = get_owner(obj);
			if (owner == 0 || owner == OF_OWNER_ROOM) {
				g.new_names[i] = 0;
				res_free(g.new_name_data[i]);
				g.new_name_data[i] = 0;
			}
		}
	}
}

void decrease_script_delay(int amount)
{
	int i;

	for (i = 0; i < NUM_SCRIPT_SLOT; i++) {
		struct script_slot *ss = &g.slot[i];
		if (ss->status == SS_PAUSED) {
			ss->delay -= amount;
			if (ss->delay < 0) {
				ss->status = SS_RUNNING;
				ss->delay = 0;
			}
		}
	}
}

int is_script_in_use(int script)
{
	int i;
	for (i = 0; i < NUM_SCRIPT_SLOT; i++)
		if (g.slot[i].number == script)
			return 1;
	return 0;
}

int is_script_running(int script)
{
	int i;
	for (i = 0; i < NUM_SCRIPT_SLOT; i++) {
		const struct script_slot *ss = &g.slot[i];
		if (ss->number == script && (ss->where == WIO_GLOBAL || ss->where == WIO_LOCAL) && ss->status != SS_DEAD)
			return 1;
	}
	return 0;
}

int is_room_script_running(int script)
{
	int i;
	for (i = 0; i < NUM_SCRIPT_SLOT; i++) {
		const struct script_slot *ss = &g.slot[i];
		if (ss->number == script && ss->where == WIO_ROOM && ss->status != SS_DEAD)
			return 1;
	}
	return 0;
}

int res_str_len(const uint8_t *src)
{
	int num = 0;
	uint8_t chr;

	if (!src)
		src = g.script_ptr;
	while ((chr = *src++) != 0) {
		num++;
		if (chr == 0xFF) {
			chr = *src++;
			num++;
			if (chr != 1 && chr != 2 && chr != 3 && chr != 8) {
				src += 2;
				num += 2;
			}
		}
	}
	return num;
}

/* ---- cutscenes ---- */

void begin_cutscene(const int32_t *args)
{
	int scr = g.current_script;

	g.slot[scr].cutscene_override++;
	if (++g.cutscene_sp >= MAX_CUTSCENE)
		fatal("cutscene stack overflow", g.cutscene_sp, 0);

	g.cutscene_data[g.cutscene_sp] = (int16_t)args[0];
	g.cutscene_script[g.cutscene_sp] = 0;
	g.cutscene_ptr[g.cutscene_sp] = 0;

	g.cutscene_script_index = (uint8_t)scr;
	if (VAR(VAR_CUTSCENE_START_SCRIPT))
		run_script(VAR(VAR_CUTSCENE_START_SCRIPT), 0, 0, args);
	g.cutscene_script_index = 0xFF;
}

void end_cutscene(void)
{
	struct script_slot *ss = &g.slot[g.current_script];
	int32_t args[NUM_SCRIPT_LOCAL] = { 0 };

	if (ss->cutscene_override > 0)
		ss->cutscene_override--;

	args[0] = g.cutscene_data[g.cutscene_sp];
	VAR(VAR_OVERRIDE) = 0;

	if (g.cutscene_ptr[g.cutscene_sp] && ss->cutscene_override > 0)
		ss->cutscene_override--;

	g.cutscene_script[g.cutscene_sp] = 0;
	g.cutscene_ptr[g.cutscene_sp] = 0;

	if (g.cutscene_sp == 0)
		fatal("cutscene stack underflow", 0, 0);
	g.cutscene_sp--;

	if (VAR(VAR_CUTSCENE_END_SCRIPT))
		run_script(VAR(VAR_CUTSCENE_END_SCRIPT), 0, 0, args);
}

void abort_cutscene(void)
{
	int idx = g.cutscene_sp;
	uint32_t offs = g.cutscene_ptr[idx];

	if (offs) {
		struct script_slot *ss = &g.slot[g.cutscene_script[idx]];
		ss->offs = offs;
		ss->status = SS_RUNNING;
		ss->freeze_count = 0;
		if (ss->cutscene_override > 0)
			ss->cutscene_override--;
		VAR(VAR_OVERRIDE) = 1;
		g.cutscene_ptr[idx] = 0;
	}
}

void begin_override(void)
{
	int idx = g.cutscene_sp;

	g.cutscene_ptr[idx] = (uint32_t)(g.script_ptr - g.script_org);
	g.cutscene_script[idx] = g.current_script;

	/* skip the jump that follows: it is what skipping a cutscene takes */
	fetch_byte();
	fetch_word();
	VAR(VAR_OVERRIDE) = 0;
}

void end_override(void)
{
	int idx = g.cutscene_sp;

	g.cutscene_ptr[idx] = 0;
	g.cutscene_script[idx] = 0;
	VAR(VAR_OVERRIDE) = 0;
}

/* ---- sentences and input ---- */

void do_sentence(int verb, int a, int b)
{
	struct sentence *st;

	if (g.sentence_num >= NUM_SENTENCE)
		fatal("sentence queue overflow", verb, a);
	st = &g.sentences[g.sentence_num++];
	st->verb = (uint8_t)verb;
	st->object_a = (uint16_t)a;
	st->object_b = (uint16_t)b;
	st->preposition = (b != 0);
	st->freeze_count = 0;
}

void check_and_run_sentence_script(void)
{
	int32_t args[NUM_SCRIPT_LOCAL] = { 0 };
	int sentence_script = VAR(VAR_SENTENCE_SCRIPT);
	int i;
	struct sentence *st;

	if (is_script_in_use(sentence_script)) {
		for (i = 0; i < NUM_SCRIPT_SLOT; i++) {
			const struct script_slot *ss = &g.slot[i];
			if (ss->number == sentence_script && ss->status != SS_DEAD && ss->freeze_count == 0)
				return;
		}
	}

	if (!g.sentence_num || g.sentences[g.sentence_num - 1].freeze_count)
		return;

	g.sentence_num--;
	st = &g.sentences[g.sentence_num];

	if (st->preposition && st->object_b == st->object_a)
		return;

	args[0] = st->verb;
	args[1] = st->object_a;
	args[2] = st->object_b;

	g.current_script = 0xFF;
	if (sentence_script)
		run_script(sentence_script, 0, 0, args);
}

void run_input_script(int click_area, int val, int mode)
{
	int32_t args[NUM_SCRIPT_LOCAL] = { 0 };
	int verb_script = VAR(VAR_VERB_SCRIPT);

	args[0] = click_area;
	args[1] = val;
	args[2] = mode;
	if (verb_script)
		run_script(verb_script, 0, 0, args);
}

void vm_init(void)
{
	int i;

	for (i = 0; i < NUM_SCRIPT_SLOT; i++)
		g.slot[i].status = SS_DEAD;
	g.num_nested = 0;
	g.cutscene_sp = 0;
	g.cutscene_script_index = 0xFF;
	g.current_script = 0xFF;
	g.sentence_num = 0;
}
