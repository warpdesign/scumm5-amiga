/* scumm5-amiga - platform layer for a host OS (development and tracing)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "platform.h"

int app_main(int argc, char **argv);

int main(int argc, char **argv)
{
	return app_main(argc, argv);
}

plat_file plat_open(const char *path)
{
	return fopen(path, "rb");
}

plat_file plat_create(const char *path)
{
	return fopen(path, "wb");
}

void plat_close(plat_file f)
{
	fclose(f);
}

int32_t plat_read(plat_file f, void *buf, uint32_t n)
{
	return (int32_t)fread(buf, 1, n, f);
}

int32_t plat_write(plat_file f, const void *buf, uint32_t n)
{
	return (int32_t)fwrite(buf, 1, n, f);
}

int plat_seek(plat_file f, uint32_t pos)
{
	return fseek(f, (long)pos, SEEK_SET);
}

int32_t plat_size(plat_file f)
{
	long cur = ftell(f), end;
	fseek(f, 0, SEEK_END);
	end = ftell(f);
	fseek(f, cur, SEEK_SET);
	return (int32_t)end;
}

void *plat_alloc(uint32_t n, int flags)
{
	return (flags & PLAT_CLEAR) ? calloc(1, n) : malloc(n);
}

void plat_free(void *p, uint32_t n)
{
	(void)n;
	free(p);
}

void plat_log(const char *s)
{
	fprintf(stderr, "%s\n", s);
}

void plat_exit(int rc)
{
	exit(rc);
}

uint32_t plat_ticks(void)
{
	return (uint32_t)(clock() * 50 / CLOCKS_PER_SEC);
}
