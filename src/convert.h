/* scumm5-amiga - DOS data files to an Amiga pack (tools/mkpack.py in C)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef CONVERT_H
#define CONVERT_H

#include "platform.h"

enum {
	CONV_OK, CONV_STOPPED, CONV_ERR_READ, CONV_ERR_WRITE, CONV_ERR_FORMAT, CONV_ERR_MEMORY, CONV_ERR_VERIFY
};

enum { CONV_PHASE_ROOMS, CONV_PHASE_SCRIPTS, CONV_PHASE_COSTUMES, CONV_PHASE_SOUNDS, CONV_PHASE_CHARSETS,
	CONV_PHASE_VERIFY };

struct conv_status {
	uint32_t done, total;	/* work units, for a progress bar */
	uint16_t phase, item, items;	/* e.g. room 37 of 110 */
};

/* Called often (every image strip); nonzero stops the conversion. */
typedef int (*conv_poll_fn)(const struct conv_status *st, void *ctx);

/* Writes out_path from the index file and n data files (.001, .002...),
 * then reads it back and checks every block. On any error or a stop, the
 * output is deleted. */
int convert_game(const char *index_path, const char *const *data_paths, int n, const char *out_path,
		 conv_poll_fn poll, void *ctx);

const char *convert_error(int rc);

#endif
