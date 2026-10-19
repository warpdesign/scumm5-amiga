/* scumm5-amiga - objects, inventory and object names
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/object.cpp and resource.cpp
 * (readGlobalObjects), ScummVM commit
 * 96b42d42bc8533953bdd701db059cc33c839a9ff, reduced to SCUMM v5. ScummVM
 * is the legal property of its developers, whose names are too numerous
 * to list here; see the COPYRIGHT file distributed with the ScummVM source.
 */
#include "scumm.h"

void read_global_objects(const uint8_t *dobj)
{
	uint16_t num = rd_le16(dobj + 8), i;
	const uint8_t *p = dobj + 10;

	g.num_global_objects = num;
	g.object_owner = res_alloc(num);
	g.object_state = res_alloc(num);
	g.class_data = res_alloc((uint32_t)num * 4);
	if (!g.object_owner || !g.object_state || !g.class_data)
		fatal("out of memory for object tables", num, 0);
	for (i = 0; i < num; i++) {
		g.object_state[i] = p[i] >> 4;
		g.object_owner[i] = p[i] & 0x0F;
	}
	p += num;
	for (i = 0; i < num; i++)
		g.class_data[i] = rd_le32(p + i * 4);
}

static void check_obj(int obj)
{
	if (obj < 0 || obj >= g.num_global_objects)
		fatal("object out of range", obj, 0);
}

int get_class(int obj, int cls)
{
	check_obj(obj);
	cls &= 0x7F;
	if (cls < 1 || cls > 32)
		fatal("class out of range", cls, obj);
	return (g.class_data[obj] & (1u << (cls - 1))) != 0;
}

void put_class(int obj, int cls, int set)
{
	check_obj(obj);
	cls &= 0x7F;
	if (cls < 1 || cls > 32)
		fatal("class out of range", cls, obj);
	if (set)
		g.class_data[obj] |= 1u << (cls - 1);
	else
		g.class_data[obj] &= ~(1u << (cls - 1));
}

int get_owner(int obj)
{
	check_obj(obj);
	return g.object_owner[obj];
}

void put_owner(int obj, int owner)
{
	check_obj(obj);
	g.object_owner[obj] = (uint8_t)owner;
}

int get_state(int obj)
{
	check_obj(obj);
	return g.object_state[obj];
}

void put_state(int obj, int state)
{
	check_obj(obj);
	g.object_state[obj] = (uint8_t)state;
}

int get_object_index(int object)
{
	int i;

	if (object < 1)
		return -1;
	for (i = NUM_LOCAL_OBJECTS - 1; i > 0; i--)
		if (g.objs[i].obj_nr == object)
			return i;
	return -1;
}

int where_is_object(int object)
{
	int i;

	if (object >= g.num_global_objects || object < 1)
		return WIO_NOT_FOUND;

	if (g.object_owner[object] != OF_OWNER_ROOM) {
		for (i = 0; i < NUM_INVENTORY; i++)
			if (g.inventory[i] == object)
				return WIO_INVENTORY;
		return WIO_NOT_FOUND;
	}

	for (i = NUM_LOCAL_OBJECTS - 1; i > 0; i--) {
		if (g.objs[i].obj_nr == object) {
			if (g.objs[i].fl_object_index)
				return WIO_FLOBJECT;
			return WIO_ROOM;
		}
	}
	return WIO_NOT_FOUND;
}

/* ---- room objects ---- */

static const uint8_t *room_ptr(void)
{
	return res_get(RT_ROOM, g.room_resource);
}

uint32_t get_obcd_offs(int object)
{
	int i;

	if (g.object_owner[object] != OF_OWNER_ROOM)
		return 0;
	for (i = NUM_LOCAL_OBJECTS - 1; i > 0; i--) {
		if (g.objs[i].obj_nr == object) {
			if (g.objs[i].fl_object_index)
				return 8;
			return g.objs[i].obcd_offset;
		}
	}
	return 0;
}

const uint8_t *get_obcd_from_object(int obj)
{
	int i;

	if (g.object_owner[obj] != OF_OWNER_ROOM) {
		for (i = 0; i < NUM_INVENTORY; i++)
			if (g.inventory[i] == obj)
				return g.inventory_data[i];
	} else {
		for (i = NUM_LOCAL_OBJECTS - 1; i > 0; i--) {
			if (g.objs[i].obj_nr == obj) {
				if (g.objs[i].fl_object_index)
					return g.fl_objects[g.objs[i].fl_object_index] + g.objs[i].obcd_offset;
				return room_ptr() + g.objs[i].obcd_offset;
			}
		}
	}
	return 0;
}

static int find_local_object_slot(void)
{
	int i;

	for (i = 1; i < NUM_LOCAL_OBJECTS; i++) {
		if (!g.objs[i].obj_nr) {
			memset(&g.objs[i], 0, sizeof(g.objs[i]));
			return i;
		}
	}
	return -1;
}

void clear_room_objects(void)
{
	int i;

	for (i = 0; i < NUM_LOCAL_OBJECTS; i++) {
		struct object_data *od = &g.objs[i];
		if (od->obj_nr < 1)
			continue;
		if (od->fl_object_index == 0) {
			od->obj_nr = 0;
		} else {
			/* flobjects are never locked in v5 */
			res_free(g.fl_objects[od->fl_object_index]);
			g.fl_objects[od->fl_object_index] = 0;
			od->obj_nr = 0;
			od->fl_object_index = 0;
		}
	}
}

/* CDHD v5: obj_id u16, x, y, w, h (in 8 px units), flags, parent,
 * walk_x s16, walk_y s16, actordir */
static void reset_room_object(struct object_data *od, const uint8_t *obcd)
{
	const uint8_t *cdhd = res_find(TAG('C', 'D', 'H', 'D'), obcd);

	if (!cdhd)
		fatal("room missing CDHD block", g.room_resource, 0);
	cdhd += 8;
	od->flags = 1;
	od->obj_nr = rd_le16(cdhd);
	od->x_pos = (int16_t)(cdhd[2] * 8);
	od->y_pos = (int16_t)(cdhd[3] * 8);
	od->width = (uint16_t)(cdhd[4] * 8);
	od->height = (uint16_t)(cdhd[5] * 8);
	od->parentstate = cdhd[6] == 0x80 ? 1 : (cdhd[6] & 0xF);
	od->parent = cdhd[7];
	od->walk_x = (int16_t)rd_le16(cdhd + 8);
	od->walk_y = (int16_t)rd_le16(cdhd + 10);
	od->actordir = cdhd[12];
	od->fl_object_index = 0;
}

static uint16_t obim_id(const uint8_t *obim)
{
	const uint8_t *imhd = res_find(TAG('I', 'M', 'H', 'D'), obim);
	if (!imhd)
		fatal("OBIM without IMHD", g.room_resource, 0);
	return rd_le16(imhd + 8);
}

void reset_room_objects(void)
{
	const uint8_t *room = room_ptr(), *p, *end;
	int i, j, n;

	if (g.num_objects_in_room == 0)
		return;
	if (g.num_objects_in_room > NUM_LOCAL_OBJECTS)
		fatal("too many objects in room", g.room_resource, g.num_objects_in_room);

	end = room + rd_be32(room + 4);
	n = 0;
	for (p = room + 8; p < end && n < g.num_objects_in_room; p += rd_be32(p + 4)) {
		if (rd_be32(p) != TAG('O', 'B', 'C', 'D'))
			continue;
		i = find_local_object_slot();
		g.objs[i].obcd_offset = (uint32_t)(p - room);
		g.objs[i].obj_nr = rd_le16(res_find(TAG('C', 'D', 'H', 'D'), p) + 8);
		n++;
	}
	if (n < g.num_objects_in_room)
		fatal("room missing object code blocks", g.room_resource, n);

	n = 0;
	for (p = room + 8; p < end && n < g.num_objects_in_room; p += rd_be32(p + 4)) {
		uint16_t id;
		if (rd_be32(p) != TAG('O', 'B', 'I', 'M'))
			continue;
		id = obim_id(p);
		for (j = 1; j < NUM_LOCAL_OBJECTS; j++)
			if (g.objs[j].obj_nr == id)
				g.objs[j].obim_offset = (uint32_t)(p - room);
		n++;
	}
	if (n < g.num_objects_in_room)
		fatal("room missing image blocks", g.room_resource, n);

	for (i = 1; i < NUM_LOCAL_OBJECTS; i++)
		if (g.objs[i].obj_nr && !g.objs[i].fl_object_index)
			reset_room_object(&g.objs[i], room + g.objs[i].obcd_offset);
}

void update_object_states(void)
{
	int i;
	for (i = 1; i < NUM_LOCAL_OBJECTS; i++)
		if (g.objs[i].obj_nr > 0)
			g.objs[i].state = (uint8_t)get_state(g.objs[i].obj_nr);
}

void add_object_to_draw_queue(int idx)
{
	if (g.draw_queue_nr >= MAX_DRAW_QUEUE)
		fatal("draw object queue overflow", idx, 0);
	g.draw_queue[g.draw_queue_nr++] = (uint16_t)idx;
}

void clear_draw_object_queue(void)
{
	g.draw_queue_nr = 0;
}

void mark_object_rect_as_dirty(int obj)
{
	int i, strip;

	for (i = 1; i < NUM_LOCAL_OBJECTS; i++) {
		if (g.objs[i].obj_nr == (uint16_t)obj) {
			if (g.objs[i].width != 0) {
				int min_strip = g.objs[i].x_pos / 8 > g.screen_start_strip ? g.objs[i].x_pos / 8 : g.screen_start_strip;
				int max_strip = g.objs[i].x_pos / 8 + g.objs[i].width / 8;
				if (max_strip > g.screen_end_strip + 1)
					max_strip = g.screen_end_strip + 1;
				for (strip = min_strip; strip < max_strip; strip++)
					set_gfx_usage_bit(strip, USAGE_BIT_DIRTY);
			}
			g.bg_needs_redraw = 1;
			return;
		}
	}
}

/* Locates an object's OBCD (and OBIM) in a room that need not be the current one. */
static void find_object_in_room(int id, int room, const uint8_t **obcd, const uint8_t **obim)
{
	const uint8_t *roomptr = res_get(RT_ROOM, (uint16_t)room), *p, *end;
	const uint8_t *rmhd;
	int numobj, n;

	if (!roomptr)
		fatal("findObjectInRoom: room not found", room, id);
	rmhd = res_find(TAG('R', 'M', 'H', 'D'), roomptr);
	numobj = rd_le16(rmhd + 12);
	if (numobj == 0)
		fatal("findObjectInRoom: no object in room", room, id);

	end = roomptr + rd_be32(roomptr + 4);
	*obcd = 0;
	n = 0;
	for (p = roomptr + 8; p < end && n < numobj; p += rd_be32(p + 4)) {
		if (rd_be32(p) != TAG('O', 'B', 'C', 'D'))
			continue;
		n++;
		if (rd_le16(res_find(TAG('C', 'D', 'H', 'D'), p) + 8) == id) {
			*obcd = p;
			break;
		}
	}
	if (!*obcd)
		fatal("findObjectInRoom: object not found", id, room);
	if (!obim)
		return;
	*obim = 0;
	n = 0;
	for (p = roomptr + 8; p < end && n < numobj; p += rd_be32(p + 4)) {
		if (rd_be32(p) != TAG('O', 'B', 'I', 'M'))
			continue;
		n++;
		if (obim_id(p) == id) {
			*obim = p;
			break;
		}
	}
	if (!*obim)
		fatal("findObjectInRoom: object image not found", id, room);
}

/* ---- inventory ---- */

static int get_inventory_slot(void)
{
	int i;
	for (i = 0; i < NUM_INVENTORY; i++)
		if (g.inventory[i] == 0)
			return i;
	fatal("inventory full", NUM_INVENTORY, 0);
	return -1;
}

void add_object_to_inventory(int obj, int room)
{
	const uint8_t *ptr;
	uint32_t size;
	int slot, idx;
	uint8_t *dst;

	if (where_is_object(obj) == WIO_FLOBJECT) {
		idx = get_object_index(obj);
		ptr = g.fl_objects[g.objs[idx].fl_object_index] + 8;
	} else {
		find_object_in_room(obj, room, &ptr, 0);
	}
	size = rd_be32(ptr + 4);

	slot = get_inventory_slot();
	g.inventory[slot] = (uint16_t)obj;
	dst = res_alloc(size);
	if (!dst)
		fatal("out of memory for inventory object", obj, (int)size);
	/* the allocation may have expired the room: look the object up again */
	if (where_is_object(obj) == WIO_FLOBJECT) {
		idx = get_object_index(obj);
		ptr = g.fl_objects[g.objs[idx].fl_object_index] + 8;
	} else {
		find_object_in_room(obj, room, &ptr, 0);
	}
	memcpy(dst, ptr, size);
	g.inventory_data[slot] = dst;
}

int find_inventory(int owner, int idx)
{
	int count = 1, i, obj;
	for (i = 0; i < NUM_INVENTORY; i++) {
		obj = g.inventory[i];
		if (obj && get_owner(obj) == owner && count++ == idx)
			return obj;
	}
	return 0;
}

int get_inventory_count(int owner)
{
	int i, obj, count = 0;
	for (i = 0; i < NUM_INVENTORY; i++) {
		obj = g.inventory[i];
		if (obj && get_owner(obj) == owner)
			count++;
	}
	return count;
}

static void clear_owner_of(int obj)
{
	int i;

	stop_object_script(obj);

	if (get_owner(obj) == OF_OWNER_ROOM) {
		for (i = 0; i < NUM_LOCAL_OBJECTS; i++) {
			if (g.objs[i].obj_nr == obj && g.objs[i].fl_object_index) {
				res_free(g.fl_objects[g.objs[i].fl_object_index]);
				g.fl_objects[g.objs[i].fl_object_index] = 0;
				g.objs[i].obj_nr = 0;
				g.objs[i].fl_object_index = 0;
			}
		}
		return;
	}

	for (i = 0; i < NUM_INVENTORY; i++) {
		if (g.inventory[i] == obj) {
			res_free(g.inventory_data[i]);
			g.inventory_data[i] = 0;
			g.inventory[i] = 0;
			for (i = 0; i < NUM_INVENTORY - 1; i++) {
				if (!g.inventory[i] && g.inventory[i + 1]) {
					g.inventory[i] = g.inventory[i + 1];
					g.inventory[i + 1] = 0;
					g.inventory_data[i] = g.inventory_data[i + 1];
					g.inventory_data[i + 1] = 0;
				}
			}
			break;
		}
	}
}

void set_owner_of(int obj, int owner)
{
	if (obj <= 0)
		fatal("setOwnerOf: bad object", obj, owner);

	if (owner == 0) {
		clear_owner_of(obj);
		if (g.current_script != 0xFF) {
			const struct script_slot *ss = &g.slot[g.current_script];
			if (ss->where == WIO_INVENTORY) {
				if (ss->number < NUM_INVENTORY && g.inventory[ss->number] == obj)
					fatal("odd setOwnerOf case #1", obj, 0);
				if (ss->number == obj)
					fatal("odd setOwnerOf case #2", obj, 0);
			}
		}
	}

	put_owner(obj, owner);
	run_inventory_script(0);
}

/* ---- positions ---- */

int get_object_or_actor_xy(int object, int *x, int *y)
{
	struct actor *act;

	if (object < NUM_ACTORS) {
		act = deref_actor_safe(object);
		if (act && is_in_current_room(act)) {
			*x = act->pos.x;
			*y = act->pos.y;
			return 0;
		}
		return -1;
	}

	switch (where_is_object(object)) {
	case WIO_NOT_FOUND:
		return -1;
	case WIO_INVENTORY:
		if (g.object_owner[object] < NUM_ACTORS) {
			act = deref_actor(g.object_owner[object]);
			if (act && is_in_current_room(act)) {
				*x = act->pos.x;
				*y = act->pos.y;
				return 0;
			}
		}
		return -1;
	}
	{
		int dir;
		get_object_xy_pos(object, x, y, &dir);
	}
	return 0;
}

void get_object_xy_pos(int object, int *x, int *y, int *dir)
{
	int idx = get_object_index(object);
	const struct object_data *od;

	if (idx < 0)
		fatal("getObjectXYPos: object not in room", object, 0);
	od = &g.objs[idx];
	*x = od->walk_x;
	*y = od->walk_y;
	*dir = old_dir_to_new_dir(od->actordir & 3);
}

static int get_dist(int x, int y, int x2, int y2)
{
	int a = y - y2, b = x - x2;
	if (a < 0)
		a = -a;
	if (b < 0)
		b = -b;
	return a > b ? a : b;
}

int get_obj_act_to_obj_act_dist(int a, int b)
{
	int x, y, x2, y2;
	struct actor *acta = 0, *actb = 0;

	if (a < NUM_ACTORS)
		acta = deref_actor_safe(a);
	if (b < NUM_ACTORS)
		actb = deref_actor_safe(b);

	if (acta && actb && acta->room == actb->room && acta->room && !is_in_current_room(acta))
		return 0;

	if (get_object_or_actor_xy(a, &x, &y) == -1)
		return 0xFF;
	if (get_object_or_actor_xy(b, &x2, &y2) == -1)
		return 0xFF;

	if (acta && !actb) {
		struct adjust_box_result r = adjust_xy_to_be_in_box(acta, x2, y2);
		x2 = r.x;
		y2 = r.y;
	}
	return get_dist(x, y, x2, y2);
}

int find_object(int x, int y)
{
	int i, b;
	uint8_t a;

	for (i = 1; i < NUM_LOCAL_OBJECTS; i++) {
		if (g.objs[i].obj_nr < 1 || get_class(g.objs[i].obj_nr, CLS_UNTOUCHABLE))
			continue;
		b = i;
		do {
			a = g.objs[b].parentstate;
			b = g.objs[b].parent;
			if (b == 0) {
				if (g.objs[i].x_pos <= x && g.objs[i].width + g.objs[i].x_pos > x &&
				    g.objs[i].y_pos <= y && g.objs[i].height + g.objs[i].y_pos > y)
					return g.objs[i].obj_nr;
				break;
			}
		} while ((g.objs[b].state & 0xF) == a);
	}
	return 0;
}

int get_obj_x(int obj)
{
	int x, y;

	if (obj < 1)
		return 0;
	if (obj < NUM_ACTORS)
		return deref_actor(obj)->pos.x;
	if (where_is_object(obj) == WIO_NOT_FOUND)
		return -1;
	if (get_object_or_actor_xy(obj, &x, &y) == -1)
		return -1;
	return x;
}

int get_obj_y(int obj)
{
	int x, y;

	if (obj < 1)
		return 0;
	if (obj < NUM_ACTORS)
		return deref_actor(obj)->pos.y;
	if (where_is_object(obj) == WIO_NOT_FOUND)
		return -1;
	if (get_object_or_actor_xy(obj, &x, &y) == -1)
		return -1;
	return y;
}

/* ---- names ---- */

const uint8_t *get_obj_or_actor_name(int obj)
{
	const uint8_t *objptr, *obna;
	int i;

	if (obj < NUM_ACTORS)
		return deref_actor(obj)->name;

	for (i = 0; i < NUM_NEW_NAMES; i++)
		if (g.new_names[i] == obj)
			return g.new_name_data[i];

	objptr = get_obcd_from_object(obj);
	if (!objptr)
		return 0;
	obna = res_find(TAG('O', 'B', 'N', 'A'), objptr);
	return obna ? obna + 8 : 0;
}

void set_object_name(int obj)
{
	int i, len;

	if (obj < NUM_ACTORS)
		fatal("can't set actor name with new-name-of", obj, 0);

	for (i = 0; i < NUM_NEW_NAMES; i++) {
		if (g.new_names[i] == obj) {
			res_free(g.new_name_data[i]);
			g.new_name_data[i] = 0;
			g.new_names[i] = 0;
			break;
		}
	}

	for (i = 0; i < NUM_NEW_NAMES; i++) {
		if (g.new_names[i] == 0) {
			len = res_str_len(0) + 1;
			g.new_name_data[i] = res_alloc((uint32_t)len);
			if (!g.new_name_data[i])
				fatal("out of memory for object name", obj, 0);
			memcpy(g.new_name_data[i], g.script_ptr, (size_t)len);
			g.script_ptr += len;
			g.new_names[i] = (uint16_t)obj;
			run_inventory_script(0);
			return;
		}
	}
	fatal("new name table overflow", obj, 0);
}

/* ---- floating objects ---- */

static int find_fl_object_slot(void)
{
	int i;
	for (i = 1; i < NUM_FL_OBJECTS; i++)
		if (!g.fl_objects[i])
			return i;
	fatal("out of flobject slots", NUM_FL_OBJECTS, 0);
	return -1;
}

void load_fl_object(int object, int room)
{
	const uint8_t *obcd, *obim;
	uint32_t obcd_size, obim_size, flob_size;
	struct object_data *od;
	int slot, objslot, was_locked;
	uint8_t *flob;

	if (get_object_index(object) != -1)
		return;

	find_object_in_room(object, room, &obcd, &obim);
	objslot = find_local_object_slot();
	if (objslot == -1)
		fatal("local object table overflow", object, 0);
	od = &g.objs[objslot];

	obcd_size = rd_be32(obcd + 4);
	obim_size = rd_be32(obim + 4);
	od->obcd_offset = 8;
	od->obim_offset = obcd_size + 8;
	flob_size = obcd_size + obim_size + 8;

	was_locked = res_locked(RT_ROOM, (uint16_t)room);
	res_lock(RT_ROOM, (uint16_t)room);
	slot = find_fl_object_slot();
	flob = res_alloc(flob_size);
	if (!flob)
		fatal("out of memory for flobject", object, (int)flob_size);
	find_object_in_room(object, room, &obcd, &obim);
	flob[0] = 'F';
	flob[1] = 'L';
	flob[2] = 'O';
	flob[3] = 'B';
	flob[4] = (uint8_t)(flob_size >> 24);
	flob[5] = (uint8_t)(flob_size >> 16);
	flob[6] = (uint8_t)(flob_size >> 8);
	flob[7] = (uint8_t)flob_size;
	memcpy(flob + 8, obcd, obcd_size);
	memcpy(flob + 8 + obcd_size, obim, obim_size);
	if (!was_locked)
		res_unlock(RT_ROOM, (uint16_t)room);

	g.fl_objects[slot] = flob;
	reset_room_object(od, flob + 8);
	od->fl_object_index = (uint8_t)slot;
}
