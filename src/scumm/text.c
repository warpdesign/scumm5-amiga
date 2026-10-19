/* scumm5-amiga - messages, charsets, strings and screen layout
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from ScummVM engines/scumm/string.cpp, charset.cpp (classic
 * charset renderer), actor.cpp (actorTalk, stopTalk) and gfx.cpp
 * (initScreens, findVirtScreen, restoreCharsetBg), ScummVM commit
 * 96b42d42bc8533953bdd701db059cc33c839a9ff, reduced to SCUMM v5. Glyph
 * pixels are not drawn yet; positions, extents and timing are exact.
 * ScummVM is the legal property of its developers, whose names are too
 * numerous to list here; see the COPYRIGHT file distributed with the
 * ScummVM source.
 */
#include "scumm.h"
#include "gfx.h"

/* virtual screens: 0 main, 1 text, 2 verb */
int16_t vs_topline[3], vs_h[3];
int16_t screen_b, screen_h;

static struct charset_state {
	int16_t top, left, start_left, right;
	uint8_t center;
	uint8_t color;
	uint8_t disable_offs_x, first_char;
	uint8_t text_screen_id;
	int8_t cur_id;
	struct rect str;
} cs;

uint8_t charset_has_mask;
int16_t next_left, next_top;
static int16_t msg_count;
static uint8_t charset_color;
static uint8_t use_talk_anims;

void init_screens(int b, int h)
{
	gfx_init_screens(b, h);
	vs_topline[0] = (int16_t)b;
	vs_h[0] = (int16_t)(h - b);
	vs_topline[1] = 0;
	vs_h[1] = (int16_t)b;
	vs_topline[2] = (int16_t)h;
	vs_h[2] = (int16_t)(SCREEN_HEIGHT - h);
	screen_b = (int16_t)b;
	screen_h = (int16_t)h;
}

int find_virt_screen(int y)
{
	int i;
	for (i = 0; i < 3; i++)
		if (y >= vs_topline[i] && y < vs_topline[i] + vs_h[i])
			return i;
	return -1;
}

/* ---- charsets ---- */

static const uint8_t *font_ptr(void)
{
	const uint8_t *p = res_get(RT_CHARSET, (uint16_t)cs.cur_id);
	if (!p)
		fatal("charset not found", cs.cur_id, 0);
	return p + 29;
}

static void set_cur_id(int id)
{
	if (id == -1)
		return;
	if (id < 0 || id >= NUM_CHARSETS)
		fatal("charset out of range", id, 0);
	cs.cur_id = (int8_t)id;
	font_ptr();
}

static int font_height(void)
{
	return font_ptr()[1];
}

static int char_width(int chr)
{
	const uint8_t *fp = font_ptr();
	uint32_t offs = rd_le32(fp + chr * 4 + 4);
	return offs ? fp[offs] + (int8_t)fp[offs + 2] : 0;
}

void load_charset(int no)
{
	const uint8_t *ptr;
	int i;

	if (no < 1 || no >= NUM_CHARSETS)
		fatal("loadCharset: bad charset", no, 0);
	ptr = res_get(RT_CHARSET, (uint16_t)no);
	if (!ptr)
		fatal("loadCharset: charset missing", no, 0);
	for (i = 0; i < 15; i++)
		g.charset_data[no][i + 1] = ptr[i + 14];
}

void init_charset(int no)
{
	if (!res_get(RT_CHARSET, (uint16_t)no))
		load_charset(no);
	g.string[0].def.charset = (uint8_t)no;
	g.string[1].def.charset = (uint8_t)no;
	memcpy(g.charset_color_map, g.charset_data[no], 16);
}

static int string_width(int arg, const uint8_t *text)
{
	int pos = 0, width = 1, chr, old_id = cs.cur_id;

	while ((chr = text[pos++]) != 0) {
		if (chr == '\n' || chr == '\r')
			break;
		if (chr == '@')
			continue;
		if (chr == 255 || chr == 254) {
			chr = text[pos++];
			if (chr == 3)
				break;
			if (chr == 8) {
				if (arg == 1)
					break;
				while (text[pos++] == ' ')
					;
				continue;
			}
			if (chr == 10 || chr == 21 || chr == 12 || chr == 13) {
				pos += 2;
				continue;
			}
			if (chr == 9 || chr == 1 || chr == 2)
				break;
			if (chr == 14) {
				int set = text[pos] | (text[pos + 1] << 8);
				pos += 2;
				set_cur_id(set);
				continue;
			}
		}
		width += char_width(chr);
	}
	set_cur_id(old_id);
	return width;
}

int charset_cur_id(void)
{
	return cs.cur_id;
}

int charset_string_width(int arg, const uint8_t *str)
{
	return string_width(arg, str);
}

static void add_linebreaks(int a, uint8_t *str, int pos, int maxwidth)
{
	int lastspace = -1, curw = 1, chr, old_id = cs.cur_id;

	while ((chr = str[pos++]) != 0) {
		if (chr == '@')
			continue;
		if (chr == 255 || chr == 254) {
			chr = str[pos++];
			if (chr == 3)
				break;
			if (chr == 8) {
				if (a == 1)
					curw = 1;
				else
					while (str[pos] == ' ')
						str[pos++] = '@';
				continue;
			}
			if (chr == 10 || chr == 21 || chr == 12 || chr == 13) {
				pos += 2;
				continue;
			}
			if (chr == 1) {
				curw = 1;
				continue;
			}
			if (chr == 2)
				break;
			if (chr == 14) {
				int set = str[pos] | (str[pos + 1] << 8);
				pos += 2;
				set_cur_id(set);
				continue;
			}
		}
		if (chr == ' ')
			lastspace = pos - 1;
		curw += char_width(chr);
		if (lastspace == -1)
			continue;
		if (curw > maxwidth) {
			str[lastspace] = 0xD;
			curw = 1;
			pos = lastspace + 1;
			lastspace = -1;
		}
	}
	set_cur_id(old_id);
}

/* drawBitsN into a planar virtual screen; on the main screen also sets the
 * charset mask (mask plane 0) so actors stay behind the text. */
static void draw_glyph(int v, const uint8_t *src, int bpp, int x0, int y0, int width, int height, int set_mask)
{
	const struct vscreen *vs = &vscr[v];
	int x, y, color, numbits = 8, strip, bit;
	uint8_t bits = *src++, chunky[8], m;

	for (y = 0; y < height && y + y0 < vs->h; y++) {
		strip = -1;
		m = 0;
		for (x = 0; x <= width; x++) {
			int sx = x0 + x;
			int s2 = x < width ? (vs->xstart + sx) >> 3 : -2;
			if (s2 != strip) {
				if (m) {
					uint8_t *d = vs->pixels + (uint32_t)(y + y0) * VS_PITCH + strip;
					planar_put8(d, chunky, m);
					if (set_mask) {
						*gfx_mask_buffer(strip, y + y0, 0) |= m;
						charset_mask_used = 1;
					}
				}
				strip = s2;
				m = 0;
			}
			if (x == width)
				break;
			color = (bits >> (8 - bpp)) & 0xFF;
			if (color && y + y0 >= 0 && sx >= 0 && sx < SCREEN_WIDTH) {
				bit = (vs->xstart + sx) & 7;
				chunky[bit] = g.charset_color_map[color];
				m |= (uint8_t)(0x80 >> bit);
			}
			bits = (uint8_t)(bits << bpp);
			numbits -= bpp;
			if (numbits == 0) {
				bits = *src++;
				numbits = 8;
			}
		}
	}
}

/* CharsetRendererClassic::printChar. */
static void print_char(int chr, int ignore_mask)
{
	const uint8_t *fp, *cp;
	uint32_t offs;
	int vs, width, height, offs_x, offs_y;

	if (cs.cur_id < 1 || cs.cur_id >= NUM_CHARSETS)
		fatal("printChar: bad charset", cs.cur_id, 0);
	if ((vs = find_virt_screen(cs.top)) < 0 && (vs = find_virt_screen(cs.top + font_height())) < 0)
		return;
	if (chr == '@')
		return;

	g.charset_color_map[1] = cs.color;

	fp = font_ptr();
	offs = rd_le32(fp + chr * 4 + 4);
	if (!offs)
		return;
	cp = fp + offs;
	width = cp[0];
	height = cp[1];
	offs_x = cs.disable_offs_x ? 0 : (int8_t)cp[2];
	offs_y = (int8_t)cp[3];

	if (cs.first_char) {
		cs.str.left = cs.str.top = cs.str.right = cs.str.bottom = 0;
	}

	cs.top += (int16_t)offs_y;
	cs.left += (int16_t)offs_x;

	if (cs.left + width > cs.right + 1 || cs.left < 0) {
		cs.left += (int16_t)width;
		cs.top -= (int16_t)offs_y;
		return;
	}

	cs.disable_offs_x = 0;

	if (cs.first_char) {
		cs.str.left = cs.left;
		cs.str.top = cs.top;
		cs.str.right = cs.left;
		cs.str.bottom = cs.top;
		cs.first_char = 0;
	}
	if (cs.left < cs.str.left)
		cs.str.left = cs.left;
	if (cs.top < cs.str.top)
		cs.str.top = cs.top;

	{
		int draw_top = cs.top - vscr[vs].topline;
		gfx_mark_rect_dirty(vs, cs.left, cs.left + width, draw_top, draw_top + height, 0);
		if (!ignore_mask) {
			charset_has_mask = 1;
			cs.text_screen_id = (uint8_t)vs;
		}
		draw_glyph(vs, cp + 4, fp[0], cs.left, draw_top, width, height, !ignore_mask && vs == 0);
	}

	cs.left += (int16_t)width;
	if (cs.str.right < cs.left)
		cs.str.right = cs.left;
	if (cs.str.bottom < cs.top + height)
		cs.str.bottom = (int16_t)(cs.top + height);
	cs.top -= (int16_t)offs_y;
}

/* ---- message conversion ---- */

static int convert_message(const uint8_t *msg, uint8_t *dst, int dst_size);

static int append_int(uint8_t *dst, int dst_size, int val)
{
	char buf[12];
	int i = sizeof(buf), n = 0, neg = val < 0;
	uint32_t v = neg ? (uint32_t)-val : (uint32_t)val;

	do {
		buf[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	if (neg)
		buf[--i] = '-';
	while (i < (int)sizeof(buf) && n < dst_size - 1)
		dst[n++] = (uint8_t)buf[i++];
	return n;
}

static int convert_verb(uint8_t *dst, int dst_size, int var)
{
	int num = read_var(var), k;

	if (num) {
		for (k = 1; k < NUM_VERBS; k++)
			if (num == g.verbs[k].verbid && !g.verbs[k].type && !g.verbs[k].saveid)
				return g.verbs[k].name ? convert_message(g.verbs[k].name, dst, dst_size) : 0;
	}
	return 0;
}

static int convert_name(uint8_t *dst, int dst_size, int var)
{
	int num = read_var(var);
	const uint8_t *ptr;

	if (num) {
		ptr = get_obj_or_actor_name(num);
		if (ptr)
			return convert_message(ptr, dst, dst_size);
	}
	return 0;
}

static int convert_string(uint8_t *dst, int dst_size, int var)
{
	if (var > 0 && var < NUM_ARRAY && g.strings[var])
		return convert_message(g.strings[var], dst, dst_size);
	return 0;
}

static int convert_message(const uint8_t *msg, uint8_t *dst, int dst_size)
{
	uint8_t *start = dst, *end = dst + dst_size;
	uint32_t num = 0;
	uint8_t chr;
	int val;

	if (!msg)
		return 0;
	for (;;) {
		chr = msg[num++];
		if (chr == 0)
			break;
		if (chr == 0xFF) {
			chr = msg[num++];
			if (chr == 1 || chr == 2 || chr == 3 || chr == 8) {
				*dst++ = 0xFF;
				*dst++ = chr;
			} else {
				val = rd_le16(msg + num);
				switch (chr) {
				case 4:
					dst += append_int(dst, (int)(end - dst), read_var(val));
					break;
				case 5:
					dst += convert_verb(dst, (int)(end - dst), val);
					break;
				case 6:
					dst += convert_name(dst, (int)(end - dst), val);
					break;
				case 7:
					dst += convert_string(dst, (int)(end - dst), val);
					break;
				case 9: case 10: case 12: case 13: case 14:
					*dst++ = 0xFF;
					*dst++ = chr;
					*dst++ = msg[num];
					*dst++ = msg[num + 1];
					break;
				default:
					num -= 2;
				}
				num += 2;
			}
		} else if (chr != '@') {
			*dst++ = chr;
		}
		if (dst >= end - 1)
			fatal("convertMessageToString: buffer overflow", 0, 0);
	}
	*dst = 0;
	return (int)(dst - start);
}

/* ---- talking ---- */

static void restore_charset_bg(void)
{
	next_left = g.string[0].cur.xpos;
	next_top = (int16_t)(g.string[0].cur.ypos + g.screen_top);
	if (charset_has_mask) {
		struct vscreen *vs = &vscr[cs.text_screen_id];
		charset_has_mask = 0;
		cs.str.left = -1;
		cs.left = -1;
		if (!vs->h)
			return;
		gfx_mark_rect_dirty(cs.text_screen_id, 0, SCREEN_WIDTH, 0, vs->h, USAGE_BIT_RESTORED);
		if (cs.text_screen_id == 0) {
			/* text lives in the main screen under the charset mask */
			int y, k, p, lights = (VAR(VAR_CURRENT_LIGHTS) & 2) && g.current_room != 0;
			for (y = 0; y < vs->h; y++) {
				uint8_t *m = gfx_screen_mask_buffer(0, y, 0);
				uint8_t *f = vs_pixels(vs, 0, y), *b = vs_back_pixels(vs, 0, y);
				for (k = 0; k < NUM_STRIPS; k++) {
					uint8_t mb = m[k];
					if (!mb)
						continue;
					for (p = 0; p < 8; p++)
						f[k + p * VS_PLANE] = (uint8_t)((f[k + p * VS_PLANE] & ~mb) |
										(lights ? (b[k + p * VS_PLANE] & mb) : 0));
					m[k] = 0;
				}
			}
			charset_mask_used = 0;
		} else {
			memset(vs->pixels, 0, vs->size);
		}
	}
}

static void run_actor_talk_script(struct actor *a, int f)
{
	if (!get_talking_actor() || a->room != g.current_room || a->frame == f)
		return;
	if (a->talk_script) {
		int32_t args[NUM_SCRIPT_LOCAL] = { 0 };
		args[1] = f;
		args[0] = a->number;
		run_script(a->talk_script, 1, 0, args);
	} else {
		start_anim_actor(a, f);
	}
}

int get_talking_actor(void)
{
	return VAR(VAR_TALK_ACTOR);
}

void set_talking_actor(int i)
{
	VAR(VAR_TALK_ACTOR) = i;
}

void stop_talk(void)
{
	int act;

	g.have_msg = 0;
	g.talk_delay = 0;

	act = get_talking_actor();
	if (act && act < 0x80) {
		struct actor *a = deref_actor(act);
		if (is_in_current_room(a) && use_talk_anims) {
			run_actor_talk_script(a, a->talk_stop_frame);
			use_talk_anims = 0;
		}
		set_talking_actor(0xFF);
	}
	g.keep_text = 0;
	restore_charset_bg();
}

/* Handles control codes at the buffer position; returns 0 at a wait or
 * end-of-text-without-newline code. */
static int handle_next_charset_code(struct actor *a, int *code)
{
	uint8_t *buffer = g.charset_buf + g.charset_buf_pos;
	int c = 0, end_loop = 0;

	while (!end_loop) {
		c = *buffer++;
		if (!(c == 0xFF || c == 0xFE))
			break;
		c = *buffer++;
		switch (c) {
		case 1:
			c = 13;
			msg_count = SCREEN_WIDTH;
			end_loop = 1;
			break;
		case 2:
			g.have_msg = 0;
			g.keep_text = 1;
			end_loop = 1;
			break;
		case 3:
			g.have_msg = 0xFF;
			g.keep_text = 0;
			msg_count = 0;
			end_loop = 1;
			break;
		case 8:
			break;
		case 9: {
			int frame = buffer[0] | (buffer[1] << 8);
			buffer += 2;
			if (a)
				start_anim_actor(a, frame);
			break;
		}
		case 10:
			buffer += 14;
			g.have_actor_speech_msg = 0;
			break;
		case 12: {
			int color = buffer[0] | (buffer[1] << 8);
			buffer += 2;
			cs.color = (uint8_t)(color == 0xFF ? charset_color : color);
			break;
		}
		case 13:
			buffer += 2;
			break;
		case 14: {
			int oldy = font_height();
			set_cur_id(*buffer++);
			buffer += 2;
			memcpy(g.charset_color_map, g.charset_data[cs.cur_id], 4);
			next_top -= (int16_t)(font_height() - oldy);
			break;
		}
		default:
			fatal("handleNextCharsetCode: invalid code", c, 0);
		}
	}
	g.charset_buf_pos = (uint16_t)(buffer - g.charset_buf);
	*code = c;
	return c != 2 && c != 3;
}

static int new_line(void)
{
	next_left = g.string[0].cur.xpos;
	if (cs.center) {
		next_left -= (int16_t)(string_width(0, g.charset_buf + g.charset_buf_pos) / 2);
		if (next_left < 0)
			next_left = 0;
	}
	if (g.string[0].cur.height)
		next_top += g.string[0].cur.height;
	else
		next_top += (int16_t)font_height();
	cs.disable_offs_x = 1;
	return 1;
}

void display_dialog(void)
{
	struct actor *a = 0;
	struct string_tab_part *s0 = &g.string[0].cur;
	int c = 0;

	if (!g.have_msg)
		return;
	if (g.camera.dest.x / 8 != g.camera.cur.x / 8 || g.camera.cur.x != g.camera.last.x)
		return;

	if (get_talking_actor() != 0xFF)
		a = deref_actor_safe(get_talking_actor());

	if (a && s0->overhead) {
		int s;
		s0->xpos = (int16_t)(a->pos.x - g.screen_start_strip * 8);
		s0->ypos = (int16_t)(a->pos.y - a->elevation - g.screen_top);
		if (VAR(VAR_V5_TALK_STRING_Y) < 0) {
			s = (a->scaley * (int)VAR(VAR_V5_TALK_STRING_Y)) / 0xFF;
			s0->ypos += (int16_t)(((VAR(VAR_V5_TALK_STRING_Y) - s) / 2) + s);
		} else {
			s0->ypos = (int16_t)VAR(VAR_V5_TALK_STRING_Y);
		}
		if (s0->ypos < 1)
			s0->ypos = 1;
		if (s0->xpos < 80)
			s0->xpos = 80;
		if (s0->xpos > SCREEN_WIDTH - 80)
			s0->xpos = SCREEN_WIDTH - 80;
	}

	cs.top = (int16_t)(s0->ypos + g.screen_top);
	cs.start_left = cs.left = s0->xpos;
	cs.right = s0->right;
	cs.center = s0->center;
	cs.color = charset_color;

	if (a && a->charset)
		set_cur_id(a->charset);
	else
		set_cur_id(s0->charset);
	memcpy(g.charset_color_map, g.charset_data[cs.cur_id], 4);

	if (g.talk_delay)
		return;

	if (g.have_msg == 1) {
		stop_talk();
		return;
	}

	if (a && !s0->no_talk_anim) {
		run_actor_talk_script(a, a->talk_start_frame);
		use_talk_anims = 1;
	}

	g.talk_delay = 60;

	if (!g.keep_text) {
		restore_charset_bg();
		msg_count = 0;
	}

	{
		int max_width = cs.right - s0->xpos - 1;
		if (cs.center) {
			if (max_width > next_left)
				max_width = next_left;
			max_width *= 2;
		}
		add_linebreaks(0, g.charset_buf + g.charset_buf_pos, 0, max_width);
	}

	if (cs.center) {
		next_left -= (int16_t)(string_width(0, g.charset_buf + g.charset_buf_pos) / 2);
		if (next_left < 0)
			next_left = 0;
	}

	cs.disable_offs_x = cs.first_char = !g.keep_text;

	while (handle_next_charset_code(a, &c)) {
		if (c == 0) {
			g.have_msg = 1;
			g.keep_text = 0;
			msg_count = 0;
			break;
		}
		if (c == 13) {
			if (!new_line())
				break;
			continue;
		}
		cs.left = next_left;
		cs.top = next_top;
		print_char(c, 0);
		next_left = cs.left;
		next_top = cs.top;
		g.talk_delay += VAR(VAR_CHARINC);
	}
}

void actor_talk(const uint8_t *msg)
{
	struct actor *a;

	convert_message(msg, g.charset_buf, sizeof(g.charset_buf));

	if (g.actor_to_print_str_for == 0xFF) {
		if (!g.keep_text)
			stop_talk();
		set_talking_actor(0xFF);
	} else {
		int oldact;
		a = deref_actor(g.actor_to_print_str_for);
		if (!is_in_current_room(a)) {
			oldact = 0xFF;
		} else {
			if (!g.keep_text)
				stop_talk();
			set_talking_actor(a->number);
			if (!g.string[0].cur.no_talk_anim) {
				run_actor_talk_script(a, a->talk_start_frame);
				use_talk_anims = 1;
			}
			oldact = get_talking_actor();
		}
		if (oldact >= 0x80)
			return;
	}

	if (get_talking_actor() > 0x7F)
		charset_color = g.string[0].cur.color;
	else
		charset_color = deref_actor(get_talking_actor())->talk_color;

	g.charset_buf_pos = 0;
	g.talk_delay = 0;
	g.have_msg = 0xFF;
	VAR(VAR_HAVE_MSG) = 0xFF;
	g.have_actor_speech_msg = 1;
	display_dialog();
}

static void draw_string(int a, const uint8_t *msg)
{
	uint8_t buf[270], *space, *tmp;
	struct string_tab_part *st = &g.string[a].cur;
	int i, c, fh;

	convert_message(msg, buf, sizeof(buf));

	cs.top = (int16_t)(st->ypos + g.screen_top);
	cs.start_left = cs.left = st->xpos;
	cs.right = st->right;
	cs.center = st->center;
	cs.color = st->color;
	cs.disable_offs_x = cs.first_char = 1;
	set_cur_id(st->charset);

	memcpy(g.charset_color_map, g.charset_data[cs.cur_id], 4);
	fh = font_height();

	space = 0;
	for (tmp = buf; *tmp; tmp++) {
		if (*tmp == ' ') {
			if (!space)
				space = tmp;
		} else {
			space = 0;
		}
	}
	if (space)
		*space = 0;

	if (cs.center)
		cs.left -= (int16_t)(string_width(a, buf) / 2);

	if (!buf[0]) {
		buf[0] = ' ';
		buf[1] = 0;
	}

	for (i = 0; (c = buf[i++]) != 0;) {
		if (c == 0xFF || c == 0xFE) {
			c = buf[i++];
			switch (c) {
			case 9: case 10: case 13: case 14:
				i += 2;
				break;
			case 1: case 8:
				if (cs.center)
					cs.left = (int16_t)(cs.start_left - string_width(a, buf + i));
				else
					cs.left = cs.start_left;
				if (g.string[0].cur.height)
					next_top += g.string[0].cur.height;
				else
					cs.top += (int16_t)fh;
				break;
			case 12: {
				int color = buf[i] + (buf[i + 1] << 8);
				i += 2;
				cs.color = (uint8_t)(color == 0xFF ? st->color : color);
				break;
			}
			}
		} else {
			print_char(c, 1);
		}
	}

	if (a == 0) {
		next_left = cs.left;
		next_top = cs.top;
	}
	st->xpos = cs.str.right;
}

/* Lays out a verb's text and returns its extent, as drawVerb does. */
void layout_verb_text(int slot, struct rect *r, int color)
{
	struct verb_slot *vs = &g.verbs[slot];
	struct string_tab_part *st = &g.string[4].cur;

	st->charset = vs->charset_nr;
	st->xpos = vs->cur_rect.left;
	st->ypos = vs->cur_rect.top;
	st->right = SCREEN_WIDTH - 1;
	st->center = vs->center;
	st->color = (uint8_t)color;
	draw_string(4, vs->name);
	r->left = cs.str.left;
	r->top = cs.str.top;
	r->right = cs.str.right;
	r->bottom = cs.str.bottom;
}

void print_string(int m, const uint8_t *msg)
{
	switch (m) {
	case 0:
		actor_talk(msg);
		break;
	case 1:
		draw_string(1, msg);
		break;
	case 2:
	case 3:
		break;
	}
}

/* ---- string resources ---- */

void nuke_string(int idx)
{
	if (idx < 0 || idx >= NUM_ARRAY)
		fatal("string index out of range", idx, 0);
	res_free(g.strings[idx]);
	g.strings[idx] = 0;
	g.string_size[idx] = 0;
}

/* Strings get two spare bytes, like ScummVM's resource safety area. */
uint8_t *create_string(int idx, int size)
{
	nuke_string(idx);
	g.strings[idx] = res_alloc((uint32_t)size + 2);
	if (!g.strings[idx])
		fatal("out of memory for string", idx, size);
	memset(g.strings[idx], 0, (size_t)size + 2);
	g.string_size[idx] = (uint16_t)size;
	return g.strings[idx];
}

void load_ptr_to_string(int idx, const uint8_t *src)
{
	int from_script = !src, len;

	nuke_string(idx);
	len = res_str_len(src) + 1;
	if (len <= 0)
		return;
	create_string(idx, len);
	memcpy(g.strings[idx], from_script ? g.script_ptr : src, (size_t)len);
	if (from_script)
		g.script_ptr += len;
}

void redefine_builtin_cursor_from_char(int idx, int chr)
{
	(void)idx;
	(void)chr;
}

void redefine_builtin_cursor_hotspot(int idx, int x, int y)
{
	(void)idx;
	(void)x;
	(void)y;
}
