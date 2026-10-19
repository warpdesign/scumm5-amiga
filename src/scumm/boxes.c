/* scumm5-amiga - walk boxes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/boxes.cpp, ScummVM commit
 * 96b42d42bc8533953bdd701db059cc33c839a9ff, reduced to SCUMM v5. ScummVM
 * is the legal property of its developers, whose names are too numerous
 * to list here; see the COPYRIGHT file distributed with the ScummVM source.
 *
 * Box record (20 bytes): s16 ulx, uly, urx, ury, lrx, lry, llx, lly;
 * u8 mask, flags; u16 scale (bit 15: scale slot).
 */
#include "scumm.h"

#define SIZEOF_BOX 20
#define BOX_MATRIX_SIZE 2000
#define OLD_INVALID_BOX 255
#define BOX_INVISIBLE 0x80

static uint8_t *box_base(int box)
{
	uint8_t *ptr = g.box_data;

	if (!ptr || box == OLD_INVALID_BOX)
		return 0;
	if (box < 0 || box > ptr[0] - 1)
		fatal("box out of range", box, ptr[0]);
	return ptr + box * SIZEOF_BOX + 2;
}

int get_num_boxes(void)
{
	return g.box_data ? (uint8_t)rd_le16(g.box_data) : 0;
}

int get_mask_from_box(int box)
{
	uint8_t *p = box_base(box);
	return p ? p[16] : 0;
}

int get_box_flags(int box)
{
	uint8_t *p = box_base(box);
	return p ? p[17] : 0;
}

void set_box_flags(int box, int val)
{
	if (val & 0xC000) {
		if (box < 0 || box >= 65)
			fatal("extra box flags out of range", box, val);
		g.extra_box_flags[box] = (uint16_t)val;
	} else {
		uint8_t *p = box_base(box);
		if (p)
			p[17] = (uint8_t)val;
	}
}

int get_box_scale(int box)
{
	uint8_t *p = box_base(box);
	return p ? rd_le16(p + 18) : 255;
}

void set_box_scale(int box, int scale)
{
	uint8_t *p = box_base(box);
	if (!p)
		fatal("setBoxScale: no box", box, 0);
	p[18] = (uint8_t)scale;
	p[19] = (uint8_t)(scale >> 8);
}

void set_scale_slot(int slot, int x1, int y1, int scale1, int x2, int y2, int scale2)
{
	struct scale_slot *s;

	if (slot < 1 || slot > NUM_SCALE_SLOTS)
		fatal("scale slot out of range", slot, 0);
	s = &g.scale_slots[slot - 1];
	s->x2 = (int16_t)x2;
	s->y2 = (int16_t)y2;
	s->scale2 = (int16_t)scale2;
	s->x1 = (int16_t)x1;
	s->y1 = (int16_t)y1;
	s->scale1 = (int16_t)scale1;
}

static int get_scale_from_slot(int slot, int x, int y)
{
	struct scale_slot *s = &g.scale_slots[slot - 1];
	int scale, scale_x = 0, scale_y = 0;

	if (s->y1 == s->y2 && s->x1 == s->x2)
		fatal("invalid scale slot", slot, 0);
	if (s->y1 != s->y2) {
		if (y < 0)
			y = 0;
		scale_y = (s->scale2 - s->scale1) * (y - s->y1) / (s->y2 - s->y1) + s->scale1;
	}
	if (s->x1 == s->x2) {
		scale = scale_y;
	} else {
		scale_x = (s->scale2 - s->scale1) * (x - s->x1) / (s->x2 - s->x1) + s->scale1;
		scale = s->y1 == s->y2 ? scale_x : (scale_x + scale_y) / 2;
	}
	if (scale < 1)
		scale = 1;
	else if (scale > 255)
		scale = 255;
	return scale;
}

int get_scale(int box, int x, int y)
{
	uint8_t *p = box_base(box);
	int scale, slot = 0;

	if (!p)
		return 255;
	scale = rd_le16(p + 18);
	if (scale & 0x8000)
		slot = (scale & 0x7FFF) + 1;
	if (slot)
		scale = get_scale_from_slot(slot, x, y);
	return scale;
}

struct box_coords get_box_coordinates(int box)
{
	struct box_coords b;
	uint8_t *p = box_base(box);

	if (!p)
		fatal("getBoxCoordinates: no box", box, 0);
	b.ul.x = (int16_t)rd_le16(p);
	b.ul.y = (int16_t)rd_le16(p + 2);
	b.ur.x = (int16_t)rd_le16(p + 4);
	b.ur.y = (int16_t)rd_le16(p + 6);
	b.lr.x = (int16_t)rd_le16(p + 8);
	b.lr.y = (int16_t)rd_le16(p + 10);
	b.ll.x = (int16_t)rd_le16(p + 12);
	b.ll.y = (int16_t)rd_le16(p + 14);
	return b;
}

static int compare_slope(struct point p1, struct point p2, struct point p3)
{
	return (p2.y - p1.y) * (p3.x - p1.x) <= (p3.y - p1.y) * (p2.x - p1.x);
}

static struct point closest_pt_on_line(struct point start, struct point end, struct point p)
{
	struct point result;
	int lxdiff = end.x - start.x;
	int lydiff = end.y - start.y;

	if (end.x == start.x) {
		result.x = start.x;
		result.y = p.y;
	} else if (end.y == start.y) {
		result.x = p.x;
		result.y = start.y;
	} else {
		int dist = lxdiff * lxdiff + lydiff * lydiff;
		int a, b, c;
		if ((lxdiff < 0 ? -lxdiff : lxdiff) > (lydiff < 0 ? -lydiff : lydiff)) {
			a = start.x * lydiff / lxdiff;
			b = p.x * lxdiff / lydiff;
			c = (a + b - start.y + p.y) * lydiff * lxdiff / dist;
			result.x = (int16_t)c;
			result.y = (int16_t)(c * lydiff / lxdiff - a + start.y);
		} else {
			a = start.y * lxdiff / lydiff;
			b = p.y * lydiff / lxdiff;
			c = (a + b - start.x + p.x) * lydiff * lxdiff / dist;
			result.x = (int16_t)(c * lxdiff / lydiff - a + start.x);
			result.y = (int16_t)c;
		}
	}

	if ((lydiff < 0 ? -lydiff : lydiff) < (lxdiff < 0 ? -lxdiff : lxdiff)) {
		if (lxdiff > 0) {
			if (result.x < start.x)
				result = start;
			else if (result.x > end.x)
				result = end;
		} else {
			if (result.x > start.x)
				result = start;
			else if (result.x < end.x)
				result = end;
		}
	} else {
		if (lydiff > 0) {
			if (result.y < start.y)
				result = start;
			else if (result.y > end.y)
				result = end;
		} else {
			if (result.y > start.y)
				result = start;
			else if (result.y < end.y)
				result = end;
		}
	}
	return result;
}

static unsigned sqr_dist(struct point a, struct point b)
{
	int dx = a.x - b.x, dy = a.y - b.y;
	return (unsigned)(dx * dx + dy * dy);
}

static int pt_eq(struct point a, struct point b)
{
	return a.x == b.x && a.y == b.y;
}

int check_xy_in_box_bounds(int boxnum, int x, int y)
{
	struct box_coords box;
	struct point p;

	if (boxnum < 0 || boxnum == INVALID_BOX)
		return 0;

	box = get_box_coordinates(boxnum);
	p.x = (int16_t)x;
	p.y = (int16_t)y;

	if (x < box.ul.x && x < box.ur.x && x < box.lr.x && x < box.ll.x)
		return 0;
	if (x > box.ul.x && x > box.ur.x && x > box.lr.x && x > box.ll.x)
		return 0;
	if (y < box.ul.y && y < box.ur.y && y < box.lr.y && y < box.ll.y)
		return 0;
	if (y > box.ul.y && y > box.ur.y && y > box.lr.y && y > box.ll.y)
		return 0;

	if ((pt_eq(box.ul, box.ur) && pt_eq(box.lr, box.ll)) ||
	    (pt_eq(box.ul, box.ll) && pt_eq(box.ur, box.lr))) {
		struct point tmp = closest_pt_on_line(box.ul, box.lr, p);
		if (sqr_dist(p, tmp) <= 4)
			return 1;
	}

	if (!compare_slope(box.ul, box.ur, p))
		return 0;
	if (!compare_slope(box.ur, box.lr, p))
		return 0;
	if (!compare_slope(box.lr, box.ll, p))
		return 0;
	if (!compare_slope(box.ll, box.ul, p))
		return 0;
	return 1;
}

int get_closest_pt_on_box(const struct box_coords *box, int x, int y, int16_t *ox, int16_t *oy)
{
	struct point p, tmp;
	unsigned dist, best = 0xFFFFFF;

	p.x = (int16_t)x;
	p.y = (int16_t)y;

	tmp = closest_pt_on_line(box->ul, box->ur, p);
	dist = sqr_dist(p, tmp);
	if (dist < best) {
		best = dist;
		*ox = tmp.x;
		*oy = tmp.y;
	}
	tmp = closest_pt_on_line(box->ur, box->lr, p);
	dist = sqr_dist(p, tmp);
	if (dist < best) {
		best = dist;
		*ox = tmp.x;
		*oy = tmp.y;
	}
	tmp = closest_pt_on_line(box->lr, box->ll, p);
	dist = sqr_dist(p, tmp);
	if (dist < best) {
		best = dist;
		*ox = tmp.x;
		*oy = tmp.y;
	}
	tmp = closest_pt_on_line(box->ll, box->ul, p);
	dist = sqr_dist(p, tmp);
	if (dist < best) {
		best = dist;
		*ox = tmp.x;
		*oy = tmp.y;
	}
	return (int)best;
}

static const uint8_t *box_matrix_base(void)
{
	const uint8_t *ptr = g.box_matrix;
	if (!ptr)
		fatal("no box matrix", g.room_resource, 0);
	if (*ptr == 0xFF)
		ptr++;
	return ptr;
}

int get_next_box(uint8_t from, uint8_t to)
{
	const uint8_t *boxm, *end;
	int i, dest = -1;
	int num = get_num_boxes();

	if (from == to)
		return to;
	if (to == INVALID_BOX)
		return -1;
	if (from == INVALID_BOX)
		return to;
	if (from >= num || to >= num)
		fatal("getNextBox: box out of range", from, to);

	boxm = box_matrix_base();
	end = boxm + g.box_matrix_size;

	for (i = 0; i < from && boxm < end; i++) {
		while (boxm < end && *boxm != 0xFF)
			boxm += 3;
		boxm++;
	}
	while (boxm < end && boxm[0] != 0xFF) {
		if (boxm[0] <= to && to <= boxm[1])
			dest = (int8_t)boxm[2];
		boxm += 3;
	}
	return dest;
}

#define SWAP16(a, b) do { int16_t t_ = (a); (a) = (b); (b) = t_; } while (0)

static void rotate(struct box_coords *b)
{
	struct point tmp = b->ul;
	b->ul = b->ur;
	b->ur = b->lr;
	b->lr = b->ll;
	b->ll = tmp;
}

static int boxes_are_neighbors(int box1nr, int box2nr)
{
	struct box_coords box, box2;
	int j, k;

	if ((get_box_flags(box1nr) & BOX_INVISIBLE) || (get_box_flags(box2nr) & BOX_INVISIBLE))
		return 0;

	box2 = get_box_coordinates(box1nr);
	box = get_box_coordinates(box2nr);

	for (j = 0; j < 4; j++) {
		for (k = 0; k < 4; k++) {
			if (box2.ur.x == box2.ul.x && box.ul.x == box2.ul.x && box.ur.x == box2.ul.x) {
				int sw2 = 0, sw1 = 0;
				if (box2.ur.y < box2.ul.y) {
					sw2 = 1;
					SWAP16(box2.ur.y, box2.ul.y);
				}
				if (box.ur.y < box.ul.y) {
					sw1 = 1;
					SWAP16(box.ur.y, box.ul.y);
				}
				if (!(box.ur.y < box2.ul.y || box.ul.y > box2.ur.y ||
				      ((box.ul.y == box2.ur.y || box.ur.y == box2.ul.y) &&
				       box2.ur.y != box2.ul.y && box.ul.y != box.ur.y)))
					return 1;
				if (sw2)
					SWAP16(box2.ur.y, box2.ul.y);
				if (sw1)
					SWAP16(box.ur.y, box.ul.y);
			}

			if (box2.ur.y == box2.ul.y && box.ul.y == box2.ul.y && box.ur.y == box2.ul.y) {
				int sw2 = 0, sw1 = 0;
				if (box2.ur.x < box2.ul.x) {
					sw2 = 1;
					SWAP16(box2.ur.x, box2.ul.x);
				}
				if (box.ur.x < box.ul.x) {
					sw1 = 1;
					SWAP16(box.ur.x, box.ul.x);
				}
				if (!(box.ur.x < box2.ul.x || box.ul.x > box2.ur.x ||
				      ((box.ul.x == box2.ur.x || box.ur.x == box2.ul.x) &&
				       box2.ur.x != box2.ul.x && box.ul.x != box.ur.x)))
					return 1;
				if (sw2)
					SWAP16(box2.ur.x, box2.ul.x);
				if (sw1)
					SWAP16(box.ur.x, box.ul.x);
			}
			rotate(&box2);
		}
		rotate(&box);
	}
	return 0;
}

void create_box_matrix(void)
{
	static uint8_t adjacent[64 * 64], itinerary[64 * 64];
	int num = get_num_boxes(), i, j, k;
	uint8_t *m, *start;

	if (num > 64)
		fatal("too many boxes", num, 0);

	for (i = 0; i < num; i++) {
		for (j = 0; j < num; j++) {
			if (i == j) {
				adjacent[i * 64 + j] = 0;
				itinerary[i * 64 + j] = (uint8_t)j;
			} else if (boxes_are_neighbors(i, j)) {
				adjacent[i * 64 + j] = 1;
				itinerary[i * 64 + j] = (uint8_t)j;
			} else {
				adjacent[i * 64 + j] = 255;
				itinerary[i * 64 + j] = INVALID_BOX;
			}
		}
	}
	for (k = 0; k < num; k++)
		for (i = 0; i < num; i++)
			for (j = 0; j < num; j++) {
				uint8_t ik, kj;
				if (i == j)
					continue;
				ik = adjacent[64 * i + k];
				kj = adjacent[64 * k + j];
				if (adjacent[64 * i + j] > ik + kj) {
					adjacent[64 * i + j] = (uint8_t)(ik + kj);
					itinerary[64 * i + j] = itinerary[64 * i + k];
				}
			}

	res_free(g.box_matrix);
	start = m = res_alloc(BOX_MATRIX_SIZE);
	if (!m)
		fatal("out of memory for box matrix", num, 0);
	memset(m, 0, BOX_MATRIX_SIZE);
	for (i = 0; i < num; i++) {
		*m++ = 0xFF;
		for (j = 0; j < num; j++) {
			uint8_t it = itinerary[64 * i + j];
			if (it != INVALID_BOX) {
				*m++ = (uint8_t)j;
				while (j < num - 1 && it == itinerary[64 * i + j + 1])
					j++;
				*m++ = (uint8_t)j;
				*m++ = it;
			}
		}
	}
	*m++ = 0xFF;
	if (m - start > BOX_MATRIX_SIZE)
		fatal("box matrix overflow", (int)(m - start), 0);
	g.box_matrix = start;
	g.box_matrix_size = BOX_MATRIX_SIZE;
}
