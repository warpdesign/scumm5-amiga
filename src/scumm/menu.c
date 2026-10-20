/* scumm5-amiga - the main menu (save, load, play, quit)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The interpreter's own menu of the DOS v5 games, opened with the key in
 * VAR_MAINMENU_KEY. Layout, colours and behaviour are those ScummVM
 * reproduces for MI2 with its "original GUI" option: engines/scumm/
 * gfx_gui.cpp (showMainMenu, executeMainMenuOperation,
 * setUpMainMenuControls, drawInternalGUIControl, drawMainMenuTitle,
 * showBannerAndPause, queryQuit), ScummVM commit
 * 96b42d42bc8533953bdd701db059cc33c839a9ff. Colours are game string 21,
 * labels game strings 6-20. ScummVM is the legal property of its
 * developers, whose names are too numerous to list here; see the
 * COPYRIGHT file distributed with the ScummVM source.
 *
 * The menu runs one step per frame instead of a loop of its own; the game
 * frame that opened it finishes when it closes.
 */
#include "scumm.h"
#include "gfx.h"
#include "saveload.h"
#include "imuse.h"

enum { PAGE_MAIN, PAGE_SAVE, PAGE_LOAD };
enum {
	C_FIRST_SG = 1, C_LAST_SG = 9, C_SAVE = 10, C_LOAD, C_PLAY, C_QUIT, C_OK, C_CANCEL, C_UP, C_DOWN,
	C_OUTER = 26, C_INNER = 27, NUM_CTRLS = 28
};
enum { S_IDLE, S_PRESSED, S_SAVING, S_LOADING, S_QUERY };

#define LABEL_MAX 39
#define PREFIX 4		/* "NN. " */
#define PRESS_JIFFIES 4		/* 60 ms of a highlighted button */

struct ctrl {
	int16_t x0, y0, x1, y1;	/* x0 < 0: not on this page */
	uint8_t fill, text, top, bottom, left, right, hitext, hifill;
	uint8_t center, double_lines;
	const char *label;
};

static struct ctrl ctrls[NUM_CTRLS], banner;
static uint8_t active, page, state, cur_label, slot_page, pending;
static int8_t old_cursor_state;
static int wait;
static char labels[C_LAST_SG][LABEL_MAX + 2];
static uint8_t *saved_rows;
static uint32_t saved_offs, saved_size;
static int16_t box_y0, box_y1;

int menu_active(void)
{
	return active;
}

static const char *game_string(int n)
{
	return n < NUM_ARRAY && g.strings[n] ? (const char *)g.strings[n] : "";
}

static int color(int n)
{
	return g.strings[21] && n < g.string_size[21] ? g.strings[21][n] : 0;
}

static int y_center(void)
{
	return vscr[0].topline + vscr[0].h / 2;
}

/* ---- drawing (drawInternalGUIControl, v5 path) ---- */

static void box(int x0, int y0, int x1, int y1, int c)
{
	gfx_draw_box(x0, y0 + g.screen_top, x1, y1 + g.screen_top, c);
}

static void draw_ctrl(const struct ctrl *c, int hl, int slot_selected)
{
	int x0 = c->x0, y0 = c->y0, x = c->x1, y = c->y1, tx, ty;
	char buf[LABEL_MAX + 3];
	int n;

	if (c->x0 < 0)
		return;
	if (c->double_lines) {
		box(x0 + 1, y0 + 1, x - 1, y - 1, hl ? c->hifill : c->fill);
		box(x0 + 1, y0, x - 1, y0, c->top);
		box(x0 + 1, y, x - 1, y, c->bottom);
		box(x0, y0 + 1, x0, y - 1, c->left);
		box(x, y0 + 1, x, y - 1, c->right);
		box(x0 + 1, y0 + 1, x - 1, y0 + 1, c->top);
		box(x0 + 1, y - 1, x - 1, y - 1, c->bottom);
		box(x0 + 1, y0 + 1, x0 + 1, y - 1, c->left);
		box(x - 1, y0 + 1, x - 1, y - 1, c->right);
	} else {
		box(x0, y0, x, y, hl ? c->hifill : c->fill);
		box(x0, y0, x, y0, c->top);
		box(x0, y, x, y, c->bottom);
		box(x0, y0, x0, y, c->left);
		box(x, y0, x, y, c->right);
	}
	if (!c->label || !c->label[0])
		return;
	tx = c->center ? x0 + (x - x0) / 2 : x0 + 2;
	ty = y0 + (y - 8 - y0 + 2) / 2;
	n = (int)strlen(c->label);
	if (n > LABEL_MAX)
		n = LABEL_MAX;
	memcpy(buf, c->label, (size_t)n);
	if (slot_selected > 0)
		buf[n++] = '_';
	buf[n] = 0;
	gui_text(buf, tx, ty, slot_selected >= 0 ? x : SCREEN_WIDTH - 1, hl ? c->hitext : c->text, c->center);
}

static int is_slot(int id)
{
	return id >= C_FIRST_SG && id <= C_LAST_SG;
}

static void draw_control(int id, int hl)
{
	if (is_slot(id))
		draw_ctrl(&ctrls[id], hl, page == PAGE_SAVE && cur_label == id);
	else
		draw_ctrl(&ctrls[id], hl, -1);
}

static void draw_title(const char *title)
{
	int yc = y_center();

	gfx_draw_box(22, yc - 56, 298, yc - 48, color(4));
	gui_text(title, 160, yc - 56, SCREEN_WIDTH - 1, color(2), 1);
}

static void set_ctrl(int id, int fill, int text, int top, int bottom, int left, int right, int hitext, int hifill,
		     int x0, int y0, int x1, int y1, const char *label, int center, int double_lines)
{
	struct ctrl *c = id < 0 ? &banner : &ctrls[id];

	c->x0 = (int16_t)x0;
	c->y0 = (int16_t)y0;
	c->x1 = (int16_t)(x1 < 0 ? x0 - x1 : x1);
	c->y1 = (int16_t)(y1 < 0 ? y0 - y1 : y1);
	c->fill = (uint8_t)color(fill);
	c->text = (uint8_t)color(text);
	c->top = (uint8_t)color(top);
	c->bottom = (uint8_t)color(bottom);
	c->left = (uint8_t)color(left);
	c->right = (uint8_t)color(right);
	c->hitext = (uint8_t)color(hitext);
	c->hifill = (uint8_t)color(hifill);
	c->label = label;
	c->center = (uint8_t)center;
	c->double_lines = (uint8_t)double_lines;
}

/* setUpMainMenuControls() for MI2 (no arrows on the main page) */
static void set_up_controls(void)
{
	int yc = y_center(), i, j;

	for (i = 0; i < NUM_CTRLS; i++)
		ctrls[i].x0 = -1;
	set_ctrl(C_OUTER, 4, 2, 13, 14, 15, 16, 6, 4, 20, yc - 60, 300, yc + 60 < 0 ? -120 : yc + 60, "", 1, 1);
	set_ctrl(C_INNER, 4, 5, 18, 17, 20, 19, 6, 7, 26, yc - 47, 212, yc - 47 + 102, "", 1, 1);
	if (page == PAGE_MAIN) {
		set_ctrl(C_SAVE, 4, 5, 17, 18, 19, 20, 6, 7, 242, yc - 23, 292, yc - 23 + 12, game_string(7), 1, 1);
		set_ctrl(C_LOAD, 4, 5, 17, 18, 19, 20, 6, 7, 242, yc - 8, 292, yc - 8 + 12, game_string(8), 1, 1);
		set_ctrl(C_PLAY, 4, 5, 17, 18, 19, 20, 6, 7, 242, yc + 7, 292, yc + 19, game_string(9), 1, 1);
		set_ctrl(C_QUIT, 4, 5, 17, 18, 19, 20, 6, 7, 242, yc + 22, 292, yc + 34, game_string(11), 1, 1);
		return;
	}
	set_ctrl(C_UP, 9, 10, 17, 18, 19, 20, 11, 12, 216, yc - 43, 232, yc - 43 + 47, "\x18", 1, 1);
	set_ctrl(C_DOWN, 9, 10, 17, 18, 19, 20, 11, 12, 216, yc + 7, 232, yc + 52, "\x19", 1, 1);
	if (page == PAGE_SAVE)
		set_ctrl(C_OK, 4, 5, 17, 18, 19, 20, 6, 7, 242, yc - 8, 292, yc - 8 + 12, game_string(12), 1, 1);
	j = page == PAGE_LOAD ? yc - 1 : yc + 7;
	set_ctrl(C_CANCEL, 4, 5, 17, 18, 19, 20, 6, 7, 242, j, 292, j + 12, game_string(10), 1, 1);
	for (i = C_FIRST_SG, j = 0; i <= C_LAST_SG; i++, j += 11)
		set_ctrl(i, 9, 10, 4, 4, 4, 4, 11, 12, 28, yc - 45 + j, 210, -9, labels[i - 1], 0, 0);
}

static void draw_controls(void)
{
	int i;

	draw_control(C_OUTER, 0);
	if (page == PAGE_MAIN) {
		draw_control(C_SAVE, 0);
		draw_control(C_LOAD, 0);
		draw_control(C_PLAY, 0);
		draw_control(C_QUIT, 0);
	} else {
		draw_control(C_INNER, 0);
		draw_control(C_OK, 0);
		draw_control(C_CANCEL, 0);
		for (i = C_FIRST_SG; i <= C_LAST_SG; i++)
			draw_control(i, 0);
		draw_control(C_UP, 0);
		draw_control(C_DOWN, 0);
		draw_title(game_string(page == PAGE_SAVE ? 19 : 20));
	}
	if (cur_label)
		draw_control(cur_label, 1);
}

static int control_at(int x, int y)
{
	int id;

	for (id = 0; id < NUM_CTRLS; id++) {
		const struct ctrl *c = &ctrls[id];
		if (c->x0 >= 0 && c->x0 <= x && c->x1 >= x && c->y0 <= y && c->y1 >= y)
			return id;
	}
	return -1;
}

/* ---- slots ---- */

static int slot_of(int label)
{
	return label + slot_page * 9;
}

static void put_num(char *d, int n)
{
	d[0] = n >= 10 ? (char)('0' + n / 10) : ' ';
	d[1] = (char)('0' + n % 10);
	d[2] = '.';
	d[3] = ' ';
	d[4] = 0;
}

static void fill_labels(void)
{
	char name[SAVE_NAME_LEN + 1];
	int i, r;

	for (i = 0; i < C_LAST_SG; i++) {
		put_num(labels[i], slot_of(i + 1));
		r = save_slot_name(slot_of(i + 1), name);
		if (r < 0)
			memcpy(name, "WARNING: wrong save version", 28);
		if (r) {
			int n = (int)strlen(name);
			if (n > LABEL_MAX - PREFIX)
				n = LABEL_MAX - PREFIX;
			memcpy(labels[i] + PREFIX, name, (size_t)n);
			labels[i][PREFIX + n] = 0;
		}
	}
}

/* "Saving '%s'" and the like */
static void format(char *dst, const char *fmt, const char *arg)
{
	int n = 0;

	for (; *fmt && n < 90; fmt++) {
		if (fmt[0] == '%' && fmt[1] == 's') {
			int k = (int)strlen(arg);
			if (k > 90 - n)
				k = 90 - n;
			memcpy(dst + n, arg, (size_t)k);
			n += k;
			fmt++;
		} else {
			dst[n++] = *fmt;
		}
	}
	dst[n] = 0;
}

/* ---- the screen under the menu ---- */

static void save_screen(void)
{
	struct vscreen *vs = &vscr[0];
	int yc = y_center();
	int y0 = yc - 60 + g.screen_top - vs->topline, y1 = yc + 61 + g.screen_top - vs->topline;

	if (y0 < 0)
		y0 = 0;
	if (y1 > vs->h)
		y1 = vs->h;
	box_y0 = (int16_t)y0;
	box_y1 = (int16_t)y1;
	saved_rows = 0;
	if (y1 <= y0)
		return;
	/* one more row: with the screen scrolled, a row's right end is in the next */
	saved_offs = (uint32_t)y0 * VS_PITCH;
	saved_size = (uint32_t)(y1 - y0 + 1) * VS_PITCH;
	if (saved_offs + saved_size > vs->size)
		saved_size = vs->size - saved_offs;
	display_sync();
	saved_rows = res_alloc(saved_size);
	if (saved_rows)
		memcpy(saved_rows, vs->pixels + saved_offs, saved_size);
}

static void restore_screen(void)
{
	struct vscreen *vs = &vscr[0];
	int i;

	if (box_y1 <= box_y0)
		return;
	display_sync();
	if (saved_rows) {
		memcpy(vs->pixels + saved_offs, saved_rows, saved_size);
		res_free(saved_rows);
		saved_rows = 0;
		gfx_mark_rect_dirty(0, 0, SCREEN_WIDTH, box_y0, box_y1, 0);
		return;
	}
	/* no memory to keep it: the background, and actors drawn again */
	gfx_draw_box(0, box_y0 + vs->topline, SCREEN_WIDTH - 1, box_y1 - 1 + vs->topline, -1);
	for (i = 1; i < NUM_ACTORS; i++)
		if (is_in_current_room(&g.actors[i]))
			g.actors[i].needs_redraw = 1;
}

/* ---- opening and closing ---- */

void menu_open(void)
{
	if (active || !res_get(RT_CHARSET, 1))
		return;
	active = 1;
	page = PAGE_MAIN;
	state = S_IDLE;
	cur_label = 0;
	slot_page = 0;
	save_screen();
	set_up_controls();
	draw_controls();
	old_cursor_state = g.cursor_state;
	g.cursor_state = 1;
	audio_pause(1);
	clear_clicked_status();
}

static void menu_close(int loaded)
{
	active = 0;
	audio_pause(0);
	if (loaded) {
		res_free(saved_rows);
		saved_rows = 0;
	} else {
		restore_screen();
		g.cursor_state = old_cursor_state;
	}
	clear_clicked_status();
}

/* ---- what the controls do (executeMainMenuOperation) ---- */

static void show_page(int p)
{
	page = (uint8_t)p;
	cur_label = 0;
	if (p != PAGE_MAIN)
		fill_labels();
	set_up_controls();
	draw_controls();
}

static void query_quit(void)
{
	static char msg[96];
	const char *q = game_string(6);
	int n = (int)strlen(q), w, h, rw;

	if (!n || n >= (int)sizeof(msg))
		return;
	memcpy(msg, q, (size_t)n - 1);
	msg[n - 1] = 0;
	h = gui_font_height() + 5;
	w = gui_text_width(msg);
	if (w < 100)
		w = 100;
	rw = w / 2;
	set_ctrl(-1, 26, 25, 27, 28, 29, 30, 0, 0, 156 - rw, 80, rw + 164, 1 - h, msg, 1, 1);
	draw_ctrl(&banner, 0, -1);
	state = S_QUERY;
}

static void operation(int op)
{
	char title[96];
	const char *name;

	switch (op) {
	case C_SAVE:
		show_page(PAGE_SAVE);
		break;
	case C_LOAD:
		show_page(PAGE_LOAD);
		break;
	case C_PLAY:
		menu_close(0);
		break;
	case C_QUIT:
		query_quit();
		break;
	case C_OK:
		if (!cur_label)
			break;
		name = labels[cur_label - 1] + PREFIX;
		if (page == PAGE_SAVE) {
			if (!*name) {
				draw_title(game_string(14));
				break;
			}
			format(title, game_string(17), name);
			draw_title(title);
			state = S_SAVING;
			wait = PRESS_JIFFIES;
		} else if (page == PAGE_LOAD) {
			if (!*name) {
				draw_controls();
				break;
			}
			format(title, game_string(18), name);
			draw_title(title);
			state = S_LOADING;
			wait = PRESS_JIFFIES;
		}
		break;
	case C_CANCEL:
		show_page(PAGE_MAIN);
		break;
	case C_UP:
	case C_DOWN:
		if (page == PAGE_MAIN)
			break;
		if (op == C_UP && slot_page > 0)
			slot_page--;
		else if (op == C_DOWN && slot_page < 10)
			slot_page++;
		show_page(page);
		break;
	}
}

static void click(int x, int y)
{
	int id = control_at(x, y), old;

	if (id < 0 || id == C_OUTER || id == C_INNER)
		return;
	if (!is_slot(id)) {
		draw_control(id, 1);
		pending = (uint8_t)id;
		state = S_PRESSED;
		wait = PRESS_JIFFIES;
		return;
	}
	old = cur_label;
	cur_label = (uint8_t)id;
	if (old)
		draw_control(old, 0);
	draw_control(cur_label, 1);
	if (page == PAGE_LOAD)
		operation(C_OK);
}

/* userWriteLabelRoutine() */
static void type_key(int key)
{
	char *l = labels[cur_label - 1];
	int n = (int)strlen(l);

	if (key == 13) {
		operation(C_OK);
		return;
	}
	if (key == 8) {
		if (n > PREFIX)
			l[n - 1] = 0;
	} else if (key >= 32 && key <= 122) {
		if (n >= LABEL_MAX)
			return;
		l[n] = (char)key;
		l[n + 1] = 0;
	} else {
		return;
	}
	draw_control(cur_label, 1);
}

void menu_frame(int delta)
{
	int key = input_take_key(), button = input_take_click(), yes;

	switch (state) {
	case S_PRESSED:
		if ((wait -= delta) > 0)
			return;
		state = S_IDLE;
		draw_control(pending, 0);
		operation(pending);
		return;
	case S_SAVING:
		if ((wait -= delta) > 0)
			return;
		state = S_IDLE;
		if (save_state(slot_of(cur_label), labels[cur_label - 1] + PREFIX))
			menu_close(0);
		else
			draw_title(game_string(15));
		return;
	case S_LOADING:
		if ((wait -= delta) > 0)
			return;
		state = S_IDLE;
		if (load_state(slot_of(cur_label)))
			menu_close(1);
		else
			draw_title(game_string(16));
		return;
	case S_QUERY:
		if (!key && !button)
			return;
		yes = game_string(6)[strlen(game_string(6)) - 1];
		if (key && (key | 0x20) == (yes | 0x20)) {
			g.quit = 1;
			return;
		}
		state = S_IDLE;
		draw_controls();
		return;
	}
	if (page == PAGE_SAVE && cur_label) {
		if (button == 2)
			button = 0;
		if (key && !button)
			type_key(key);
	}
	if (button)
		click(g.mouse.x, g.mouse.y);
}
