/* scumm5-amiga - which game is in a folder of DOS files
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef DETECT_H
#define DETECT_H

#include "platform.h"

#define GAME_FILES 4

struct game_def {
	const char *id;		/* folder, pack and saved game names */
	const char *name;	/* shown in the launcher */
	const char *files[GAME_FILES];	/* index file, then the data files */
	uint32_t sizes[GAME_FILES];
	uint32_t index_sum;	/* res_checksum of the index file as stored */
	const char *extra;	/* also copied (the demo's recording), or 0 */
	uint8_t supported;
};

enum { DETECT_NONE, DETECT_OK, DETECT_UNSUPPORTED, DETECT_UNKNOWN_VERSION };

/* Looks at dir; on anything but DETECT_NONE, *def is the game found. */
int detect_game(const char *dir, const struct game_def **def);
const struct game_def *game_by_id(const char *id);
int game_count(void);
const struct game_def *game_at(int i);

/* dir + name with the separator AmigaDOS (and Unix) expect; 0 if too long */
int join_path(char *out, int size, const char *dir, const char *name);

#endif
