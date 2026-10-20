/* scumm5-amiga - DOS data files to an Amiga pack (tools/mkpack.py in C)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Writes the same bytes as tools/mkpack.py (pack format: see there), one
 * resource at a time so it fits beside AmigaOS in 2 MB. The strip decoders
 * follow tools/room2planar.py, which follows ScummVM engines/scumm/gfx.cpp
 * (drawStripBasicH/V, drawStripComplex), and the index and sound variant
 * selection ScummVM engines/scumm/resource.cpp and sound.cpp, ScummVM
 * commit 96b42d42bc8533953bdd701db059cc33c839a9ff. ScummVM is the legal
 * property of its developers, whose names are too numerous to list here;
 * see the COPYRIGHT file distributed with the ScummVM source.
 */
#include "convert.h"

#define XOR_KEY 0x69
#define HEADER_SIZE 60
#define PAD 64
#define MAX_DATA 4
#define MAX_H 512
#define MAX_OBCD 512
#define ROOM_WEIGHT 4	/* rooms cost far more than copying the rest */

static const char type_tags[5][5] = { "DROO", "DSCR", "DCOS", "DSOU", "DCHR" };
static const char res_tags[5][5] = { "ROOM", "SCRP", "COST", "SOUN", "CHAR" };

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)(p[1] << 8 | p[0]);
}

static int tag_is(const uint8_t *p, const char *t)
{
	return p[0] == t[0] && p[1] == t[1] && p[2] == t[2] && p[3] == t[3];
}

static uint32_t checksum(const uint8_t *p, uint32_t n)
{
	uint32_t a = 1, c = 0;

	while (n--) {
		a += *p++;
		c += a;
	}
	return c << 16 ^ a;
}

/* ---- state ---- */

static int err;
static struct conv_status st;
static conv_poll_fn poll_fn;
static void *poll_ctx;

static int poll(void)
{
	if (!err && poll_fn && poll_fn(&st, poll_ctx))
		err = CONV_STOPPED;
	return err;
}

static void *alloc(uint32_t n)
{
	void *p = n ? plat_alloc(n, 0) : 0;

	if (n && !p && !err)
		err = CONV_ERR_MEMORY;
	return p;
}

/* growing output buffer */
struct ob {
	uint8_t *p;
	uint32_t len, cap;
};

static void ob_need(struct ob *o, uint32_t n)
{
	uint32_t cap;
	uint8_t *q;

	if (o->len + n <= o->cap || err)
		return;
	cap = o->cap ? o->cap * 2 : 65536;
	while (cap < o->len + n)
		cap *= 2;
	q = alloc(cap);
	if (!q)
		return;
	if (o->p) {
		memcpy(q, o->p, o->len);
		plat_free(o->p, o->cap);
	}
	o->p = q;
	o->cap = cap;
}

static void ob_put(struct ob *o, const void *d, uint32_t n)
{
	ob_need(o, n);
	if (err)
		return;
	memcpy(o->p + o->len, d, n);
	o->len += n;
}

static void ob_u8(struct ob *o, int v)
{
	uint8_t b = (uint8_t)v;
	ob_put(o, &b, 1);
}

static void ob_be16(struct ob *o, uint32_t v)
{
	uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };
	ob_put(o, b, 2);
}

static void ob_be32(struct ob *o, uint32_t v)
{
	uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
	ob_put(o, b, 4);
}

static void ob_patch32(struct ob *o, uint32_t pos, uint32_t v)
{
	if (err)
		return;
	o->p[pos] = (uint8_t)(v >> 24);
	o->p[pos + 1] = (uint8_t)(v >> 16);
	o->p[pos + 2] = (uint8_t)(v >> 8);
	o->p[pos + 3] = (uint8_t)v;
}

static void ob_free(struct ob *o)
{
	if (o->p)
		plat_free(o->p, o->cap);
	o->p = 0;
	o->len = o->cap = 0;
}

/* ---- strip decoders (room2planar.py) ---- */

struct bits {
	const uint8_t *src;
	uint32_t len, pos, bits;
	int count;
};

static void fill(struct bits *b)
{
	if (b->count <= 8) {
		b->bits |= (uint32_t)(b->pos < b->len ? b->src[b->pos] : 0) << b->count;
		b->pos++;
		b->count += 8;
	}
}

static unsigned rd(struct bits *b, int n)
{
	unsigned v = b->bits & ((1u << n) - 1);

	b->bits >>= n;
	b->count -= n;
	return v;
}

static uint8_t px[8 * MAX_H], tmp[8 * MAX_H];

static uint8_t at(const uint8_t *src, uint32_t len, uint32_t pos)
{
	return pos < len ? src[pos] : 0;
}

static void strip_basic(const uint8_t *src, uint32_t len, uint32_t pos, int height, int shift, int vertical)
{
	struct bits b;
	int i, n = 8 * height, inc = -1;
	unsigned mask = 0xFFu >> (8 - shift), color = at(src, len, pos);
	uint8_t *out = vertical ? tmp : px;

	b.src = src;
	b.len = len;
	b.pos = pos + 2;
	b.bits = at(src, len, pos + 1);
	b.count = 8;
	for (i = 0; i < n; i++) {
		fill(&b);
		out[i] = (uint8_t)color;
		if (!rd(&b, 1)) {
		} else if (!rd(&b, 1)) {
			fill(&b);
			color = rd(&b, shift) & mask;
			inc = -1;
		} else if (!rd(&b, 1)) {
			color = (color + (unsigned)inc) & 0xFF;
		} else {
			inc = -inc;
			color = (color + (unsigned)inc) & 0xFF;
		}
	}
	if (vertical) {
		int x, y;
		for (y = 0; y < height; y++)
			for (x = 0; x < 8; x++)
				px[y * 8 + x] = tmp[x * height + y];
	}
}

static void strip_majmin(const uint8_t *src, uint32_t len, uint32_t pos, int height, int shift)
{
	struct bits b;
	int i, n = 8 * height;
	unsigned color = at(src, len, pos), repeat = 0;

	b.src = src;
	b.len = len;
	b.pos = pos + 3;
	b.bits = at(src, len, pos + 1) | (unsigned)at(src, len, pos + 2) << 8;
	b.count = 16;
	for (i = 0; i < n; i++) {
		px[i] = (uint8_t)color;
		if (repeat) {
			repeat--;
			continue;
		}
		fill(&b);
		if (!rd(&b, 1))
			continue;
		fill(&b);
		if (rd(&b, 1)) {
			int diff;
			fill(&b);
			diff = (int)rd(&b, 3) - 4;
			if (diff) {
				color = (color + (unsigned)diff) & 0xFF;
			} else {
				fill(&b);
				repeat = (rd(&b, 8) - 1) & 0xFF;
			}
		} else {
			fill(&b);
			color = rd(&b, shift);
		}
	}
}

/* 8 x height pixels into px */
static void decode_strip(const uint8_t *src, uint32_t len, uint32_t pos, int height)
{
	int code = at(src, len, pos), shift = code % 10, i;

	pos++;
	if (code == 1) {
		for (i = 0; i < 8 * height; i++)
			px[i] = at(src, len, pos + (uint32_t)i);
	} else if ((code >= 14 && code <= 18) || (code >= 34 && code <= 38)) {
		strip_basic(src, len, pos, height, shift, 1);
	} else if ((code >= 24 && code <= 28) || (code >= 44 && code <= 48)) {
		strip_basic(src, len, pos, height, shift, 0);
	} else if ((code >= 64 && code <= 68) || (code >= 84 && code <= 88) || (code >= 104 && code <= 108) ||
		   (code >= 124 && code <= 128)) {
		strip_majmin(src, len, pos, height, shift);
	} else if (!err) {
		err = CONV_ERR_FORMAT;
	}
}

/* ---- planar strips (PSMP) ---- */

static int transparent_codec(int c)
{
	return (c >= 34 && c <= 38) || (c >= 44 && c <= 48) || (c >= 84 && c <= 88) || (c >= 124 && c <= 128);
}

/* PackBits (IFF ByteRun1), exactly as mkpack.py's packbits() */
static void packbits(struct ob *o, const uint8_t *b, int n)
{
	int i = 0, j;

	while (i < n) {
		j = i;
		while (j + 1 < n && b[j + 1] == b[i] && j - i < 127)
			j++;
		if (j > i) {
			ob_u8(o, (257 - (j - i + 1)) & 0xFF);
			ob_u8(o, b[i]);
			i = j + 1;
			continue;
		}
		j = i;
		while (j < n && j - i < 128 && !(j + 2 < n && b[j] == b[j + 1] && b[j + 1] == b[j + 2]))
			j++;
		ob_u8(o, j - i - 1);
		ob_put(o, b + i, (uint32_t)(j - i));
		i = j;
	}
}

static uint8_t mask[MAX_H], cols[8 * MAX_H];

static void planar_strip(struct ob *o, const uint8_t *d, uint32_t len, uint32_t pos, int height, int trns)
{
	int code = at(d, len, pos), partial, flags, x, y, p, any = 0, all = 1;

	if (code == 150) {
		for (x = 0; x < 8 * height; x++)
			px[x] = at(d, len, pos + 1 + (uint32_t)x);
	} else {
		decode_strip(d, len, pos, height);
	}
	partial = transparent_codec(code) || code == 150;
	flags = (transparent_codec(code) ? 1 : 0) | (partial ? 2 : 0);
	memset(mask, 0, (size_t)height);
	memset(cols, 0, (size_t)(8 * height));
	for (y = 0; y < height; y++) {
		for (x = 0; x < 8; x++) {
			int v = px[y * 8 + x];
			if (partial && v == trns)
				continue;
			mask[y] |= (uint8_t)(0x80 >> x);
			for (p = 0; p < 8; p++)
				if (v >> p & 1)
					cols[p * height + y] |= (uint8_t)(0x80 >> x);
		}
		any |= mask[y] != 0;
		all &= mask[y] == 0xFF;
	}
	if (partial && !any) {
		ob_u8(o, flags | 4);
		return;
	}
	if (partial && all) {
		flags &= ~2;
		partial = 0;
	}
	ob_u8(o, flags);
	if (partial)
		packbits(o, mask, height);
	packbits(o, cols, 8 * height);
}

static void convert_smap(struct ob *o, const uint8_t *d, uint32_t len, uint32_t smap, int height, int trns)
{
	uint32_t first = le32(d + smap + 8), n = (first - 8) / 4, start = o->len, k;

	if (height > MAX_H || smap + 8 + 4 * n > len) {
		err = err ? err : CONV_ERR_FORMAT;
		return;
	}
	ob_put(o, "PSMP", 4);
	ob_be32(o, 0);
	ob_be16(o, n);
	ob_be16(o, (uint32_t)height);
	for (k = 0; k < n; k++)
		ob_be32(o, 0);
	for (k = 0; k < n && !err; k++) {
		ob_patch32(o, start + 12 + 4 * k, o->len - start);
		planar_strip(o, d, len, smap + le32(d + smap + 8 + 4 * k), height, trns);
		if (poll())
			return;
	}
	ob_patch32(o, start + 4, o->len - start);
}

/* ---- rooms ---- */

enum { RB_RMIM, RB_IMAGE, RB_OBIM };

static int32_t child(const uint8_t *d, uint32_t start, uint32_t size, const char *tag)
{
	uint32_t p, s;

	for (p = start + 8; p + 8 <= start + size; p += s) {
		s = be32(d + p + 4);
		if (s < 8)
			return -1;
		if (tag_is(d + p, tag))
			return (int32_t)p;
	}
	return -1;
}

static void copy_block(struct ob *o, const uint8_t *d, uint32_t len, uint32_t p, uint32_t s)
{
	if (p + s > len) {
		err = err ? err : CONV_ERR_FORMAT;
		return;
	}
	ob_put(o, d + p, s);
}

static void rebuild(struct ob *o, const uint8_t *d, uint32_t len, uint32_t start, uint32_t size, int mode,
		    int height, int trns)
{
	uint32_t hdr = o->len, p, s, end = start + size;

	ob_put(o, d + start, 4);
	ob_be32(o, 0);
	for (p = start + 8; p + 8 <= end && !err; p += s) {
		const uint8_t *t = d + p;
		s = be32(t + 4);
		if (s < 8) {
			err = CONV_ERR_FORMAT;
			return;
		}
		if (mode == RB_RMIM && tag_is(t, "IM00"))
			rebuild(o, d, len, p, s, RB_IMAGE, height, trns);
		else if (mode == RB_IMAGE && tag_is(t, "SMAP"))
			convert_smap(o, d, len, p, height, trns);
		else if (mode == RB_OBIM && t[0] == 'I' && t[1] == 'M' && !tag_is(t, "IMHD"))
			rebuild(o, d, len, p, s, RB_IMAGE, height, trns);
		else
			copy_block(o, d, len, p, s);
	}
	ob_patch32(o, hdr + 4, o->len - hdr);
}

static uint16_t obcd_id[MAX_OBCD], obcd_h[MAX_OBCD];

static void convert_room(struct ob *o, const uint8_t *d, uint32_t size, uint32_t base)
{
	int32_t rmhd = child(d, 0, size, "RMHD"), t;
	int room_h, trns, nobcd = 0, i;
	uint32_t p, s, hdr = o->len, n = 0;
	struct ob remap = { 0, 0, 0 };

	if (rmhd < 0) {
		err = CONV_ERR_FORMAT;
		return;
	}
	room_h = le16(d + rmhd + 10);
	t = child(d, 0, size, "TRNS");
	trns = t >= 0 ? d[t + 8] : 255;
	for (p = 8; p + 8 <= size; p += s) {
		s = be32(d + p + 4);
		if (s < 8)
			break;
		if (tag_is(d + p, "OBCD") && nobcd < MAX_OBCD) {
			int32_t cdhd = child(d, p, s, "CDHD");
			if (cdhd < 0) {
				err = CONV_ERR_FORMAT;
				return;
			}
			obcd_id[nobcd] = le16(d + cdhd + 8);
			obcd_h[nobcd++] = (uint16_t)(d[cdhd + 8 + 5] * 8);
		}
	}

	ob_put(o, d, 4);
	ob_be32(o, 0);
	for (p = 8; p + 8 <= size && !err; p += s) {
		s = be32(d + p + 4);
		if (s < 8) {
			err = CONV_ERR_FORMAT;
			break;
		}
		ob_be32(&remap, o->len - hdr);
		ob_be32(&remap, p);
		n++;
		st.done = base + (ROOM_WEIGHT - 1) * p;
		if (tag_is(d + p, "RMIM")) {
			rebuild(o, d, size, p, s, RB_RMIM, room_h, trns);
		} else if (tag_is(d + p, "OBIM")) {
			int32_t imhd = child(d, p, s, "IMHD");
			int oid, ih, ch;
			if (imhd < 0) {
				err = CONV_ERR_FORMAT;
				break;
			}
			oid = le16(d + imhd + 8);
			ih = le16(d + imhd + 8 + 14) / 8 * 8;
			ch = ih;
			for (i = nobcd - 1; i >= 0; i--)
				if (obcd_id[i] == oid) {
					ch = obcd_h[i];
					break;
				}
			rebuild(o, d, size, p, s, RB_OBIM, ih > ch ? ih : ch, trns);
		} else {
			copy_block(o, d, size, p, s);
		}
		if (poll())
			break;
	}
	ob_put(o, "S5OF", 4);
	ob_be32(o, 8 + 2 + 8 * n);
	ob_be16(o, n);
	if (remap.len)
		ob_put(o, remap.p, remap.len);
	ob_free(&remap);
	ob_patch32(o, hdr + 4, o->len - hdr);
}

/* The background as interleaved 8-bitplane rows padded to 64 px. */
static uint8_t *room_planes(const uint8_t *d, uint32_t size, uint32_t base, uint32_t *out_size, uint16_t *pw_out,
			    uint16_t *h_out, uint16_t *w_out)
{
	int32_t rmhd = child(d, 0, size, "RMHD"), rmim, im00, smap;
	int width, height, pw, rowbytes, s, y, x, p;
	uint32_t n;
	uint8_t *out;

	if (rmhd < 0 || child(d, 0, size, "CLUT") < 0 || (rmim = child(d, 0, size, "RMIM")) < 0 ||
	    (im00 = child(d, (uint32_t)rmim, be32(d + rmim + 4), "IM00")) < 0 ||
	    (smap = child(d, (uint32_t)im00, be32(d + im00 + 4), "SMAP")) < 0) {
		err = err ? err : CONV_ERR_FORMAT;
		return 0;
	}
	width = le16(d + rmhd + 8);
	height = le16(d + rmhd + 10);
	if (height > MAX_H) {
		err = CONV_ERR_FORMAT;
		return 0;
	}
	pw = (width + PAD - 1) / PAD * PAD;
	rowbytes = pw / 8;
	n = (uint32_t)rowbytes * 8 * (uint32_t)height;
	out = alloc(n ? n : 1);
	if (!out)
		return 0;
	memset(out, 0, n);
	for (s = 0; s < width / 8 && !err; s++) {
		decode_strip(d, size, (uint32_t)smap + le32(d + smap + 8 + 4 * s), height);
		for (y = 0; y < height; y++) {
			uint8_t *row = out + (uint32_t)y * 8 * (uint32_t)rowbytes + (uint32_t)s;
			for (p = 0; p < 8; p++) {
				uint8_t v = 0;
				for (x = 0; x < 8; x++)
					v |= (uint8_t)(((px[y * 8 + x] >> p) & 1) << (7 - x));
				row[p * rowbytes] = v;
			}
		}
		st.done = base + (ROOM_WEIGHT - 1) * size + size / (uint32_t)(width / 8) * (uint32_t)s;
		poll();
	}
	*out_size = n;
	*pw_out = (uint16_t)pw;
	*h_out = (uint16_t)height;
	*w_out = (uint16_t)width;
	if (err) {
		plat_free(out, n ? n : 1);
		return 0;
	}
	return out;
}

/* ---- sounds: the variant ScummVM plays on General MIDI ---- */

static int sound_pri(const uint8_t *t)
{
	static const struct {
		char tag[5];
		int pri;
	} pri[] = { { "TOWS", 16 }, { "SBL ", 15 }, { "GMD ", 4 }, { "ROL ", 3 }, { "AMI ", 3 }, { "MAC ", 2 },
		    { "ADL ", 1 } };
	unsigned i;

	for (i = 0; i < sizeof(pri) / sizeof(pri[0]); i++)
		if (tag_is(t, pri[i].tag))
			return pri[i].pri;
	return -1;
}

/* d holds the block from its start, len bytes: the part to keep, or 0 if
 * no variant plays on General MIDI */
static int pick_sound(const uint8_t *d, uint32_t len, uint32_t *start, uint32_t *n)
{
	uint32_t size = be32(d + 4), total, p, sub, best = 0, best_n = 0;
	int best_pri = -1, pri;

	if (tag_is(d + 8, "MIDI") || tag_is(d + 8, "iMUS")) {
		total = be32(d + 12);
		*start = 8;
		*n = 8 + total;
		return 1;
	}
	if (!tag_is(d + 8, "SOU ")) {
		*start = 0;
		*n = size;
		return 1;
	}
	total = be32(d + 12);
	for (p = 16; p < 16 + total && p + 8 <= len; p += sub) {
		sub = be32(d + p + 4) + 8;
		pri = sound_pri(d + p);
		if (pri > best_pri) {
			best = p;
			best_n = sub;
			best_pri = pri;
		}
	}
	if (best_pri < 0)
		return 0;
	*start = best;
	*n = best_n;
	return 1;
}

/* ---- files ---- */

static plat_file data_f[MAX_DATA];
static int ndata;

static int read_at(plat_file f, uint32_t off, uint8_t *buf, uint32_t n)
{
	uint32_t i;

	if (plat_seek(f, off) || plat_read(f, buf, n) != (int32_t)n) {
		err = err ? err : CONV_ERR_READ;
		return -1;
	}
	for (i = 0; i < n; i++)
		buf[i] ^= XOR_KEY;
	return 0;
}

/* The block at off: its size from its header, then all of it (at least
 * min bytes when the file has them). */
static uint8_t *read_block(plat_file f, uint32_t off, uint32_t min, uint32_t *len)
{
	uint8_t h[8], *b;
	uint32_t n;

	if (read_at(f, off, h, 8))
		return 0;
	n = be32(h + 4);
	if (n < min)
		n = min;
	if (n < 8 || n > 0x1000000) {
		err = CONV_ERR_FORMAT;
		return 0;
	}
	b = alloc(n);
	if (!b || read_at(f, off, b, n)) {
		if (b)
			plat_free(b, n);
		return 0;
	}
	*len = n;
	return b;
}

/* buffered writer */
static plat_file out_f;
static uint32_t out_pos, out_n;
static uint8_t out_buf[8192];

static void out_flush(void)
{
	if (out_n && !err && plat_write(out_f, out_buf, out_n) != (int32_t)out_n)
		err = CONV_ERR_WRITE;
	out_n = 0;
}

static void out_put(const void *d, uint32_t n)
{
	const uint8_t *s = d;

	out_pos += n;
	while (n && !err) {
		uint32_t k = sizeof(out_buf) - out_n;
		if (k > n)
			k = n;
		memcpy(out_buf + out_n, s, k);
		out_n += k;
		s += k;
		n -= k;
		if (out_n == sizeof(out_buf))
			out_flush();
	}
}

static uint32_t blob(const uint8_t *d, uint32_t n)
{
	static const uint8_t zero[4];
	uint32_t off;

	if (out_pos & 3)
		out_put(zero, 4 - (out_pos & 3));
	off = out_pos;
	out_put(d, n);
	return off;
}

static void put_be(uint8_t *b, uint32_t v, int n)
{
	int i;

	for (i = 0; i < n; i++)
		b[i] = (uint8_t)(v >> (8 * (n - 1 - i)));
}

/* ---- the index ---- */

struct list {
	uint16_t num;
	const uint8_t *rn, *offs;
};

static struct list lists[5];
static uint32_t room_off[256];
static uint8_t room_disk[256], room_known[256];

static int read_room_locations(int k)
{
	uint8_t h[8], *loff;
	uint32_t lecf, p, s, n, i, len;

	if (read_at(data_f[k], 0, h, 8))
		return -1;
	lecf = be32(h + 4);
	for (p = 8; p + 8 <= lecf; p += s) {
		if (read_at(data_f[k], p, h, 8))
			return -1;
		s = be32(h + 4);
		if (s < 8)
			break;
		if (!tag_is(h, "LOFF"))
			continue;
		loff = read_block(data_f[k], p, 0, &len);
		if (!loff)
			return -1;
		n = loff[8];
		for (i = 0; i < n && 9 + i * 5 + 5 <= len; i++) {
			int room = loff[9 + i * 5];
			room_off[room] = le32(loff + 10 + i * 5);
			room_disk[room] = (uint8_t)k;
			room_known[room] = 1;
		}
		plat_free(loff, len);
		return 0;
	}
	err = CONV_ERR_FORMAT;
	return -1;
}

/* ---- the pack ---- */

static int verify(const char *path)
{
	plat_file f = plat_open(path);
	uint8_t hdr[HEADER_SIZE], e[28], *b = 0;
	uint32_t cap = 0, t, i;
	int rc = CONV_ERR_VERIFY;

	if (!f)
		return CONV_ERR_VERIFY;
	if (plat_read(f, hdr, HEADER_SIZE) != HEADER_SIZE || !tag_is(hdr, "S5PK"))
		goto out;
	for (t = 0; t <= 5; t++) {
		uint32_t count = t < 5 ? (uint32_t)(hdr[8 + 8 * t] << 8 | hdr[9 + 8 * t]) : lists[0].num;
		uint32_t dir = t < 5 ? be32(hdr + 12 + 8 * t) : be32(hdr + 48), es = t < 5 ? 12 : 28;
		st.phase = CONV_PHASE_VERIFY;
		for (i = 0; i < count; i++) {
			uint32_t off, n;
			if (plat_seek(f, dir + i * es) || plat_read(f, e, es) != (int32_t)es)
				goto out;
			off = be32(e);
			n = be32(e + 4);
			if (!n)
				continue;
			if (n > cap) {
				if (b)
					plat_free(b, cap);
				cap = n;
				b = plat_alloc(cap, 0);
				if (!b) {
					rc = CONV_ERR_MEMORY;
					goto out;
				}
			}
			if (plat_seek(f, off) || plat_read(f, b, n) != (int32_t)n || checksum(b, n) != be32(e + 8))
				goto out;
			st.done += n / 4;
			if (poll()) {
				rc = err;
				goto out;
			}
		}
	}
	rc = CONV_OK;
out:
	if (b)
		plat_free(b, cap);
	plat_close(f);
	return rc;
}

static int build(const char *index_path)
{
	plat_file f;
	uint8_t *idx = 0, hdr[HEADER_SIZE], e[28];
	int32_t idx_len;
	uint32_t p, s, i, t, keep_n = 0, dir_off[5], bg_off, idx_off, keep_off;
	struct ob keep = { 0, 0, 0 }, entries = { 0, 0, 0 }, bg = { 0, 0, 0 }, room = { 0, 0, 0 };
	int k;

	/* index file, all in memory (a few KB) */
	f = plat_open(index_path);
	if (!f)
		return err = CONV_ERR_READ;
	idx_len = plat_size(f);
	idx = idx_len > 0 ? alloc((uint32_t)idx_len) : 0;
	if (!idx || read_at(f, 0, idx, (uint32_t)idx_len)) {
		plat_close(f);
		err = err ? err : CONV_ERR_READ;
		goto out;
	}
	plat_close(f);
	memset(lists, 0, sizeof(lists));
	for (p = 0; p + 8 <= (uint32_t)idx_len; p += s) {
		s = be32(idx + p + 4);
		if (s < 8) {
			err = CONV_ERR_FORMAT;
			goto out;
		}
		for (t = 0; t < 5; t++)
			if (tag_is(idx + p, type_tags[t])) {
				lists[t].num = le16(idx + p + 8);
				lists[t].rn = idx + p + 10;
				lists[t].offs = idx + p + 10 + lists[t].num;
			}
		if (tag_is(idx + p, "MAXS") || tag_is(idx + p, "DOBJ"))
			ob_put(&keep, idx + p, s);
	}
	keep_n = keep.len;

	memset(room_known, 0, sizeof(room_known));
	for (k = 0; k < ndata; k++)
		if (read_room_locations(k))
			goto out;

	/* work: rooms by their size (weighted), the rest by the file size */
	st.total = 0;
	for (i = 0; i < lists[0].num; i++)
		if (i < 256 && room_known[i]) {
			uint8_t h[8];
			if (read_at(data_f[room_disk[i]], room_off[i], h, 8))
				goto out;
			st.total += ROOM_WEIGHT * be32(h + 4);
		}
	for (k = 0; k < ndata; k++)
		st.total += (uint32_t)plat_size(data_f[k]) / 2;
	st.done = 0;

	memset(hdr, 0, sizeof(hdr));
	out_put(hdr, HEADER_SIZE);
	for (t = 0; t < 5 && !err; t++) {
		st.phase = (uint16_t)t;
		st.items = lists[t].num;
		entries.len = 0;
		for (i = 0; i < lists[t].num && !err; i++) {
			uint32_t rn = lists[t].rn[i], roff = le32(lists[t].offs + 4 * i), off, len = 0, start = 0, n = 0;
			uint8_t *d;
			st.item = (uint16_t)(i + 1);
			if (t == 0) {
				rn = i;
				roff = 0;
			}
			memset(e, 0, sizeof(e));
			if (rn > 255 || !room_known[rn] || (t > 0 && rn == 0 && roff == 0)) {
				ob_put(&entries, e, 12);
				if (t == 0)
					ob_put(&bg, e, 28);
				continue;
			}
			off = room_off[rn] + roff;
			if (t > 0) {
				uint8_t h[8];
				if (read_at(data_f[room_disk[rn]], off, h, 8))
					break;
				if (!tag_is(h, res_tags[t])) {
					/* a stale index entry (Atlantis costume 182): absent,
					 * as ScummVM only checks a resource when it loads it */
					ob_put(&entries, e, 12);
					continue;
				}
			}
			d = read_block(data_f[room_disk[rn]], off, t == 3 ? 16 : 0, &len);
			if (!d)
				break;
			if (!tag_is(d, res_tags[t])) {
				err = CONV_ERR_FORMAT;
			} else if (t == 0) {
				uint32_t base = st.done, pn = 0;
				uint16_t pw = 0, h = 0, w = 0;
				uint8_t *planes;
				room.len = 0;
				convert_room(&room, d, len, base);
				planes = err ? 0 : room_planes(d, len, base, &pn, &pw, &h, &w);
				if (planes) {
					put_be(e, blob(planes, pn), 4);
					put_be(e + 4, pn, 4);
					put_be(e + 8, checksum(planes, pn), 4);
					put_be(e + 12, pw, 2);
					put_be(e + 14, h, 2);
					put_be(e + 16, w, 2);
					ob_put(&bg, e, 28);
					plat_free(planes, pn ? pn : 1);
					memset(e, 0, sizeof(e));
					put_be(e, blob(room.p, room.len), 4);
					put_be(e + 4, room.len, 4);
					put_be(e + 8, checksum(room.p, room.len), 4);
					ob_put(&entries, e, 12);
				}
				st.done = base + ROOM_WEIGHT * len;
			} else {
				if (t == 3) {
					if (!pick_sound(d, len, &start, &n)) {
						ob_put(&entries, e, 12);
						plat_free(d, len);
						continue;
					}
				} else {
					start = 0;
					n = be32(d + 4);
				}
				if (start + n > len) {
					err = CONV_ERR_FORMAT;
				} else {
					put_be(e, blob(d + start, n), 4);
					put_be(e + 4, n, 4);
					put_be(e + 8, checksum(d + start, n), 4);
					ob_put(&entries, e, 12);
				}
				st.done += len / 2;
			}
			plat_free(d, len);
			poll();
		}
		dir_off[t] = blob(entries.p, entries.len);
	}
	if (err)
		goto out;
	bg_off = blob(bg.p, bg.len);
	idx_off = blob(keep.p, keep_n);
	keep_off = idx_off;
	memcpy(hdr, "S5PK", 4);
	put_be(hdr + 4, 3, 2);
	put_be(hdr + 6, 5, 2);
	for (t = 0; t < 5; t++) {
		put_be(hdr + 8 + 8 * t, lists[t].num, 2);
		put_be(hdr + 12 + 8 * t, dir_off[t], 4);
	}
	put_be(hdr + 48, bg_off, 4);
	put_be(hdr + 52, keep_off, 4);
	put_be(hdr + 56, keep_n, 4);
	out_flush();
	if (!err && (plat_seek(out_f, 0) || plat_write(out_f, hdr, HEADER_SIZE) != HEADER_SIZE))
		err = CONV_ERR_WRITE;
out:
	ob_free(&keep);
	ob_free(&entries);
	ob_free(&bg);
	ob_free(&room);
	if (idx)
		plat_free(idx, (uint32_t)idx_len);
	return err;
}

int convert_game(const char *index_path, const char *const *data_paths, int n, const char *out_path,
		 conv_poll_fn pfn, void *ctx)
{
	int k, rc;

	err = 0;
	memset(&st, 0, sizeof(st));
	poll_fn = pfn;
	poll_ctx = ctx;
	out_pos = out_n = 0;
	ndata = 0;
	if (n < 1 || n > MAX_DATA)
		return CONV_ERR_FORMAT;
	for (k = 0; k < n; k++) {
		data_f[k] = plat_open(data_paths[k]);
		if (!data_f[k]) {
			err = CONV_ERR_READ;
			break;
		}
		ndata++;
	}
	/* a later file's rooms win, as in mkpack.py */
	out_f = err ? 0 : plat_create(out_path);
	if (!err && !out_f)
		err = CONV_ERR_WRITE;
	if (!err)
		build(index_path);
	if (out_f)
		plat_close(out_f);
	for (k = 0; k < ndata; k++)
		plat_close(data_f[k]);
	rc = err;
	if (!rc) {
		st.total = st.done + out_pos / 4;
		rc = verify(out_path);
	}
	if (rc && out_f)
		plat_delete(out_path);
	return rc;
}

const char *convert_error(int rc)
{
	switch (rc) {
	case CONV_OK:
		return "done";
	case CONV_STOPPED:
		return "stopped";
	case CONV_ERR_READ:
		return "cannot read the game files";
	case CONV_ERR_WRITE:
		return "cannot write the converted data (disk full?)";
	case CONV_ERR_FORMAT:
		return "the game files are not as expected";
	case CONV_ERR_MEMORY:
		return "not enough memory";
	default:
		return "the converted data does not read back correctly";
	}
}
