/* scumm5-amiga - which game is in a folder of DOS files
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "detect.h"

static const struct game_def games[] = {
	{ "monkey2", "Monkey Island 2 (DOS, English)", { "MONKEY2.000", "MONKEY2.001" },
	  { 11135, 9080329 }, 0xf7b7733b, 0, 1 },
	{ "mi2demo", "Monkey Island 2 demo (DOS)", { "MI2DEMO.000", "MI2DEMO.001", "MI2DEMO.002" },
	  { 9770, 1348841, 846007 }, 0x127fdfb4, "DEMO.REC", 1 },
	{ "atlantis", "Fate of Atlantis (DOS floppy, experimental)", { "ATLANTIS.000", "ATLANTIS.001" },
	  { 12030, 9679627 }, 0x565f565c, 0, 1 },
};

#define NUM_GAMES ((int)(sizeof(games) / sizeof(games[0])))

int game_count(void)
{
	return NUM_GAMES;
}

const struct game_def *game_at(int i)
{
	return i >= 0 && i < NUM_GAMES ? &games[i] : 0;
}

const struct game_def *game_by_id(const char *id)
{
	int i;

	for (i = 0; i < NUM_GAMES; i++) {
		const char *a = games[i].id, *b = id;
		while (*a && *a == *b) {
			a++;
			b++;
		}
		if (!*a && !*b)
			return &games[i];
	}
	return 0;
}

int join_path(char *out, int size, const char *dir, const char *name)
{
	int n = (int)strlen(dir), m = (int)strlen(name), sep = n && dir[n - 1] != ':' && dir[n - 1] != '/';

	if (n + sep + m + 1 > size)
		return 0;
	memcpy(out, dir, (size_t)n);
	if (sep)
		out[n++] = '/';
	memcpy(out + n, name, (size_t)m + 1);
	return 1;
}

static int32_t file_size(const char *dir, const char *name)
{
	char path[256];
	plat_file f;
	int32_t n;

	if (!join_path(path, sizeof(path), dir, name) || !(f = plat_open(path)))
		return -1;
	n = plat_size(f);
	plat_close(f);
	return n;
}

static uint32_t file_sum(const char *dir, const char *name)
{
	char path[256];
	uint8_t buf[512];
	uint32_t a = 1, c = 0;
	plat_file f;
	int32_t n, i;

	if (!join_path(path, sizeof(path), dir, name) || !(f = plat_open(path)))
		return 0;
	while ((n = plat_read(f, buf, sizeof(buf))) > 0)
		for (i = 0; i < n; i++) {
			a += buf[i];
			c += a;
		}
	plat_close(f);
	return c << 16 ^ a;
}

int detect_game(const char *dir, const struct game_def **def)
{
	int i, k;

	for (i = 0; i < NUM_GAMES; i++) {
		const struct game_def *gd = &games[i];
		if (file_size(dir, gd->files[0]) < 0)
			continue;
		*def = gd;
		if (!gd->supported)
			return DETECT_UNSUPPORTED;
		for (k = 0; k < GAME_FILES && gd->files[k]; k++)
			if (file_size(dir, gd->files[k]) != (int32_t)gd->sizes[k])
				return DETECT_UNKNOWN_VERSION;
		if (gd->extra && file_size(dir, gd->extra) < 0)
			return DETECT_UNKNOWN_VERSION;
		return file_sum(dir, gd->files[0]) == gd->index_sum ? DETECT_OK : DETECT_UNKNOWN_VERSION;
	}
	return DETECT_NONE;
}
