/* scumm5-amiga - headless interpreter run, for tracing and testing
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage: scummrun PACK [FRAMES] [-t TRACE] [-r DEMO.REC] [-a ARENA_KB]
 * Boots the game and runs FRAMES main-loop iterations (default 2000)
 * without display or sound, optionally writing a ScummVM-format trace.
 */
#include "scumm/scumm.h"
#include "util.h"

int app_main(int argc, char **argv)
{
	const char *p, *pack = 0, *trace = 0, *rec = 0;
	uint32_t frames = 2000, arena = 400, i, t0, jiffies = 0;
	int n;

	for (n = 1; n < argc; n++) {
		if (argv[n][0] == '-' && argv[n][1] && !argv[n][2] && n + 1 < argc) {
			char opt = argv[n][1];
			p = argv[++n];
			if (opt == 't')
				trace = p;
			else if (opt == 'r')
				rec = p;
			else if (opt == 'a')
				arena = parse_uint(&p);
		} else if (!pack) {
			pack = argv[n];
		} else {
			p = argv[n];
			frames = parse_uint(&p);
		}
	}
	if (!pack) {
		report("usage: scummrun PACK [FRAMES] [-t TRACE] [-r DEMO.REC] [-a ARENA_KB]");
		return 20;
	}

	if (trace)
		trace_open(trace);
	if (engine_init(pack, arena * 1024)) {
		report("scummrun: cannot open pack");
		return 20;
	}
	if (rec && playback_open(rec))
		report("scummrun: no playback stream, running without it");

	t0 = plat_ticks();
	engine_boot();
	for (i = 0; i < frames && !g.quit; i++) {
		jiffies += VAR(VAR_TIMER_NEXT) < 1 ? 1 : VAR(VAR_TIMER_NEXT);
		engine_frame();
	}
	t0 = plat_ticks() - t0;

	trace_close();
	out_str("scummrun: ");
	out_num(i);
	out_str(" frames, room ");
	out_num(g.current_room);
	out_str(", cache peak ");
	out_num(res_get_stats()->peak);
	out_str(" loads ");
	out_num(res_get_stats()->loads);
	report("");
	out_str("scummrun: game time ");
	out_num(jiffies / 60);
	out_str(" s, run time ");
	out_num(t0 / 50);
	out_str(".");
	out_num(t0 % 50 * 2);
	report(" s");
	res_close();
	return 0;
}
