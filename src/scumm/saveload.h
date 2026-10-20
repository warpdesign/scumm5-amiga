/* scumm5-amiga - saved games
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SAVELOAD_H
#define SAVELOAD_H

#include "platform.h"

#define SAVE_NAME_LEN 40
#define SAVE_SLOTS 100

/* One function both writes and reads the state: values go big-endian to
 * the file when saving and come back into the same variables when loading. */
struct ser {
	plat_file f;
	uint8_t saving, err;
	uint16_t pos, len;
	uint8_t buf[512];
};

void ser_bytes(struct ser *s, void *p, uint32_t n);
/* An integer of 1, 2 or 4 bytes, or n of them in a row. */
void ser_int(struct ser *s, void *p, int size);
void ser_ints(struct ser *s, void *p, int size, int n);
/* A res_alloc block: NULL or its bytes; loading frees the old block. */
void ser_blob(struct ser *s, uint8_t **p);

#define SV(s, x) ser_int((s), &(x), (int)sizeof(x))
#define SVA(s, a) ser_ints((s), (a), (int)sizeof((a)[0]), (int)(sizeof(a) / sizeof((a)[0])))

void saveload_set_base(const char *pack);
/* Copies the slot's description into name (SAVE_NAME_LEN + 1 bytes);
 * 0 if there is no readable game in the slot. */
int save_slot_name(int slot, char *name);
int save_state(int slot, const char *name);
int load_state(int slot);

/* Module state (text.c, imuse.c, sound.c, actor.c) */
void text_sync(struct ser *s);
void text_after_load(void);
void imuse_sync(struct ser *s);
void sound_sync(struct ser *s);
void sound_after_load(void);
void actor_usage_sync(struct ser *s);
void room_after_load(void);

/* The main menu (menu.c) */
int menu_active(void);
void menu_open(void);
void menu_frame(int delta);

/* Charset 1 text over the main screen, for the menu (text.c) */
void gui_text(const char *msg, int x, int y, int right, int color, int center);
int gui_text_width(const char *msg);
int gui_font_height(void);

#endif
