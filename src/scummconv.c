/* scumm5-amiga - convert DOS game files to a pack from the command line
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage: scummconv INDEX DATA... -o PACK  (as tools/mkpack.py)
 */
#include "convert.h"
#include "util.h"

static uint32_t last;

static int show(const struct conv_status *st, void *ctx)
{
	uint32_t pct = st->total ? (uint32_t)(st->done / (st->total / 100 + 1)) : 0;

	(void)ctx;
	if (pct != last && pct % 10 == 0) {
		last = pct;
		out_str("scummconv: ");
		out_num(pct);
		report("%");
	}
	return 0;
}

int app_main(int argc, char **argv)
{
	const char *data[4], *index = 0, *out = 0;
	int n = 0, i, rc;
	uint32_t t0;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] == 'o' && i + 1 < argc)
			out = argv[++i];
		else if (!index)
			index = argv[i];
		else if (n < 4)
			data[n++] = argv[i];
	}
	if (!index || !n || !out) {
		report("usage: scummconv INDEX DATA... -o PACK");
		return 20;
	}
	last = 1;
	t0 = plat_ticks();
	rc = convert_game(index, data, n, out, show, 0);
	out_str("scummconv: ");
	out_str(convert_error(rc));
	out_str(" in ");
	out_num((plat_ticks() - t0) / 50);
	report(" s");
	return rc ? 20 : 0;
}
