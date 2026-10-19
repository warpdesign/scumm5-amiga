/* scumm5-amiga - no display (headless runs)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "gfx.h"

void display_layout(int b, int h)
{
	(void)b;
	(void)h;
}

void display_set_palette(const uint8_t *rgb)
{
	(void)rgb;
}

void display_dirty(int v, int strip, int n, int top, int bottom)
{
	(void)v;
	(void)strip;
	(void)n;
	(void)top;
	(void)bottom;
}

void display_present(void)
{
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
