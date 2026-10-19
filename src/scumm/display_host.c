/* scumm5-amiga - host display: keeps a chunky copy of the screen and can
 * write frames as PPM files, for checking the renderer without an Amiga
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdio.h>

#include "gfx.h"

static uint8_t fb[SCREEN_HEIGHT][SCREEN_WIDTH];
static uint8_t pal[768];
static const char *dump_dir;
static uint32_t dump_every, dump_first, frames;
static const uint16_t *cur_image;
static int cur_x, cur_y, cur_color;

void display_host_dump(const char *dir, uint32_t first, uint32_t every)
{
	dump_dir = dir;
	dump_first = first;
	dump_every = every ? every : 1;
}

void display_layout(int b, int h)
{
	(void)b;
	(void)h;
}

void display_set_palette(const uint8_t *rgb)
{
	memcpy(pal, rgb, sizeof(pal));
}

void display_dirty(int v, int strip, int n, int top, int bottom)
{
	const struct vscreen *vs = &vscr[v];
	int row, s, bit;

	for (row = top; row < bottom; row++) {
		int y = vs->topline + row - g.screen_top;
		if (y < 0 || y >= SCREEN_HEIGHT)
			continue;
		for (s = strip; s < strip + n; s++) {
			const uint8_t *src = vs_pixels(vs, s * 8, row);
			for (bit = 0; bit < 8; bit++)
				fb[y][s * 8 + bit] = (uint8_t)planar_get_pixel(src, bit);
		}
	}
}

void display_cursor(const uint16_t *image, int x, int y, int color)
{
	cur_image = image;
	cur_x = x;
	cur_y = y;
	cur_color = color;
}

void display_present(void)
{
	char name[512];
	FILE *f;
	int x, y;

	frames++;
	if (!dump_dir || frames < dump_first || (frames - dump_first) % dump_every)
		return;
	snprintf(name, sizeof(name), "%s/frame%05u.ppm", dump_dir, frames);
	f = fopen(name, "wb");
	if (!f)
		return;
	fprintf(f, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
	for (y = 0; y < SCREEN_HEIGHT; y++)
		for (x = 0; x < SCREEN_WIDTH; x++) {
			int c = fb[y][x];
			if (cur_image && y >= cur_y && y < cur_y + 16 && x >= cur_x && x < cur_x + 16 &&
			    cur_image[y - cur_y] & (0x8000 >> (x - cur_x)))
				c = cur_color;
			fwrite(pal + c * 3, 1, 3, f);
		}
	fclose(f);
}

void display_copy_planes(uint8_t *dst, const uint8_t *src, int b0, int b1, int n)
{
	gfx_copy_planes_cpu(dst, src, b0, b1, n);
}

void display_sync(void)
{
}

void display_fill_planes(uint8_t *row0, int x0, int x1, int n, int color)
{
	gfx_fill_planes_cpu(row0, x0, x1, n, color);
}
