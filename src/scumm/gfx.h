/* scumm5-amiga - planar virtual screens and the display interface
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GFX_H
#define GFX_H

#include "scumm.h"

#define NUM_STRIPS 40
#define VS_PLANE 40			/* bytes per plane row */
#define VS_PITCH (8 * VS_PLANE)		/* interleaved row: 8 planes */

/* Pixel (x, y) of plane p is bit 7 - ((xstart + x) & 7) of byte
 * pixels[y * VS_PITCH + p * VS_PLANE + (xstart + x) / 8]. */
struct vscreen {
	uint8_t *pixels;	/* front: what is shown */
	uint8_t *back;		/* main screen only: background + objects */
	uint32_t size;
	int16_t topline, h;
	uint16_t xstart;
	int16_t tdirty[NUM_STRIPS], bdirty[NUM_STRIPS];
};

extern struct vscreen vscr[3];	/* 0 main, 1 text, 2 verbs */
extern int num_zbuffer;
extern int gfx_zbuffer_disabled;
extern uint32_t gfx_image_key;	/* nonzero: identifies the image being drawn */
extern uint8_t charset_mask_used;	/* main-screen charset mask may be non-zero */

void gfx_init_screens(int b, int h);
void gfx_room_changed(void);
void gfx_mark_rect_dirty(int virt, int left, int right, int top, int bottom, int dirtybit);
void gfx_draw_bitmap(const uint8_t *ptr, int vsn, int x, int y, int width, int height,
		     int stripnr, int numstrip, int flag);
void gfx_redraw_bg_areas(void);
void gfx_process_draw_queue(void);
void gfx_reset_background(int top, int bottom, int strip);
void gfx_restore_background(struct rect rect, int back_color);
void gfx_draw_box(int x, int y, int x2, int y2, int color);
void gfx_draw_dirty_screen_parts(void);
void gfx_full_dirty(void);
void gfx_set_xstart(int xstart);
void gfx_verb_cache_clear(void);

uint8_t *vs_pixels(const struct vscreen *vs, int x, int y);
uint8_t *vs_back_pixels(const struct vscreen *vs, int x, int y);
uint8_t *gfx_mask_buffer(int x, int y, int z);
uint8_t *gfx_screen_mask_buffer(int x, int y, int z);
void planar_put8(uint8_t *dst, const uint8_t *chunky, uint8_t m);
int planar_get_pixel(const uint8_t *src, int bit);
void planar_set_pixel(uint8_t *dst, int bit, int color);
int test_gfx_usage_bit_ext(int strip, int bit);

/* ---- display (display_amiga.c / display_host.c) ---- */
void display_layout(int b, int h);
/* Strips [strip, strip + n) of rows [top, bottom) of virtual screen vs
 * changed and must reach the display. */
void display_dirty(int vs, int strip, int n, int top, int bottom);
/* Shows the frame: copies dirty areas, palette, waits/flips. */
void display_present(void);
void display_set_palette(const uint8_t *rgb);
/* Copies byte columns [b0, b1) of n interleaved rows, dst and src pointing
 * at byte 0 of a row and of equal parity. May run until display_sync(). */
void display_copy_planes(uint8_t *dst, const uint8_t *src, int b0, int b1, int n);
void display_sync(void);
/* Sets pixels [x0, x1) of n interleaved rows from row0 to color. */
void display_fill_planes(uint8_t *row0, int x0, int x1, int n, int color);
void gfx_fill_planes_cpu(uint8_t *row0, int x0, int x1, int n, int color);
void gfx_copy_planes_cpu(uint8_t *dst, const uint8_t *src, int b0, int b1, int n);

#endif
